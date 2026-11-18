// the app in the background: the game waits for it to come back
#include <atomic>
#include <chrono>
#include <thread>

#include "app/app.h"
#include "audio/output.h"
#include "common/options.h"
#include "core/log.h"
#include "host.h"

namespace game {

namespace {

std::atomic<uint32_t> g_paused_ms{0};
std::atomic<uint64_t> g_game_thread{0};  // the waiting thread's std::this_thread::get_id, hashed

}  // namespace

// in the background Android takes the window away: the game thread waits there. The setting stays: Android would
// take the drawing away whether it waited or not
void WaitWhileUnfocused() {
    g_game_thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    if (host::Foreground()) return;
    LOGI("focus lost: paused");
    auto start = std::chrono::steady_clock::now();
    g_focus_paused = true;
    audio::SetPaused(true);
    host::WaitForeground();
    audio::SetPaused(false);
    auto waited = std::chrono::steady_clock::now() - start;
    g_paused_ms += uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(waited).count());
    g_focus_paused = false;
    LOGI("focus back: resumed");
}

uint32_t FocusPausedMs() { return g_paused_ms; }

bool OnGameThread() {
    uint64_t thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return g_game_thread != 0 && g_game_thread == thread;
}

}  // namespace game
