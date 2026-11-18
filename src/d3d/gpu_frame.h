#pragma once
#include <cstdint>

namespace gpu {
// until the GPU has drawn present number n, counted from 0; any thread
void WaitGpuFrame(uint64_t n);
}  // namespace gpu

namespace dynres {
// the host's dynamic resolution: how long the GPU held the frame before, in milliseconds
void GpuWait(double ms);
}  // namespace dynres
