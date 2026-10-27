#ifndef OPENCMR2_PORT_SAVE_SLOT_H
#define OPENCMR2_PORT_SAVE_SLOT_H

/*
 * The first word of a save slot is a packed little-endian field word:
 *
 *   car bits 0..5, seconds 6..12, bit13, level 14..17, category 18..21,
 *   bits22 22..24, bit25, rest 26..31.
 *
 * The original PC build expressed it as MSVC bitfields. The compiler's
 * bitfield allocation is not portable (68k GCC allocates from the MSB), so
 * game code that touches a slot reads and writes these accessors instead.
 * The accessors work on every host and keep SaveSlot's layout unchanged.
 */
#include <stdint.h>
#include <string.h>

static inline uint32_t SaveSlot_GetWord(const void *slot)
{
    uint32_t word;
    memcpy(&word, slot, sizeof(word));
    return word;
}

static inline void SaveSlot_SetWord(void *slot, uint32_t word)
{
    memcpy(slot, &word, sizeof(word));
}

static inline unsigned SaveSlot_GetCar(const void *slot)
{
    return (unsigned)(SaveSlot_GetWord(slot) & 0x3f);
}

static inline void SaveSlot_SetCar(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~0x3fu) | (value & 0x3fu));
}

static inline unsigned SaveSlot_GetSeconds(const void *slot)
{
    return (unsigned)((SaveSlot_GetWord(slot) >> 6) & 0x7f);
}

static inline void SaveSlot_SetSeconds(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~(0x7fu << 6)) | ((value & 0x7fu) << 6));
}

static inline unsigned SaveSlot_GetBit13(const void *slot)
{
    return (unsigned)((SaveSlot_GetWord(slot) >> 13) & 1u);
}

static inline void SaveSlot_SetBit13(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~(1u << 13)) | ((value & 1u) << 13));
}

static inline unsigned SaveSlot_GetLevel(const void *slot)
{
    return (unsigned)((SaveSlot_GetWord(slot) >> 14) & 0xfu);
}

static inline void SaveSlot_SetLevel(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~(0xfu << 14)) | ((value & 0xfu) << 14));
}

static inline unsigned SaveSlot_GetCategory(const void *slot)
{
    return (unsigned)((SaveSlot_GetWord(slot) >> 18) & 0xfu);
}

static inline void SaveSlot_SetCategory(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~(0xfu << 18)) | ((value & 0xfu) << 18));
}

static inline unsigned SaveSlot_GetBits22(const void *slot)
{
    return (unsigned)((SaveSlot_GetWord(slot) >> 22) & 0x7u);
}

static inline void SaveSlot_SetBits22(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~(0x7u << 22)) | ((value & 0x7u) << 22));
}

static inline unsigned SaveSlot_GetBit25(const void *slot)
{
    return (unsigned)((SaveSlot_GetWord(slot) >> 25) & 1u);
}

static inline void SaveSlot_SetBit25(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~(1u << 25)) | ((value & 1u) << 25));
}

static inline unsigned SaveSlot_GetRest(const void *slot)
{
    return (unsigned)((SaveSlot_GetWord(slot) >> 26) & 0x3fu);
}

static inline void SaveSlot_SetRest(void *slot, unsigned value)
{
    uint32_t word = SaveSlot_GetWord(slot);
    SaveSlot_SetWord(slot, (word & ~(0x3fu << 26)) | ((value & 0x3fu) << 26));
}

#endif
