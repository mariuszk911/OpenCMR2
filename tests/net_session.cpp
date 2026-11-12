// net::Session end to end on the loopback: a host and clients in one process,
// through the DirectPlay-style API the game uses.
//
// Built twice: with the service thread (the host builds) and cooperative
// (NET_SESSION_THREADED=0, the AmigaOS mode), where nobody services a session
// but the code that calls it. There the test plays the game's main loop: every
// wait pumps all the sessions of the process, as the game's frame does.

#include "net/net_session.h"

#include "net/enet_platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

void Log(const char *format, ...)
{
    if (getenv("NET_TEST_VERBOSE") == nullptr)
        return;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

const NetGuid kApp = { { 0x43, 0x4d, 0x52, 0x32, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 } };

// Every session of the process, for the cooperative mode's main loop.
std::vector<net::Session *> s_sessions;

void PumpAll()
{
    for (net::Session *session : s_sessions)
        session->Pump();    // nothing to do when a thread services
}

#if !NET_SESSION_THREADED
uint32_t TestNow()
{
    static const auto start = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
        .count();
}

uint64_t TestSeed()
{
    static uint64_t n = 0x2545f4914f6cdd1dull;
    return n += 0x9e3779b97f4a7c15ull;
}
#endif

bool WaitFor(const std::function<bool()> &condition, int ms = 3000)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        PumpAll();
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    PumpAll();
    return condition();
}

struct Message {
    NetPlayerID from = 0, to = 0;
    std::vector<uint8_t> data;
    DWORD type() const
    {
        DWORD t = 0;
        if (from == NET_ID_SYSTEM && data.size() >= 4)
            memcpy(&t, data.data(), 4);
        return t;
    }
};

struct Node {
    std::unique_ptr<net::Session> session;
    std::vector<Message> received;
    NetPlayerID player = 0;

    explicit Node(uint16_t port, uint16_t hostPort = 0, net::ByteOrder order = net::kNativeOrder)
    {
        net::Config config;
        config.payloadOrder = order;
        config.port = port;
        config.broadcast = false;
        config.timeoutMs = 1500;
        config.joinTimeoutMs = 2000;
        if (hostPort != 0)
            config.peers.push_back("127.0.0.1:" + std::to_string(hostPort));
        config.log = Log;
#if !NET_SESSION_THREADED
        config.now = TestNow;
        config.seed = TestSeed;
        config.idle = PumpAll;      // a blocking join must not stop the host in this process
#endif
        session.reset(new net::Session(config));
        CHECK(session->Start() == NET_OK);
        s_sessions.push_back(session.get());
    }

    ~Node()
    {
        for (size_t i = 0; i < s_sessions.size(); i++)
            if (s_sessions[i] == session.get())
                s_sessions.erase(s_sessions.begin() + (long)i);
    }

    // Moves every waiting message into received. Each is received into its
    // own buffer, which it keeps: system messages point into it.
    void Drain()
    {
        for (;;) {
            NetPlayerID from = 0, to = 0;
            DWORD size = 0;
            if (session->Receive(&from, &to, nullptr, &size) != NET_ERR_BUFFERTOOSMALL)
                return;
            Message m;
            m.data.resize(size);
            CHECK(session->Receive(&from, &to, m.data.data(), &size) == NET_OK);
            m.from = from;
            m.to = to;
            received.push_back(std::move(m));
        }
    }

    // Waits for a system message of this type and returns it.
    bool WaitSystem(DWORD type, Message *out = nullptr, int ms = 3000)
    {
        return WaitFor(
            [&] {
                Drain();
                for (size_t i = 0; i < received.size(); i++) {
                    if (received[i].from == NET_ID_SYSTEM && received[i].type() == type) {
                        if (out != nullptr)
                            *out = std::move(received[i]);
                        received.erase(received.begin() + (long)i);
                        return true;
                    }
                }
                return false;
            },
            ms);
    }

    size_t Count(NetPlayerID from)
    {
        Drain();
        size_t n = 0;
        for (const Message &m : received)
            n += m.from == from;
        return n;
    }

    void Create(const char *name, const char *data = "")
    {
        NetName n = { sizeof(NetName), 0, (char *)name, (char *)name };
        CHECK(session->CreatePlayer(&player, &n, data, (DWORD)strlen(data)) == NET_OK);
    }
};

struct Found {
    NetSessionDesc desc;
    std::string name;
};

bool Search(Node &node, Found *out)
{
    return WaitFor([&] {
        bool any = false;
        node.session->EnumSessions(&kApp,
                                   [](const NetSessionDesc *d, void *context) -> BOOL {
                                       Found *f = (Found *)context;
                                       f->desc = *d;
                                       f->name = d->lpszSessionNameA ? d->lpszSessionNameA : "";
                                       return FALSE;
                                   },
                                   out);
        any = out->name.size() > 0;
        return any;
    });
}

HRESULT Join(Node &node, const Found &found, const char *password)
{
    NetSessionDesc d = {};
    d.dwSize = sizeof(d);
    d.guidInstance = found.desc.guidInstance;
    d.lpszPasswordA = (char *)password;
    return node.session->Open(&d, NET_OPEN_JOIN);
}

std::vector<NetPlayerID> Players(Node &node)
{
    std::vector<NetPlayerID> ids;
    node.session->EnumPlayers(
        [](NetPlayerID id, const NetName *, void *context) -> BOOL {
            ((std::vector<NetPlayerID> *)context)->push_back(id);
            return TRUE;
        },
        &ids);
    return ids;
}

// Raw datagrams at the host: garbage, truncated searches, wrong versions.
void Garbage(uint16_t port)
{
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    ENetAddress to;
    enet_address_set_host(&to, "127.0.0.1");
    to.port = port;
    std::mt19937 rng(1234);
    for (int i = 0; i < 2000; i++) {
        uint8_t data[300];
        size_t n = rng() % sizeof(data);
        for (size_t j = 0; j < n; j++)
            data[j] = (uint8_t)rng();
        if (i % 3 == 0 && n >= 6) {
            memcpy(data, "CMR2", 4);    // looks like a search
            data[5] = (uint8_t)(i % 2 ? 1 : 0);
        }
        ENetBuffer b;
        b.data = data;
        b.dataLength = n;
        enet_socket_send(s, &to, &b, 1);
    }
    enet_socket_destroy(s);
}

// An ENet client that connects and then breaks the protocol.
void Intruder(uint16_t port, const std::vector<uint8_t> &packet)
{
    ENetHost *client = enet_host_create(nullptr, 1, 2, 0, 0);
    ENetAddress to;
    enet_address_set_host(&to, "127.0.0.1");
    to.port = port;
    ENetPeer *peer = enet_host_connect(client, &to, 2, 0);
    ENetEvent e;
    bool connected = false, dropped = false;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end && !dropped) {
        PumpAll();
        if (enet_host_service(client, &e, 10) > 0) {
            if (e.type == ENET_EVENT_TYPE_CONNECT) {
                connected = true;
                enet_peer_send(peer, 0, enet_packet_create(packet.data(), packet.size(), ENET_PACKET_FLAG_RELIABLE));
            } else if (e.type == ENET_EVENT_TYPE_DISCONNECT) {
                dropped = true;
            } else if (e.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(e.packet);
            }
        }
    }
    CHECK(connected);
    CHECK(dropped);     // the host hung up on it
    enet_host_destroy(client);
}

// Joins properly, then sends a message as someone else's player: the host
// must drop it and deliver nothing.
void Impostor(uint16_t port, const NetGuid &instance, const char *password, NetPlayerID victim)
{
    ENetHost *client = enet_host_create(nullptr, 1, 2, 0, 0);
    ENetAddress to;
    enet_address_set_host(&to, "127.0.0.1");
    to.port = port;
    ENetPeer *peer = enet_host_connect(client, &to, 2, 0);
    ENetEvent e;
    bool accepted = false, dropped = false;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end && !dropped) {
        PumpAll();
        if (enet_host_service(client, &e, 10) <= 0)
            continue;
        if (e.type == ENET_EVENT_TYPE_CONNECT) {
            std::vector<uint8_t> join = { 1 /*JOIN*/, 1 /*version*/ };
            join.insert(join.end(), instance.bytes, instance.bytes + 16);
            uint32_t n = (uint32_t)strlen(password);
            for (int i = 0; i < 4; i++)
                join.push_back((uint8_t)(n >> (i * 8)));
            join.insert(join.end(), password, password + n);
            enet_peer_send(peer, 0, enet_packet_create(join.data(), join.size(), ENET_PACKET_FLAG_RELIABLE));
        } else if (e.type == ENET_EVENT_TYPE_RECEIVE) {
            if (!accepted && e.packet->dataLength > 0 && e.packet->data[0] == 3 /*ACCEPT*/) {
                accepted = true;
                // DATA from machine 1 (the host's) as the host's player.
                std::vector<uint8_t> data = { 10 /*DATA*/, 1 };
                for (int i = 0; i < 4; i++)
                    data.push_back((uint8_t)(victim >> (i * 8)));
                for (int i = 0; i < 4; i++)
                    data.push_back(0);
                data.push_back('X');
                enet_peer_send(peer, 0, enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE));
            }
            enet_packet_destroy(e.packet);
        } else if (e.type == ENET_EVENT_TYPE_DISCONNECT) {
            dropped = true;
        }
    }
    CHECK(accepted);
    CHECK(dropped);
    enet_host_destroy(client);
}

// A big-endian machine (simulated byte for byte) and a little-endian one in
// one session: every payload is converted at the boundary of the big one.
void CrossEndian()
{
    using net::ByteOrder;
    // The application GUID as the game declares it, int[4]: its bytes differ per byte order.
    const uint32_t words[4] = { 0x1bff2d87, 0x11d24cb7, 0x60004cb1, 0xe0014e08 };
    NetGuid little, big;
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 4; b++) {
            little.bytes[4 * i + b] = (uint8_t)(words[i] >> (8 * b));
            big.bytes[4 * i + b] = (uint8_t)(words[i] >> (8 * (3 - b)));
        }

    Node host(0, 0, ByteOrder::Little);
    NetSessionDesc desc = {};
    desc.dwSize = sizeof(desc);
    desc.guidApplication = little;
    desc.dwMaxPlayers = 4;
    desc.dwUser1 = 9;
    desc.lpszSessionNameA = (char *)"Mixed";
    CHECK(host.session->Open(&desc, NET_OPEN_CREATE) == NET_OK);
    host.Create("host", "");
    Node guest(0, host.session->BoundPort(), ByteOrder::Big);

    // The big-endian guest searches with its own int[4] and sees the same GUID back.
    Found found;
    bool seen = WaitFor([&] {
        guest.session->EnumSessions(&big,
                                    [](const NetSessionDesc *d, void *context) -> BOOL {
                                        Found *f = (Found *)context;
                                        f->desc = *d;
                                        f->name = d->lpszSessionNameA;
                                        return FALSE;
                                    },
                                    &found);
        return !found.name.empty();
    });
    CHECK(seen);
    CHECK(found.name == "Mixed" && found.desc.dwUser1 == 9);
    CHECK(memcmp(&found.desc.guidApplication, &big, sizeof(big)) == 0);
    CHECK(Join(guest, found, "") == NET_OK);
    NetSessionDesc joined;
    CHECK(guest.session->GetSessionDesc(&joined) == NET_OK && memcmp(&joined.guidApplication, &big, 16) == 0);

    // Player descriptions: NetPlayerInfo, four words. The big-endian guest creates its player with an
    // integer flags word (as the lobby does) and later publishes bitfields (as the player description does).
    auto be32 = [](uint8_t *p, uint32_t v) {
        for (int i = 0; i < 4; i++)
            p[i] = (uint8_t)(v >> (8 * (3 - i)));
    };
    auto le32 = [](const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; };
    uint8_t info[16];
    be32(info, 0);
    be32(info + 4, 0x8f);               // car 0x0f and active, as an integer
    be32(info + 8, 0x01020304);
    be32(info + 12, 0x0a0b0c0d);
    NetName name = { sizeof(NetName), 0, (char *)"guest", (char *)"guest" };
    NetPlayerID guestPlayer = 0;
    CHECK(guest.session->CreatePlayer(&guestPlayer, &name, info, sizeof(info)) == NET_OK);
    Message m;
    CHECK(host.WaitSystem(NET_SYS_CREATEPLAYER, &m));
    {
        NetMsgCreatePlayer cp;
        memcpy(&cp, m.data.data(), sizeof(cp));
        CHECK(cp.dwDataSize == 16);
        CHECK(le32((const uint8_t *)cp.lpData + 4) == 0x8f && le32((const uint8_t *)cp.lpData + 8) == 0x01020304 &&
              le32((const uint8_t *)cp.lpData + 12) == 0x0a0b0c0d);
    }
    // Bitfields from the most significant bit: car (5), bit5, bit6, active, ready, finished, rest.
    be32(info + 4, 0x0fu << 27 | 1u << 24 | 1u << 22);     // car 0x0f, active (the 4th field), finished (the 6th)
    CHECK(guest.session->SetPlayerData(guestPlayer, info, sizeof(info)) == NET_OK);
    CHECK(host.WaitSystem(NET_SYS_SETPLAYERDATA, &m));
    {
        NetMsgSetPlayerData pd;
        memcpy(&pd, m.data.data(), sizeof(pd));
        CHECK(pd.dwDataSize == 16);
        CHECK(le32((const uint8_t *)pd.lpData + 4) == (0x0fu | 1u << 7 | 1u << 9));
    }
    CHECK(WaitFor([&] { return Players(guest).size() == 2 && Players(host).size() == 2; }));
    NetPlayerID hostPlayer = host.player;

    // Messages: a stage time (type 9: u8, u8, pad, i32) in both directions.
    const uint8_t timeLittle[8] = { 9, 3, 0xa1, 0xa2, 0x56, 0x34, 0x12, 0x00 };
    const uint8_t timeBig[8] = { 9, 3, 0xa1, 0xa2, 0x00, 0x12, 0x34, 0x56 };
    CHECK(guest.session->Send(guestPlayer, hostPlayer, TRUE, timeBig, 8) == NET_OK);
    CHECK(WaitFor([&] { return host.Count(guestPlayer) == 1; }));
    for (const Message &msg : host.received)
        if (msg.from == guestPlayer)
            CHECK(msg.data.size() == 8 && memcmp(msg.data.data(), timeLittle, 8) == 0);
    host.received.clear();
    CHECK(host.session->Send(host.player, NET_ID_ALL, TRUE, timeLittle, 8) == NET_OK);
    CHECK(WaitFor([&] { return guest.Count(hostPlayer) == 1; }));
    for (const Message &msg : guest.received)
        if (msg.from == hostPlayer)
            CHECK(msg.data.size() == 8 && memcmp(msg.data.data(), timeBig, 8) == 0);
    guest.received.clear();

    // A message that is not in the catalogue is not guessed at: the big-endian
    // machine neither sends it nor delivers it. A little-endian one passes it.
    const uint8_t unknown[5] = { 0x7f, 1, 2, 3, 4 };
    CHECK(guest.session->Send(guestPlayer, hostPlayer, TRUE, unknown, 5) == NET_ERR_INVALIDPARAMS);
    CHECK(host.session->Send(host.player, NET_ID_ALL, TRUE, unknown, 5) == NET_OK);
    CHECK(host.session->Send(host.player, NET_ID_ALL, TRUE, timeLittle, 8) == NET_OK);
    CHECK(WaitFor([&] { return guest.Count(hostPlayer) == 1; }));    // the time, not the unknown one
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(guest.Count(hostPlayer) == 1 && host.Count(guestPlayer) == 0);
}

} // namespace

int main()
{
    // ---- host and search ----
    Node host(0);
    uint16_t port = host.session->BoundPort();
    NetSessionDesc desc = {};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = 0x2064;
    desc.guidApplication = kApp;
    desc.dwMaxPlayers = 4;
    desc.dwUser1 = 7;
    desc.lpszSessionNameA = (char *)"Rally";
    desc.lpszPasswordA = (char *)"pw";
    CHECK(host.session->Open(&desc, NET_OPEN_CREATE) == NET_OK);
    CHECK(host.session->IsHost());
    host.Create("host", "H");

    Node a(0, port), b(0, port);
    Found found;
    CHECK(Search(a, &found));
    CHECK(found.name == "Rally");
    CHECK(found.desc.dwUser1 == 7);
    CHECK(found.desc.dwCurrentPlayers == 1);
    CHECK(found.desc.dwMaxPlayers == 4);
    CHECK((found.desc.dwFlags & 0x400) != 0);   // password required

    // ---- joining ----
    CHECK(Join(a, found, "wrong") == NET_ERR_INVALIDPASSWORD);
    CHECK(Join(a, found, "pw") == NET_OK);
    NetSessionDesc joined;
    if (a.session->GetSessionDesc(&joined) == NET_OK)
        CHECK(strcmp(joined.lpszSessionNameA, "Rally") == 0 && joined.dwUser1 == 7);
    else
        CHECK(!"GetSessionDesc after joining");
    a.Create("alice", "A");
    Message m;
    CHECK(host.WaitSystem(NET_SYS_CREATEPLAYER, &m));
    {
        NetMsgCreatePlayer cp;
        memcpy(&cp, m.data.data(), sizeof(cp));
        CHECK(cp.dpId == a.player);
        CHECK(strcmp(cp.dpnName.lpszShortNameA, "alice") == 0);
        CHECK(cp.dwDataSize == 1 && memcmp(cp.lpData, "A", 1) == 0);
    }

    Found again;
    CHECK(Search(b, &again));
    CHECK(Join(b, again, "pw") == NET_OK);
    // The game copies player names into 100-byte buffers: long ones are cut.
    std::string longName(500, 'b');
    longName.replace(0, 3, "bob");
    b.Create(longName.c_str(), "B");
    CHECK(a.WaitSystem(NET_SYS_CREATEPLAYER, &m));
    {
        NetMsgCreatePlayer cp;
        memcpy(&cp, m.data.data(), sizeof(cp));
        CHECK(strlen(cp.dpnName.lpszShortNameA) < 100 && strncmp(cp.dpnName.lpszShortNameA, "bob", 3) == 0);
    }
    CHECK(host.WaitSystem(NET_SYS_CREATEPLAYER));
    CHECK(Players(b).size() == 3);
    CHECK(Players(a).size() == 3);

    // ---- guaranteed messages arrive in order, through the host ----
    for (uint32_t i = 0; i < 500; i++)
        CHECK(a.session->Send(a.player, b.player, TRUE, &i, sizeof(i)) == NET_OK);
    CHECK(WaitFor([&] { return b.Count(a.player) == 500; }));
    {
        uint32_t expected = 0;
        bool ordered = true;
        for (const Message &msg : b.received) {
            if (msg.from != a.player)
                continue;
            uint32_t v;
            memcpy(&v, msg.data.data(), 4);
            ordered = ordered && v == expected++ && msg.to == b.player;
        }
        CHECK(ordered);
        b.received.clear();
    }
    CHECK(host.Count(a.player) == 0);   // addressed to bob only

    // ---- to everyone, but not back to the sender ----
    CHECK(host.session->Send(host.player, NET_ID_ALL, TRUE, "hello", 5) == NET_OK);
    CHECK(WaitFor([&] { return a.Count(host.player) == 1 && b.Count(host.player) == 1; }));
    CHECK(host.Count(host.player) == 0);

    // ---- unguaranteed (car states) ----
    for (uint32_t i = 0; i < 200; i++) {
        CHECK(a.session->Send(a.player, NET_ID_ALL, FALSE, &i, sizeof(i)) == NET_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    WaitFor([&] { return b.Count(a.player) >= 200 && host.Count(a.player) >= 200; }, 1500);
    CHECK(b.Count(a.player) >= 180);
    CHECK(host.Count(a.player) >= 180);
    a.received.clear();
    b.received.clear();
    host.received.clear();

    // ---- player data and the session description ----
    CHECK(b.session->SetPlayerData(b.player, "XYZ", 3) == NET_OK);
    CHECK(a.WaitSystem(NET_SYS_SETPLAYERDATA, &m));
    {
        NetMsgSetPlayerData pd;
        memcpy(&pd, m.data.data(), sizeof(pd));
        CHECK(pd.dpId == b.player && pd.dwDataSize == 3 && memcmp(pd.lpData, "XYZ", 3) == 0);
    }
    desc.dwUser2 = 9;
    desc.lpszSessionNameA = (char *)"Rally 2";
    CHECK(host.session->SetSessionDesc(&desc) == NET_OK);
    CHECK(a.WaitSystem(NET_SYS_SETSESSIONDESC, &m));
    {
        NetMsgSetSessionDesc sd;
        memcpy(&sd, m.data.data(), sizeof(sd));
        CHECK(sd.dpDesc.dwUser2 == 9 && strcmp(sd.dpDesc.lpszSessionNameA, "Rally 2") == 0);
    }
    CHECK(a.session->GetSessionDesc(&joined) == NET_OK && joined.dwUser2 == 9);
    CHECK(a.session->SetSessionDesc(&desc) == NET_ERR_ACCESSDENIED);

    // ---- a buffer too small keeps the message ----
    {
        b.Drain();
        b.received.clear();
        std::vector<uint8_t> big(1000, 0x5a);
        CHECK(a.session->Send(a.player, b.player, TRUE, big.data(), (DWORD)big.size()) == NET_OK);
        DWORD size = 10;
        uint8_t small[10];
        NetPlayerID from, to;
        CHECK(WaitFor([&] {
            size = 10;
            return b.session->Receive(&from, &to, small, &size) == NET_ERR_BUFFERTOOSMALL;
        }));
        CHECK(size == 1000);
        std::vector<uint8_t> buffer(size);
        CHECK(b.session->Receive(&from, &to, buffer.data(), &size) == NET_OK);
        CHECK(from == a.player && buffer == big);
    }

    // ---- a full session refuses machines ----
    {
        Node c(0, port);
        Found f;
        CHECK(Search(c, &f));       // 3 of 4: listed
        desc.dwMaxPlayers = 3;
        CHECK(host.session->SetSessionDesc(&desc) == NET_OK);
        CHECK(Join(c, f, "pw") == NET_ERR_NONEWPLAYERS);
        // Full sessions drop out of the list.
        CHECK(WaitFor([&] {
            bool listed = false;
            c.session->EnumSessions(&kApp,
                                    [](const NetSessionDesc *, void *context) -> BOOL {
                                        *(bool *)context = true;
                                        return TRUE;
                                    },
                                    &listed);
            return !listed;
        }));
        desc.dwMaxPlayers = 4;
        CHECK(host.session->SetSessionDesc(&desc) == NET_OK);
    }

    // ---- hostile input: the session keeps working ----
    Garbage(port);
    Intruder(port, std::vector<uint8_t>{ 0xff, 1, 2, 3 });                  // not JOIN/RESUME
    Intruder(port, std::vector<uint8_t>{ 1 /*JOIN*/, 1, 0, 0 });            // truncated JOIN
    CHECK(a.session->Send(a.player, b.player, TRUE, "ok", 2) == NET_OK);
    CHECK(WaitFor([&] { return b.Count(a.player) == 1; }));
    b.received.clear();

    // A member that speaks for someone else's player is dropped, and nobody
    // hears it.
    a.received.clear();
    b.received.clear();
    Impostor(port, found.desc.guidInstance, "pw", host.player);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(a.Count(host.player) == 0 && b.Count(host.player) == 0);

    // A guest that leaves properly: everyone hears it.
    {
        Node c(0, port);
        Found f;
        CHECK(Search(c, &f));
        CHECK(Join(c, f, "pw") == NET_OK);
        c.Create("carl");
        CHECK(host.WaitSystem(NET_SYS_CREATEPLAYER));
        CHECK(a.WaitSystem(NET_SYS_CREATEPLAYER));
        CHECK(b.WaitSystem(NET_SYS_CREATEPLAYER));
        NetPlayerID carl = c.player;
        CHECK(c.session->Close() == NET_OK);
        CHECK(a.WaitSystem(NET_SYS_DESTROYPLAYER, &m));
        {
            NetMsgDestroyPlayer dp;
            memcpy(&dp, m.data.data(), sizeof(dp));
            CHECK(dp.dpId == carl);
        }
        CHECK(host.WaitSystem(NET_SYS_DESTROYPLAYER));
        CHECK(b.WaitSystem(NET_SYS_DESTROYPLAYER));
        CHECK(Players(a).size() == 3);
    }

    // ---- host migration: the host leaves, the earliest machine takes over ----
    NetPlayerID hostPlayer = host.player;
    CHECK(host.session->Close() == NET_OK);
    CHECK(a.WaitSystem(NET_SYS_HOST, nullptr, 4000));
    CHECK(a.session->IsHost());
    CHECK(!b.session->IsHost());
    CHECK(b.WaitSystem(NET_SYS_DESTROYPLAYER, &m, 4000));
    {
        NetMsgDestroyPlayer dp;
        memcpy(&dp, m.data.data(), sizeof(dp));
        CHECK(dp.dpId == hostPlayer);
    }
    CHECK(WaitFor([&] { return Players(b).size() == 2 && Players(a).size() == 2; }, 4000));
    // Messages flow through the new host.
    a.received.clear();
    b.received.clear();
    CHECK(WaitFor([&] {
        b.session->Send(b.player, a.player, TRUE, "m", 1);
        return a.Count(b.player) > 0;
    }, 4000));
    CHECK(a.session->Send(a.player, NET_ID_ALL, TRUE, "n", 1) == NET_OK);
    CHECK(WaitFor([&] { return b.Count(a.player) > 0; }));

    // ---- the new host vanishes without a word: b notices by timeout ----
    a.session->Abandon();
    CHECK(b.WaitSystem(NET_SYS_HOST, nullptr, 6000));
    CHECK(b.session->IsHost());
    CHECK(Players(b).size() == 1);

    CrossEndian();

    if (s_failures != 0) {
        fprintf(stderr, "net_session: %d failures\n", s_failures);
        return 1;
    }
    printf("net_session (%s): ok\n", NET_SESSION_THREADED ? "threaded" : "cooperative");
    return 0;
}
