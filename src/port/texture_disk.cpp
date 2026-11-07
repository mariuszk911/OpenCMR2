// .tga/.dds header and C3D texture-record decoders, see port/texture_disk.h.

#include "port/texture_disk.h"

#include "port/types.h"
#include "port/endian.h"

#include "Texture.h"

#include <string.h>

#define TEXTURE_RECORD_HEADER_SIZE 0x10
#define TEXTURE_RECORD_SLOT_SIZE 8
#define TEXTURE_RECORD_MAX_COUNT 0x1000
#define TEXTURE_NAME_SLOT_SIZE 0x104

extern "C" void DdsDisk_Decode(DDSHeader *header)
{
    header->dwFlags = ReadLE32(&header->dwFlags);
    header->dwWidth = ReadLE32(&header->dwWidth);
    header->dwHeight = ReadLE32(&header->dwHeight);
    header->dwLinearSize = ReadLE32(&header->dwLinearSize);
    header->ddpfPixelFormat.dwFourCC = ReadLE32(&header->ddpfPixelFormat.dwFourCC);
    header->ddpfPixelFormat.dwRGBBitCount = ReadLE32(&header->ddpfPixelFormat.dwRGBBitCount);
    header->ddpfPixelFormat.dwRGBAlphaBitMask = ReadLE32(&header->ddpfPixelFormat.dwRGBAlphaBitMask);
}

extern "C" int TextureRecord_Open(const unsigned char *rec, const unsigned char *limit,
                                  unsigned int *count)
{
    *count = 0;
    if (limit != NULL &&
            (rec >= limit || (size_t)(limit - rec) < TEXTURE_RECORD_HEADER_SIZE))
        return TEXTURE_RECORD_OVERRUN;
    *count = ReadLE16(rec + 0xc);
    if (*count > TEXTURE_RECORD_MAX_COUNT ||
            (limit != NULL && (size_t)(limit - rec) <
                 TEXTURE_RECORD_HEADER_SIZE + (size_t)*count * TEXTURE_RECORD_SLOT_SIZE))
        return TEXTURE_RECORD_BAD_COUNT;
    return TEXTURE_RECORD_OK;
}

extern "C" const unsigned char *TextureRecord_Next(const unsigned char *rec, unsigned int count)
{
    return rec + TEXTURE_RECORD_HEADER_SIZE + (size_t)count * TEXTURE_RECORD_SLOT_SIZE;
}

extern "C" int TextureRecord_Name(const unsigned char *rec, unsigned int i, char *names,
                                  unsigned int nameCount, char **name, unsigned int *index)
{
    *name = NULL;
    *index = ReadLE16(rec + 0x14 + i * TEXTURE_RECORD_SLOT_SIZE);
    if (*index >= nameCount)
        return TEXTURE_RECORD_BAD_INDEX;
    if (memchr(names + *index * TEXTURE_NAME_SLOT_SIZE, '\0', TEXTURE_NAME_SLOT_SIZE) == NULL)
        return TEXTURE_RECORD_UNTERMINATED;
    *name = names + *index * TEXTURE_NAME_SLOT_SIZE;
    return TEXTURE_RECORD_OK;
}

extern "C" const char *TextureRecord_StatusText(int status)
{
    switch (status) {
    case TEXTURE_RECORD_OVERRUN:
        return "record table overruns the C3D";
    case TEXTURE_RECORD_BAD_COUNT:
        return "invalid record count";
    case TEXTURE_RECORD_BAD_INDEX:
        return "name index out of range";
    case TEXTURE_RECORD_UNTERMINATED:
        return "unterminated name";
    }
    return "ok";
}
