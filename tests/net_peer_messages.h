// The values of the cross-play peer test, shared by tests/net_cross_peer.cpp
// (Linux, wire images built by hand) and tests/amiga/net_peer_test.cpp (the
// Amiga, native structures as the game declares them). Whatever one side
// sends, the other must read back with exactly these values.
#ifndef OPENCMR2_TESTS_NET_PEER_MESSAGES_H
#define OPENCMR2_TESTS_NET_PEER_MESSAGES_H

#define PEER_PORT 28620
// The game's application GUID, int[4] (game/GameInfo.cpp g_unk0x00511cd8).
#define PEER_APP_WORDS { 0x1bff2d87, 0x11d24cb7, 0x60004cb1, (int)0xe0014e08 }

// Setup packet (type 2 from the Amiga host, type 3 from the Linux guest).
#define AMIGA_SETUP_MODE 9
#define AMIGA_SETUP_COUNTRY 5
#define AMIGA_SETUP_STAGE 3
#define AMIGA_SETUP_RALLY_A 2
#define AMIGA_SETUP_RALLY_B 1
#define AMIGA_SETUP_RALLY_C 0xa
#define AMIGA_SETUP_F14 0x12345678
#define AMIGA_SETUP_PAIRING(i) (0x1000 + (i))
#define AMIGA_SETUP_BYTE(i) (0x40 + (i))
#define AMIGA_SETUP_F1BC 0x0abc
#define AMIGA_SETUP_F1C0 0x0def

#define LINUX_SETUP_MODE 0xb
#define LINUX_SETUP_COUNTRY 6
#define LINUX_SETUP_STAGE 4
#define LINUX_SETUP_RALLY_A 1
#define LINUX_SETUP_RALLY_B 2
#define LINUX_SETUP_RALLY_C 5
#define LINUX_SETUP_F14 0x0badcafe
#define LINUX_SETUP_PAIRING(i) (0x2000 + (i))
#define LINUX_SETUP_BYTE(i) (0x80 + (i))
#define LINUX_SETUP_F1BC 0x0123
#define LINUX_SETUP_F1C0 0x0456

// Times.
#define PEER_SPLIT_INDEX 2
#define PEER_SPLIT_TIME 0x123456
#define PEER_STAGE_INDEX 4
#define PEER_STAGE_TIME 0x654321
#define PEER_FINISH_TIME 4242
#define PEER_FINISH_TOTAL 99999

// Leaderboard entry i: name letter 'A' + i, wins 100 * i + 3 (the Amiga publishes it).
#define PEER_BOARD_WINS(i) (100 * (i) + 3)

// Car state fields (NetCarPacket), the Linux side sends them, the Amiga reads them by name.
#define CAR_SEQ 0x1234
#define CAR_DISP_X 0x2345
#define CAR_DISP_Z 0x3456
#define CAR_ENGINE 0x4567
#define CAR_POS_X 0x5678
#define CAR_POS_Z 0x6789
#define CAR_SECTOR 0x0123
#define CAR_HEIGHT 0x7e
#define CAR_HEIGHT_DELTA 0x81
#define CAR_AXIS0 0xa5c3
#define CAR_AXIS1 0x5a3c
#define CAR_ANGVEL_X 0x7f
#define CAR_ANGVEL_Y 0x83
#define CAR_ANGVEL_Z 0x85
#define CAR_FLAG_B35 1
#define CAR_FLAG_B54 0
#define CAR_PROGRESS 0x2d
#define CAR_NODE 0x2f1
#define CAR_PAD 0x15
#define CAR_STEER 0x4b
#define CAR_STEER_FOLLOW 1
#define CAR_FLAG_C00 0
#define CAR_SHAKING 1
#define CAR_MENU_OPEN 0
#define CAR_STAGE 0xb
#define CAR_STAGE_BEHIND 1
#define CAR_LAST 0xcafe

// Player descriptions (NetPlayerInfo flags as the game reads them, integer masks).
#define PEER_FLAGS_AMIGA (0x1fu | 0x80u)           // car 0x1f, active
#define PEER_FLAGS_AMIGA_SET (0x0fu | 0x40u | 0x80u | 0x200u)   // car 0x0f, bit6, active, finished
#define PEER_FLAGS_LINUX (0x07u | 0x20u | 0x80u)   // car 7, bit5, active

#define AMIGA_CHAT "hello from the Amiga"
#define LINUX_CHAT "hello from Linux"

#endif
