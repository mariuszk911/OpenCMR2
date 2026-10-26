/*
 * M1: the game indexes and compares the packed callback state as a
 * little-endian DWORD. On 68k the bitfields are declared in reverse order so
 * the packed value stays identical to the MSVC layout. This is the crash that
 * made the first frontend render call address 5.
 */
#include "CallbackState.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void Check(const char *what, unsigned int actual, unsigned int expected)
{
    if (actual != expected) {
        printf("callback_state: %s: got %lx expected %lx\n", what,
               (unsigned long)actual, (unsigned long)expected);
        failures++;
    }
}

int main(void)
{
    CallbackStateRecord record;

    memset(&record, 0, sizeof(record));
    record.bits.state = 0;
    record.bits.value = 0xFF;
    record.bits.rule = 1;
    record.bits.level = 2;
    Check("packed", (unsigned int)record.packedState, 0x0201FF00u);
    Check("state byte", (unsigned int)record.packedState & 0xFF, 0);
    Check("value byte", ((unsigned int)record.packedState >> 8) & 0xFF, 0xFF);
    Check("rule byte", ((unsigned int)record.packedState >> 16) & 0xFF, 1);
    Check("level bits", ((unsigned int)record.packedState >> 24) & 3, 2);

    memset(&record, 0, sizeof(record));
    record.bits.state = 7;
    record.bits.value = 0x12;
    record.bits.rule = 0;
    record.bits.level = 0;
    Check("state 7", (unsigned int)record.packedState, 0x00001207u);

    if (failures) {
        printf("callback_state: FAIL (%d)\n", failures);
        return 1;
    }
    printf("callback_state: OK\n");
    return 0;
}
