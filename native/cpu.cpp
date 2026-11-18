// The cores' speeds, once, and the affinity the hot threads ask for (cpu.h). Android's scheduler moves a busy thread
// between clusters freely; on this phone that put the renderer on a little core most of the time.
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#include "cpu.h"

namespace cpu {

namespace {

constexpr int kMaxCores = 32;

struct Cores {
    int fast[kMaxCores];
    int fast_n = 0;
    int slow[kMaxCores];
    int slow_n = 0;
    char fast_list[64] = {};
};

long MaxFreq(int core) {
    char path[96];
    std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", core);
    FILE* f = std::fopen(path, "r");
    if (!f) return 0;
    long khz = 0;
    if (std::fscanf(f, "%ld", &khz) != 1) khz = 0;
    std::fclose(f);
    return khz;
}

// some kernels hide the frequencies but still give each core's capacity
long Capacity(int core) {
    char path[96];
    std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpu_capacity", core);
    FILE* f = std::fopen(path, "r");
    if (!f) return 0;
    long cap = 0;
    if (std::fscanf(f, "%ld", &cap) != 1) cap = 0;
    std::fclose(f);
    return cap;
}

Cores Read() {
    Cores c;
    long speed[kMaxCores] = {};
    long best = 0;
    int n = 0;
    for (int i = 0; i < kMaxCores; i++) {
        long s = MaxFreq(i);
        if (s <= 0) s = Capacity(i);
        if (s <= 0) continue;
        speed[i] = s;
        if (s > best) best = s;
        n = i + 1;
    }
    // one kind of core, or nothing to read: leave the scheduler alone
    if (!best) return c;
    for (int i = 0; i < n; i++) {
        if (speed[i] <= 0) continue;
        // a slower cluster is well under the fastest; a big cluster's cores are within a few percent
        if (speed[i] * 100 >= best * 95) {
            if (c.fast_n < kMaxCores) c.fast[c.fast_n++] = i;
        } else {
            if (c.slow_n < kMaxCores) c.slow[c.slow_n++] = i;
        }
    }
    if (c.fast_n == 0 || c.slow_n == 0) return Cores{};
    size_t at = 0;
    for (int i = 0; i < c.fast_n && at + 4 < sizeof c.fast_list; i++)
        at += size_t(std::snprintf(c.fast_list + at, sizeof c.fast_list - at, i ? ",%d" : "%d", c.fast[i]));
    return c;
}

const Cores& All() {
    static const Cores c = Read();
    return c;
}

bool Pin(const int* cores, int n) {
    if (n <= 0) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < n; i++) CPU_SET(cores[i], &set);
    // 0: the calling thread, which is all Bionic lets an app set
    return sched_setaffinity(0, sizeof set, &set) == 0;
}

}  // namespace

bool PinFast() { return Pin(All().fast, All().fast_n); }

bool PinSlow() { return Pin(All().slow, All().slow_n); }

const char* FastList() { return All().fast_list; }

long ThreadId() { return long(::syscall(SYS_gettid)); }

}  // namespace cpu
