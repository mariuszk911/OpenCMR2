#include "port/font_pcf.h"

#include "FileBuffer.h"
#include "Font.h"
#include "GenericFileLoader.h"
#include "port/endian.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

const unsigned int kMaxSlots = 256;
const unsigned int kMaxGlyphs = 0x10000;
const unsigned int kMaxKernPerGlyph = 64;
const unsigned int kMaxKernTotal = 0x4000;

/* The glyph record is FontGlyphFile; derive its fields from the declared
 * layout so the parser and the on-disk contract cannot drift apart. */
const unsigned int kGlyphRecordSize = sizeof(FontGlyphFile);
const unsigned int kGlyphKernCount = offsetof(FontGlyph, kernCount);
const unsigned int kGlyphKernOffset = offsetof(FontGlyphFile, kernOffset);

/* Native copies per slot. The glyph table is slot->pGlyphs (the game frees it
 * through Font_Release); the header, map and kerning pointer live here. */
struct FontNative {
    FontHeader header;
    short charMap[256];
    FontKernPair *kern;
};

FontHeader s_idleHeader;
short s_idleCharMap[256];
FontNative *s_native[kMaxSlots];

void FreeTables(FontNative *native, FontGlyph *glyphs, FontKernPair *kern)
{
    if (kern != NULL)
        CFileBuffer::FreeGenericFileBuffer(kern);
    if (glyphs != NULL)
        CFileBuffer::FreeGenericFileBuffer(glyphs);
    if (native != NULL)
        CFileBuffer::FreeGenericFileBuffer(native);
}

} // namespace

DWORD FontPcf_ArchiveExtent(GenericFile *file, const void *data, int inArchive)
{
    uintptr_t start;
    uintptr_t bytes;
    uintptr_t end;

    if (inArchive == 0 || file == NULL || file->buffer == NULL || data == NULL)
        return 0;
    start = (uintptr_t)file->buffer;
    bytes = (uintptr_t)data;
    end = start + file->fileSize;
    if (bytes < start || bytes > end)
        return 0;
    return (DWORD)(end - bytes);
}

void FontPcf_Release(FontSlot *slot, unsigned int index)
{
    if (slot == NULL || index >= kMaxSlots)
        return;
    if (slot->pGlyphs != NULL) {
        CFileBuffer::FreeGenericFileBuffer(slot->pGlyphs);
        slot->pGlyphs = NULL;
    }
    if (s_native[index] != NULL) {
        FreeTables(s_native[index], NULL, s_native[index]->kern);
        s_native[index] = NULL;
    }
    slot->pHeader = &s_idleHeader;
    slot->pCharMap = s_idleCharMap;
}

int FontPcf_Setup(FontSlot *slot, unsigned int index, const void *data, DWORD dataSize)
{
    const BYTE *base;
    const BYTE *records;
    FontNative *native = NULL;
    FontGlyph *glyphs = NULL;
    FontKernPair *kern = NULL;
    unsigned int count;
    unsigned int totalKern = 0;
    unsigned int kernCursor = 0;
    unsigned int i;

    if (slot == NULL || data == NULL || index >= kMaxSlots)
        return 0;
    /* Drop a previous load for this slot before touching its pointers. */
    FontPcf_Release(slot, index);
    if (dataSize != 0 && dataSize < sizeof(FontHeader) + 256 * sizeof(short))
        return 0;
    base = (const BYTE *)data;
    count = ReadLE32(base + 4);
    if (count == 0 || count > kMaxGlyphs)
        return 0;
    records = base + sizeof(FontHeader) + 256 * sizeof(short);
    if (dataSize != 0 &&
        (DWORD)(records - base) + count * kGlyphRecordSize > dataSize)
        return 0;
    for (i = 0; i < count; i++) {
        unsigned int kernCount = records[i * kGlyphRecordSize + kGlyphKernCount];
        if (kernCount > kMaxKernPerGlyph)
            return 0;
        totalKern += kernCount;
        if (totalKern > kMaxKernTotal)
            return 0;
    }
    native = (FontNative *)CFileBuffer::AllocateLockedBuffer(sizeof(FontNative));
    glyphs = (FontGlyph *)CFileBuffer::AllocateLockedBuffer(count * sizeof(FontGlyph));
    if (native == NULL || glyphs == NULL) {
        FreeTables(native, glyphs, NULL);
        return 0;
    }
    if (totalKern != 0) {
        kern = (FontKernPair *)CFileBuffer::AllocateLockedBuffer(totalKern * sizeof(FontKernPair));
        if (kern == NULL) {
            FreeTables(native, glyphs, NULL);
            return 0;
        }
    }
    native->header.field_0x0 = (int)ReadLE32(base + 0);
    native->header.glyphCount = count;
    native->header.lineHeight = (int)ReadLE32(base + 8);
    native->header.spaceWidth = (int)ReadLE32(base + 12);
    native->header.charSpacing = (int)ReadLE32(base + 16);
    native->header.lineGap = (int)ReadLE32(base + 20);
    for (i = 0; i < 256; i++) {
        short glyph = (short)ReadLE16(base + sizeof(FontHeader) + i * 2);
        /* The game tests the original -1 sentinel; keep every reachable
         * entry valid or -1 so it never indexes outside the glyph table. */
        native->charMap[i] = (glyph < 0 || (unsigned int)glyph >= count) ? -1 : glyph;
    }
    for (i = 0; i < count; i++) {
        const BYTE *record = records + i * kGlyphRecordSize;
        short x = (short)ReadLE16(record);
        short y = (short)ReadLE16(record + 2);
        unsigned int kernCount = record[kGlyphKernCount];
        unsigned int kernOffset = ReadLE32(record + kGlyphKernOffset);
        FontGlyph *glyph = &glyphs[i];

        memset(glyph, 0, sizeof(*glyph));
        memcpy(glyph->field_0x0, &x, sizeof(x));
        memcpy(glyph->field_0x0 + 2, &y, sizeof(y));
        glyph->width = (short)ReadLE16(record + 4);
        glyph->height = (short)ReadLE16(record + 6);
        glyph->field_0x8 = record[8];
        glyph->kernCount = (BYTE)kernCount;
        glyph->field_0xa[0] = record[10];
        glyph->field_0xa[1] = record[11];
        if (kernCount == 0)
            continue;
        /* Kerning pairs follow the glyph records and must fit the buffer. */
        if (kernOffset < (unsigned int)(records + count * kGlyphRecordSize - base)) {
            FreeTables(native, glyphs, kern);
            return 0;
        }
        if (dataSize != 0 && (kernOffset > dataSize || kernCount * 4 > dataSize - kernOffset)) {
            FreeTables(native, glyphs, kern);
            return 0;
        }
        {
            const BYTE *src = base + kernOffset;
            unsigned int k;
            for (k = 0; k < kernCount; k++) {
                kern[kernCursor + k].ch = src[k * 4];
                kern[kernCursor + k].pad = 0;
                kern[kernCursor + k].offset = (short)ReadLE16(src + k * 4 + 2);
            }
        }
        glyph->pKern = &kern[kernCursor];
        kernCursor += kernCount;
    }
    native->kern = kern;
    s_native[index] = native;
    slot->pHeader = &native->header;
    slot->pCharMap = native->charMap;
    slot->pGlyphs = glyphs;
    return 1;
}
