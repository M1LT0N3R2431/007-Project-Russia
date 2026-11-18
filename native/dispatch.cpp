// dispatch::InCode: src/d3d/d3d8.cpp asks it of the runtime; the SDK this game was recompiled with predates the
// function (the newer one has it as cpu/dispatch.cpp:90), so it is mirrored here over the same bounds
#include "cpu/x86_context.h"

namespace dispatch {

bool InCode(uint32_t va) { return va >= X86CodeBase && va < X86CodeEnd; }

}  // namespace dispatch
