// See net_payload.h.
//
// Offsets and layouts are the i386 ones. Bitfield words are described by the
// widths of their fields in declaration order: a little-endian compiler puts
// the first field in the lowest bits, a big-endian one (GCC for m68k) in the
// highest, and a word is converted by moving every field between the two.

#include "net/net_payload.h"

#include <string.h>

namespace net {

namespace {

enum class Kind : uint8_t { Swap16, Swap32, Bits16, Bits32 };

struct Bits {
    uint8_t count;
    uint8_t width[8];      // declaration order
};

struct Op {
    uint16_t offset;
    Kind kind;
    uint16_t count;        // consecutive elements
    uint16_t stride;       // bytes between elements
    const Bits *bits;      // Bits16/Bits32
};

// ---- bitfield layouts (widths in declaration order) -----------------------------------

// NetSetupPacket, word at 4: gameMode, country, stage, rallyA, rallyB, rallyC, unused.
const Bits kSetupBits = { 7, { 8, 4, 4, 2, 2, 4, 8 } };
// NetPlayerFlags: car, bit5, bit6, active, ready, finished, rest.
const Bits kPlayerFlagBits = { 7, { 5, 1, 1, 1, 1, 1, 22 } };
// NetCarPacket words (game/NetRace.cpp), the ones that hold bitfields.
const Bits kHeightBits = { 2, { 8, 8 } };                      // height, heightDelta
const Bits kAngVelXYBits = { 2, { 8, 8 } };                    // angVelX, angVelY
const Bits kAngVelZBits = { 4, { 8, 1, 1, 6 } };               // angVelZ, flagB35, flagB54, progress
const Bits kNodeBits = { 2, { 10, 6 } };                       // node, pad18
const Bits kSteerBits = { 7, { 7, 1, 1, 1, 1, 4, 1 } };        // steer, steerFollow, flagC00, shaking,
                                                               // menuOpen, stage, stageBehind

// ---- operations per message ----------------------------------------------------------------

const Op kSetupOps[] = {
    { 0x004, Kind::Bits32, 1, 4, &kSetupBits },
    { 0x014, Kind::Swap32, 1, 4, nullptr },     // field_0x14
    { 0x018, Kind::Swap32, 100, 4, nullptr },   // RallyPairingTables
    { 0x1bc, Kind::Swap32, 2, 4, nullptr },     // field_0x1bc, field_0x1c0
};
const Op kTimeOps[] = {
    { 0x004, Kind::Swap32, 1, 4, nullptr },     // time
};
const Op kFinishOps[] = {
    { 0x004, Kind::Swap32, 2, 4, nullptr },     // time, total
};
// The NetStats block starts at 2: seq, data[0x14] (ten words), four more words.
const Op kCarStateOps[] = {
    { 0x002, Kind::Swap16, 1, 2, nullptr },     // seq
    { 0x004, Kind::Swap16, 6, 2, nullptr },     // dispX, dispZ, engineSpeed, posX, posZ, sector
    { 0x010, Kind::Bits16, 1, 2, &kHeightBits },
    { 0x012, Kind::Swap16, 2, 2, nullptr },     // axes[2]
    { 0x016, Kind::Bits16, 1, 2, &kAngVelXYBits },
    { 0x018, Kind::Bits16, 1, 2, &kAngVelZBits },
    { 0x01a, Kind::Bits16, 1, 2, &kNodeBits },
    { 0x01c, Kind::Bits16, 1, 2, &kSteerBits },
    { 0x01e, Kind::Swap16, 1, 2, nullptr },     // field_0x1c
};
// Published leaderboard: valid at 1, then NetworkLeaderboard at 4 (isLoaded and
// three reserved bytes, then 32 entries of name[4] and wins).
const Op kLeaderboardOps[] = {
    { 0x00c, Kind::Swap32, 32, 8, nullptr },    // wins
};

struct Shape {
    uint8_t type;
    uint16_t size;
    const Op *ops;
    size_t opCount;
};

#define OPS(a) a, sizeof(a) / sizeof(a[0])
const Shape kMessages[] = {
    { 0x00, 0x101, nullptr, 0 },                // chat line: type, NUL-terminated text
    { 0x02, 0x1c4, OPS(kSetupOps) },            // setup packet from the host
    { 0x03, 0x1c4, OPS(kSetupOps) },            // setup packet from a joining player
    { 0x06, 2, nullptr, 0 },                    // car class
    { 0x07, 1, nullptr, 0 },                    // ready
    { 0x08, 8, OPS(kTimeOps) },                 // split time
    { 0x09, 8, OPS(kTimeOps) },                 // stage time
    { 0x0a, 12, OPS(kFinishOps) },              // finish: time and total
    { 0x0b, 0x20, OPS(kCarStateOps) },          // car state
    { 0x0c, 1, nullptr, 0 },                    // results quit
    { 0x0d, 1, nullptr, 0 },                    // continue
    { 0x0e, 1, nullptr, 0 },                    // close
    { 0x0f, 1, nullptr, 0 },                    // restart
    { 0x10, 1, nullptr, 0 },                    // type 16 notification
    { 0x11, 0x108, OPS(kLeaderboardOps) },      // published leaderboard
};

const Op kPlayerDataOps[] = {
    { 0x000, Kind::Swap32, 4, 4, nullptr },     // id, flags (integer), field_0x8, field_0xc
};
const Op kPlayerDataBitfieldOps[] = {
    { 0x000, Kind::Swap32, 1, 4, nullptr },     // id
    { 0x004, Kind::Bits32, 1, 4, &kPlayerFlagBits },
    { 0x008, Kind::Swap32, 2, 4, nullptr },     // field_0x8, field_0xc
};
const size_t kPlayerDataSize = 0x10;

// ---- primitives (explicit byte order, independent of the machine running the code) -----

uint32_t Load(const uint8_t *p, int bytes, bool big)
{
    uint32_t v = 0;
    for (int i = 0; i < bytes; i++)
        v |= (uint32_t)p[big ? bytes - 1 - i : i] << (8 * i);
    return v;
}

void Store(uint8_t *p, int bytes, bool big, uint32_t v)
{
    for (int i = 0; i < bytes; i++)
        p[big ? bytes - 1 - i : i] = (uint8_t)(v >> (8 * i));
}

void Reverse(uint8_t *p, int bytes)
{
    for (int i = 0; i < bytes / 2; i++) {
        uint8_t t = p[i];
        p[i] = p[bytes - 1 - i];
        p[bytes - 1 - i] = t;
    }
}

// Moves every field of a word between the two allocation orders. toWire: from
// the big-endian compiler's (first field in the top bits) to the i386's.
uint32_t Repack(uint32_t value, const Bits &bits, int word, bool toWire)
{
    uint32_t out = 0;
    int used = 0;
    for (int i = 0; i < bits.count; i++) {
        int w = bits.width[i];
        uint32_t mask = w >= 32 ? 0xffffffffu : (1u << w) - 1;
        int msbShift = word - used - w;
        int lsbShift = used;
        if (toWire)
            out |= ((value >> msbShift) & mask) << lsbShift;
        else
            out |= ((value >> lsbShift) & mask) << msbShift;
        used += w;
    }
    return out;
}

// A big-endian host's native image <-> the wire image.
void Convert(uint8_t *bytes, const Op *ops, size_t opCount, bool toWire)
{
    for (size_t i = 0; i < opCount; i++) {
        const Op &op = ops[i];
        for (unsigned n = 0; n < op.count; n++) {
            uint8_t *p = bytes + op.offset + (size_t)n * op.stride;
            switch (op.kind) {
            case Kind::Swap16:
                Reverse(p, 2);
                break;
            case Kind::Swap32:
                Reverse(p, 4);
                break;
            case Kind::Bits16:
            case Kind::Bits32: {
                int width = op.kind == Kind::Bits16 ? 2 : 4;
                // To the wire: read the native big-endian word, write it little-endian.
                uint32_t v = Load(p, width, toWire);
                v = Repack(v, *op.bits, width * 8, toWire);
                Store(p, width, !toWire, v);
                break;
            }
            }
        }
    }
}

const Shape *FindMessage(const uint8_t *bytes, size_t size)
{
    if (size == 0)
        return nullptr;
    for (const Shape &s : kMessages)
        if (s.type == bytes[0] && s.size == size)
            return &s;
    return nullptr;
}

bool Run(ByteOrder host, Payload kind, uint8_t *bytes, size_t size, bool toWire)
{
    if (host == ByteOrder::Little)
        return true;
    if (size != 0 && bytes == nullptr)
        return false;
    switch (kind) {
    case Payload::Message: {
        const Shape *shape = FindMessage(bytes, size);
        if (shape == nullptr)
            return false;
        Convert(bytes, shape->ops, shape->opCount, toWire);
        return true;
    }
    case Payload::PlayerData:
    case Payload::PlayerDataBitfields:
        if (size == 0)
            return true;
        if (size != kPlayerDataSize)
            return false;
        if (kind == Payload::PlayerDataBitfields && toWire)
            Convert(bytes, OPS(kPlayerDataBitfieldOps), true);
        else
            Convert(bytes, OPS(kPlayerDataOps), toWire);
        return true;
    }
    return false;
}

// The compiler's own bitfield allocation, for the self test: the same
// declarations as the game's.
struct ProbeSetup {
    unsigned a : 8, b : 4, c : 4, d : 2, e : 2, f : 4, g : 8;
};
struct ProbeFlags {
    unsigned car : 5, bit5 : 1, bit6 : 1, active : 1, ready : 1, finished : 1, rest : 22;
};
struct ProbeAngVelZ {
    unsigned short a : 8, b : 1, c : 1, d : 6;
};
struct ProbeSteer {
    unsigned short a : 7, b : 1, c : 1, d : 1, e : 1, f : 4, g : 1;
};

// Checks one probe: the value the fields would have in the i386 (first field
// lowest) word, put in the host's allocation order, must be what the compiler
// stored.
template <class Probe>
bool ProbeMatches(const Probe &probe, const Bits &bits, const uint32_t *fields, int word)
{
    uint32_t wire = 0;
    int used = 0;
    for (int i = 0; i < bits.count; i++) {
        wire |= fields[i] << used;
        used += bits.width[i];
    }
    bool big = kNativeOrder == ByteOrder::Big;
    uint32_t expect = big ? Repack(wire, bits, word, false) : wire;
    uint8_t raw[4] = {};
    memcpy(raw, &probe, sizeof(probe));
    return sizeof(probe) == (size_t)word / 8 && Load(raw, word / 8, big) == expect;
}

} // namespace

bool PayloadToWire(ByteOrder host, Payload kind, uint8_t *bytes, size_t size)
{
    return Run(host, kind, bytes, size, true);
}

bool PayloadFromWire(ByteOrder host, Payload kind, uint8_t *bytes, size_t size)
{
    return Run(host, kind, bytes, size, false);
}

void ApplicationGuidSwap(ByteOrder host, NetGuid *guid)
{
    if (host == ByteOrder::Little || guid == nullptr)
        return;
    for (int i = 0; i < 16; i += 4)
        Reverse(guid->bytes + i, 4);
}

bool PayloadLayoutSelfTest()
{
    ProbeSetup setup = {};
    setup.a = 0x5a; setup.b = 0x9; setup.c = 0x6; setup.d = 2; setup.e = 1; setup.f = 0xa; setup.g = 0xc3;
    const uint32_t setupFields[] = { 0x5a, 0x9, 0x6, 2, 1, 0xa, 0xc3 };

    ProbeFlags flags = {};
    flags.car = 0x15; flags.bit5 = 1; flags.bit6 = 0; flags.active = 1; flags.ready = 0; flags.finished = 1;
    flags.rest = 0x2a5a5a;
    const uint32_t flagFields[] = { 0x15, 1, 0, 1, 0, 1, 0x2a5a5a };

    ProbeAngVelZ angVel = {};
    angVel.a = 0xa7; angVel.b = 1; angVel.c = 0; angVel.d = 0x2b;
    const uint32_t angFields[] = { 0xa7, 1, 0, 0x2b };

    ProbeSteer steer = {};
    steer.a = 0x55; steer.b = 1; steer.c = 0; steer.d = 1; steer.e = 0; steer.f = 0xb; steer.g = 1;
    const uint32_t steerFields[] = { 0x55, 1, 0, 1, 0, 0xb, 1 };

    return ProbeMatches(setup, kSetupBits, setupFields, 32) && ProbeMatches(flags, kPlayerFlagBits, flagFields, 32) &&
           ProbeMatches(angVel, kAngVelZBits, angFields, 16) && ProbeMatches(steer, kSteerBits, steerFields, 16);
}

} // namespace net
