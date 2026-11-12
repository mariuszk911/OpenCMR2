// See net_api_common.h.

#include "net/net_api_common.h"

#include "port/net.h"

#include <memory>

namespace {

std::unique_ptr<net::Session> s_session;

} // namespace

namespace net {

std::vector<std::string> SplitList(const char *list)
{
    std::vector<std::string> out;
    std::string item;
    for (const char *p = list != NULL ? list : ""; ; p++) {
        if (*p == ',' || *p == ' ' || *p == '\t' || *p == '\0') {
            if (!item.empty())
                out.push_back(item);
            item.clear();
            if (*p == '\0')
                break;
        } else {
            item += *p;
        }
    }
    return out;
}

HRESULT ApiStart(const Config &config)
{
    if (!s_session)
        s_session.reset(new Session(config));
    return s_session->Start();
}

Session *ApiSession()
{
    return s_session.get();
}

void ApiShutdown()
{
    s_session.reset();
}

} // namespace net

extern "C" HRESULT Net_EnumSessions(const NetGuid *application, DWORD, NetSessionCallback callback, void *context)
{
    return s_session ? s_session->EnumSessions(application, callback, context) : NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_Open(const NetSessionDesc *desc, DWORD flags)
{
    return s_session ? s_session->Open(desc, flags) : NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_Close(void)
{
    return s_session ? s_session->Close() : NET_OK;
}

extern "C" HRESULT Net_GetSessionDesc(NetSessionDesc *desc)
{
    return s_session ? s_session->GetSessionDesc(desc) : NET_ERR_NOCONNECTION;
}

extern "C" HRESULT Net_SetSessionDesc(const NetSessionDesc *desc)
{
    return s_session ? s_session->SetSessionDesc(desc) : NET_ERR_NOCONNECTION;
}

extern "C" HRESULT Net_CreatePlayer(NetPlayerID *id, const NetName *name, const void *data, DWORD dataSize)
{
    return s_session ? s_session->CreatePlayer(id, name, data, dataSize) : NET_ERR_NOCONNECTION;
}

extern "C" HRESULT Net_DestroyPlayer(NetPlayerID id)
{
    return s_session ? s_session->DestroyPlayer(id) : NET_ERR_INVALIDPLAYER;
}

extern "C" HRESULT Net_EnumPlayers(NetPlayerCallback callback, void *context)
{
    return s_session ? s_session->EnumPlayers(callback, context) : NET_ERR_NOCONNECTION;
}

extern "C" HRESULT Net_SetPlayerData(NetPlayerID id, const void *data, DWORD dataSize)
{
    return s_session ? s_session->SetPlayerData(id, data, dataSize) : NET_ERR_NOCONNECTION;
}

extern "C" HRESULT Net_Send(NetPlayerID from, NetPlayerID to, BOOL guaranteed, const void *data, DWORD dataSize)
{
    return s_session ? s_session->Send(from, to, guaranteed, data, dataSize) : NET_ERR_NOCONNECTION;
}

extern "C" HRESULT Net_Receive(NetPlayerID *from, NetPlayerID *to, void *data, DWORD *dataSize)
{
    return s_session ? s_session->Receive(from, to, data, dataSize) : NET_ERR_NOMESSAGES;
}
