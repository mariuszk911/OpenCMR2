#include "GenericFileLoader.h"
#include "FileBuffer.h"
#include "port/endian.h"
#include <string.h>

GenericFile CGenericFileLoader::m_genericFile;
DWORD CGenericFileLoader::m_fileSize;

// FUNCTION: CMR2 0x004a9d70
bool CGenericFileLoader::LoadIntoFileRecord(GenericFile *file, char *fileName)
{
    file->didFileLoad = FALSE;
    if (ReadIntoFileRecord(fileName, file) != FALSE)
        return file->didFileLoad = TRUE;

    return false;
}

// FUNCTION: CMR2 0x004a9c30
bool CGenericFileLoader::ReadIntoFileRecord(char *fileName, GenericFile *param_2)
{
    param_2->buffer = CFileBuffer::GetGenericFileBuffer(fileName, 0);
    if (param_2->buffer != NULL)
    {
        param_2->fileSize = GetGenericFileSize();
        return true;
    }

    param_2->fileSize = 0;
    return false;
}

// FUNCTION: CMR2 0x004aa590
int CGenericFileLoader::GetGenericFileSize(void)
{
    return m_fileSize;
}

// FUNCTION: CMR2 0x004d21a0
GenericFile* CGenericFileLoader::GetGenericFile(void) {
    return &m_genericFile;
}

// Directory entry of an archive file: the directory starts at the offset in
// the file's last four bytes and runs up to them.
struct ArchiveEntry {
    DWORD id;
    int offset;             // 0x4 of the file data from the archive start
    DWORD nameLength;       // 0x8 of the name that follows, padded to 4 bytes
};
static_assert(sizeof(ArchiveEntry) == 12, "BFL directory stride");

// FUNCTION: CMR2 0x004a9c70
void *CGenericFileLoader::FindFileInArchive(GenericFile *pFile, char *name, DWORD *pId)
{
    BYTE *pBuffer;
    BYTE *pEntry;
    BYTE *pDirectoryEnd;
    DWORD directoryOffset;
    DWORD remaining;
    int offset;
    unsigned int nameLen;
    unsigned int pad;
    char entryName[260];

    // PORT: AMIGA: a truncated archive must not read outside the buffer.
    if (pFile == NULL || pFile->buffer == NULL || pFile->fileSize < 4)
        return NULL;
    _strlwr(name);
    pBuffer = (BYTE *)pFile->buffer;
    pDirectoryEnd = pBuffer + pFile->fileSize - 4;
    // PORT: AMIGA: directory and entry fields are little-endian; read without casting.
    directoryOffset = ReadLE32(pDirectoryEnd);
    if (directoryOffset > (DWORD)(pDirectoryEnd - pBuffer))
        return NULL;
    pEntry = pBuffer + directoryOffset;
    while (pEntry < pDirectoryEnd) {
        remaining = (DWORD)(pDirectoryEnd - pEntry);
        if (remaining < sizeof(ArchiveEntry))
            return NULL;
        if (pId != NULL)
            *pId = ReadLE32(pEntry);
        nameLen = ReadLE32(pEntry + 8);
        offset = (int)ReadLE32(pEntry + 4);
        pEntry += sizeof(ArchiveEntry);
        remaining -= sizeof(ArchiveEntry);
        if (nameLen > remaining || nameLen >= sizeof(entryName))
            return NULL;
        memcpy(entryName, pEntry, nameLen);
        entryName[nameLen] = '\0';
        if (strcmp(entryName, name) == 0) {
            if (offset < 0 || (DWORD)offset >= pFile->fileSize)
                return NULL;
            return pBuffer + offset;
        }
        pad = nameLen % 4;
        if (pad != 0)
            pEntry += nameLen - pad + 4;
        else
            pEntry += nameLen;
    }
    return NULL;
}

// FUNCTION: CMR2 0x004a9da0
void CGenericFileLoader::GetFileNameFromPath(char *path, char *out, size_t outSize)
{
    char *pName;
    char *pSlash;
    size_t length;

    pName = strrchr(path, '/');
    if (pName == NULL) {
        pSlash = strrchr(path, '\\');
        if (pSlash == NULL)
            pSlash = path;
        else
            pSlash++;
        pName = pSlash;
    }
    length = strlen(pName);
    if (outSize == 0)
        return;
    if (length >= outSize)
        length = outSize - 1;
    memcpy(out, pName, length);
    out[length] = '\0';
}

// match 75%: below the 90% bar; kept as FUNCTION on purpose so reccmp measures it (see CONVENCIONES)
// FUNCTION: CMR2 0x004a9f90
BYTE *CGenericFileLoader::StrUpperPolish(BYTE *str)
{
    BYTE *p;
    char c;

    c = *str;
    p = str;
    while (c != 0) {
        if ((c >= 'a' && c <= 'z') || (c >= (char)0xE0 && c <= (char)0xFF))
            *p = c - 0x20;
        switch (c) {
        case -0x5d: *p = 0x7e; break;
        case -0x57: *p = 0x24; break;
        case -0x52: *p = 0x2a; break;
        case -0x4f: *p = 0x7c; break;
        case -0x44: *p = 0xf7; break;
        case -0xd: *p = 0xd3; break;
        case -0x43: *p = 0x7b; break;
        case -0x42: *p = 0x7d; break;
        case -0x29: *p = 0x99; break;
        }
        p++;
        c = *p;
    }
    return str;
}

// FUNCTION: CMR2 0x004aa090
char *CGenericFileLoader::StrLowerPolish(char *str)
{
    char *p;
    char c;

    c = *str;
    p = str;
    while (c != '\0') {
        if ((c >= 'A' && c <= 'Z') || (c >= (char)0xC0 && c <= (char)0xDF))
            *p = c + ' ';
        switch (c) {
        case '~': *p = -0x5d; break;
        case '$': *p = -0x57; break;
        case '*': *p = -0x52; break;
        case '|': *p = -0x4f; break;
        case -9: *p = -0x44; break;
        case -0x2d: *p = -0xd; break;
        case '{': *p = -0x43; break;
        case '}': *p = -0x42; break;
        case -0x67: *p = -0x29; break;
        }
        p++;
        c = *p;
    }
    return str;
}

// Looks a path up inside the archive held by pFile; *pFound is set to 1
// when it was found there. bSkipArchive forces a miss.
// FUNCTION: CMR2 0x004a9df0
void *CGenericFileLoader::FindFile(GenericFile *pFile, char *pPath, BYTE *pFound, DWORD *pId, BYTE bSkipArchive)
{
    void *pResult;
    char name[64];

    if (pFound != NULL)
        *pFound = 0;
    if (pFile != NULL && pFile->didFileLoad != 0 && bSkipArchive == 0) {
        GetFileNameFromPath(pPath, name, sizeof(name));
        pResult = FindFileInArchive(pFile, name, pId);
        if (pResult != NULL) {
            if (pFound != NULL)
                *pFound = 1;
            return pResult;
        }
    }
    if (pId != NULL)
        *pId = 0;
    return NULL;
}
