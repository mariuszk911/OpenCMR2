// Profiling is host-only for now. The Amiga target keeps the observer hook
// (src/diagnostics/observer.cpp) but does not sample or write timings, so the
// boot path has no SDL dependency and no per-frame file I/O.
#include "diagnostics/timing.h"

namespace Diagnostics {
void StartProfileFromEnvironment() {}
void StopProfile() {}
}
