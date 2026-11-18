#pragma once
// Phones mix fast and little cores: a hot thread on a little core takes close to twice as long. The game thread and
// the renderer are pinned to the fast cores so they never fall to a little one; everything else is left to Android.
namespace cpu {

// pins the calling thread to the phone's fastest cores; false when the phone has one kind of core only
bool PinFast();
// pins the calling thread to the cores that are not the fastest, for the threads that must not take them
bool PinSlow();
// the fast cores, comma separated, for the log; empty when there are none
const char* FastList();
// the calling thread's id, for the log
long ThreadId();

}  // namespace cpu
