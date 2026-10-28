// .cod co-driver call decoder, see port/cod_disk.h.

#include "port/cod_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

extern "C" int CodDisk_Decode(void *nativeWords, size_t nativeSize,
                              const void *file, size_t size,
                              unsigned int *outCount)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)nativeWords;
    size_t offset;

    if (nativeWords == NULL || file == NULL || size == 0 || size % 4 != 0 ||
            nativeSize < size)
        return 0;
    memmove(dst, src, size);
    for (offset = 0; offset < size; offset += 4) {
        uint32_t value = ReadLE32(src + offset);
        memcpy(dst + offset, &value, sizeof(value));
    }
    if (outCount != NULL)
        *outCount = (unsigned int)(size / 4);
    return 1;
}
