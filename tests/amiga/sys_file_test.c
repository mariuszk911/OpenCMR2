/*
 * M1 Sys files/paths smoke for AmigaOS. Run from a Shell:
 *
 *     DH20:amiga_sys_file_test > DH20:sys_file.log
 *
 * The DH20: drawer must contain sysfile-user/ and sysfile-data/FrontEnd/Menu.TGA.
 * An emulator run is emulator evidence, not PiStorm evidence.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <stdio.h>
#include <string.h>

static char listed[8][64];
static int listedCount;

static int Fail(const char *what)
{
    Sys_Log("sys_file: FAIL %s", what);
    printf("sys_file: FAIL %s\n", what);
    Sys_Exit(1);
    return 1;
}

static int Collect(const char *name, void *context)
{
    (void)context;
    if (listedCount < 8) {
        strncpy(listed[listedCount], name, sizeof(listed[0]) - 1);
        listed[listedCount][sizeof(listed[0]) - 1] = '\0';
        ++listedCount;
    }
    return 1;
}

int main(void)
{
    char buffer[16];

    AmigaSys_SetUserDir("DH20:sysfile-user");
    AmigaSys_SetDataDir("DH20:sysfile-data");
    Sys_Log("sys_file: begin");

    // A data file through a differently-spelled, DOS-style game path.
    SysFile *file = Sys_OpenFile("frontend\\menu.tga", SYS_FILE_READ);
    if (file == NULL)
        return Fail("open data file");
    if (Sys_GetFileSize(file) != 6)
        return Fail("data file size");
    if (Sys_ReadFile(file, buffer, sizeof(buffer)) != 6 || memcmp(buffer, "MENU42", 6) != 0)
        return Fail("read data file");
    Sys_CloseFile(file);

    // Writing creates the parent directory in the user drawer. Create the
    // names in reverse order so the listing has to sort them.
    file = Sys_OpenFile("Saves/zz.sav", SYS_FILE_WRITE);
    if (file == NULL)
        return Fail("create second save");
    if (Sys_WriteFile(file, "ZZ", 2) != 2)
        return Fail("write second save");
    Sys_CloseFile(file);

    file = Sys_OpenFile("Saves\\slot1.sav", SYS_FILE_WRITE);
    if (file == NULL)
        return Fail("create save");
    if (Sys_WriteFile(file, "HELLO", 5) != 5)
        return Fail("write save");
    Sys_FlushFile(file);
    Sys_CloseFile(file);
    if (!Sys_FileExists("saves/SLOT1.SAV"))
        return Fail("case-insensitive exists");

    file = Sys_OpenFile("Saves\\slot1.sav", SYS_FILE_READ);
    if (file == NULL)
        return Fail("reopen save");
    if (Sys_GetFileSize(file) != 5)
        return Fail("save size");
    if (Sys_SeekFile(file, 1, SYS_SEEK_SET) != 1)
        return Fail("seek save");
    if (Sys_ReadFile(file, buffer, sizeof(buffer)) != 4 || memcmp(buffer, "ELLO", 4) != 0)
        return Fail("read after seek");
    Sys_CloseFile(file);

    Sys_ListFiles("Saves", "*.sav", Collect, NULL);
    if (listedCount != 2 || strcmp(listed[0], "slot1.sav") != 0 ||
        strcmp(listed[1], "zz.sav") != 0) {
        Sys_Log("sys_file: list count %d", listedCount);
        for (int i = 0; i < listedCount; ++i)
            Sys_Log("sys_file: list[%d]=%s", i, listed[i]);
        return Fail("list save files");
    }

    if (!Sys_DeleteFile("Saves\\slot1.sav"))
        return Fail("delete save");
    // The user copy is gone, so the data-directory copy is visible again.
    if (!Sys_FileExists("Saves/slot1.sav"))
        return Fail("data fallback after delete");
    file = Sys_OpenFile("Saves\\slot1.sav", SYS_FILE_READ);
    if (file == NULL)
        return Fail("open data fallback");
    if (Sys_ReadFile(file, buffer, sizeof(buffer)) != 4 || memcmp(buffer, "DATA", 4) != 0)
        return Fail("read data fallback");
    Sys_CloseFile(file);
    if (!Sys_DeleteFile("Saves\\zz.sav"))
        return Fail("delete second save");

    if (Sys_OpenFile("missing\\file.bin", SYS_FILE_READ) != NULL)
        return Fail("missing file opened");
    if (strcmp(Sys_GetLastMissingFile(), "missing\\file.bin") != 0)
        return Fail("missing file name");

    Sys_Log("sys_file: OK (listed %d)", listedCount);
    printf("sys_file: OK\n");
    Sys_Exit(0);
    return 1;
}
