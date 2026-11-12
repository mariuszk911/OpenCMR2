// Cross-play peer for Linux (or any little-endian host): finds the session
// hosted by the Amiga's amiga_net_peer_test (tests/amiga/net_peer_test.cpp)
// on the local network (broadcast) or at host[:port] given on the command
// line, joins it, and trades the game's messages as wire images built byte by
// byte: the original PC layout, which is also this machine's native one.
//
//     net_cross_peer [amiga-address[:port]]
//
// Not part of ctest: it needs the other machine. Exits 0 when everything the
// Amiga sent arrived with its values intact and the Amiga confirmed ours.
#include "net/net_session.h"
#include "net_peer_messages.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures;

void Check(bool ok, const char *what)
{
    if (!ok) {
        failures++;
        fprintf(stderr, "cross peer: FAIL %s\n", what);
    }
}

void Log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fputs("  [net] ", stdout);
    vprintf(format, args);
    fputc('\n', stdout);
    va_end(args);
}

void Put32(std::vector<uint8_t> &v, size_t at, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        v[at + i] = (uint8_t)(x >> (8 * i));
}

uint32_t Get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint16_t Get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

void Put16(std::vector<uint8_t> &v, size_t at, unsigned x)
{
    v[at] = (uint8_t)x;
    v[at + 1] = (uint8_t)(x >> 8);
}

unsigned seen;
enum { SEEN_SETUP = 1, SEEN_CAR = 2, SEEN_BOARD = 4, SEEN_CHAT = 8, SEEN_SPLIT = 16, SEEN_FINISH = 32, SEEN_CREATE = 64,
       SEEN_DATA = 128, SEEN_ALL = 255 };

void Handle(const uint8_t *d, size_t n)
{
    switch (d[0]) {
    case 2: {
        bool ok = n == 0x1c4;
        if (ok) {
            uint32_t w = Get32(d + 4);
            ok = (w & 0xff) == AMIGA_SETUP_MODE && (w >> 8 & 0xf) == AMIGA_SETUP_COUNTRY &&
                 (w >> 12 & 0xf) == AMIGA_SETUP_STAGE && (w >> 16 & 3) == AMIGA_SETUP_RALLY_A &&
                 (w >> 18 & 3) == AMIGA_SETUP_RALLY_B && (w >> 20 & 0xf) == AMIGA_SETUP_RALLY_C &&
                 Get32(d + 0x14) == (uint32_t)AMIGA_SETUP_F14 && Get32(d + 0x1bc) == AMIGA_SETUP_F1BC &&
                 Get32(d + 0x1c0) == AMIGA_SETUP_F1C0;
            for (int i = 0; i < 100; i++)
                ok = ok && Get32(d + 0x18 + 4 * i) == (uint32_t)AMIGA_SETUP_PAIRING(i);
            for (int i = 0; i < 0x14; i++)
                ok = ok && d[0x1a8 + i] == AMIGA_SETUP_BYTE(i);
            printf("  setup from the Amiga: word %08x\n", w);
        }
        Check(ok, "setup packet from the Amiga");
        if (ok)
            seen |= SEEN_SETUP;
        break;
    }
    case 0xb: {
        bool ok = n == 0x20;
        if (ok) {
            const uint8_t *s = d + 2;
            uint16_t w16 = Get16(s + 0x14), w18 = Get16(s + 0x16), w1a = Get16(s + 0x18), w1c = Get16(s + 0x1a);
            uint16_t w0e = Get16(s + 0x0e);
            ok = Get16(s) == CAR_SEQ && Get16(s + 2) == CAR_DISP_X && Get16(s + 4) == CAR_DISP_Z &&
                 Get16(s + 6) == CAR_ENGINE && Get16(s + 8) == CAR_POS_X && Get16(s + 10) == CAR_POS_Z &&
                 Get16(s + 12) == CAR_SECTOR && (w0e & 0xff) == CAR_HEIGHT && (w0e >> 8) == CAR_HEIGHT_DELTA &&
                 Get16(s + 0x10) == CAR_AXIS0 && Get16(s + 0x12) == CAR_AXIS1 && (w16 & 0xff) == CAR_ANGVEL_X &&
                 (w16 >> 8) == CAR_ANGVEL_Y && (w18 & 0xff) == CAR_ANGVEL_Z && (w18 >> 8 & 1) == CAR_FLAG_B35 &&
                 (w18 >> 9 & 1) == CAR_FLAG_B54 && (w18 >> 10) == CAR_PROGRESS && (w1a & 0x3ff) == CAR_NODE &&
                 (w1a >> 10) == CAR_PAD && (w1c & 0x7f) == CAR_STEER && (w1c >> 7 & 1) == CAR_STEER_FOLLOW &&
                 (w1c >> 8 & 1) == CAR_FLAG_C00 && (w1c >> 9 & 1) == CAR_SHAKING && (w1c >> 10 & 1) == CAR_MENU_OPEN &&
                 (w1c >> 11 & 0xf) == CAR_STAGE && (w1c >> 15) == CAR_STAGE_BEHIND && Get16(s + 0x1c) == CAR_LAST;
        }
        Check(ok, "car state from the Amiga");
        if (ok)
            seen |= SEEN_CAR;
        break;
    }
    case 0x11: {
        bool ok = n == 0x108 && d[1] == 1 && d[4] == 1;
        for (int i = 0; ok && i < 32; i++)
            ok = d[8 + 8 * i] == 'A' + i && Get32(d + 12 + 8 * i) == (uint32_t)PEER_BOARD_WINS(i);
        Check(ok, "leaderboard from the Amiga");
        if (ok)
            seen |= SEEN_BOARD;
        break;
    }
    case 0:
        if (n == 0x101 && strcmp((const char *)d + 1, AMIGA_CHAT) == 0)
            seen |= SEEN_CHAT;
        else
            Check(false, "chat from the Amiga");
        break;
    case 8:
        if (n == 8 && d[1] == PEER_SPLIT_INDEX && Get32(d + 4) == PEER_SPLIT_TIME)
            seen |= SEEN_SPLIT;
        else
            Check(false, "split time from the Amiga");
        break;
    case 10:
        if (n == 12 && Get32(d + 4) == PEER_FINISH_TIME && Get32(d + 8) == PEER_FINISH_TOTAL)
            seen |= SEEN_FINISH;
        else
            Check(false, "finish from the Amiga");
        break;
    default:
        break;
    }
}

void Drain(net::Session &s)
{
    static uint8_t buffer[8192];
    for (;;) {
        NetPlayerID from, to;
        DWORD size = sizeof(buffer);
        if (s.Receive(&from, &to, buffer, &size) != NET_OK)
            return;
        if (from != NET_ID_SYSTEM) {
            Handle(buffer, size);
            continue;
        }
        DWORD type;
        memcpy(&type, buffer, 4);
        if (type == NET_SYS_CREATEPLAYER) {
            NetMsgCreatePlayer cp;
            memcpy(&cp, buffer, sizeof(cp));
            printf("  Amiga player %x \"%s\", description %u bytes\n", (unsigned)cp.dpId, cp.dpnName.lpszShortNameA,
                   (unsigned)cp.dwDataSize);
            if (cp.dwDataSize == 16) {
                const uint8_t *p = (const uint8_t *)cp.lpData;
                Check(Get32(p + 4) == PEER_FLAGS_AMIGA && Get32(p + 8) == 0x01020304 && Get32(p + 12) == 0x0a0b0c0d,
                      "the Amiga's player description");
                seen |= SEEN_CREATE;
            }
        } else if (type == NET_SYS_SETPLAYERDATA) {
            NetMsgSetPlayerData pd;
            memcpy(&pd, buffer, sizeof(pd));
            if (pd.dwDataSize == 16) {
                uint32_t flags = Get32((const uint8_t *)pd.lpData + 4);
                printf("  Amiga description published through bitfields: flags %08x\n", flags);
                Check(flags == PEER_FLAGS_AMIGA_SET, "the Amiga's bitfield description arrives as the integer");
                seen |= SEEN_DATA;
            }
        }
    }
}

BOOL OnSession(const NetSessionDesc *d, void *context)
{
    if (d->lpszSessionNameA != nullptr && strcmp(d->lpszSessionNameA, "Amiga peer test") == 0) {
        *(NetSessionDesc *)context = *d;
        return FALSE;
    }
    return TRUE;
}

} // namespace

int main(int argc, char **argv)
{
    net::Config config;
    config.port = 0;
    config.log = Log;
    if (argc > 1)
        config.peers.push_back(argv[1]);
    net::Session session(config);
    if (session.Start() != NET_OK) {
        fprintf(stderr, "cannot open the network\n");
        return 2;
    }
    const int words[4] = PEER_APP_WORDS;
    NetGuid app;
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 4; b++)
            app.bytes[4 * i + b] = (uint8_t)((uint32_t)words[i] >> (8 * b));

    printf("searching for the Amiga's session (%s)...\n", argc > 1 ? argv[1] : "broadcast");
    NetSessionDesc found = {};
    auto start = std::chrono::steady_clock::now();
    while (found.lpszSessionNameA == nullptr && std::chrono::steady_clock::now() - start < std::chrono::seconds(60)) {
        session.EnumSessions(&app, OnSession, &found);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (found.lpszSessionNameA == nullptr) {
        fprintf(stderr, "cross peer: FAIL the Amiga's session was not found\n");
        return 1;
    }
    printf("found it, joining\n");
    NetSessionDesc join = {};
    join.dwSize = sizeof(join);
    join.guidInstance = found.guidInstance;
    Check(session.Open(&join, NET_OPEN_JOIN) == NET_OK, "join");

    std::vector<uint8_t> info(16, 0);
    Put32(info, 4, PEER_FLAGS_LINUX);
    Put32(info, 8, 0x11121314);
    Put32(info, 12, 0x15161718);
    NetName name = { sizeof(NetName), 0, (char *)"linux", (char *)"linux" };
    NetPlayerID me = 0;
    Check(session.CreatePlayer(&me, &name, info.data(), 16) == NET_OK, "create the player");

    // Setup packet (type 3), wire layout.
    std::vector<uint8_t> setup(0x1c4, 0);
    setup[0] = 3;
    Put32(setup, 4, LINUX_SETUP_MODE | LINUX_SETUP_COUNTRY << 8 | LINUX_SETUP_STAGE << 12 | LINUX_SETUP_RALLY_A << 16 |
                        LINUX_SETUP_RALLY_B << 18 | LINUX_SETUP_RALLY_C << 20);
    Put32(setup, 0x14, LINUX_SETUP_F14);
    for (int i = 0; i < 100; i++)
        Put32(setup, 0x18 + 4 * i, LINUX_SETUP_PAIRING(i));
    for (int i = 0; i < 0x14; i++)
        setup[0x1a8 + i] = LINUX_SETUP_BYTE(i);
    Put32(setup, 0x1bc, LINUX_SETUP_F1BC);
    Put32(setup, 0x1c0, LINUX_SETUP_F1C0);
    Check(session.Send(me, NET_ID_ALL, TRUE, setup.data(), setup.size()) == NET_OK, "send setup");

    std::vector<uint8_t> chat(0x101, 0);
    strcpy((char *)chat.data() + 1, LINUX_CHAT);
    session.Send(me, NET_ID_ALL, TRUE, chat.data(), chat.size());

    std::vector<uint8_t> split(8, 0), fin(12, 0);
    split[0] = 8;
    split[1] = PEER_SPLIT_INDEX;
    Put32(split, 4, PEER_SPLIT_TIME);
    session.Send(me, NET_ID_ALL, TRUE, split.data(), 8);
    std::vector<uint8_t> stage(8, 0);
    stage[0] = 9;
    stage[1] = PEER_STAGE_INDEX;
    Put32(stage, 4, PEER_STAGE_TIME);
    session.Send(me, NET_ID_ALL, TRUE, stage.data(), 8);
    fin[0] = 0xa;
    Put32(fin, 4, PEER_FINISH_TIME);
    Put32(fin, 8, PEER_FINISH_TOTAL);
    session.Send(me, NET_ID_ALL, TRUE, fin.data(), 12);

    // The Amiga's check of the leaderboard from here is the 'a' + i, 7 * i + 1 one.
    std::vector<uint8_t> board(0x108, 0);
    board[0] = 0x11;
    board[1] = 1;
    board[4] = 1;
    for (int i = 0; i < 32; i++) {
        board[8 + 8 * i] = (uint8_t)('a' + i);
        Put32(board, 12 + 8 * i, 7 * i + 1);
    }
    session.Send(me, NET_ID_ALL, TRUE, board.data(), board.size());

    std::vector<uint8_t> car(0x20, 0);
    car[0] = 0xb;
    car[1] = 0xa1;
    Put16(car, 2, CAR_SEQ);
    Put16(car, 4, CAR_DISP_X);
    Put16(car, 6, CAR_DISP_Z);
    Put16(car, 8, CAR_ENGINE);
    Put16(car, 10, CAR_POS_X);
    Put16(car, 12, CAR_POS_Z);
    Put16(car, 14, CAR_SECTOR);
    Put16(car, 16, CAR_HEIGHT | CAR_HEIGHT_DELTA << 8);
    Put16(car, 18, CAR_AXIS0);
    Put16(car, 20, CAR_AXIS1);
    Put16(car, 22, CAR_ANGVEL_X | CAR_ANGVEL_Y << 8);
    Put16(car, 24, CAR_ANGVEL_Z | CAR_FLAG_B35 << 8 | CAR_FLAG_B54 << 9 | CAR_PROGRESS << 10);
    Put16(car, 26, CAR_NODE | CAR_PAD << 10);
    Put16(car, 28, CAR_STEER | CAR_STEER_FOLLOW << 7 | CAR_FLAG_C00 << 8 | CAR_SHAKING << 9 | CAR_MENU_OPEN << 10 |
                       CAR_STAGE << 11 | CAR_STAGE_BEHIND << 15);
    Put16(car, 30, CAR_LAST);
    // Unreliable, like the game's: a few, spaced, in case the first is lost.
    for (int i = 0; i < 5; i++) {
        session.Send(me, NET_ID_ALL, FALSE, car.data(), car.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // The Amiga's player was created before this machine joined: the roster has it.
    {
        int players = 0;
        session.EnumPlayers([](NetPlayerID, const NetName *, void *c) -> BOOL { ++*(int *)c; return TRUE; }, &players);
        Check(players == 2, "the roster has the Amiga's player and ours");
        if (players == 2)
            seen |= SEEN_CREATE;
    }

    start = std::chrono::steady_clock::now();
    while (seen != SEEN_ALL && failures == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(30)) {
        Drain(session);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    Check(seen == SEEN_ALL, "everything from the Amiga arrived");
    printf("seen mask %02x\n", seen);
    std::this_thread::sleep_for(std::chrono::seconds(2));    // let the Amiga drain what we sent
    session.Close();
    if (failures != 0) {
        printf("cross peer: %d failures\n", failures);
        return 1;
    }
    printf("cross peer: OK\n");
    return 0;
}
