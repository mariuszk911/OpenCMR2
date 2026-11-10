// Tests for the .pcf member decoder (src/port/font_pcf.cpp). The fixture is
// assembled through WriteLE*, so it can also be compiled together with the
// decoder under OPENCMR2_TEST_FORCE_BIG_ENDIAN to exercise the swap path.
#include "port/types.h"

#include "Font.h"
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

using Bytes = std::vector<unsigned char>;

short Host16(const Bytes &bytes, unsigned int offset)
{
    short value;
    std::memcpy(&value, &bytes[offset], sizeof(value));
    return value;
}

int Host32(const Bytes &bytes, unsigned int offset)
{
    int value;
    std::memcpy(&value, &bytes[offset], sizeof(value));
    return value;
}

Bytes Decode(const Bytes &file, bool *ok)
{
    Bytes native(file.size() + 1);
    *ok = FontPcf_Decode(native.data(), native.size(), file.data(), file.size()) == 1;
    return native;
}

void ExpectRejected(const Bytes &file, const char *what)
{
    bool ok;
    Decode(file, &ok);
    Check(!ok, what);
}

void CheckNative(const Bytes &n)
{
    // The header as upstream's Font_Setup reads it.
    Check(Host32(n, 4) == (int)kGlyphCount, "glyph count");
    Check(Host32(n, 8) == 12 && Host32(n, 12) == 4 && Host32(n, 16) == 1 && Host32(n, 20) == 2,
          "header fields");
    Check(Host16(n, kMapOffset + 'A' * 2) == 0 && Host16(n, kMapOffset + 'B' * 2) == 1 &&
          Host16(n, kMapOffset + 'C' * 2) == 2, "valid char map entries");
    Check(Host16(n, kMapOffset + 'Y' * 2) == -1 && Host16(n, kMapOffset + 'Z' * 2) == -1,
          "invalid char map entries");
    Check(Host16(n, kMapOffset + ' ' * 2) == -1, "missing char");
    // Glyph 0: origin, extent, the two bytes either side of kernCount, kern offset.
    Check(Host16(n, kRecordsOffset + 0) == 1 && Host16(n, kRecordsOffset + 2) == 2, "glyph origin");
    Check(Host16(n, kRecordsOffset + 4) == 5 && Host16(n, kRecordsOffset + 6) == 6, "glyph extent");
    Check(n[kRecordsOffset + 8] == 7 && n[kRecordsOffset + 9] == 2 &&
          n[kRecordsOffset + 10] == 8 && n[kRecordsOffset + 11] == 9, "glyph bytes");
    Check((unsigned int)Host32(n, kRecordsOffset + 12) == kKernOffset, "kern offset");
    // Kerning pairs.
    Check(n[kKernOffset] == 'B' && Host16(n, kKernOffset + 2) == -3, "kern 0");
    Check(n[kKernOffset + 4] == 'C' && Host16(n, kKernOffset + 4 + 2) == 4, "kern 1");
    Check(n[kKernOffset + 8] == 'A' && Host16(n, kKernOffset + 8 + 2) == 1000, "kern 2");
    Check(Host16(n, kRecordsOffset + 2 * kRecordSize + 4) == 9, "glyph 2 width");
}

} // namespace

int main()
{
    bool ok;
    Bytes data = MakePcf();
    Bytes native = Decode(data, &ok);

    Check(ok, "valid pcf rejected");
    CheckNative(native);
    Check(data == MakePcf(), "the member is not modified");

    {   // Glyphs may share a kerning run: it is converted once.
        Bytes bytes = MakePcf();
        PutGlyph(bytes, 2, 20, 21, 9, 10, 2, kKernOffset);
        native = Decode(bytes, &ok);
        Check(ok, "shared kern run rejected");
        Check(Host16(native, kKernOffset + 2) == -3 && Host16(native, kKernOffset + 4 + 2) == 4,
              "shared kern run converted once");
    }

    {
        Bytes bytes = MakePcf();
        Put32(bytes, 4, 0);
        ExpectRejected(bytes, "zero glyph count");
        Put32(bytes, 4, 0x10001);
        ExpectRejected(bytes, "glyph count cap");
        bytes = MakePcf();
        bytes.resize(kKernOffset - 1);
        ExpectRejected(bytes, "truncated glyph records");
        bytes = MakePcf();
        bytes.resize(sizeof(FontHeader));
        ExpectRejected(bytes, "short header");
    }

    {   // A glyph with an invalid kerning run keeps its data and loses only its kerning.
        const char *what[] = { "kern count over the cap", "kern offset inside the records",
                               "kern offset past the buffer", "unaligned kern offset",
                               "junk like the unused last record of hel_20pt.pcf" };
        for (int variant = 0; variant < 5; variant++) {
            Bytes bytes = MakePcf();
            if (variant == 0)
                PutGlyph(bytes, 0, 1, 2, 5, 6, 65, kKernOffset);
            else if (variant == 1)
                PutGlyph(bytes, 0, 1, 2, 5, 6, 2, kRecordsOffset);
            else if (variant == 2)
                PutGlyph(bytes, 0, 1, 2, 5, 6, 2, (unsigned int)bytes.size());
            else if (variant == 3)
                PutGlyph(bytes, 0, 1, 2, 5, 6, 2, kKernOffset + 2);
            else
                PutGlyph(bytes, 0, 1, 2, 5, 6, 205, 0xCDAB1234u);
            native = Decode(bytes, &ok);
            Check(ok, what[variant]);
            Check(native[kRecordsOffset + 9] == 0 && Host32(native, kRecordsOffset + 12) == 0, what[variant]);
            Check(Host16(native, kRecordsOffset + 4) == 5 && Host16(native, kRecordsOffset + 6) == 6,
                  "glyph extent survives a bad kerning run");
            // The other glyphs keep their kerning.
            Check(native[kRecordsOffset + 2 * kRecordSize + 9] == 1 &&
                  (unsigned int)Host32(native, kRecordsOffset + 2 * kRecordSize + 12) == kKernOffset + 2 * 4,
                  "other glyphs keep their kerning");
        }
        Bytes bytes = MakePcf();
        bytes[kRecordsOffset + 9] = 0;
        Check(Decode(bytes, &ok).size() != 0 && ok, "no kerning at all");
    }

    Check(FontPcf_Decode(NULL, 8, data.data(), data.size()) == 0, "NULL destination");
    Check(FontPcf_Decode(native.data(), 8, data.data(), data.size()) == 0, "destination too small");

    std::puts("font_pcf: all checks passed");
    return 0;
}
