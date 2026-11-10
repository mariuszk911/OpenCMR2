#include "port/font_pcf.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

#include <vector>

namespace {

const size_t kHeaderSize = 0x18;
const size_t kMapSize = 256 * 2;
const size_t kRecordSize = 0x10;
const size_t kKernSize = 4;
const unsigned int kMaxGlyphs = 0x10000;
const unsigned int kMaxKernPerGlyph = 64;
const unsigned int kMaxKernTotal = 0x4000;
const size_t kGlyphKernCount = 9;      // BYTE in the record
const size_t kGlyphKernOffset = 0xc;   // DWORD from the start of the member

// The kerning run of a glyph record: aligned, after the glyph records and inside
// the member. A record that is not used can hold junk here (the original game
// never reads it), so an invalid run only loses that glyph's kerning.
bool KernRunValid(const unsigned char *record, size_t recordsEnd, size_t size)
{
    unsigned int kernCount = record[kGlyphKernCount];
    uint32_t kernOffset = ReadLE32(record + kGlyphKernOffset);

    if (kernCount == 0)
        return true;
    return kernCount <= kMaxKernPerGlyph && kernOffset >= recordsEnd && kernOffset % 4 == 0 &&
           kernOffset <= size && (size_t)kernCount * kKernSize <= size - kernOffset;
}

void Store16(unsigned char *p, uint16_t value)
{
    memcpy(p, &value, sizeof(value));
}

void Store32(unsigned char *p, uint32_t value)
{
    memcpy(p, &value, sizeof(value));
}

} // namespace

extern "C" int FontPcf_Decode(void *native, size_t nativeSize, const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)native;
    const unsigned char *records;
    size_t recordsEnd;
    unsigned int count;
    unsigned int totalKern = 0;
    std::vector<unsigned char> kernDone;
    unsigned int i;

    if (native == NULL || file == NULL || nativeSize < size || size < kHeaderSize + kMapSize)
        return 0;
    count = ReadLE32(src + 4);
    if (count == 0 || count > kMaxGlyphs)
        return 0;
    recordsEnd = kHeaderSize + kMapSize + (size_t)count * kRecordSize;
    if (recordsEnd > size)
        return 0;
    records = src + kHeaderSize + kMapSize;
    for (i = 0; i < count; i++) {
        const unsigned char *record = records + (size_t)i * kRecordSize;

        if (!KernRunValid(record, recordsEnd, size))
            continue;                   // dropped below
        totalKern += record[kGlyphKernCount];
        if (totalKern > kMaxKernTotal)
            return 0;
    }

    memmove(dst, src, size);
    for (i = 0; i < kHeaderSize; i += 4)
        Store32(dst + i, ReadLE32(src + i));
    for (i = 0; i < 256; i++) {
        int16_t glyph = (int16_t)ReadLE16(src + kHeaderSize + (size_t)i * 2);
        Store16(dst + kHeaderSize + (size_t)i * 2,
                (uint16_t)((glyph < 0 || (unsigned int)glyph >= count) ? -1 : glyph));
    }
    kernDone.assign(size / kKernSize + 1, 0);
    for (i = 0; i < count; i++) {
        const unsigned char *record = records + (size_t)i * kRecordSize;
        unsigned char *out = dst + kHeaderSize + kMapSize + (size_t)i * kRecordSize;
        bool kernOk = KernRunValid(record, recordsEnd, size);
        unsigned int kernCount = kernOk ? record[kGlyphKernCount] : 0;
        uint32_t kernOffset = kernOk ? ReadLE32(record + kGlyphKernOffset) : 0;
        unsigned int k;

        for (k = 0; k < 8; k += 2)      // origin x, y, width, height
            Store16(out + k, ReadLE16(record + k));
        out[kGlyphKernCount] = (unsigned char)kernCount;
        Store32(out + kGlyphKernOffset, kernOffset);
        for (k = 0; k < kernCount; k++) {
            size_t pair = kernOffset / kKernSize + k;

            if (kernDone[pair])         // glyphs may share a kerning run
                continue;
            kernDone[pair] = 1;
            Store16(dst + pair * kKernSize + 2, ReadLE16(src + pair * kKernSize + 2));
        }
    }
    return 1;
}
