#ifndef _CALLBACK_STATE_H
#define _CALLBACK_STATE_H

#include <windows.h>

typedef void (*StateUpdateCallback)(struct CallbackStateMachine *, BYTE);
typedef void (*StateRenderCallback)(struct CallbackStateMachine *, BYTE);

// One callback slot: packed transition state and the number of timer steps
// since its last transition (AdvanceCallbackStateTimers resets the timer).
struct CallbackStateRecord
{
    union {
        int packedState;             // 0x00  same storage as bits
        struct {
            // PORT: AMIGA: fields reversed on big-endian so the packed DWORD matches MSVC.
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
            unsigned int reserved : 6;
            unsigned int level : 2;
            unsigned int rule : 8;
            unsigned int value : 8;
            unsigned int state : 8;
#else
            unsigned int state : 8;
            unsigned int value : 8;
            unsigned int rule : 8;
            unsigned int level : 2;
            unsigned int reserved : 6;
#endif
        } bits;
    };
    int elapsedTicks;                // 0x04
};

struct StateCallbackPair
{
    StateUpdateCallback update;
    StateRenderCallback render;
};

// The callback cursor is read as a DWORD and updated through its low byte.
union CallbackIndex {
    BYTE index;
    DWORD packed;
};

struct CallbackStateMachine
{
    BYTE count;                      // 0x00  number of records
    BYTE pad[3];
    CallbackStateRecord *records;     // 0x04  mutable state of each slot
    StateCallbackPair *callbacks;     // 0x08  indexed by the slot's state byte
    unsigned int *rules;             // 0x0c  packed DWORD rules, terminated by 0xffffffff
};

#endif
