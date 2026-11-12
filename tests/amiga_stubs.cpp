// Stubs on the host: the no-op Amiga profiling hooks. The Amiga target compiles the same translation units, so this test
// protects the contract the game relies on. The movie player's fallback (one
// black frame) is tested with the player itself, in tests/movie_amiga.cpp.
#include "diagnostics/timing.h"

#include <cstdio>

namespace {

struct FakeObserver : Diagnostics::Observer {};
FakeObserver fakeObserver;

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    // ---- diagnostics ------------------------------------------------------
    Check(Diagnostics::observer == NULL, "no profile observer by default");
    Diagnostics::StartProfileFromEnvironment();
    Diagnostics::StopProfile();
    Check(Diagnostics::observer == NULL, "profiling stays off on Amiga");
    Diagnostics::observer = &fakeObserver;
    Diagnostics::StopProfile();
    Check(Diagnostics::observer == &fakeObserver, "the no-op does not clear a host observer");
    Diagnostics::observer = NULL;

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("amiga stubs: ok\n");
    return 0;
}
