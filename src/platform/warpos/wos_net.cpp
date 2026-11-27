// Network sessions on WarpOS: not available yet (bsdsocket.library from the
// PPC is a later milestone). No service provider is listed, so the game's
// network menu offers nothing; every other entry point reports that the
// service is unavailable instead of pretending to work.
#include "port/net.h"

extern "C" HRESULT Net_EnumProviders(NetProviderCallback callback, void *context)
{
    (void)callback;
    (void)context;
    return NET_OK;
}

extern "C" HRESULT Net_InitializeConnection(int provider)
{
    (void)provider;
    return NET_ERR_UNAVAILABLE;
}

extern "C" HRESULT Net_EnumSessions(const NetGuid *application, DWORD timeout, NetSessionCallback callback,
                                    void *context)
{
    (void)application;
    (void)timeout;
    (void)callback;
    (void)context;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_Open(const NetSessionDesc *desc, DWORD flags)
{
    (void)desc;
    (void)flags;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_Close(void)
{
    return NET_OK;
}

extern "C" HRESULT Net_GetSessionDesc(NetSessionDesc *desc)
{
    (void)desc;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_SetSessionDesc(const NetSessionDesc *desc)
{
    (void)desc;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_CreatePlayer(NetPlayerID *id, const NetName *name, const void *data, DWORD dataSize)
{
    (void)id;
    (void)name;
    (void)data;
    (void)dataSize;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_DestroyPlayer(NetPlayerID id)
{
    (void)id;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_EnumPlayers(NetPlayerCallback callback, void *context)
{
    (void)callback;
    (void)context;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_SetPlayerData(NetPlayerID id, const void *data, DWORD dataSize)
{
    (void)id;
    (void)data;
    (void)dataSize;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_Send(NetPlayerID from, NetPlayerID to, BOOL guaranteed, const void *data, DWORD dataSize)
{
    (void)from;
    (void)to;
    (void)guaranteed;
    (void)data;
    (void)dataSize;
    return NET_ERR_UNINITIALIZED;
}

extern "C" HRESULT Net_Receive(NetPlayerID *from, NetPlayerID *to, void *data, DWORD *dataSize)
{
    (void)from;
    (void)to;
    (void)data;
    (void)dataSize;
    return NET_ERR_NOMESSAGES;
}
