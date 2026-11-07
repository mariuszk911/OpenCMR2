/*
 * Little-endian access to file and asset data.
 *
 * Every multi-byte value that comes from or goes to a file must pass through
 * these helpers. Raw bytes are never cast to short, int, float, double or a
 * structure: the helpers copy through memcpy, so unaligned addresses, strict
 * aliasing and padding are not part of the contract. The generated swap
 * metadata in port/disk_layouts.h uses the same primitives.
 *
 * The header is C99 and C++ clean because it is included from both compilation
 * domains (game layout and OS/renderer glue).
 *
 * On a little-endian target ReadLE* and WriteLE* are plain stores and loads;
 * on a big-endian target they reverse the bytes. Swap* always reverses,
 * independent of the target, so callers that need a conditional swap use the
 * OPENCMR2_LITTLE_ENDIAN / OPENCMR2_BIG_ENDIAN identity macros instead.
 */
#ifndef OPENCMR2_PORT_ENDIAN_H
#define OPENCMR2_PORT_ENDIAN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
#error "OpenCMR2: the compiler must define __BYTE_ORDER__ and the byte-order constants"
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define OPENCMR2_LITTLE_ENDIAN 1
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define OPENCMR2_BIG_ENDIAN 1
#else
#error "OpenCMR2: unsupported byte order"
#endif

/*
 * BIGENDIAN is provided by CMake for the Amiga target. Fail the build if it
 * disagrees with the compiler, rather than letting target identity and code
 * generation drift apart.
 */
#if defined(BIGENDIAN) && !defined(OPENCMR2_BIG_ENDIAN)
#error "OpenCMR2: BIGENDIAN is defined but the compiler targets little-endian"
#endif

/*
 * Which branch the scalar and array helpers take. A test translation unit may
 * define OPENCMR2_TEST_FORCE_BIG_ENDIAN before including this header to prove
 * the big-endian branch is wired to Swap*; production builds never define it.
 */
#if defined(OPENCMR2_BIG_ENDIAN) || defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN)
#define OPENCMR2_ENDIAN_NEEDS_SWAP 1
#else
#define OPENCMR2_ENDIAN_NEEDS_SWAP 0
#endif

static inline uint16_t Swap16(uint16_t value)
{
    return (uint16_t)((uint16_t)(value >> 8) | (uint16_t)(value << 8));
}

static inline uint32_t Swap32(uint32_t value)
{
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
}

static inline uint64_t Swap64(uint64_t value)
{
    return ((uint64_t)Swap32((uint32_t)value) << 32) | (uint64_t)Swap32((uint32_t)(value >> 32));
}

/* Reverses the IEEE-754 bit pattern; the value is not a numeric conversion. */
static inline float SwapFloat(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits = Swap32(bits);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/*
 * Big-endian access for formats that are big-endian on disk by design (the
 * Amiga-native saved games). They shift bytes explicitly, so unlike ReadLE*
 * they do not depend on the host byte order or on OPENCMR2_TEST_FORCE_BIG_ENDIAN
 * and give the same result everywhere.
 */
static inline uint32_t ReadBE32(const void *source)
{
    const unsigned char *bytes = (const unsigned char *)source;
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | (uint32_t)bytes[3];
}

static inline void WriteBE32(void *destination, uint32_t value)
{
    unsigned char *bytes = (unsigned char *)destination;
    bytes[0] = (unsigned char)(value >> 24);
    bytes[1] = (unsigned char)(value >> 16);
    bytes[2] = (unsigned char)(value >> 8);
    bytes[3] = (unsigned char)value;
}

static inline uint16_t ReadLE16(const void *source)
{
    uint16_t value;
    memcpy(&value, source, sizeof(value));
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = Swap16(value);
#endif
    return value;
}

static inline uint32_t ReadLE32(const void *source)
{
    uint32_t value;
    memcpy(&value, source, sizeof(value));
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = Swap32(value);
#endif
    return value;
}

static inline float ReadLEFloat(const void *source)
{
    float value;
    memcpy(&value, source, sizeof(value));
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = SwapFloat(value);
#endif
    return value;
}

static inline uint64_t ReadLE64(const void *source)
{
    uint64_t value;
    memcpy(&value, source, sizeof(value));
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = Swap64(value);
#endif
    return value;
}

static inline void WriteLE16(void *destination, uint16_t value)
{
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = Swap16(value);
#endif
    memcpy(destination, &value, sizeof(value));
}

static inline void WriteLE32(void *destination, uint32_t value)
{
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = Swap32(value);
#endif
    memcpy(destination, &value, sizeof(value));
}

static inline void WriteLE64(void *destination, uint64_t value)
{
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = Swap64(value);
#endif
    memcpy(destination, &value, sizeof(value));
}

static inline void WriteLEFloat(void *destination, float value)
{
#if OPENCMR2_ENDIAN_NEEDS_SWAP
    value = SwapFloat(value);
#endif
    memcpy(destination, &value, sizeof(value));
}

/*
 * Whole arrays. The source or destination byte pointer may be unaligned; the
 * host-order array is a normal typed array and must be aligned and large
 * enough for count elements.
 */
static inline void ReadLE16Array(uint16_t *destination, const void *source, size_t count)
{
    const unsigned char *bytes = (const unsigned char *)source;
    size_t i;

    for (i = 0; i < count; ++i)
        destination[i] = ReadLE16(bytes + i * 2u);
}

static inline void ReadLE32Array(uint32_t *destination, const void *source, size_t count)
{
    const unsigned char *bytes = (const unsigned char *)source;
    size_t i;

    for (i = 0; i < count; ++i)
        destination[i] = ReadLE32(bytes + i * 4u);
}

static inline void ReadLEFloatArray(float *destination, const void *source, size_t count)
{
    const unsigned char *bytes = (const unsigned char *)source;
    size_t i;

    for (i = 0; i < count; ++i)
        destination[i] = ReadLEFloat(bytes + i * 4u);
}

static inline void WriteLE16Array(void *destination, const uint16_t *source, size_t count)
{
    unsigned char *bytes = (unsigned char *)destination;
    size_t i;

    for (i = 0; i < count; ++i)
        WriteLE16(bytes + i * 2u, source[i]);
}

static inline void WriteLE32Array(void *destination, const uint32_t *source, size_t count)
{
    unsigned char *bytes = (unsigned char *)destination;
    size_t i;

    for (i = 0; i < count; ++i)
        WriteLE32(bytes + i * 4u, source[i]);
}

static inline void WriteLEFloatArray(void *destination, const float *source, size_t count)
{
    unsigned char *bytes = (unsigned char *)destination;
    size_t i;

    for (i = 0; i < count; ++i)
        WriteLEFloat(bytes + i * 4u, source[i]);
}

#endif
