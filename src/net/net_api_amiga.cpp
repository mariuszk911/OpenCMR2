// port/net.h on AmigaOS: net::Session (UDP with ENet) over bsdsocket.library,
// serviced cooperatively by the game's own task (net/net_service.h). The
// platform layer calls Net_Pump from the event poll and the file reads, so the
// connections keep being answered while the game loads a stage.
//
// Settings, [net] in opencmr2.ini, same names and defaults as the host:
//
//   port = 28620        UDP port hosted on and searched at
//   peers = a, b:2000   more hosts to search (names or addresses); a name is
//                       resolved by the stack's resolver, which blocks, at
//                       most once every 30 s while the session list is open
//   broadcast = 1       search the local network
//
// There is no `tailscale` setting on this platform (no process to ask).
//
// Never includes SDL, platform/platform.h or the NDK: Domain B, plain port/
// interfaces only (enet_bsdsocket.c holds the OS calls).

#include "net/enet_platform.h"
#include "net/net_api_common.h"
#include "net/net_payload.h"
#include "platform/amiga/amiga_sys.h"
#include "port/net.h"
#include "port/sys.h"

namespace {

net::Config LoadConfig()
{
    net::Config config;
    int port = Sys_GetOption("net.port", config.port);
    if (port > 0 && port < 65536)
        config.port = (uint16_t)port;
    config.peers = net::SplitList(Sys_GetOptionText("net.peers", ""));
    config.broadcast = Sys_GetOption("net.broadcast", 1) != 0;
    config.log = Sys_Log;
    config.now = Sys_GetTicks;
    config.seed = [] { return ((uint64_t)Sys_GetUnixTime() << 20) ^ (uint64_t)Sys_GetTicks(); };
    return config;
}

uint32_t s_lastPump;

void Shutdown()
{
    net::ApiShutdown();
    Sys_Log("net: stopped (longest time between pumps %lu ms)", AmigaSys_GetPumpGapMax());
}

} // namespace

// Called from the game's task by the platform layer (AmigaSys_SetBackgroundPump).
extern "C" void Net_Pump(void)
{
    net::Session *session = net::ApiSession();
    if (session == NULL)
        return;
    // A long silence is worth one line: it is the time a peer waits unanswered.
    uint32_t now = Sys_GetTicks();
    if (s_lastPump != 0 && now - s_lastPump > 2000)
        Sys_Log("net: %lu ms since the last pump", (unsigned long)(now - s_lastPump));
    s_lastPump = now;
    session->Pump();
}

// A connection exists only when the stack is running: opening its library is
// the check, and the reason is logged when it fails.
extern "C" HRESULT Net_EnumProviders(NetProviderCallback callback, void *context)
{
    if (enet_initialize() != 0) {
        Sys_Log("net: no network provider offered");
        return NET_ERR_UNAVAILABLE;
    }
    enet_deinitialize();
    if (callback != NULL)
        callback(0, "Internet / LAN (UDP)", context);
    return NET_OK;
}

extern "C" HRESULT Net_InitializeConnection(int provider)
{
    if (provider != 0)
        return NET_ERR_INVALIDPARAMS;
    // The conversion of the game's payloads assumes how this compiler lays
    // out bitfields; if it does not hold, play would corrupt the other side.
    if (!net::PayloadLayoutSelfTest()) {
        Sys_Log("net: the compiler's bitfield layout is not the one the payload conversion models; network off");
        return NET_ERR_UNAVAILABLE;
    }
    HRESULT result = net::ApiStart(LoadConfig());
    if (result == NET_OK) {
        s_lastPump = Sys_GetTicks();
        AmigaSys_SetBackgroundPump(Net_Pump);
        AmigaSys_SetShutdownHook(Shutdown);
    }
    return result;
}
