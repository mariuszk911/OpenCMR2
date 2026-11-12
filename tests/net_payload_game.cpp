// Pins the wire layout of the game's network payloads against the game itself.
//
// The wire format is the original PC layout (docs/NETWORK.md, net_payload.h).
// On a little-endian build the game's structures are that layout, so this test
// drives the real senders and receive handlers (linked from the game, with
// the Net_* entry points replaced by a recorder) and checks sizes, offsets and
// bitfield positions against the table the big-endian conversion is built on.
// A change to a game structure that moves a field fails here before it breaks
// cross-play between machines of different byte order.
#include "port/types.h"
#include "port/net.h"
#include "Car.h"
#include "Game.h"
#include "Sector.h"
#include "GameInfo.h"
#include "NetPlayers.h"
#include "NetworkLeaderboards.h"
#include "RallyData.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the layout pin runs on little-endian hosts: there the game's structures are the wire images"
#endif

struct NetSetupPacket;
extern void FrontendNetwork_WriteSetupPacket(NetSetupPacket *);
extern void FrontendNetwork_SendSetupPacket(void);
extern void FrontendNetwork_ReadSetupPacket(NetSetupPacket *);
extern void FrontendNetwork_SendPlayerDescription(void);
extern char FrontendNetwork_SendLobbyPlayerDescription(void);
extern void NetRace_SendLocalCarState(NetStats *);
extern void NetRace_SendStageAndOverallTimes(int);
extern void NetRace_SendSplitTime(int, int);
extern void NetRace_SendStageTime(int, int);
extern void NetRace_SendReady(void);
extern void NetRace_SendResultsQuit(void);
extern void NetRace_DispatchRacePacket(DWORD *, BYTE *);
extern void NetPlayers_SendCarClass(BYTE);
extern void NetPlayers_SendType16Notification(void);
extern void NetPlayers_SendPublishedLeaderboard(void);
extern void NetworkChat_SendLine(char *);
extern BYTE *NetworkLeaderboard_GetPublishedBoard(void);
extern void NetworkLeaderboard_PublishBoard(BYTE *);
extern unsigned char RallyDataCountryIndex(void);
extern unsigned char RallyDataStageIndex(void);
extern RallyDriverGroupTable *RallyData_GetDriverGroupTable(void);
extern void RallyData_SetCountrySelectionBits(BYTE);
extern void RallyData_SetStageSelectionAndRefreshFlags(BYTE);
extern void RallyData_SetSelectionBits10To11(BYTE);
extern void RallyData_SetSelectionBits12To13(BYTE);
extern void RallyData_SetSelectionBits16To19(BYTE);
extern void GameInfo_SetConfiguredGameMode(BYTE);
extern BYTE RallyData_GetDriverRecordSelectionValue(BYTE);
extern int g_unk0x00818ce4;
extern NetStats g_localCarStats;
extern CarNetRecord g_carNetRecords[8];
extern int NetRace_DecodeReceivedCarState(CarNetRecord *, int *);

// ---- the recorder that stands in for the network layer ----------------------------------------

struct Sent {
    int guaranteed;
    std::vector<uint8_t> data;
};
static std::vector<Sent> sent;
static std::vector<uint8_t> createdData, playerData;

extern "C" {
HRESULT Net_EnumProviders(NetProviderCallback, void *) { return NET_OK; }
HRESULT Net_InitializeConnection(int) { return NET_OK; }
HRESULT Net_EnumSessions(const NetGuid *, DWORD, NetSessionCallback, void *) { return NET_OK; }
HRESULT Net_Open(const NetSessionDesc *, DWORD) { return NET_OK; }
HRESULT Net_Close(void) { return NET_OK; }
HRESULT Net_GetSessionDesc(NetSessionDesc *) { return NET_ERR_NOCONNECTION; }
HRESULT Net_SetSessionDesc(const NetSessionDesc *) { return NET_OK; }
HRESULT Net_CreatePlayer(NetPlayerID *id, const NetName *, const void *data, DWORD size)
{
    *id = 0x00010001;
    createdData.assign((const uint8_t *)data, (const uint8_t *)data + size);
    return NET_OK;
}
HRESULT Net_DestroyPlayer(NetPlayerID) { return NET_OK; }
HRESULT Net_EnumPlayers(NetPlayerCallback, void *) { return NET_OK; }
HRESULT Net_SetPlayerData(NetPlayerID, const void *data, DWORD size)
{
    playerData.assign((const uint8_t *)data, (const uint8_t *)data + size);
    return NET_OK;
}
HRESULT Net_Send(NetPlayerID, NetPlayerID, BOOL guaranteed, const void *data, DWORD size)
{
    sent.push_back({ guaranteed, std::vector<uint8_t>((const uint8_t *)data, (const uint8_t *)data + size) });
    return NET_OK;
}
HRESULT Net_Receive(NetPlayerID *, NetPlayerID *, void *, DWORD *) { return NET_ERR_NOMESSAGES; }
}

static int failures;
static void Check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "net payload game: FAILED %s\n", what);
        failures++;
    }
}

static uint32_t U32(const std::vector<uint8_t> &d, size_t at)
{
    return (uint32_t)d[at] | (uint32_t)d[at + 1] << 8 | (uint32_t)d[at + 2] << 16 | (uint32_t)d[at + 3] << 24;
}
static uint16_t U16(const std::vector<uint8_t> &d, size_t at)
{
    return (uint16_t)(d[at] | d[at + 1] << 8);
}
static void PutU32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static const Sent &Last(const char *what, size_t size, int guaranteed)
{
    static Sent none;
    if (sent.empty()) {
        Check(false, what);
        return none;
    }
    const Sent &s = sent.back();
    Check(s.data.size() == size && s.guaranteed == guaranteed, what);
    return s;
}

int main()
{
    CGame::m_pDirectPlay4A = (void *)1;

    // ---- sizes and offsets from the headers (what net_payload.cpp is built on) ----
    static_assert(sizeof(NetStats) == 0x1e && offsetof(NetStats, seq) == 0 && offsetof(NetStats, data) == 2 &&
                      offsetof(NetStats, speed) == 0x16 && offsetof(NetStats, field_0x18) == 0x18 &&
                      offsetof(NetStats, field_0x1a) == 0x1a && offsetof(NetStats, field_0x1c) == 0x1c,
                  "NetStats");
    static_assert(sizeof(NetPlayerInfo) == 0x10 && offsetof(NetPlayerInfo, flags) == 4 &&
                      offsetof(NetPlayerInfo, field_0x8) == 8 && offsetof(NetPlayerInfo, field_0xc) == 0xc,
                  "NetPlayerInfo");
    static_assert(sizeof(NetworkLeaderboardEntry) == 8 && offsetof(NetworkLeaderboardEntry, wins) == 4 &&
                      sizeof(NetworkLeaderboard) == 0x104 && offsetof(NetworkLeaderboard, entries) == 4,
                  "NetworkLeaderboard");
    static_assert(sizeof(RallyPairingTables) == 400 && offsetof(RallyPairingTables, skills) == 160 &&
                      offsetof(RallyPairingTables, flags) == 240 && offsetof(RallyPairingTables, otherFlags) == 320,
                  "RallyPairingTables");
    static_assert(sizeof(int) == 4 && sizeof(short) == 2, "scalar sizes");

    // ---- player descriptions ----
    GameInfo_SetConfiguredGameMode(9);
    Check(FrontendNetwork_SendLobbyPlayerDescription() != 0, "create the local player");
    Check(createdData.size() == 0x10, "lobby description size");
    {
        uint32_t flags = U32(createdData, 4);
        Check((flags & 0x1f) == (RallyData_GetDriverRecordSelectionValue(0) & 0x1f) && (flags & 0x80) != 0,
              "lobby flags are an integer: car in bits 0-4, active bit 7");
    }
    g_unk0x00818ce4 = 1;
    FrontendNetwork_SendPlayerDescription();
    Check(playerData.size() == 0x10 && U32(playerData, 0) == 0x00010001, "player description id");
    {
        uint32_t flags = U32(playerData, 4);
        Check((flags & 0x1f) == (RallyData_GetDriverRecordSelectionValue(0) & 0x1f), "bitfield car in bits 0-4");
        Check((flags & 0x40) != 0 && (flags & 0x80) != 0, "bitfields bit6 and active in bits 6 and 7");
        Check((flags & 0x300) == 0 && (flags >> 10) == 0, "bitfields ready, finished and the rest are clear");
    }

    // ---- setup packet: the bitfield word and the rally tables ----
    GameInfo_SetConfiguredGameMode(9);
    RallyData_SetCountrySelectionBits(5);
    RallyData_SetStageSelectionAndRefreshFlags(3);
    sent.clear();
    FrontendNetwork_SendSetupPacket();
    {
        const Sent &s = Last("setup packet size", 0x1c4, 1);
        if (s.data.size() == 0x1c4) {
            Check(s.data[0] == 3, "setup type");
            uint32_t word = U32(s.data, 4);
            Check((word & 0xff) == 9 && (word >> 8 & 0xf) == 5 && (word >> 12 & 0xf) == 3,
                  "gameMode, country and stage in the low half of the word at 4");
            RallyDriverGroupTable *table = RallyData_GetDriverGroupTable();
            bool tables = true;
            for (int i = 0; i < 20; i++) {
                tables = tables && U32(s.data, 0x18 + 8 * i) == (uint32_t)table->pairing.pairs[i][0];
                tables = tables && U32(s.data, 0x1c + 8 * i) == (uint32_t)table->pairing.pairs[i][1];
                tables = tables && U32(s.data, 0xb8 + 4 * i) == (uint32_t)table->pairing.skills[i];
                tables = tables && U32(s.data, 0x108 + 4 * i) == (uint32_t)table->pairing.flags[i];
                tables = tables && U32(s.data, 0x158 + 4 * i) == (uint32_t)table->pairing.otherFlags[i];
                tables = tables && s.data[0x1a8 + i] == table->field_0x6d4[i];
            }
            Check(tables, "pairing tables at 0x18, 0xb8, 0x108, 0x158 and the bytes at 0x1a8");
            Check(U32(s.data, 0x1bc) == (uint32_t)table->field_0x53c && U32(s.data, 0x1c0) == (uint32_t)table->field_0x6d0,
                  "setup trailer words");
        }
    }
    GameInfo_SetConfiguredGameMode(0xb);
    RallyData_SetSelectionBits10To11(2);
    RallyData_SetSelectionBits12To13(1);
    RallyData_SetSelectionBits16To19(0xa);
    sent.clear();
    FrontendNetwork_SendSetupPacket();
    {
        const Sent &s = Last("setup packet size (rally)", 0x1c4, 1);
        if (s.data.size() == 0x1c4) {
            uint32_t word = U32(s.data, 4);
            Check((word & 0xff) == 0xb && (word >> 16 & 3) == 2 && (word >> 18 & 3) == 1 && (word >> 20 & 0xf) == 0xa,
                  "rallyA, rallyB and rallyC in bits 16-23");
        }
    }

    // Received: the host side reads a wire image at the same offsets.
    {
        std::vector<uint8_t> in(0x1c4, 0);
        in[0] = 3;
        for (int i = 0; i < 0xb; i++)
            in[8 + i] = 1;
        PutU32(&in[0x14], 0x1234);
        for (int i = 0; i < 100; i++)
            PutU32(&in[0x18 + 4 * i], 0x100 + i);
        for (int i = 0; i < 0x14; i++)
            in[0x1a8 + i] = (uint8_t)(0x40 + i);
        PutU32(&in[0x1bc], 0xabc);
        PutU32(&in[0x1c0], 0xdef);
        // (The handler that applies the bitfield word also drives the menus; the
        // word's positions are the declaration the sender above is checked against.)
        FrontendNetwork_ReadSetupPacket((NetSetupPacket *)in.data());
        RallyDriverGroupTable *table = RallyData_GetDriverGroupTable();
        Check(table->pairing.pairs[0][0] == 0x100 && table->pairing.pairs[1][1] == 0x103 &&
                  table->pairing.skills[0] == 0x100 + 40 && table->pairing.flags[0] == 0x100 + 60 &&
                  table->pairing.otherFlags[19] == 0x100 + 99 && table->field_0x6d4[19] == 0x40 + 19 &&
                  table->field_0x53c == 0xabc && table->field_0x6d0 == 0xdef,
              "received pairing tables");
    }

    // ---- race messages ----

    sent.clear();
    NetRace_SendSplitTime(3, 0x123456);
    {
        const Sent &s = Last("split time", 8, 0);
        if (s.data.size() == 8)
            Check(s.data[0] == 8 && s.data[1] == 3 && U32(s.data, 4) == 0x123456, "split time layout");
    }
    NetRace_SendStageTime(4, 0x654321);
    {
        const Sent &s = Last("stage time", 8, 1);
        if (s.data.size() == 8)
            Check(s.data[0] == 9 && s.data[1] == 4 && U32(s.data, 4) == 0x654321, "stage time layout");
    }
    NetRace_SendStageAndOverallTimes(1000);
    {
        const Sent &s = Last("finish", 12, 1);
        if (s.data.size() == 12)
            Check(s.data[0] == 0xa && U32(s.data, 4) == 1000 && U32(s.data, 8) == 1000 + (uint32_t)g_netTotal,
                  "finish layout");
    }
    NetRace_SendReady();
    Check(Last("ready", 1, 1).data[0] == 7, "ready");
    NetRace_SendResultsQuit();
    Check(Last("results quit", 1, 1).data[0] == 0xc, "results quit");
    NetPlayers_SendCarClass(9);
    {
        const Sent &s = Last("car class", 2, 1);
        Check(s.data[0] == 6 && s.data[1] == 9, "car class layout");
    }
    NetPlayers_SendType16Notification();
    Check(Last("type 16", 1, 1).data[0] == 0x10, "type 16");

    NetStats stats;
    for (size_t i = 0; i < sizeof(stats); i++)
        ((uint8_t *)&stats)[i] = (uint8_t)(0x10 + i);
    NetRace_SendLocalCarState(&stats);
    {
        const Sent &s = Last("car state", 0x20, 0);
        if (s.data.size() == 0x20)
            Check(s.data[0] == 0xb && memcmp(&s.data[2], &stats, sizeof(stats)) == 0 && U16(s.data, 2) == stats.seq &&
                      U16(s.data, 0x18) == stats.speed && U16(s.data, 0x1a) == stats.field_0x18 &&
                      U16(s.data, 0x1c) == stats.field_0x1a && U16(s.data, 0x1e) == stats.field_0x1c,
                  "car state layout: the 0x1e byte statistics block at 2");
    }

    // The car state bitfield words: the receiver's decoder unpacks them with the
    // game's own NetCarPacket declaration (NetRace.cpp). One bit at a time, at
    // the i386 position, must switch exactly the field it is documented as.
    g_netTriangleState.enabled = 1;
    {
        static Sector sector;
        memset(&sector, 0, sizeof(sector));
        g_sectors[0] = &sector;
        g_sectorCount = 1;
        struct Bit {
            int offset, bit;
            const char *what;
            int (*read)(const CarNetRecord &, int);
        };
        // NetStats offsets (the message has them 2 higher): 0x16 = angVelZ:8 flagB35 flagB54 progress:6,
        // 0x1a = steer:7 steerFollow flagC00 shaking menuOpen stage:4 stageBehind.
        const Bit bits[] = {
            { 0x16, 8, "flagB35", [](const CarNetRecord &r, int) { return (int)r.flag_0xcc; } },
            { 0x16, 9, "flagB54", [](const CarNetRecord &r, int) { return r.braking; } },
            { 0x1a, 7, "steerFollow", [](const CarNetRecord &r, int) { return (int)(r.throttleTorque != 0); } },
            { 0x1a, 8, "flagC00", [](const CarNetRecord &r, int) { return r.useUpperCollisionCorners; } },
            { 0x1a, 9, "shaking", [](const CarNetRecord &r, int) { return r.resetPose; } },
            { 0x1a, 10, "menuOpen", [](const CarNetRecord &r, int) { return r.field_0xe4; } },
        };
        for (const Bit &b : bits) {
            memset(&g_carNetRecords[0], 0, sizeof(g_carNetRecords[0]));
            memset(&g_localCarStats, 0, sizeof(g_localCarStats));
            g_localCarStats.seq = 1;
            uint16_t word = (uint16_t)(1u << b.bit);
            memcpy((uint8_t *)&g_localCarStats + b.offset, &word, 2);
            int steer = 0;
            Check(NetRace_DecodeReceivedCarState(&g_carNetRecords[0], &steer) == 1, b.what);
            // Every flag but this one stays clear: read all six, one must be set.
            int set = 0;
            for (const Bit &other : bits)
                set += other.read(g_carNetRecords[0], 0) != 0 ? 1 : 0;
            Check(b.read(g_carNetRecords[0], 0) != 0 && set == 1, b.what);
        }
        // steer is the low 7 bits of the word at 0x1a; heightDelta/height the two bytes at 0xe.
        memset(&g_carNetRecords[0], 0, sizeof(g_carNetRecords[0]));
        memset(&g_localCarStats, 0, sizeof(g_localCarStats));
        g_localCarStats.seq = 1;
        uint16_t steerWord = 0x7f;
        memcpy((uint8_t *)&g_localCarStats + 0x1a, &steerWord, 2);
        int steer = 0;
        Check(NetRace_DecodeReceivedCarState(&g_carNetRecords[0], &steer) == 1 && steer == 0x10000, "steer 0x7f");
        steerWord = 0x80;       // steerFollow only: steer stays 0, which decodes to full left lock
        memset(&g_carNetRecords[0], 0, sizeof(g_carNetRecords[0]));
        memcpy((uint8_t *)&g_localCarStats + 0x1a, &steerWord, 2);
        Check(NetRace_DecodeReceivedCarState(&g_carNetRecords[0], &steer) == 1 && steer == -0x10000, "steer 0");
        memset(&g_carNetRecords[0], 0, sizeof(g_carNetRecords[0]));
        memset(&g_localCarStats, 0, sizeof(g_localCarStats));
        g_localCarStats.seq = 1;
        uint16_t heightWord = 0xff;     // height in the low byte: 255 -> the full 10 units
        memcpy((uint8_t *)&g_localCarStats + 0xe, &heightWord, 2);
        Check(NetRace_DecodeReceivedCarState(&g_carNetRecords[0], &steer) == 1 && g_carNetRecords[0].field_0xbc != 0 &&
                  g_carNetRecords[0].field_0xc0 == 0,
              "height is the low byte of the word at 0xe");
        memset(&g_carNetRecords[0], 0, sizeof(g_carNetRecords[0]));
        heightWord = 0x7f00;            // heightDelta in the high byte
        memcpy((uint8_t *)&g_localCarStats + 0xe, &heightWord, 2);
        Check(NetRace_DecodeReceivedCarState(&g_carNetRecords[0], &steer) == 1 && g_carNetRecords[0].field_0xbc == 0 &&
                  g_carNetRecords[0].field_0xc0 != 0,
              "heightDelta is the high byte of the word at 0xe");
    }

    // ---- race messages received: offsets read by the dispatcher ----
    g_netPlayers[0].id = 0x00020001;
    g_netPlayers[0].flags = 0x80;
    g_netTriangleState.enabled = 1;
    {
        DWORD from = 0x00020001;
        uint8_t split[8] = { 8, 3, 0xa1, 0xa2 };
        PutU32(split + 4, 0x2468);
        NetRace_DispatchRacePacket(&from, split);
        Check(g_netPlayers[0].splits[2] == 0x2468, "received split time");
        uint8_t stage[8] = { 9, 2, 0xa1, 0xa2 };
        PutU32(stage + 4, 0x1357);
        NetRace_DispatchRacePacket(&from, stage);
        Check(g_netPlayers[0].stageTimes[1] == 0x1357, "received stage time");
        uint8_t finish[12] = { 0xa, 0xa1, 0xa2, 0xa3 };
        PutU32(finish + 4, 4242);
        PutU32(finish + 8, 99999);
        NetRace_DispatchRacePacket(&from, finish);
        Check(g_netPlayers[0].time == 4242 && g_netPlayers[0].field_0x7c == 99999 && (g_netPlayers[0].flags & 0x200),
              "received finish");
        uint8_t car[0x20];
        for (int i = 0; i < 0x20; i++)
            car[i] = (uint8_t)(0x20 + i);
        car[0] = 0xb;
        car[2] = 0x34;      // seq 0x1234 (greater than the cleared one)
        car[3] = 0x12;
        g_netPlayers[0].stats.seq = 0;
        NetRace_DispatchRacePacket(&from, car);
        Check(memcmp(&g_netPlayers[0].stats, car + 2, sizeof(NetStats)) == 0 && g_netPlayers[0].stats.seq == 0x1234,
              "received car state: the statistics block at 2");
    }

    // ---- published leaderboard ----
    {
        NetworkLeaderboard board;
        memset(&board, 0, sizeof(board));
        board.isLoaded = 1;
        for (int i = 0; i < 32; i++) {
            board.entries[i].name[0] = (char)('A' + i);
            board.entries[i].wins = 100 * i + 3;
        }
        NetworkLeaderboard_PublishBoard((BYTE *)&board);
        sent.clear();
        NetPlayers_SendPublishedLeaderboard();
        const Sent &s = Last("leaderboard", 0x108, 1);
        if (s.data.size() == 0x108) {
            bool ok = s.data[0] == 0x11 && s.data[1] == 1 && s.data[4] == 1;
            for (int i = 0; i < 32; i++)
                ok = ok && s.data[8 + 8 * i] == 'A' + i && U32(s.data, 12 + 8 * i) == 100u * i + 3;
            Check(ok, "leaderboard layout: valid at 1, board at 4, wins at 12 + 8 * entry");
        }
    }

    // ---- chat ----
    {
        char text[] = "hello";
        sent.clear();
        NetworkChat_SendLine(text);
        const Sent &s = Last("chat", 0x101, 0);
        Check(s.data.size() == 0x101 && s.data[0] == 0 && strcmp((const char *)&s.data[1], "hello") == 0,
              "chat layout");
    }

    if (failures != 0) {
        fprintf(stderr, "net payload game: %d failures\n", failures);
        return 1;
    }
    puts("net payload game: the game's own senders and handlers use the wire layout");
    return 0;
}
