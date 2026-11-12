// What the host (net_api.cpp) and the AmigaOS (net_api_amiga.cpp) entry points
// share: the one Session behind port/net.h, and the Net_* functions that only
// forward to it. Each platform file provides Net_EnumProviders and
// Net_InitializeConnection, which are where settings are read and the
// transport differs.

#ifndef OPENCMR2_NET_API_COMMON_H
#define OPENCMR2_NET_API_COMMON_H

#include "net/net_session.h"

#include <string>
#include <vector>

namespace net {

// "a, b:2000" -> {"a", "b:2000"} (commas, spaces and tabs separate).
std::vector<std::string> SplitList(const char *list);

// Creates the session from the first configuration given, and starts it.
HRESULT ApiStart(const Config &config);

// The session, or null before ApiStart.
Session *ApiSession();

// Destroys the session (closing the socket) and the transport behind it.
void ApiShutdown();

} // namespace net

#endif
