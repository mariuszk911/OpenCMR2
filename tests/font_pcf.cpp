// Tests for the fork-owned .pcf parser (src/port/font_pcf.cpp). The fixture is
// assembled through WriteLE*, so it can also be compiled together with the
// parser under OPENCMR2_TEST_FORCE_BIG_ENDIAN to exercise the swap path.
#include "port/types.h"

#include "Font.h"
#include "GenericFileLoader.h"
#include "port/endian.h"
#include "port/font_pcf.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const unsigned int kMapOffset = sizeof(FontHeader);
const unsigned int kMapSize = 256 * sizeof(short);
const unsigned int kRecordsOffset = kMapOffset + kMapSize;
const unsigned int kRecordSize = 16;
const unsigned int kGlyphCount = 3;
const unsigned int kKernOffset = kRecordsOffset + kGlyphCount * kRecordSize;

void Check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "font_pcf: %s\n", what);
        std::exit(1);
    }
}

void Resize(std::vector<unsigned char> &bytes, unsigned int size)
{
    if (bytes.size() < size)
        bytes.resize(size);
}

/* Write the on-disk (little-endian) form. WriteLE* stays correct when this
 * test is compiled with OPENCMR2_TEST_FORCE_BIG_ENDIAN on a little-endian
 * host, which is how the big-endian read path is exercised. */
void Put16(std::vector<unsigned char> &bytes, unsigned int offset, unsigned int value)
{
    Resize(bytes, offset + 2);
    WriteLE16(&bytes[offset], (uint16_t)value);
}

void Put32(std::vector<unsigned char> &bytes, unsigned int offset, unsigned int value)
{
    Resize(bytes, offset + 4);
    WriteLE32(&bytes[offset], (uint32_t)value);
}

void PutGlyph(std::vector<unsigned char> &bytes, unsigned int index,
              short x, short y, short width, short height,
              unsigned char kernCount, unsigned int kernOffset)
{
    unsigned int offset = kRecordsOffset + index * kRecordSize;
    Resize(bytes, offset + kRecordSize);
    Put16(bytes, offset + 0, (unsigned short)x);
    Put16(bytes, offset + 2, (unsigned short)y);
    Put16(bytes, offset + 4, (unsigned short)width);
    Put16(bytes, offset + 6, (unsigned short)height);
    bytes[offset + 8] = 7;
    bytes[offset + 9] = kernCount;
    bytes[offset + 10] = 8;
    bytes[offset + 11] = 9;
    Put32(bytes, offset + 12, kernOffset);
}

void PutKern(std::vector<unsigned char> &bytes, unsigned int index, unsigned char ch, short offset)
{
    unsigned int at = kKernOffset + index * 4;
    Resize(bytes, at + 4);
    bytes[at] = ch;
    bytes[at + 1] = 0;
    Put16(bytes, at + 2, (unsigned short)offset);
}

std::vector<unsigned char> MakePcf()
{
    std::vector<unsigned char> bytes(kKernOffset + 3 * 4);

    Put32(bytes, 0, 0);                // header.field_0x0
    Put32(bytes, 4, kGlyphCount);
    Put32(bytes, 8, 12);               // lineHeight
    Put32(bytes, 12, 4);               // spaceWidth
    Put32(bytes, 16, 1);               // charSpacing
    Put32(bytes, 20, 2);               // lineGap
    for (unsigned int i = 0; i < 256; i++)
        Put16(bytes, kMapOffset + i * 2, 0xffffu); // -1 = missing
    Put16(bytes, kMapOffset + 'A' * 2, 0);
    Put16(bytes, kMapOffset + 'B' * 2, 1);
    Put16(bytes, kMapOffset + 'C' * 2, 2);
    Put16(bytes, kMapOffset + 'Y' * 2, 0xfffeu); // -2, invalid
    Put16(bytes, kMapOffset + 'Z' * 2, 7);       // past glyphCount
    PutGlyph(bytes, 0, 1, 2, 5, 6, 2, kKernOffset);
    PutGlyph(bytes, 1, 10, 11, 7, 8, 0, 0);
    PutGlyph(bytes, 2, 20, 21, 9, 10, 1, kKernOffset + 2 * 4);
    PutKern(bytes, 0, 'B', -3);
    PutKern(bytes, 1, 'C', 4);
    PutKern(bytes, 2, 'A', 1000);
    return bytes;
}

void CheckTables(FontSlot &slot)
{
    Check(slot.pHeader != NULL, "header bound");
    Check(slot.pGlyphs != NULL, "glyphs bound");
    Check(slot.pCharMap != NULL, "char map bound");
    Check(slot.pHeader->glyphCount == kGlyphCount, "glyph count");
    Check(slot.pHeader->lineHeight == 12 && slot.pHeader->spaceWidth == 4 &&
          slot.pHeader->charSpacing == 1 && slot.pHeader->lineGap == 2, "header fields");
    Check(slot.pCharMap['A'] == 0 && slot.pCharMap['B'] == 1 && slot.pCharMap['C'] == 2,
          "valid char map entries");
    Check(slot.pCharMap['Y'] == -1 && slot.pCharMap['Z'] == -1, "invalid char map entries");
    Check(slot.pCharMap[' '] == -1, "missing char");
    Check(slot.pGlyphs[0].width == 5 && slot.pGlyphs[0].height == 6, "glyph extent");
    Check(((short *)slot.pGlyphs[0].field_0x0)[0] == 1 &&
          ((short *)slot.pGlyphs[0].field_0x0)[1] == 2, "glyph origin");
    Check(slot.pGlyphs[0].kernCount == 2 && slot.pGlyphs[0].pKern != NULL, "kern count");
    Check(slot.pGlyphs[0].pKern[0].ch == 'B' && slot.pGlyphs[0].pKern[0].offset == -3, "kern 0");
    Check(slot.pGlyphs[0].pKern[1].ch == 'C' && slot.pGlyphs[0].pKern[1].offset == 4, "kern 1");
    Check(slot.pGlyphs[1].kernCount == 0 && slot.pGlyphs[1].pKern == NULL, "empty kern table");
    Check(slot.pGlyphs[2].pKern[0].ch == 'A' && slot.pGlyphs[2].pKern[0].offset == 1000, "kern 2");
}

void CheckOutlineCells(const FontSlot &slot)
{
    OutlineFontGlyph cells[256];
    int count = FontPcf_OutlineCells(&slot, cells);

    Check(count == 3, "outline cell count");
    Check(cells[0].code == 'A' && cells[0].x == 1 && cells[0].y == 2 &&
          cells[0].width == 5 && cells[0].height == 6, "outline cell A");
    Check(cells[1].code == 'B' && cells[1].x == 10 && cells[1].y == 11 &&
          cells[1].width == 7 && cells[1].height == 8, "outline cell B");
    Check(cells[2].code == 'C' && cells[2].x == 20 && cells[2].y == 21 &&
          cells[2].width == 9 && cells[2].height == 10, "outline cell C");
}

void ExpectRejected(const std::vector<unsigned char> &bytes, unsigned int index,
                    DWORD dataSize, const char *what)
{
    FontSlot slot;
    std::memset(&slot, 0, sizeof(slot));
    Check(FontPcf_Setup(&slot, index, bytes.data(), dataSize) == 0, what);
    Check(slot.pGlyphs == NULL, "rejected data leaves no glyph table");
    Check(slot.pHeader != NULL && slot.pHeader->glyphCount == 0, "rejected data leaves idle tables");
}

} // namespace

int main()
{
    std::vector<unsigned char> data = MakePcf();
    FontSlot slot;

    std::memset(&slot, 0, sizeof(slot));
    Check(FontPcf_Setup(&slot, 0, data.data(), (DWORD)data.size()) == 1, "valid pcf rejected");
    CheckTables(slot);
    CheckOutlineCells(slot);

    FontPcf_Release(&slot, 0);
    Check(slot.pGlyphs == NULL, "release clears the glyph table");
    Check(slot.pHeader != NULL && slot.pHeader->glyphCount == 0, "release leaves idle tables");
    Check(FontPcf_Setup(&slot, 0, data.data(), 0) == 1, "unknown extent rejected");
    CheckTables(slot);

    {
        std::vector<unsigned char> changed = MakePcf();
        Put32(changed, 8, 99);
        Check(FontPcf_Setup(&slot, 0, changed.data(), (DWORD)changed.size()) == 1, "reload rejected");
        Check(slot.pHeader->lineHeight == 99, "reload keeps the new header");
    }
    FontPcf_Release(&slot, 0);

    {
        std::vector<unsigned char> bytes = MakePcf();
        ExpectRejected(bytes, 1, (DWORD)(sizeof(FontHeader)), "short header");
        Put32(bytes, 4, 0);
        ExpectRejected(bytes, 1, (DWORD)bytes.size(), "zero glyph count");
        Put32(bytes, 4, 0x10001);
        ExpectRejected(bytes, 1, (DWORD)bytes.size(), "glyph count cap");
        bytes = MakePcf();
        ExpectRejected(bytes, 1, (DWORD)(kKernOffset - 1), "truncated glyph records");
        bytes = MakePcf();
        bytes[kRecordsOffset + 9] = 65;
        ExpectRejected(bytes, 1, (DWORD)bytes.size(), "kern count cap");
        bytes = MakePcf();
        PutGlyph(bytes, 0, 1, 2, 5, 6, 2, kRecordsOffset);
        ExpectRejected(bytes, 1, (DWORD)bytes.size(), "kern offset inside the records");
        bytes = MakePcf();
        PutGlyph(bytes, 0, 1, 2, 5, 6, 2, (unsigned int)bytes.size());
        ExpectRejected(bytes, 1, (DWORD)bytes.size(), "kern offset past the buffer");
    }

    {
        std::vector<unsigned char> bytes = MakePcf();
        GenericFile file;
        file.buffer = bytes.data();
        file.fileSize = (unsigned int)bytes.size();
        file.didFileLoad = TRUE;
        Check(FontPcf_ArchiveExtent(&file, bytes.data(), 1) == (DWORD)bytes.size(), "archive extent");
        Check(FontPcf_ArchiveExtent(&file, bytes.data() + 10, 1) == (DWORD)(bytes.size() - 10),
              "archive extent from an offset");
        Check(FontPcf_ArchiveExtent(&file, bytes.data(), 0) == 0, "extent outside an archive");
        Check(FontPcf_ArchiveExtent(NULL, bytes.data(), 1) == 0, "extent with no file");
    }

    std::puts("font_pcf: all checks passed");
    return 0;
}
