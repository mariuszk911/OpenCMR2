// The replay recorder extends a 4-byte input packet run only while the
// controls stay the same (Replay_CanExtendControlPacketRun). On the Amiga the
// check used to read the packet through LSB-first bitfields, which GCC lays
// out MSB-first on big-endian m68k: the two control flags in the top bits of
// byte 2 were never compared, so their changes were not recorded and the
// replay of a just-driven stage left the recorded line. This test runs on the
// host and on the target (amiga_replay_packet_test).
#include "ReplayStream.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void Expect(int got, int want, const char *what)
{
    if (got != want) {
        printf("replay_packet: FAIL %s got=%d want=%d\n", what, got, want);
        fails++;
    }
}

// Byte layout of Replay_EncodeControlPacket: byte 2 holds the repeat count in
// bits 0..5, control field 0xc in bit 6 and control field 8 in bit 7.
static void Packet(BYTE *p, BYTE count, int field8, int fieldC)
{
    p[0] = 0x5a;
    p[1] = 0x25;
    p[2] = (BYTE)((field8 ? 0x80 : 0) | (fieldC ? 0x40 : 0) | (count & 0x3f));
    p[3] = 0xc7;
}

int main(void)
{
    BYTE a[4], b[4];
    int i;

    Packet(a, 5, 1, 0);
    Expect(ReplayPacket_Count(a), 5, "count bits 0..5");
    Packet(a, 0x3f, 1, 1);
    Expect(ReplayPacket_Count(a), 0x3f, "count full run ignores flags");

    // The repeat count alone never makes two packets differ.
    Packet(a, 5, 1, 0);
    Packet(b, 2, 1, 0);
    Expect(ReplayPacket_SameControls(a, b), 1, "count difference ignored");

    // Each byte 2 control flag does.
    Packet(b, 5, 0, 0);
    Expect(ReplayPacket_SameControls(a, b), 0, "byte 2 bit 7 change");
    Packet(b, 5, 1, 1);
    Expect(ReplayPacket_SameControls(a, b), 0, "byte 2 bit 6 change");

    // So does every bit of bytes 0, 1 and 3.
    for (i = 0; i < 8; i++) {
        Packet(b, 5, 1, 0);
        b[0] ^= (BYTE)(1 << i);
        Expect(ReplayPacket_SameControls(a, b), 0, "byte 0 bit change");
        Packet(b, 5, 1, 0);
        b[1] ^= (BYTE)(1 << i);
        Expect(ReplayPacket_SameControls(a, b), 0, "byte 1 bit change");
        Packet(b, 5, 1, 0);
        b[3] ^= (BYTE)(1 << i);
        Expect(ReplayPacket_SameControls(a, b), 0, "byte 3 bit change");
    }

    if (fails != 0)
        return 1;
    printf("replay_packet: OK\n");
    return 0;
}
