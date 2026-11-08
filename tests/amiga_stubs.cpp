// Stubs on the host: the unavailable network and the no-op Amiga profiling
// hooks. The Amiga target compiles the same translation units, so this test
// protects the contract the game relies on. The movie player's fallback (one
// black frame) is tested with the player itself, in tests/movie_amiga.cpp.
#include "diagnostics/timing.h"
#include "port/net.h"

#include <cstdio>

namespace {

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

int main()
{
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
