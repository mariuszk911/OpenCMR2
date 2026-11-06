/*
 * Saved-controller path check. The game builds the path as
 * ".\Configuration\Controller.rcf" and reads a 0x11a4-byte file from the user
 * directory. This test resolves that exact path through the Sys layer and
 * opens it with dos.library, so it does not depend on the game or the logger.
 *
 * The fixture is created under RAM:; the test never opens PROGDIR:.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#define CONTROLLER_SIZE 0x11a4

static int EnsureDir(const char *path)
{
    BPTR created = CreateDir((STRPTR)path);
    if (created != 0) {
        UnLock(created);
        return 1;
    }
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock == 0)
        return 0;
    UnLock(lock);
    return 1;
}

static int WriteFixture(const char *path)
{
    unsigned char data[0x11a4];
    FILE *file;

    memset(data, 0x5a, sizeof(data));
    file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    if (fwrite(data, 1, sizeof(data), file) != sizeof(data)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

int main(void)
{
    char resolved[512];
    unsigned char head[4];
    const char *gamePath = ".\\Configuration\\Controller.rcf";
    BPTR file;
    LONG size = 0;

    if (!EnsureDir("RAM:cmr2test-controller") ||
            !EnsureDir("RAM:cmr2test-controller/Configuration") ||
            !WriteFixture("RAM:cmr2test-controller/Configuration/Controller.rcf")) {
        printf("controller_path: FAIL fixture\n");
        return 1;
    }
    AmigaSys_SetUserDir("RAM:cmr2test-controller");
    AmigaSys_SetDataDir("RAM:cmr2test-controller-data");

    if (!Sys_FileExists(gamePath)) {
        printf("controller_path: FAIL exists\n");
        return 1;
    }
    if (!Sys_ResolvePath(gamePath, 0, resolved, sizeof(resolved))) {
        printf("controller_path: FAIL resolve\n");
        return 1;
    }
    if (strcmp(resolved, "RAM:cmr2test-controller/Configuration/Controller.rcf") != 0) {
        printf("controller_path: FAIL path %s\n", resolved);
        return 1;
    }

    file = Open(resolved, MODE_OLDFILE);
    if (file == 0) {
        printf("controller_path: FAIL open\n");
        return 1;
    }
    /* AmigaDOS Seek returns the previous position; the second call is EOF. */
    if (Seek(file, 0, OFFSET_END) < 0 || (size = Seek(file, 0, OFFSET_CURRENT)) != CONTROLLER_SIZE) {
        printf("controller_path: FAIL size %ld\n", (long)size);
        Close(file);
        return 1;
    }
    Seek(file, 0, OFFSET_BEGINNING);
    if (Read(file, head, sizeof(head)) != (LONG)sizeof(head) || head[0] != 0x5a) {
        printf("controller_path: FAIL read\n");
        Close(file);
        return 1;
    }
    Close(file);

    printf("controller_path: OK (%ld bytes)\n", (long)size);
    return 0;
}
