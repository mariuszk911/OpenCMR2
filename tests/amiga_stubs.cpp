// Stubs on the host: the black movie frame, the unavailable network and
// the no-op Amiga profiling hooks. The Amiga target compiles the same
// translation units, so this test protects the contract the game relies on.
#include "diagnostics/timing.h"
#include "port/gfx.h"
#include "port/movie.h"
#include "port/net.h"

#include <cstdio>

namespace {

int clearCount;
int presentCount;
int logCount;
DWORD lastClearFlags;
DWORD lastClearColor;
BOOL lastVsync;

int providerCalls;
int sessionCalls;
int playerCalls;

struct FakeObserver : Diagnostics::Observer {};
FakeObserver fakeObserver;

BOOL CountProvider(int, const char *, void *)
{
    ++providerCalls;
    return TRUE;
}

BOOL CountSession(const NetSessionDesc *, void *)
{
    ++sessionCalls;
    return TRUE;
}

BOOL CountPlayer(NetPlayerID, const NetName *, void *)
{
    ++playerCalls;
    return TRUE;
}

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

extern "C" void Gfx_Clear(DWORD flags, DWORD color, float z)
{
    (void)z;
    ++clearCount;
    lastClearFlags = flags;
    lastClearColor = color;
}

extern "C" void Gfx_Present(BOOL vsync)
{
    ++presentCount;
    lastVsync = vsync;
}

extern "C" void Sys_Log(const char *format, ...)
{
    (void)format;
    ++logCount;
}

int main()
{
    // ---- movie ------------------------------------------------------------
    Movie *movie = Movie_Open("Movies\\intro.bik", 0);
    if (!movie) {
        std::fprintf(stderr, "FAIL: Movie_Open succeeds without the file\n");
        return 1;
    }
    Check(Movie_GetWidth(movie) == 640 && Movie_GetHeight(movie) == 480, "movie is 640x480");
    Check(Movie_GetFrameCount(movie) == 1, "movie has one frame");
    Check(Movie_GetFrameNumber(movie) == 1, "frame numbers start at 1");

    Movie_DecodeFrame(movie);
    Movie_Present(movie, 0, 0, 640, 480);
    Check(clearCount == 1, "present clears the frame");
    Check(presentCount == 1, "present presents once");
    Check(lastClearColor == 0xff000000, "the stub frame is black");
    Check((lastClearFlags & GFX_CLEAR_TARGET) != 0, "present clears the target");
    Check(lastVsync == TRUE, "present uses vsync as the host path does");

    Movie_NextFrame(movie);
    Check(Movie_GetFrameNumber(movie) == 2, "next frame advances the frame number");
    Check(Movie_GetFrameNumber(movie) > Movie_GetFrameCount(movie),
          "the intro loop ends after one frame");
    Movie_NextFrame(movie);
    Check(Movie_GetFrameNumber(movie) == 2, "next frame does not run past the count");
    Check(Movie_Wait(movie) == FALSE, "the stub never waits");
    Movie_Close(movie);

    Movie *unnamed = Movie_Open(NULL, 0);
    Check(unnamed != NULL, "Movie_Open accepts a null path");
    Movie_Close(unnamed);
    Movie_Close(NULL);
    Check(logCount == 2, "the stub logs each open once");

    // ---- network ----------------------------------------------------------
    NetGuid application = {};
    NetSessionDesc desc = {};
    NetName name = {};
    NetPlayerID player = 0;
    NetPlayerID receiver = 0;
    DWORD size = 0;
    Check(Net_EnumProviders(CountProvider, NULL) == NET_ERR_UNAVAILABLE, "no providers");
    Check(Net_InitializeConnection(0) == NET_ERR_UNAVAILABLE, "no provider connection");
    Check(Net_EnumSessions(&application, 1000, CountSession, NULL) == NET_ERR_UNAVAILABLE,
          "no sessions");
    Check(Net_Open(&desc, NET_OPEN_CREATE) == NET_ERR_UNAVAILABLE, "cannot host");
    Check(Net_Close() == NET_ERR_UNAVAILABLE, "close reports unavailable");
    Check(Net_GetSessionDesc(&desc) == NET_ERR_UNAVAILABLE, "no session description");
    Check(Net_SetSessionDesc(&desc) == NET_ERR_UNAVAILABLE, "cannot change the description");
    Check(Net_CreatePlayer(&player, &name, NULL, 0) == NET_ERR_UNAVAILABLE, "no local player");
    Check(Net_DestroyPlayer(0) == NET_ERR_UNAVAILABLE, "no local player to destroy");
    Check(Net_EnumPlayers(CountPlayer, NULL) == NET_ERR_UNAVAILABLE, "no players");
    Check(Net_SetPlayerData(0, NULL, 0) == NET_ERR_UNAVAILABLE, "no player data");
    Check(Net_Send(0, NET_ID_ALL, TRUE, NULL, 0) == NET_ERR_UNAVAILABLE, "cannot send");
    Check(Net_Receive(&player, &receiver, NULL, &size) == NET_ERR_UNAVAILABLE, "cannot receive");
    Check(providerCalls == 0 && sessionCalls == 0 && playerCalls == 0,
          "the stub never invokes a callback");

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
