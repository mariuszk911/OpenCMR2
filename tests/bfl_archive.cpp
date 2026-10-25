// M0.7 BFL directory fixture. The normal build parses a little-endian archive
// on the host. The forced big-endian variant pre-swaps the fixture bytes so the
// forced branch yields the same values, proving every field is decoded as
// little-endian rather than loaded raw.
#include "FileBuffer.h"
#include "GenericFileLoader.h"
#include "port/endian.h"

#include <cstdio>
#include <cstring>

namespace {

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void PutDisk32(unsigned char *p, DWORD value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    // The forced branch swaps on this little-endian host; pre-swap so the
    // parsed value is still the fixture value.
    p[0] = (unsigned char)((value >> 24) & 0xff);
    p[1] = (unsigned char)((value >> 16) & 0xff);
    p[2] = (unsigned char)((value >> 8) & 0xff);
    p[3] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
    p[2] = (unsigned char)((value >> 16) & 0xff);
    p[3] = (unsigned char)((value >> 24) & 0xff);
#endif
}

} // namespace

// The fixture calls only FindFileInArchive; these stubs satisfy the other
// functions in GenericFileLoader.cpp without pulling in the file system.
extern "C" char *Crt_Strlwr(char *s)
{
    for (char *p = s; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + ('a' - 'A'));
    }
    return s;
}

void *CFileBuffer::GetGenericFileBuffer(char *, BOOL)
{
    return NULL;
}

int main()
{
    // data[0..15], then three directory entries, then the directory offset.
    unsigned char buffer[80];
    const unsigned int directoryOffset = 16;
    const unsigned int secondEntry = directoryOffset + 12 + 8;
    const unsigned int thirdEntry = secondEntry + 12 + 12;
    const unsigned int directoryEnd = thirdEntry + 12 + 4;
    std::memset(buffer, 0, sizeof(buffer));
    std::memcpy(buffer, "HELLO!stage01!XY", 16);

    unsigned char *entry = buffer + directoryOffset;
    PutDisk32(entry, 0x11223344);
    PutDisk32(entry + 4, 0);
    PutDisk32(entry + 8, 5);
    std::memcpy(entry + 12, "hello", 5);

    entry = buffer + secondEntry;
    PutDisk32(entry, 0xa1b2c3d4);
    PutDisk32(entry + 4, 6);
    PutDisk32(entry + 8, 11);
    std::memcpy(entry + 12, "stage01.dat", 11);

    // A four-byte name exercises the no-padding path.
    entry = buffer + thirdEntry;
    PutDisk32(entry, 0x55667788);
    PutDisk32(entry + 4, 14);
    PutDisk32(entry + 8, 4);
    std::memcpy(entry + 12, "wxyz", 4);

    PutDisk32(buffer + directoryEnd, directoryOffset);
    Check(directoryEnd + 4 == sizeof(buffer), "fixture size");

    GenericFile file;
    file.buffer = buffer;
    file.fileSize = sizeof(buffer);
    file.didFileLoad = TRUE;

    DWORD id = 0;
    char hello[] = "hello";
    void *found = CGenericFileLoader::FindFileInArchive(&file, hello, &id);
    Check(found == buffer, "first entry returns its data offset");
    Check(id == 0x11223344, "first entry id");
    Check(found != NULL && std::memcmp(found, "HELLO!", 6) == 0, "first entry data");

    char stage[] = "stage01.dat";
    found = CGenericFileLoader::FindFileInArchive(&file, stage, &id);
    Check(found == buffer + 6, "second entry returns its data offset");
    Check(id == 0xa1b2c3d4, "second entry id");
    Check(found != NULL && std::memcmp(found, "stage01!", 8) == 0, "second entry data");

    char wxyz[] = "wxyz";
    found = CGenericFileLoader::FindFileInArchive(&file, wxyz, &id);
    Check(found == buffer + 14, "unpadded entry returns its data offset");
    Check(id == 0x55667788, "unpadded entry id");
    Check(found != NULL && std::memcmp(found, "XY", 2) == 0, "unpadded entry data");

    char upper[] = "STAGE01.DAT";
    found = CGenericFileLoader::FindFileInArchive(&file, upper, NULL);
    Check(found == buffer + 6, "query is lowercased and pId may be null");

    char missing[] = "missing";
    Check(CGenericFileLoader::FindFileInArchive(&file, missing, &id) == NULL,
          "missing name is not found");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("bfl archive: ok\n");
    return 0;
}
