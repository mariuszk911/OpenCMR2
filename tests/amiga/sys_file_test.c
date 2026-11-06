/*
 * Sys files/paths smoke for AmigaOS. All writable state is under RAM:;
 * the test builds its own user/data fixture tree and never opens PROGDIR:.
 *
 * An emulator run is emulator evidence, not PiStorm evidence.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

static char listed[8][64];
static int listedCount;

static int Fail(const char *what)
{
    Sys_Log("sys_file: FAIL %s", what);
    printf("sys_file: FAIL %s\n", what);
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

static int WriteFixture(const char *path, const char *data)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(data);
    if (file == NULL)
        return 0;
    if (fwrite(data, 1, length, file) != length) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

int main(void)
{
    char buffer[16];

    // RAM: is the only safe writable scratch on the target.
    AmigaSys_SetUserDir("RAM:");
    if (!EnsureDir("RAM:cmr2sysfile"))
        return Fail("fixture root");
    if (!EnsureDir("RAM:cmr2sysfile/user"))
        return Fail("fixture user");
    if (!EnsureDir("RAM:cmr2sysfile/data"))
        return Fail("fixture data");
    if (!EnsureDir("RAM:cmr2sysfile/data/FrontEnd"))
        return Fail("fixture frontend");
    if (!EnsureDir("RAM:cmr2sysfile/data/Saves"))
        return Fail("fixture saves");
    if (!WriteFixture("RAM:cmr2sysfile/data/FrontEnd/Menu.TGA", "MENU42"))
        return Fail("fixture menu");
    if (!WriteFixture("RAM:cmr2sysfile/data/Saves/slot1.sav", "DATA"))
        return Fail("fixture slot");
    AmigaSys_SetUserDir("RAM:cmr2sysfile/user");
    AmigaSys_SetDataDir("RAM:cmr2sysfile/data");
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

    // Native Amiga paths bypass the data-directory join; use the native
    // separator here. The relative game path above exercises '\' handling.
    file = Sys_OpenFile("RAM:cmr2sysfile/data/frontend/menu.tga", SYS_FILE_READ);
    if (file == NULL)
        return Fail("open native path");
    if (Sys_ReadFile(file, buffer, sizeof(buffer)) != 6 || memcmp(buffer, "MENU42", 6) != 0)
        return Fail("read native path");
    Sys_CloseFile(file);

    listedCount = 0;
    Sys_ListFiles("FrontEnd", "*", Collect, NULL);
    if (listedCount != 1 || strcmp(listed[0], "Menu.TGA") != 0) {
        printf("sys_file: list count %d\n", listedCount);
        for (int i = 0; i < listedCount; ++i)
            printf("sys_file: list[%d]=%s\n", i, listed[i]);
        printf("sys_file: exists=%d\n", Sys_FileExists("FrontEnd/Menu.TGA"));
        return Fail("list data path");
    }

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

    listedCount = 0;
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
    return 0;
}
