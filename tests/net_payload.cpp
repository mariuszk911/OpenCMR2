// The byte order of the game's network payloads (src/net/net_payload.*).
//
// A big-endian machine is simulated byte for byte: the "native" images below
// are built the way a m68k compiler lays the game's structures out (big-endian
// integers, bitfields from the most significant bit), the "wire" images the
// way an i386 compiler does. They are built from the same field values by
// code that shares nothing with the converter, so a table error shows as a
// difference. The little-endian side of the layout (that the game's own
// structures really are the wire images on this host) is pinned in
// net_payload_game.cpp.
#include "net/net_payload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <random>
#include <vector>

using net::ByteOrder;
using net::Payload;

typedef std::vector<uint8_t> Image;

static int failures;

static void Check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "net payload: FAILED %s\n", what);
        failures++;
    }
}

// ---- independent image builders -------------------------------------------------------------

static void Put(Image &im, size_t at, uint32_t v, int bytes, bool big)
{
    for (int i = 0; i < bytes; i++)
        im[at + (big ? bytes - 1 - i : i)] = (uint8_t)(v >> (8 * i));
}

// A word of bitfields: the first field is the lowest bits on i386 and the
// highest on a big-endian compiler.
static void PutBits(Image &im, size_t at, int word, const std::vector<std::pair<int, uint32_t>> &fields, bool big)
{
    uint32_t v = 0;
    int used = 0;
    for (const auto &f : fields) {
        uint32_t masked = f.second & (f.first >= 32 ? 0xffffffffu : (1u << f.first) - 1);
        v |= big ? masked << (word - used - f.first) : masked << used;
        used += f.first;
    }
    Check(used == word, "bitfield widths fill the word");
    Put(im, at, v, word / 8, big);
}

static const uint8_t kPad[4] = { 0xa1, 0xa2, 0xa3, 0xa4 };   // padding bytes pass through

struct Pair {
    Image native, wire;
};

static Pair Setup(uint8_t type)
{
    Pair p;
    for (int big = 0; big < 2; big++) {
        Image &im = big ? p.native : p.wire;
        im.assign(0x1c4, 0);
        im[0] = type;
        memcpy(&im[1], kPad, 3);
        PutBits(im, 4, 32, { { 8, 0x0b }, { 4, 0x9 }, { 4, 0x6 }, { 2, 2 }, { 2, 1 }, { 4, 0xa }, { 8, 0xc3 } }, big);
        for (int i = 0; i < 12; i++)
            im[8 + i] = (uint8_t)(0x30 + i);
        Put(im, 0x14, 0x11223344, 4, big);
        for (int i = 0; i < 100; i++)
            Put(im, 0x18 + 4 * i, 0x01020304u * (i + 1) ^ (uint32_t)i << 17, 4, big);
        for (int i = 0; i < 0x14; i++)
            im[0x1a8 + i] = (uint8_t)(0x80 + i);
        Put(im, 0x1bc, 0xdeadbeef, 4, big);
        Put(im, 0x1c0, 0x8badf00d, 4, big);
    }
    return p;
}

static Pair Time(uint8_t type)
{
    Pair p;
    for (int big = 0; big < 2; big++) {
        Image &im = big ? p.native : p.wire;
        im.assign(8, 0);
        im[0] = type;
        im[1] = 5;
        im[2] = 0xa1;
        im[3] = 0xa2;
        Put(im, 4, 0x00123456, 4, big);
    }
    return p;
}

static Pair Finish()
{
    Pair p;
    for (int big = 0; big < 2; big++) {
        Image &im = big ? p.native : p.wire;
        im.assign(12, 0);
        im[0] = 0x0a;
        memcpy(&im[1], kPad, 3);
        Put(im, 4, 0x00012345, 4, big);
        Put(im, 8, 0xfffedcba, 4, big);
    }
    return p;
}

static Pair CarState()
{
    Pair p;
    for (int big = 0; big < 2; big++) {
        Image &im = big ? p.native : p.wire;
        im.assign(0x20, 0);
        im[0] = 0x0b;
        im[1] = 0xa1;
        Put(im, 0x02, 0xbeef, 2, big);                                 // seq
        for (int i = 0; i < 6; i++)
            Put(im, 0x04 + 2 * i, 0x1234u + 0x0101u * i, 2, big);     // dispX .. sector
        PutBits(im, 0x10, 16, { { 8, 0x7e }, { 8, 0x81 } }, big);      // height, heightDelta
        Put(im, 0x12, 0xa5c3, 2, big);                                 // axes[0]
        Put(im, 0x14, 0x5a3c, 2, big);                                 // axes[1]
        PutBits(im, 0x16, 16, { { 8, 0x7f }, { 8, 0x83 } }, big);      // angVelX, angVelY
        PutBits(im, 0x18, 16, { { 8, 0x85 }, { 1, 1 }, { 1, 0 }, { 6, 0x2d } }, big);  // angVelZ, flags, progress
        PutBits(im, 0x1a, 16, { { 10, 0x2f1 }, { 6, 0x15 } }, big);   // node, pad
        PutBits(im, 0x1c, 16, { { 7, 0x4b }, { 1, 1 }, { 1, 0 }, { 1, 1 }, { 1, 0 }, { 4, 0xb }, { 1, 1 } }, big);
        Put(im, 0x1e, 0xcafe, 2, big);
    }
    return p;
}

static Pair Leaderboard(bool valid)
{
    Pair p;
    for (int big = 0; big < 2; big++) {
        Image &im = big ? p.native : p.wire;
        im.assign(0x108, 0);
        im[0] = 0x11;
        im[1] = valid ? 1 : 0;
        im[2] = 0xa1;
        im[3] = 0xa2;
        im[4] = 1;                                                     // isLoaded
        im[5] = 0xb1;                                                  // reserved
        im[6] = 0xb2;
        im[7] = 0xb3;
        for (int i = 0; i < 32; i++) {
            size_t entry = 8 + 8 * i;
            memcpy(&im[entry], "ab\0\0", 4);
            im[entry] = (uint8_t)('A' + i);
            Put(im, entry + 4, 1000u * i + 7, 4, big);                 // wins
        }
    }
    return p;
}

static Pair PlayerInfo(bool bitfields, bool nativeBitfields)
{
    // Wire: id, flags (integer: car 0x15, bit5, active, finished), 8, c.
    Pair p;
    const uint32_t car = 0x15, bit5 = 1, bit6 = 0, active = 1, ready = 0, finished = 1, rest = 0x2a5a5a;
    for (int big = 0; big < 2; big++) {
        Image &im = big ? p.native : p.wire;
        im.assign(0x10, 0);
        Put(im, 0, 0x00010002, 4, big);
        if (big && nativeBitfields && bitfields)
            PutBits(im, 4, 32, { { 5, car }, { 1, bit5 }, { 1, bit6 }, { 1, active }, { 1, ready }, { 1, finished },
                                 { 22, rest } }, true);
        else
            Put(im, 4, car | bit5 << 5 | bit6 << 6 | active << 7 | ready << 8 | finished << 9 | rest << 10, 4, big);
        Put(im, 8, 0x12345678, 4, big);
        Put(im, 12, 0x9abcdef0, 4, big);
    }
    return p;
}

// ---- tests -----------------------------------------------------------------------------------

static void Message(const char *name, const Pair &p)
{
    Image a = p.native;
    Check(net::PayloadToWire(ByteOrder::Big, Payload::Message, a.data(), a.size()) && a == p.wire, name);
    Image b = p.wire;
    Check(net::PayloadFromWire(ByteOrder::Big, Payload::Message, b.data(), b.size()) && b == p.native, name);
    // A little-endian machine's native image is the wire image.
    Image c = p.wire;
    Check(net::PayloadToWire(ByteOrder::Little, Payload::Message, c.data(), c.size()) && c == p.wire, name);
    Check(net::PayloadFromWire(ByteOrder::Little, Payload::Message, c.data(), c.size()) && c == p.wire, name);
}

static void Plain(const char *name, uint8_t type, size_t size)
{
    Pair p;
    p.native.assign(size, 0x5c);
    p.native[0] = type;
    p.wire = p.native;
    Message(name, p);
}

int main()
{
    Message("setup 2", Setup(2));
    Message("setup 3", Setup(3));
    Message("split time", Time(8));
    Message("stage time", Time(9));
    Message("finish", Finish());
    Message("car state", CarState());
    Message("leaderboard", Leaderboard(true));
    Message("leaderboard (none)", Leaderboard(false));
    Plain("chat", 0x00, 0x101);
    Plain("car class", 0x06, 2);
    for (uint8_t type : { 0x07, 0x0c, 0x0d, 0x0e, 0x0f, 0x10 })
        Plain("one byte message", type, 1);

    // Player data: the integer description, and the one the game fills through bitfields.
    {
        Pair p = PlayerInfo(false, false);
        Image a = p.native;
        Check(net::PayloadToWire(ByteOrder::Big, Payload::PlayerData, a.data(), a.size()) && a == p.wire,
              "player data to wire");
        Image b = p.wire;
        Check(net::PayloadFromWire(ByteOrder::Big, Payload::PlayerData, b.data(), b.size()) && b == p.native,
              "player data from wire");
        Pair q = PlayerInfo(true, true);
        Image c = q.native;
        Check(net::PayloadToWire(ByteOrder::Big, Payload::PlayerDataBitfields, c.data(), c.size()) && c == q.wire,
              "bitfield player data to wire");
        Image d = q.wire;
        Check(net::PayloadFromWire(ByteOrder::Big, Payload::PlayerDataBitfields, d.data(), d.size()) &&
                  d == PlayerInfo(true, false).native,
              "bitfield player data from wire is the integer form");
        Check(c != PlayerInfo(true, false).native, "the bitfield image differs from the integer one");
        // Little-endian: unchanged.
        Image e = q.wire;
        Check(net::PayloadToWire(ByteOrder::Little, Payload::PlayerDataBitfields, e.data(), e.size()) && e == q.wire,
              "little-endian player data");
        // No data is fine, other sizes are not catalogued on a big-endian host.
        Check(net::PayloadToWire(ByteOrder::Big, Payload::PlayerData, nullptr, 0), "empty player data");
        Image odd(4, 0);
        Check(!net::PayloadToWire(ByteOrder::Big, Payload::PlayerData, odd.data(), odd.size()),
              "unexpected player data size refused");
        Check(net::PayloadToWire(ByteOrder::Little, Payload::PlayerData, odd.data(), odd.size()),
              "little-endian host passes any size");
    }

    // Unknown messages: refused on a big-endian host, passed on a little-endian one.
    {
        Image unknown(5, 0);
        unknown[0] = 0x7f;
        Check(!net::PayloadToWire(ByteOrder::Big, Payload::Message, unknown.data(), unknown.size()), "unknown type");
        Check(!net::PayloadFromWire(ByteOrder::Big, Payload::Message, unknown.data(), unknown.size()),
              "unknown type from the wire");
        Check(net::PayloadToWire(ByteOrder::Little, Payload::Message, unknown.data(), unknown.size()),
              "unknown type on a little-endian host");
        for (uint8_t type : { 2, 3, 8, 9, 0x0a, 0x0b, 0x11, 0x00, 0x06, 0x07 }) {
            Image wrong(type == 0x07 ? 2 : 3, 0);
            wrong[0] = type;
            Check(!net::PayloadToWire(ByteOrder::Big, Payload::Message, wrong.data(), wrong.size()),
                  "wrong size refused");
        }
        Check(!net::PayloadToWire(ByteOrder::Big, Payload::Message, nullptr, 0), "empty message");
        Check(!net::PayloadToWire(ByteOrder::Big, Payload::Message, nullptr, 8), "null message");
    }

    // Every catalogued shape: random bytes survive native -> wire -> native.
    {
        std::mt19937 rng(1234);
        const struct { uint8_t type; size_t size; } shapes[] = {
            { 0, 0x101 }, { 2, 0x1c4 }, { 3, 0x1c4 }, { 6, 2 }, { 7, 1 }, { 8, 8 }, { 9, 8 }, { 10, 12 },
            { 11, 0x20 }, { 12, 1 }, { 13, 1 }, { 14, 1 }, { 15, 1 }, { 16, 1 }, { 17, 0x108 },
        };
        for (const auto &s : shapes) {
            for (int n = 0; n < 50; n++) {
                Image x(s.size);
                for (auto &b : x)
                    b = (uint8_t)rng();
                x[0] = s.type;
                Image y = x;
                Check(net::PayloadToWire(ByteOrder::Big, Payload::Message, y.data(), y.size()), "catalogued");
                Check(net::PayloadFromWire(ByteOrder::Big, Payload::Message, y.data(), y.size()) && y == x,
                      "round trip");
            }
        }
        for (int n = 0; n < 50; n++) {
            Image x(0x10);
            for (auto &b : x)
                b = (uint8_t)rng();
            Image y = x;
            Check(net::PayloadToWire(ByteOrder::Big, Payload::PlayerData, y.data(), y.size()), "ok");
            Check(net::PayloadFromWire(ByteOrder::Big, Payload::PlayerData, y.data(), y.size()) && y == x,
                  "player data round trip");
        }
    }

    // The application GUID: int[4] in the native byte order, four little-endian words on the wire.
    {
        const int words[4] = { 0x1bff2d87, 0x11d24cb7, 0x60004cb1, (int)0xe0014e08 };
        NetGuid native, wire;
        for (int i = 0; i < 4; i++) {
            for (int b = 0; b < 4; b++) {
                native.bytes[4 * i + b] = (uint8_t)((uint32_t)words[i] >> (8 * (3 - b)));   // big-endian memory
                wire.bytes[4 * i + b] = (uint8_t)((uint32_t)words[i] >> (8 * b));           // little-endian memory
            }
        }
        NetGuid g = native;
        net::ApplicationGuidSwap(ByteOrder::Big, &g);
        Check(memcmp(&g, &wire, sizeof(g)) == 0, "guid to wire");
        net::ApplicationGuidSwap(ByteOrder::Big, &g);
        Check(memcmp(&g, &native, sizeof(g)) == 0, "guid back");
        g = wire;
        net::ApplicationGuidSwap(ByteOrder::Little, &g);
        Check(memcmp(&g, &wire, sizeof(g)) == 0, "little-endian guid is untouched");
    }

    // The compiler allocates bitfields the way the table models it for this machine.
    Check(net::PayloadLayoutSelfTest(), "bitfield layout of this compiler");

    if (failures != 0) {
        fprintf(stderr, "net payload: %d failures\n", failures);
        return 1;
    }
    puts("net payload: catalogue, big-endian conversion, GUID and layout self test passed");
    return 0;
}
