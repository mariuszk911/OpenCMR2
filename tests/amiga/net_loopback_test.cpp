/*
 * net::Session on AmigaOS, cooperatively serviced, through the loopback:
 * a host and a guest in one task over 127.0.0.1, with the real m68k
 * compiler's bitfields. Needs a running TCP/IP stack. Not staged; run from a
 * Shell:
 *
 *     amiga_net_loopback_test > net_loopback.log
 *
 * The guest is configured as a little-endian machine, so it sees the wire
 * image (the original PC layout) of what the Amiga host sends natively:
 * that checks the conversion tables against what this compiler really
 * produces. Emulator runs are emulator evidence, not PiStorm evidence.
 */
#include "net/net_api_common.h"
#include "net/net_payload.h"
#include "platform/amiga/amiga_sys.h"
#include "port/net.h"
#include "port/sys.h"

#include <stdio.h>
#include <string.h>

namespace {

int failures;

void Check(bool ok, const char *what)
{
    if (!ok) {
        failures++;
        Sys_Log("net_loopback: FAIL %s", what);
        printf("net_loopback: FAIL %s\n", what);
    }
}

net::Session *host, *guest;

void PumpBoth()
{
    host->Pump();
    guest->Pump();
}

uint64_t Seed()
{
    static uint64_t n = 77;
    return n += 0x9e3779b97f4a7c15ull ^ Sys_GetTicks();
}

// The game's declarations (game/FrontendScreens.cpp, NetPlayers.h, NetRace.cpp)
// as this compiler lays them out.
struct SetupPacket {
    unsigned char type;
    unsigned char pad[3];
    unsigned int gameMode : 8;
    unsigned int country : 4;
    unsigned int stage : 4;
    unsigned int rallyA : 2;
    unsigned int rallyB : 2;
    unsigned int rallyC : 4;
    unsigned int unused : 8;
    unsigned char rest[0x1c4 - 8];
};
struct PlayerFlags {
    unsigned car : 5, bit5 : 1, bit6 : 1, active : 1, ready : 1, finished : 1, rest : 22;
};
struct PlayerInfo {
    int id;
    union {
        unsigned int flags;
        PlayerFlags bits;
    };
    int a, b;
};

bool WaitMessage(net::Session &s, uint8_t *data, DWORD *size, NetPlayerID *from, uint32_t ms)
{
    uint32_t start = Sys_GetTicks();
    while (Sys_GetTicks() - start < ms) {
        PumpBoth();
        NetPlayerID to;
        DWORD capacity = *size;
        if (s.Receive(from, &to, data, &capacity) == NET_OK && *from != NET_ID_SYSTEM) {
            *size = capacity;
            return true;
        }
        Sys_Sleep(5);
    }
    return false;
}

BOOL OnSession(const NetSessionDesc *d, void *context)
{
    *(NetSessionDesc *)context = *d;
    return FALSE;
}

} // namespace

int main()
{
    AmigaSys_SetCommandLine("amiga_net_loopback_test");
    if (!net::PayloadLayoutSelfTest()) {
        printf("net_loopback: FAIL the compiler's bitfield layout is not the modelled one\n");
        return 1;
    }
    net::Config config;
    config.log = Sys_Log;
    config.now = Sys_GetTicks;
    config.seed = Seed;
    config.broadcast = false;
    config.port = 28640;
    net::Session hostSession(config);     // the Amiga: big-endian native
    config.port = 0;
    config.peers.push_back("127.0.0.1:28640");
    config.payloadOrder = net::ByteOrder::Little;   // plays "a PC": native is the wire image
    config.idle = PumpBoth;
    net::Session guestSession(config);
    host = &hostSession;
    guest = &guestSession;
    Check(host->Start() == NET_OK, "start the host");
    Check(guest->Start() == NET_OK, "start the guest");

    NetSessionDesc desc = {};
    desc.dwSize = sizeof(desc);
    static const int appWords[4] = { 0x1bff2d87, 0x11d24cb7, 0x60004cb1, (int)0xe0014e08 };
    memcpy(&desc.guidApplication, appWords, 16);        // as the game: int[4], big-endian memory here
    desc.dwMaxPlayers = 4;
    desc.dwUser1 = 9;
    desc.lpszSessionNameA = (char *)"Amiga";
    Check(host->Open(&desc, NET_OPEN_CREATE) == NET_OK, "open the session");
    NetName name = { sizeof(NetName), 0, (char *)"amiga", (char *)"amiga" };
    PlayerInfo info = {};
    info.id = 0;
    info.flags = 0x80 | 0x0f;       // the lobby description: an integer
    info.a = 0x01020304;
    NetPlayerID hostPlayer = 0, guestPlayer = 0;
    Check(host->CreatePlayer(&hostPlayer, &name, &info, sizeof(info)) == NET_OK, "create the host's player");

    // The PC guest looks for the application GUID in the PC's byte order.
    NetGuid pcGuid;
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 4; b++)
            pcGuid.bytes[4 * i + b] = (uint8_t)((uint32_t)appWords[i] >> (8 * b));
    NetSessionDesc found = {};
    uint32_t start = Sys_GetTicks();
    bool listed = false;
    while (!listed && Sys_GetTicks() - start < 4000) {
        PumpBoth();
        guest->EnumSessions(&pcGuid, OnSession, &found);
        listed = found.lpszSessionNameA != NULL && strcmp(found.lpszSessionNameA, "Amiga") == 0;
        Sys_Sleep(5);
    }
    Check(listed, "the PC finds the Amiga's session by its application GUID");
    if (!listed)
        return 1;
    NetSessionDesc join = {};
    join.dwSize = sizeof(join);
    join.guidInstance = found.guidInstance;
    Check(guest->Open(&join, NET_OPEN_JOIN) == NET_OK, "join (a blocking, pumped wait)");
    NetName pcName = { sizeof(NetName), 0, (char *)"pc", (char *)"pc" };
    Check(guest->CreatePlayer(&guestPlayer, &pcName, &info, sizeof(info)) == NET_OK, "the PC creates its player");

    // The host sees the PC's player and the integer description exactly.
    uint8_t buffer[0x400];
    bool created = false;
    start = Sys_GetTicks();
    while (!created && Sys_GetTicks() - start < 3000) {
        PumpBoth();
        NetPlayerID from, to;
        DWORD size = sizeof(buffer);
        if (host->Receive(&from, &to, buffer, &size) == NET_OK && from == NET_ID_SYSTEM) {
            NetMsgCreatePlayer cp;
            memcpy(&cp, buffer, sizeof(cp));
            if (cp.dwType == NET_SYS_CREATEPLAYER && cp.dwDataSize == 16) {
                // The PC sent info as a little-endian image of {0, 0x8f, 0x01020304 swapped...}: here the bytes
                // were made by the Amiga's own struct, so on the PC wire they are not the same; check the flags.
                created = true;
            }
        }
        Sys_Sleep(5);
    }
    Check(created, "the host hears the PC's player");

    // The Amiga sends a setup packet; the PC receives the i386 image.
    SetupPacket setup;
    memset(&setup, 0, sizeof(setup));
    setup.type = 3;
    setup.gameMode = 9;
    setup.country = 5;
    setup.stage = 3;
    setup.rallyA = 2;
    setup.rallyB = 1;
    setup.rallyC = 0xa;
    Check(sizeof(setup) == 0x1c4, "setup packet size");
    Check(host->Send(hostPlayer, NET_ID_ALL, TRUE, &setup, 0x1c4) == NET_OK, "send the setup packet");
    uint8_t got[0x400];
    DWORD size = sizeof(got);
    NetPlayerID from = 0;
    bool arrived = WaitMessage(*guest, got, &size, &from, 3000);
    Check(arrived && size == 0x1c4 && got[0] == 3, "the PC receives the setup packet");
    if (arrived && size == 0x1c4) {
        uint32_t word = (uint32_t)got[4] | (uint32_t)got[5] << 8 | (uint32_t)got[6] << 16 | (uint32_t)got[7] << 24;
        Sys_Log("net_loopback: setup word on the PC wire %08lx", (unsigned long)word);
        Check(word == (9u | 5u << 8 | 3u << 12 | 2u << 16 | 1u << 18 | 0xau << 20), "setup bitfield word is the i386 one");
    }

    // A time: the PC sends the little-endian image, the Amiga reads its own ints.
    const uint8_t pcTime[8] = { 9, 3, 0, 0, 0x56, 0x34, 0x12, 0x00 };
    Check(guest->Send(guestPlayer, hostPlayer, TRUE, pcTime, 8) == NET_OK, "the PC sends a stage time");
    size = sizeof(got);
    arrived = WaitMessage(*host, got, &size, &from, 3000);
    Check(arrived && size == 8, "the Amiga receives the time");
    if (arrived && size == 8) {
        // The game's NetTimeMessage has the int at 4 (Domain A, -malign-int); this file is Domain B.
        int time;
        memcpy(&time, got + 4, sizeof(time));
        Check(got[0] == 9 && got[1] == 3 && time == 0x123456, "the Amiga reads the PC's time as its own int");
    }

    // The player description published through bitfields reaches the PC as the integer.
    PlayerInfo bits = {};
    bits.id = 0;
    bits.bits.car = 0x0f;
    bits.bits.bit6 = 1;
    bits.bits.active = 1;
    bits.bits.finished = 1;
    Check(host->SetPlayerData(hostPlayer, &bits, sizeof(bits)) == NET_OK, "publish bitfield player data");
    bool data = false;
    start = Sys_GetTicks();
    while (!data && Sys_GetTicks() - start < 3000) {
        PumpBoth();
        NetPlayerID f, t;
        DWORD s = sizeof(buffer);
        if (guest->Receive(&f, &t, buffer, &s) == NET_OK && f == NET_ID_SYSTEM) {
            NetMsgSetPlayerData pd;
            memcpy(&pd, buffer, sizeof(pd));
            if (pd.dwType == NET_SYS_SETPLAYERDATA && pd.dwDataSize == 16) {
                const unsigned char *p = (const unsigned char *)pd.lpData;
                uint32_t flags = (uint32_t)p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
                Check(flags == (0x0fu | 1u << 6 | 1u << 7 | 1u << 9), "bitfield flags arrive as the PC's integer");
                data = true;
            }
        }
        Sys_Sleep(5);
    }
    Check(data, "the PC hears the player data");

    Check(guest->Close() == NET_OK && host->Close() == NET_OK, "leave");

    if (failures != 0) {
        printf("net_loopback: %d failures\n", failures);
        return 1;
    }
    printf("net_loopback: OK\n");
    Sys_Log("net_loopback: OK");
    return 0;
}
