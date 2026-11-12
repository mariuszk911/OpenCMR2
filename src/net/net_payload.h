// Byte order of the game's network payloads.
//
// The game sends its C structures raw (Net_Send, Net_CreatePlayer,
// Net_SetPlayerData). On the wire every payload is in the original PC layout:
// i386, little-endian, i386 bitfield order (the first declared bitfield is the
// least significant). Little-endian machines send and receive it unchanged; a
// big-endian machine (the Amiga) converts between its native image and the
// wire image at the network boundary, so an Amiga and a Linux machine read
// each other's messages.
//
// The table in net_payload.cpp is the catalogue of every message the game
// sends, taken from game/ (NetRace.cpp, NetPlayers.cpp, FrontendScreens.cpp,
// GameInfo.cpp) and documented in docs/NETWORK.md. A big-endian host does not
// guess: a message that is not in the catalogue is refused (false) and the
// caller drops it and logs it once per type. A little-endian host passes
// everything through.
//
// The conversions take the host byte order as an argument so that host tests
// can simulate a big-endian machine byte for byte; production code passes
// kNativeOrder.

#ifndef OPENCMR2_NET_PAYLOAD_H
#define OPENCMR2_NET_PAYLOAD_H

#include "port/net.h"

#include <stddef.h>
#include <stdint.h>

namespace net {

enum class ByteOrder { Little, Big };

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr ByteOrder kNativeOrder = ByteOrder::Big;
#else
constexpr ByteOrder kNativeOrder = ByteOrder::Little;
#endif

// What the bytes mean in the native image.
enum class Payload {
    // A message from Net_Send / Net_Receive: the first byte is its type.
    Message,
    // The 0x10-byte NetPlayerInfo the game gives Net_CreatePlayer. Its flags
    // word is an integer (the game builds it with masks).
    PlayerData,
    // The NetPlayerInfo the game gives Net_SetPlayerData. The game fills it
    // through the NetPlayerFlags bitfields, which a big-endian compiler lays
    // out from the most significant bit. On the wire it is the same integer
    // as PlayerData, and that is what the game reads back from either.
    PlayerDataBitfields,
};

// Native image -> wire image, in place. Little-endian hosts always succeed.
// A big-endian host returns false for a payload that is not in the catalogue
// (its type or size is unknown); the bytes are then unspecified.
bool PayloadToWire(ByteOrder host, Payload kind, uint8_t *bytes, size_t size);

// Wire image -> native image, in place. PlayerDataBitfields is read like
// PlayerData: the game reads received descriptions as integers.
bool PayloadFromWire(ByteOrder host, Payload kind, uint8_t *bytes, size_t size);

// The application GUID is declared by the game as int[4] and copied by
// memcpy; on the wire it is four little-endian words. Swapping is its own
// inverse. The session instance GUID is opaque bytes and is not touched.
void ApplicationGuidSwap(ByteOrder host, NetGuid *guid);

// Checks the one assumption conversions rely on that the compiler decides:
// that bitfields are allocated as the table models them for kNativeOrder.
// Returns false (and the network must stay off) when it is not so.
bool PayloadLayoutSelfTest();

} // namespace net

#endif
