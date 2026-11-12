// port/net.h over net::Session (UDP with ENet): one connection, "Internet /
// LAN (UDP)". Settings, [net] in opencmr2.ini:
//
//   port = 28620        UDP port hosted on and searched at
//   peers = a, b:2000   more hosts to search (names or addresses), for the
//                       internet: the host forwards the UDP port
//   tailscale = auto    also search the Tailscale peers that are online
//                       (auto: when the tailscale command is installed)
//   broadcast = 1       search the local network

#include "net/net_api_common.h"
#include "platform/platform.h"
#include "port/net.h"
#include "port/sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

namespace {

// Online Tailscale peers from `tailscale status`: one line per machine,
// its Tailscale address first and "offline" in the status when it is.
std::vector<std::string> TailscalePeers()
{
    std::vector<std::string> peers;
#ifdef _WIN32
    FILE *f = _popen("tailscale status 2>NUL", "r");
#else
    FILE *f = popen("tailscale status 2>/dev/null", "r");
#endif
    if (f == NULL)
        return peers;
    char line[512];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#' || strstr(line, "offline") != NULL)
            continue;
        unsigned a, b, c, d;
        char rest;
        if (sscanf(line, "%u.%u.%u.%u%c", &a, &b, &c, &d, &rest) == 5 && a < 256 && b < 256 && c < 256 && d < 256 &&
            (rest == ' ' || rest == '\t'))
            peers.push_back(std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + "." +
                            std::to_string(d));
    }
#ifdef _WIN32
    _pclose(f);
#else
    pclose(f);
#endif
    if (!peers.empty())
        Sys_Log("net: %u Tailscale peers online", (unsigned)peers.size());
    return peers;
}

net::Config LoadConfig()
{
    net::Config config;
    int port = Platform_GetSettingInt("net.port", config.port);
    if (port > 0 && port < 65536)
        config.port = (uint16_t)port;
    config.peers = net::SplitList(Platform_GetSetting("net.peers", ""));
    config.broadcast = Platform_GetSettingInt("net.broadcast", 1) != 0;
    const char *tailscale = Platform_GetSetting("net.tailscale", "auto");
    if (SDL_strcasecmp(tailscale, "0") != 0 && SDL_strcasecmp(tailscale, "off") != 0)
        config.extraPeers = TailscalePeers;
    config.log = Sys_Log;
    return config;
}

} // namespace

extern "C" HRESULT Net_EnumProviders(NetProviderCallback callback, void *context)
{
    if (callback != NULL)
        callback(0, "Internet / LAN (UDP)", context);
    return NET_OK;
}

extern "C" HRESULT Net_InitializeConnection(int provider)
{
    if (provider != 0)
        return NET_ERR_INVALIDPARAMS;
    return net::ApiStart(LoadConfig());
}
