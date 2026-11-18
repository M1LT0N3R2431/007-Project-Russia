// The render resolution following the frame rate. RENDER RESOLUTION is the ceiling; when DYNAMIC RESOLUTION is on the
// picture drops a step at a time while the frame rate is under the target and the GPU is the one waiting, and climbs
// back once the GPU has kept up for a while. The game itself has no dynamic resolution of its own.
#include <algorithm>
#include <atomic>
#include <chrono>

#include "app/app.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "core/log.h"

namespace dynres {

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kStep = 5;            // percent of the screen's height, the row's own step
constexpr uint32_t kFloorPercent = 30;   // below this the picture is too soft to be worth the frames
constexpr double kLowFps = 54.0;         // under this a step down is considered
constexpr double kGoodFps = 57.0;        // at or above this a step up is considered
constexpr double kGpuWaitDown = 1.0;     // ms: the GPU is behind, the resolution is what costs the frames
constexpr double kGpuWaitUp = 0.6;       // ms: the GPU has room again
constexpr double kLowWindow = 0.5;       // s under the target before a step down
constexpr double kGoodWindow = 3.0;      // s at the target before a step up
constexpr double kSettleDown = 0.9;      // s the new size gets before it is judged
constexpr double kSettleUp = 1.2;

// the vulkan thread owns the timing; the game thread only reads the percent
std::atomic<uint32_t> g_percent{0};        // 0 until the first present
std::atomic<uint32_t> g_ceiling{0};        // the RENDER RESOLUTION setting the controller last saw
std::atomic<double> g_gpu_wait_ms{-1.0};   // the last wait for the frame before, -1 for none yet

double g_settle = 0.0;
double g_low_time = 0.0;
double g_good_time = 0.0;
Clock::time_point g_last = Clock::now();
bool g_timed = false;

uint32_t Ceiling() { return std::max<uint32_t>(app::Get().render_percent, 1); }

uint32_t Floor(uint32_t ceiling) { return std::min(ceiling, kFloorPercent); }

bool Enabled() { return game::Opt().dynamic_resolution; }

void Reset() {
    g_low_time = 0.0;
    g_good_time = 0.0;
}

}  // namespace

uint32_t Percent() {
    uint32_t ceiling = Ceiling();
    if (!Enabled()) {
        g_ceiling.store(ceiling);
        g_percent.store(ceiling);
        return ceiling;
    }
    uint32_t percent = g_percent.load();
    if (percent == 0 || g_ceiling.load() != ceiling) {
        // first use, or the setting moved under the controller: start from it and let the controller settle
        g_ceiling.store(ceiling);
        g_percent.store(ceiling);
        g_settle = kSettleUp;
        Reset();
        return ceiling;
    }
    return std::clamp(percent, Floor(ceiling), ceiling);
}

void GpuWait(double ms) { g_gpu_wait_ms.store(std::max(ms, 0.0)); }

void Frame(double fps) {
    Clock::time_point now = Clock::now();
    double dt = std::chrono::duration<double>(now - g_last).count();
    g_last = now;
    if (!g_timed) {
        g_timed = true;
        return;
    }
    if (dt <= 0.0 || dt > 0.25) dt = 1.0 / 60.0;  // a stall or the first frames: no judgement from it

    uint32_t ceiling = Ceiling();
    if (!Enabled()) {
        g_ceiling.store(ceiling);
        g_percent.store(ceiling);
        g_settle = 0.0;
        Reset();
        return;
    }
    uint32_t percent = Percent();
    // the menus, the films and the paused game pace themselves: their frame rate says nothing about what the picture
    // costs, and the controller must not take their frames for the game's
    if (!input::InPlay()) {
        Reset();
        return;
    }
    if (g_settle > 0.0) {
        g_settle = std::max(g_settle - dt, 0.0);
        return;
    }

    double wait = g_gpu_wait_ms.load();
    bool gpu_behind = wait >= kGpuWaitDown;
    bool gpu_room = wait >= 0.0 && wait <= kGpuWaitUp;

    // a line every couple of seconds while the picture is not at the setting, to see what the controller sees
    static double g_report = 0.0;
    g_report += dt;
    if (g_report >= 2.0 && percent < ceiling) {
        g_report = 0.0;
        LOGR("dynres: %.1f fps, GPU waited %.2f ms, render %u%% of %u%%", fps, wait, percent, ceiling);
    }

    if (fps > 0.0 && fps < kLowFps && gpu_behind) {
        g_low_time += dt;
        if (g_low_time >= kLowWindow && percent > Floor(ceiling)) {
            uint32_t next = std::max(percent - kStep, Floor(ceiling));
            g_percent.store(next);
            g_ceiling.store(ceiling);
            g_settle = kSettleDown;
            Reset();
            LOGR("dynres: %.1f fps, GPU waited %.2f ms: render %u%%", fps, wait, next);
        }
    } else {
        g_low_time = 0.0;
    }

    if (fps >= kGoodFps && gpu_room) {
        g_good_time += dt;
        if (g_good_time >= kGoodWindow && percent < ceiling) {
            uint32_t next = std::min(percent + kStep, ceiling);
            g_percent.store(next);
            g_ceiling.store(ceiling);
            g_settle = kSettleUp;
            Reset();
            LOGR("dynres: %.1f fps, GPU waited %.2f ms: render %u%%", fps, wait, next);
        }
    } else {
        g_good_time = 0.0;
    }
}

}  // namespace dynres
