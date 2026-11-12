/*
 * Cross-play peer for AmigaOS: hosts a session through the real Net_* entry
 * points (port/net.h, [net] settings of opencmr2.ini) and trades the game's
 * messages with another machine, normally a Linux one running
 * tests/net_cross_peer. Native structures as the game declares them go out;
 * what comes in is checked field by field. Needs a running TCP/IP stack and
 * the other machine on the same network. Not staged; run from a Shell:
 *
 *     amiga_net_peer_test > net_peer.log
 *
 * It waits up to 120 s for the other machine and exits 0 when every
 * message arrived with its values intact.
 */
#include "../net_peer_messages.h"

#include "platform/amiga/amiga_sys.h"
#include "port/net.h"
#include "port/sys.h"

#include <stdio.h>
#include <string.h>

extern "C" void Net_Pump(void);

namespace {

int failures;

void Check(bool ok, const char *what)
{
    if (!ok) {
        failures++;
        Sys_Log("net_peer: FAIL %s", what);
        printf("net_peer: FAIL %s\n", what);
    }
}

// ---- the game's structures, as this compiler lays them out ----
struct SetupPacket {
    unsigned char type, pad[3];
    unsigned int gameMode : 8, country : 4, stage : 4, rallyA : 2, rallyB : 2, rallyC : 4, unused : 8;
    unsigned char stageStates[0xc];
    int field14;
    int pairing[100];
    unsigned char bytes[0x14];
    int field1bc, field1c0;
};
struct CarPacket {
    unsigned short seq, dispX, dispZ, engineSpeed, posX, posZ;
    short sector;
    unsigned short height : 8, heightDelta : 8;
    unsigned short axes[2];
    unsigned short angVelX : 8, angVelY : 8;
    unsigned short angVelZ : 8, flagB35 : 1, flagB54 : 1, progress : 6;
    unsigned short node : 10, pad18 : 6;
    unsigned short steer : 7, steerFollow : 1, flagC00 : 1, shaking : 1, menuOpen : 1, stage : 4, stageBehind : 1;
    unsigned short last;
};
struct CarMessage {
    unsigned char type, pad;
    CarPacket car;
};
struct BoardEntry {
    char name[4];
    int wins;
};
struct BoardMessage {
    unsigned char type, valid, pad[2];
    unsigned char loaded, reserved[3];
    BoardEntry entries[32];
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

static_assert(sizeof(SetupPacket) == 0x1c4, "setup");
static_assert(sizeof(CarMessage) == 0x20, "car");
static_assert(sizeof(BoardMessage) == 0x108, "board");
static_assert(sizeof(PlayerInfo) == 0x10, "info");

unsigned seen;      // bit per message kind received intact
enum { SEEN_SETUP = 1, SEEN_SPLIT = 2, SEEN_STAGE = 4, SEEN_FINISH = 8, SEEN_CAR = 16, SEEN_BOARD = 32, SEEN_CHAT = 64,
       SEEN_PLAYER = 128, SEEN_ALL = 255 };

int ReadInt(const unsigned char *p)
{
    int v;
    memcpy(&v, p, 4);
    return v;
}

void CheckSetup(const unsigned char *data, unsigned size)
{
    SetupPacket p;
    if (size != sizeof(p)) {
        Check(false, "setup size");
        return;
    }
    memcpy(&p, data, sizeof(p));
    bool ok = p.type == 3 && p.gameMode == LINUX_SETUP_MODE && p.country == LINUX_SETUP_COUNTRY &&
              p.stage == LINUX_SETUP_STAGE && p.rallyA == LINUX_SETUP_RALLY_A && p.rallyB == LINUX_SETUP_RALLY_B &&
              p.rallyC == LINUX_SETUP_RALLY_C && p.field14 == LINUX_SETUP_F14 && p.field1bc == LINUX_SETUP_F1BC &&
              p.field1c0 == LINUX_SETUP_F1C0;
    for (int i = 0; i < 100; i++)
        ok = ok && p.pairing[i] == LINUX_SETUP_PAIRING(i);
    for (int i = 0; i < 0x14; i++)
        ok = ok && p.bytes[i] == LINUX_SETUP_BYTE(i);
    Sys_Log("net_peer: setup from Linux: mode %u country %u stage %u rally %u/%u/%u", p.gameMode, p.country, p.stage,
            p.rallyA, p.rallyB, p.rallyC);
    Check(ok, "setup packet from Linux");
    if (ok)
        seen |= SEEN_SETUP;
}

void CheckCar(const unsigned char *data, unsigned size)
{
    CarMessage m;
    if (size != sizeof(m)) {
        Check(false, "car state size");
        return;
    }
    memcpy(&m, data, sizeof(m));
    const CarPacket &c = m.car;
    bool ok = m.type == 0xb && c.seq == CAR_SEQ && c.dispX == CAR_DISP_X && c.dispZ == CAR_DISP_Z &&
              c.engineSpeed == CAR_ENGINE && c.posX == CAR_POS_X && c.posZ == CAR_POS_Z && c.sector == CAR_SECTOR &&
              c.height == CAR_HEIGHT && c.heightDelta == CAR_HEIGHT_DELTA && c.axes[0] == CAR_AXIS0 &&
              c.axes[1] == CAR_AXIS1 && c.angVelX == CAR_ANGVEL_X && c.angVelY == CAR_ANGVEL_Y &&
              c.angVelZ == CAR_ANGVEL_Z && c.flagB35 == CAR_FLAG_B35 && c.flagB54 == CAR_FLAG_B54 &&
              c.progress == CAR_PROGRESS && c.node == CAR_NODE && c.pad18 == CAR_PAD && c.steer == CAR_STEER &&
              c.steerFollow == CAR_STEER_FOLLOW && c.flagC00 == CAR_FLAG_C00 && c.shaking == CAR_SHAKING &&
              c.menuOpen == CAR_MENU_OPEN && c.stage == CAR_STAGE && c.stageBehind == CAR_STAGE_BEHIND &&
              c.last == CAR_LAST;
    Check(ok, "car state from Linux");
    if (ok)
        seen |= SEEN_CAR;
}

void Handle(const unsigned char *data, unsigned size)
{
    if (size == 0)
        return;
    Sys_Log("net_peer: message type %u, %u bytes", (unsigned)data[0], size);
    switch (data[0]) {
    case 3:
        CheckSetup(data, size);
        break;
    case 8:
        if (size == 8 && data[1] == PEER_SPLIT_INDEX && ReadInt(data + 4) == PEER_SPLIT_TIME)
            seen |= SEEN_SPLIT;
        else
            Check(false, "split time from Linux");
        break;
    case 9:
        if (size == 8 && data[1] == PEER_STAGE_INDEX && ReadInt(data + 4) == PEER_STAGE_TIME)
            seen |= SEEN_STAGE;
        else
            Check(false, "stage time from Linux");
        break;
    case 10:
        if (size == 12 && ReadInt(data + 4) == PEER_FINISH_TIME && ReadInt(data + 8) == PEER_FINISH_TOTAL)
            seen |= SEEN_FINISH;
        else
            Check(false, "finish from Linux");
        break;
    case 0xb:
        CheckCar(data, size);
        break;
    case 0x11: {
        BoardMessage b;
        bool ok = size == sizeof(b);
        if (ok) {
            memcpy(&b, data, sizeof(b));
            ok = b.valid == 1 && b.loaded == 1;
            for (int i = 0; i < 32; i++)
                ok = ok && (unsigned char)b.entries[i].name[0] == 'a' + i && b.entries[i].wins == 7 * i + 1;
        }
        Check(ok, "leaderboard from Linux");
        if (ok)
            seen |= SEEN_BOARD;
        break;
    }
    case 0:
        if (size == 0x101 && strcmp((const char *)data + 1, LINUX_CHAT) == 0)
            seen |= SEEN_CHAT;
        else
            Check(false, "chat from Linux");
        break;
    default:
        break;
    }
}

void SendAll(NetPlayerID me)
{
    static SetupPacket setup;
    memset(&setup, 0, sizeof(setup));
    setup.type = 2;
    setup.gameMode = AMIGA_SETUP_MODE;
    setup.country = AMIGA_SETUP_COUNTRY;
    setup.stage = AMIGA_SETUP_STAGE;
    setup.rallyA = AMIGA_SETUP_RALLY_A;
    setup.rallyB = AMIGA_SETUP_RALLY_B;
    setup.rallyC = AMIGA_SETUP_RALLY_C;
    setup.field14 = AMIGA_SETUP_F14;
    for (int i = 0; i < 100; i++)
        setup.pairing[i] = AMIGA_SETUP_PAIRING(i);
    for (int i = 0; i < 0x14; i++)
        setup.bytes[i] = AMIGA_SETUP_BYTE(i);
    setup.field1bc = AMIGA_SETUP_F1BC;
    setup.field1c0 = AMIGA_SETUP_F1C0;
    Check(Net_Send(me, NET_ID_ALL, TRUE, &setup, sizeof(setup)) == NET_OK, "send setup");

    unsigned char chat[0x101];
    memset(chat, 0, sizeof(chat));
    strcpy((char *)chat + 1, AMIGA_CHAT);
    Check(Net_Send(me, NET_ID_ALL, TRUE, chat, sizeof(chat)) == NET_OK, "send chat");

    static BoardMessage board;
    memset(&board, 0, sizeof(board));
    board.type = 0x11;
    board.valid = 1;
    board.loaded = 1;
    for (int i = 0; i < 32; i++) {
        board.entries[i].name[0] = (char)('A' + i);
        board.entries[i].wins = PEER_BOARD_WINS(i);
    }
    Check(Net_Send(me, NET_ID_ALL, TRUE, &board, sizeof(board)) == NET_OK, "send leaderboard");

    CarMessage car;
    memset(&car, 0, sizeof(car));
    car.type = 0xb;
    CarPacket &c = car.car;
    c.seq = CAR_SEQ; c.dispX = CAR_DISP_X; c.dispZ = CAR_DISP_Z; c.engineSpeed = CAR_ENGINE; c.posX = CAR_POS_X;
    c.posZ = CAR_POS_Z; c.sector = CAR_SECTOR; c.height = CAR_HEIGHT; c.heightDelta = CAR_HEIGHT_DELTA;
    c.axes[0] = CAR_AXIS0; c.axes[1] = CAR_AXIS1; c.angVelX = CAR_ANGVEL_X; c.angVelY = CAR_ANGVEL_Y;
    c.angVelZ = CAR_ANGVEL_Z; c.flagB35 = CAR_FLAG_B35; c.flagB54 = CAR_FLAG_B54; c.progress = CAR_PROGRESS;
    c.node = CAR_NODE; c.pad18 = CAR_PAD; c.steer = CAR_STEER; c.steerFollow = CAR_STEER_FOLLOW;
    c.flagC00 = CAR_FLAG_C00; c.shaking = CAR_SHAKING; c.menuOpen = CAR_MENU_OPEN; c.stage = CAR_STAGE;
    c.stageBehind = CAR_STAGE_BEHIND; c.last = CAR_LAST;
    Check(Net_Send(me, NET_ID_ALL, FALSE, &car, sizeof(car)) == NET_OK, "send car state");

    unsigned char split[8] = { 8, PEER_SPLIT_INDEX, 0, 0 };
    int t = PEER_SPLIT_TIME;
    memcpy(split + 4, &t, 4);
    Check(Net_Send(me, NET_ID_ALL, TRUE, split, 8) == NET_OK, "send split");
    unsigned char fin[12] = { 0xa, 0, 0, 0 };
    t = PEER_FINISH_TIME;
    memcpy(fin + 4, &t, 4);
    t = PEER_FINISH_TOTAL;
    memcpy(fin + 8, &t, 4);
    Check(Net_Send(me, NET_ID_ALL, TRUE, fin, 12) == NET_OK, "send finish");
}

BOOL OnProvider(int, const char *name, void *)
{
    printf("net_peer: provider \"%s\"\n", name);
    return TRUE;
}

} // namespace

int main()
{
    AmigaSys_SetCommandLine("amiga_net_peer_test");
    Check(Net_EnumProviders(OnProvider, NULL) == NET_OK, "a provider is offered");
    Check(Net_InitializeConnection(0) == NET_OK, "initialise the connection");

    NetSessionDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    static const int words[4] = PEER_APP_WORDS;
    memcpy(&desc.guidApplication, words, 16);
    desc.dwMaxPlayers = 4;
    desc.dwUser1 = AMIGA_SETUP_MODE;
    desc.lpszSessionNameA = (char *)"Amiga peer test";
    Check(Net_Open(&desc, NET_OPEN_CREATE) == NET_OK, "host the session");

    NetName name = { sizeof(NetName), 0, (char *)"amiga", (char *)"amiga" };
    PlayerInfo info;
    memset(&info, 0, sizeof(info));
    info.flags = PEER_FLAGS_AMIGA;
    info.a = 0x01020304;
    info.b = 0x0a0b0c0d;
    NetPlayerID me = 0;
    Check(Net_CreatePlayer(&me, &name, &info, sizeof(info)) == NET_OK, "create the local player");
    printf("net_peer: hosting \"Amiga peer test\", waiting for the Linux machine (up to 120 s)\n");

    static unsigned char buffer[4096];
    bool sent = false;
    unsigned long start = Sys_GetTicks();
    while (Sys_GetTicks() - start < 120000 && seen != SEEN_ALL && failures == 0) {
        Net_Pump();
        NetPlayerID from, to;
        DWORD size = sizeof(buffer);
        HRESULT hr = Net_Receive(&from, &to, buffer, &size);
        if (hr != NET_OK) {
            Sys_Sleep(5);
            continue;
        }
        if (from != NET_ID_SYSTEM) {
            Handle(buffer, size);
            continue;
        }
        DWORD type;
        memcpy(&type, buffer, 4);
        if (type == NET_SYS_CREATEPLAYER) {
            NetMsgCreatePlayer cp;
            memcpy(&cp, buffer, sizeof(cp));
            Sys_Log("net_peer: player %lx joined (%s), data %lu bytes", (unsigned long)cp.dpId,
                    cp.dpnName.lpszShortNameA, (unsigned long)cp.dwDataSize);
            if (cp.dwDataSize == 16) {
                PlayerInfo got;
                memcpy(&got, cp.lpData, 16);
                Check(got.flags == PEER_FLAGS_LINUX && got.a == 0x11121314 && got.b == 0x15161718,
                      "the Linux player's description");
                seen |= SEEN_PLAYER;
            }
            if (!sent) {
                // The description as the game publishes it: through the bitfields.
                PlayerInfo bits;
                memset(&bits, 0, sizeof(bits));
                bits.bits.car = 0x0f;
                bits.bits.bit6 = 1;
                bits.bits.active = 1;
                bits.bits.finished = 1;
                Check(Net_SetPlayerData(me, &bits, sizeof(bits)) == NET_OK, "publish the description");
                SendAll(me);
                sent = true;
            }
        }
    }
    // Keep answering for a moment: the other side may still be receiving.
    for (unsigned long linger = Sys_GetTicks(); Sys_GetTicks() - linger < 3000;) {
        Net_Pump();
        Sys_Sleep(5);
    }
    Check(seen == SEEN_ALL, "every message arrived from Linux");
    Sys_Log("net_peer: seen mask %02x", seen);
    printf("net_peer: seen mask %02x\n", seen);
    Net_Close();
    if (failures != 0) {
        printf("net_peer: %d failures\n", failures);
        return 1;
    }
    printf("net_peer: OK\n");
    return 0;
}
