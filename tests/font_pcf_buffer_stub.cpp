// Plain-heap stand-ins for CFileBuffer, so the forced big-endian variant of
// font_pcf_test can compile the parser on its own.
#include "port/types.h"

#include "FileBuffer.h"

#include <stdlib.h>

void *CFileBuffer::AllocateLockedBuffer(size_t size)
{
    return calloc(1, size);
}

void CFileBuffer::FreeGenericFileBuffer(void *buffer)
{
    free(buffer);
}
