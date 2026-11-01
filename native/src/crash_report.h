#pragma once
#include <cstddef>

namespace tmvr {

// Logs the call stack (module+offset) of a stack overflow anywhere in the process to
// TrackmaniaVR.log before the game dies. The stack is walked from a separate thread, since the
// overflowing thread has almost no stack left.
void InstallCrashReporter();

// Reserves stack for exception handling on the calling thread, so the reporter's handler can
// still run there after an overflow.
void EnsureStackGuarantee();

// Names a code region outside any module (e.g. another overlay's hook stub) for reports.
void RegisterCodeRange(const void* start, size_t length, const char* name);

// "module.dll+0x1234", or a registered range name, for log messages.
void DescribeCode(const void* address, char* out, size_t size);

} // namespace tmvr
