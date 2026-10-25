/* Test-drawer paths without hardcoding a volume or assign name. */
#ifndef OPENCMR2_TEST_PATHS_H
#define OPENCMR2_TEST_PATHS_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <dos/dos.h>
#include <proto/dos.h>

static inline int TestDir(char *out, size_t size)
{
    char name[512];
    BPTR lock = GetProgramDir();
    if (lock == 0)
        return 0;
    const int ok = NameFromLock(lock, name, sizeof(name)) != 0;
    UnLock(lock);
    if (!ok)
        return 0;
    size_t length = strlen(name);
    if (length + 2 > size)
        return 0;
    memcpy(out, name, length);
    if (length > 0 && name[length - 1] != ':' && name[length - 1] != '/')
        out[length++] = '/';
    out[length] = '\0';
    return 1;
}

static inline int TestPath(char *out, size_t size, const char *relative)
{
    char dir[512];
    if (!TestDir(dir, sizeof(dir)))
        return 0;
    if (snprintf(out, size, "%s%s", dir, relative) >= (int)size)
        return 0;
    return 1;
}

#endif
