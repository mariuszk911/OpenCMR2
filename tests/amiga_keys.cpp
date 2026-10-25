// Amiga raw-key to DirectInput scan-code mapping.
#include "platform/amiga/amiga_keys.h"

#include <cstdio>

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

    Check(AmigaKey_ToDIK(0x7f), 0, "unmapped key");
    Check(AmigaKey_ToDIK(0xff), 0, "unmapped release key");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("amiga keys: ok\n");
    return 0;
}
