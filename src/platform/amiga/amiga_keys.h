/*
 * Amiga raw-key codes to DirectInput scan codes (DIK_*) for the game's
 * event model. The mapping covers the navigation and text keys the menus and
 * default bindings use; the input slice completes the rest when the keyboard
 * state is implemented.
 */
#ifndef OPENCMR2_AMIGA_KEYS_H
#define OPENCMR2_AMIGA_KEYS_H

static inline int AmigaKey_ToDIK(int code)
{
    switch (code & 0x7f) {
    case 0x45: return 0x01; /* Escape */
    case 0x44: return 0x1c; /* Return */
    case 0x41: return 0x0e; /* Backspace */
    case 0x42: return 0x0f; /* Tab */
    case 0x40: return 0x39; /* Space */

    case 0x4c: return 0xc8; /* Up */
    case 0x4d: return 0xd0; /* Down */
    case 0x4e: return 0xcd; /* Right */
    case 0x4f: return 0xcb; /* Left */

    case 0x01: return 0x02; /* 1 */
    case 0x02: return 0x03; /* 2 */
    case 0x03: return 0x04; /* 3 */
    case 0x04: return 0x05; /* 4 */
    case 0x05: return 0x06; /* 5 */
    case 0x06: return 0x07; /* 6 */
    case 0x07: return 0x08; /* 7 */
    case 0x08: return 0x09; /* 8 */
    case 0x09: return 0x0a; /* 9 */
    case 0x0a: return 0x0b; /* 0 */

    case 0x10: return 0x10; /* Q */
    case 0x11: return 0x11; /* W */
    case 0x12: return 0x12; /* E */
    case 0x13: return 0x13; /* R */
    case 0x14: return 0x14; /* T */
    case 0x15: return 0x15; /* Y */
    case 0x16: return 0x16; /* U */
    case 0x17: return 0x17; /* I */
    case 0x18: return 0x18; /* O */
    case 0x19: return 0x19; /* P */

    case 0x20: return 0x1e; /* A */
    case 0x21: return 0x1f; /* S */
    case 0x22: return 0x20; /* D */
    case 0x23: return 0x21; /* F */
    case 0x24: return 0x22; /* G */
    case 0x25: return 0x23; /* H */
    case 0x26: return 0x24; /* J */
    case 0x27: return 0x25; /* K */
    case 0x28: return 0x26; /* L */

    case 0x31: return 0x2c; /* Z */
    case 0x32: return 0x2d; /* X */
    case 0x33: return 0x2e; /* C */
    case 0x34: return 0x2f; /* V */
    case 0x35: return 0x30; /* B */
    case 0x36: return 0x31; /* N */
    case 0x37: return 0x32; /* M */

    case 0x50: return 0x3b; /* F1 */
    case 0x51: return 0x3c; /* F2 */
    case 0x52: return 0x3d; /* F3 */
    case 0x53: return 0x3e; /* F4 */
    case 0x54: return 0x3f; /* F5 */
    case 0x55: return 0x40; /* F6 */
    case 0x56: return 0x41; /* F7 */
    case 0x57: return 0x42; /* F8 */
    case 0x58: return 0x43; /* F9 */
    case 0x59: return 0x44; /* F10 */
    case 0x5a: return 0x57; /* F11 */
    case 0x5b: return 0x58; /* F12 */

    case 0x60: return 0x2a; /* Left Shift */
    case 0x61: return 0x36; /* Right Shift */
    case 0x63: return 0x1d; /* Ctrl */
    case 0x64: return 0x38; /* Left Alt */

    default: return 0;
    }
}

#endif
