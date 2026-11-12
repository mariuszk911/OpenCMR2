// Amiga raw-key to DirectInput scan-code mapping.
#include "platform/amiga/amiga_keys.h"

#include <cstdio>
#include <cstring>

namespace {

int failures;

void Check(int actual, int expected, const char *what)
{
    if (actual != expected) {
        std::fprintf(stderr, "FAIL: %s (got 0x%x, want 0x%x)\n", what, actual, expected);
        ++failures;
    }
}

} // namespace

int main()
{
    Check(AmigaKey_ToDIK(0x45), 0x01, "Escape");
    Check(AmigaKey_ToDIK(0xc5), 0x01, "Escape release");
    Check(AmigaKey_ToDIK(0x44), 0x1c, "Return");
    Check(AmigaKey_ToDIK(0x41), 0x0e, "Backspace");
    Check(AmigaKey_ToDIK(0x42), 0x0f, "Tab");
    Check(AmigaKey_ToDIK(0x40), 0x39, "Space");

    Check(AmigaKey_ToDIK(0x4c), 0xc8, "Up");
    Check(AmigaKey_ToDIK(0x4d), 0xd0, "Down");
    Check(AmigaKey_ToDIK(0x4e), 0xcd, "Right");
    Check(AmigaKey_ToDIK(0x4f), 0xcb, "Left");

    Check(AmigaKey_ToDIK(0x01), 0x02, "1");
    Check(AmigaKey_ToDIK(0x0a), 0x0b, "0");
    Check(AmigaKey_ToDIK(0x10), 0x10, "Q");
    Check(AmigaKey_ToDIK(0x20), 0x1e, "A");
    Check(AmigaKey_ToDIK(0x31), 0x2c, "Z");
    Check(AmigaKey_ToDIK(0x37), 0x32, "M");

    Check(AmigaKey_ToDIK(0x50), 0x3b, "F1");
    Check(AmigaKey_ToDIK(0x59), 0x44, "F10");
    Check(AmigaKey_ToDIK(0x5a), 0x57, "F11");
    Check(AmigaKey_ToDIK(0x5b), 0x58, "F12");

    Check(AmigaKey_ToDIK(0x60), 0x2a, "Left Shift");
    Check(AmigaKey_ToDIK(0x61), 0x36, "Right Shift");
    Check(AmigaKey_ToDIK(0x63), 0x1d, "Ctrl");
    Check(AmigaKey_ToDIK(0x64), 0x38, "Left Alt");

    Check(AmigaKey_ToDIK(0x1a), 0x1a, "[");
    Check(AmigaKey_ToDIK(0x1b), 0x1b, "]");
    Check(AmigaKey_ToDIK(0x46), 0xd3, "Del");

    // The keypad: player 2's default keys.
    Check(AmigaKey_ToDIK(0x0f), 0x52, "Keypad 0");
    Check(AmigaKey_ToDIK(0x1f), 0x51, "Keypad 3");
    Check(AmigaKey_ToDIK(0x2d), 0x4b, "Keypad 4");
    Check(AmigaKey_ToDIK(0x2f), 0x4d, "Keypad 6");
    Check(AmigaKey_ToDIK(0x3d), 0x47, "Keypad 7");
    Check(AmigaKey_ToDIK(0x3e), 0x48, "Keypad 8");
    Check(AmigaKey_ToDIK(0x1e), 0x50, "Keypad 2");
    Check(AmigaKey_ToDIK(0x3f), 0x49, "Keypad 9");
    Check(AmigaKey_ToDIK(0x43), 0x9c, "Keypad Enter");
    Check(AmigaKey_ToDIK(0x4a), 0x4a, "Keypad -");
    Check(AmigaKey_ToDIK(0x5e), 0x4e, "Keypad +");

    // Each raw key maps to its own DIK code.
    for (int a = 0; a < 0x80; ++a) {
        for (int b = a + 1; b < 0x80; ++b) {
            if (AmigaKey_ToDIK(a) != 0 && AmigaKey_ToDIK(a) == AmigaKey_ToDIK(b)) {
                std::fprintf(stderr, "FAIL: raw keys 0x%x and 0x%x share DIK 0x%x\n", a, b, AmigaKey_ToDIK(a));
                ++failures;
            }
        }
    }

    Check(AmigaKey_ToDIK(0x7f), 0, "unmapped key");
    Check(AmigaKey_ToDIK(0xff), 0, "unmapped release key");

    // Every key the adapter produces has a name; anything else is "".
    for (int code = 0; code < 0x80; ++code) {
        const int dik = AmigaKey_ToDIK(code);
        if (dik != 0 && AmigaKey_DIKName(dik)[0] == '\0') {
            std::fprintf(stderr, "FAIL: raw key 0x%x (DIK 0x%x) has no name\n", code, dik);
            ++failures;
        }
    }
    Check(std::strcmp(AmigaKey_DIKName(0x1c), "Return"), 0, "Return name");
    Check(std::strcmp(AmigaKey_DIKName(0xcb), "Left"), 0, "Left name");
    Check(AmigaKey_DIKName(0)[0], 0, "no key name");
    Check(AmigaKey_DIKName(0x58 + 1)[0], 0, "unmapped DIK name");

    unsigned char keys[256];
    std::memset(keys, 0, sizeof(keys));
    Check(AmigaKey_UpdateState(keys, 0x45), 0x01, "press mapped");
    Check(keys[0x01], 0x80, "escape held");
    Check(AmigaKey_UpdateState(keys, 0x45), 0x01, "repeat press mapped");
    Check(keys[0x01], 0x80, "escape still held");
    Check(AmigaKey_UpdateState(keys, 0xc5), 0x01, "release mapped");
    Check(keys[0x01], 0, "escape released");
    Check(AmigaKey_UpdateState(keys, 0x7f), 0, "press unmapped");
    Check(AmigaKey_UpdateState(keys, 0xff), 0, "release unmapped");
    Check(keys[0x7f], 0, "unmapped key untouched");

    // MapRawKey results: letters are text, cursor/function keys are not.
    unsigned char text[8];
    const unsigned char letter[] = { 'a' };
    Check(AmigaKey_TextFromMapped(letter, 1, text), 1, "letter is text");
    Check(text[0], 'a', "letter kept");
    const unsigned char latin1[] = { 0xe9 };
    Check(AmigaKey_TextFromMapped(latin1, 1, text), 1, "Latin-1 letter is text");
    const unsigned char up[] = { 0x9b, 'A' };
    Check(AmigaKey_TextFromMapped(up, 2, text), 0, "cursor up (CSI A) is not text");
    const unsigned char down[] = { 0x9b, 'B' };
    Check(AmigaKey_TextFromMapped(down, 2, text), 0, "cursor down (CSI B) is not text");
    const unsigned char f1[] = { 0x9b, '0', '~' };
    Check(AmigaKey_TextFromMapped(f1, 3, text), 0, "F1 (CSI 0 ~) is not text");
    // Backspace, Return and Esc reach the game's text entries as on Windows.
    const unsigned char backspace[] = { 0x08 };
    Check(AmigaKey_TextFromMapped(backspace, 1, text), 1, "Backspace is a character");
    Check(text[0], 8, "Backspace is 8");
    const unsigned char ret[] = { 0x0d };
    Check(AmigaKey_TextFromMapped(ret, 1, text), 1, "Return is a character");
    Check(text[0], 13, "Return is 13");
    const unsigned char esc[] = { 0x1b };
    Check(AmigaKey_TextFromMapped(esc, 1, text), 1, "Esc is a character");
    Check(text[0], 27, "Esc is 27");
    const unsigned char escSequence[] = { 0x1b, '[', 'A' };
    Check(AmigaKey_TextFromMapped(escSequence, 3, text), 0, "ESC sequence is not text");
    const unsigned char controls[] = { 0x09, 0x7f, 0x85 };
    Check(AmigaKey_TextFromMapped(controls, 3, text), 0, "other control characters are not text");
    Check(AmigaKey_TextFromMapped(letter, 0, text), 0, "empty result");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("amiga keys: ok\n");
    return 0;
}
