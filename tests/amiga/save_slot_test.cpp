/*
 * SaveSlot is read both field-wise (category, car, ...) and as a packed DWORD
 * by the profile code. It must keep the MSVC LSB-first layout on the 68k.
 */
#include "SavedGame.h"
#include "port/save_slot.h"

#include <cstdio>
#include <cstring>

static int failures;

static void Check(int condition, const char *name)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static void Load(SaveSlot *slot, unsigned int raw)
{
    std::memset(slot, 0, sizeof(*slot));
    std::memcpy(slot, &raw, sizeof(raw));
}

int main(void)
{
    SaveSlot slot;

    Check(sizeof(SaveSlot) == 0x30, "SaveSlot size");
    Check(offsetof(SaveSlot, time) == 4, "SaveSlot time offset");

    Load(&slot, 1u << 18);
    Check(SaveSlot_GetCategory(&slot) == 1, "category bits 18..21");
    Check(SaveSlot_GetCar(&slot) == 0, "category does not leak into car");

    Load(&slot, 0x2au);
    Check(SaveSlot_GetCar(&slot) == 0x2a, "car bits 0..5");

    Load(&slot, 0x2au | (2u << 6) | (1u << 13) | (3u << 14) | (1u << 18) |
                  (2u << 22) | (1u << 25) | (7u << 26));
    Check(SaveSlot_GetCar(&slot) == 0x2a, "packed car");
    Check(SaveSlot_GetSeconds(&slot) == 2, "packed seconds");
    Check(SaveSlot_GetBit13(&slot) == 1, "packed bit13");
    Check(SaveSlot_GetLevel(&slot) == 3, "packed level");
    Check(SaveSlot_GetCategory(&slot) == 1, "packed category");
    Check(SaveSlot_GetBits22(&slot) == 2, "packed bits22");
    Check(SaveSlot_GetBit25(&slot) == 1, "packed bit25");
    Check(SaveSlot_GetRest(&slot) == 7, "packed rest");

    if (failures != 0)
        return 1;
    std::printf("save slot: OK\n");
    return 0;
}
