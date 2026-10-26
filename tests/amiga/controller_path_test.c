/*
 * M1 saved-controller path check. The game builds the path as
 * ".\Configuration\Controller.rcf" and reads a 0x11a4-byte file from the user
 * directory. This test resolves that exact path through the Sys layer and
 * opens it with dos.library, so it does not depend on the game or the logger.
 *
 * The drawer must contain controller-user/Configuration/Controller.rcf with
 * 0x11a4 bytes of 0x5a. Run the binary from its own drawer.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#define CONTROLLER_SIZE 0x11a4

int main(void)
{
    char resolved[512];
    unsigned char head[4];
    const char *gamePath = ".\\Configuration\\Controller.rcf";
    BPTR file;
    LONG size = 0;

    AmigaSys_SetUserDir("PROGDIR:controller-user");
    AmigaSys_SetDataDir("PROGDIR:controller-data");

    if (!Sys_FileExists(gamePath)) {
        printf("controller_path: FAIL exists\n");
        return 1;
    }
    if (!Sys_ResolvePath(gamePath, 0, resolved, sizeof(resolved))) {
        printf("controller_path: FAIL resolve\n");
        return 1;
    }
    if (strcmp(resolved, "PROGDIR:controller-user/Configuration/Controller.rcf") != 0) {
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
