/*
 * Atomic saved-game writes for AmigaOS: the staged write, the commit, the
 * repair of an interrupted commit and the listing of the saved-game drawer.
 * All state is under RAM:; PROGDIR: is never opened. An emulator run is
 * emulator evidence, not PiStorm evidence.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#define USER "RAM:cmr2test-savefile/user"
#define DRAWER USER "/gamesave"

static char listed[16][64];
static int listedCount;

static int Fail(const char *what)
{
    Sys_Log("save_file: FAIL %s", what);
    printf("save_file: FAIL %s\n", what);
    AmigaSys_Shutdown(); /* closes the log so RAM: can be cleaned up */
    return 1;
}

static int Collect(const char *name, void *context)
{
    (void)context;
    if (listedCount < 16) {
        strncpy(listed[listedCount], name, sizeof(listed[0]) - 1);
        listed[listedCount][sizeof(listed[0]) - 1] = '\0';
        ++listedCount;
    }
    return 1;
}

static int List(const char *pattern)
{
    listedCount = 0;
    Sys_ListFiles("gamesave", pattern, Collect, NULL);
    return listedCount;
}

static int Listed(const char *name)
{
    for (int i = 0; i < listedCount; ++i) {
        if (strcmp(listed[i], name) == 0)
            return 1;
    }
    return 0;
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

/* Whether a file exists, found by scanning its directory: a file that is open
   for writing refuses a lock, which is exactly the state being checked. */
static int Exists(const char *path)
{
    char dir[256];
    const char *name = strrchr(path, '/');
    struct FileInfoBlock *info;
    BPTR lock;
    int found = 0;

    if (name == NULL || (size_t)(name - path) >= sizeof(dir))
        return 0;
    memcpy(dir, path, (size_t)(name - path));
    dir[name - path] = '\0';
    ++name;
    info = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (info == NULL)
        return 0;
    lock = Lock((STRPTR)dir, SHARED_LOCK);
    if (lock != 0) {
        if (Examine(lock, info)) {
            while (!found && ExNext(lock, info))
                found = strcmp(info->fib_FileName, name) == 0;
        }
        UnLock(lock);
    }
    FreeDosObject(DOS_FIB, info);
    return found;
}

/* The content of a game path through Sys, or "" when it cannot be read. */
static const char *Content(const char *gamePath)
{
    static char text[32];
    SysFile *file = Sys_OpenFile(gamePath, SYS_FILE_READ);
    uint32_t size;

    text[0] = '\0';
    if (file == NULL)
        return text;
    size = Sys_ReadFile(file, text, sizeof(text) - 1);
    text[size] = '\0';
    Sys_CloseFile(file);
    return text;
}

static int Save(const char *gamePath, const char *data)
{
    SysFile *file = Sys_OpenFile(gamePath, SYS_FILE_WRITE);
    uint32_t size = (uint32_t)strlen(data);
    int ok;

    if (file == NULL)
        return 0;
    ok = Sys_WriteFile(file, data, size) == size;
    Sys_CloseFile(file);
    return ok;
}

int main(void)
{
    AmigaSys_SetUserDir("RAM:");
    if (!EnsureDir("RAM:cmr2test-savefile") || !EnsureDir(USER) || !EnsureDir("RAM:cmr2test-savefile/data") ||
        !EnsureDir(DRAWER))
        return Fail("fixture tree");
    AmigaSys_SetUserDir(USER);
    AmigaSys_SetDataDir("RAM:cmr2test-savefile/data");
    Sys_Log("save_file: begin");

    /* A new save is invisible until it is committed, then complete. */
    SysFile *file = Sys_OpenFile(".\\gamesave\\game00000.rcs", SYS_FILE_WRITE);
    if (file == NULL)
        return Fail("open new save");
    if (Sys_WriteFile(file, "FIRST", 5) != 5)
        return Fail("write new save");
    if (Sys_FileExists(".\\gamesave\\game00000.rcs"))
        return Fail("a save exists before its commit");
    if (!Exists(DRAWER "/game00000.rcs.tmp"))
        return Fail("the write goes to a staging file");
    Sys_CloseFile(file);
    if (Exists(DRAWER "/game00000.rcs.tmp"))
        return Fail("the staging file is gone after the commit");
    if (strcmp(Content(".\\gamesave\\game00000.rcs"), "FIRST") != 0)
        return Fail("committed content");

    /* An overwrite keeps the previous content until the commit. */
    file = Sys_OpenFile(".\\gamesave\\game00000.rcs", SYS_FILE_WRITE);
    if (file == NULL)
        return Fail("open overwrite");
    if (Sys_WriteFile(file, "SECOND!", 7) != 7)
        return Fail("write overwrite");
    if (strcmp(Content(".\\gamesave\\game00000.rcs"), "FIRST") != 0)
        return Fail("the previous save survives an unfinished overwrite");
    Sys_CloseFile(file);
    if (strcmp(Content(".\\gamesave\\game00000.rcs"), "SECOND!") != 0)
        return Fail("overwritten content");
    if (Exists(DRAWER "/game00000.rcs.bak") || Exists(DRAWER "/game00000.rcs.tmp"))
        return Fail("an overwrite leaves no backup or staging file");

    /* Repeated cycles on one slot leave exactly that slot. */
    for (int i = 0; i < 100; ++i) {
        char data[16];
        sprintf(data, "CYCLE%03d", i);
        if (!Save(".\\gamesave\\game00001.rcs", data))
            return Fail("cycle save");
        if (strcmp(Content(".\\gamesave\\game00001.rcs"), data) != 0)
            return Fail("cycle content");
    }
    if (List("*") != 2 || !Listed("game00000.rcs") || !Listed("game00001.rcs"))
        return Fail("the drawer holds exactly the two slots after repeated cycles");

    /* The residue of interrupted commits: an unfinished staging file, a
       backup whose live file is missing, and a stale backup. */
    if (!WriteFixture(DRAWER "/game00002.rcs.tmp", "PART") ||
        !WriteFixture(DRAWER "/game00003.rcs.bak", "KEPT") ||
        !WriteFixture(DRAWER "/game00004.rcs", "LIVE") ||
        !WriteFixture(DRAWER "/game00004.rcs.bak", "OLD"))
        return Fail("interrupted-commit fixtures");
    if (List("*.rcs") != 4 || !Listed("game00000.rcs") || !Listed("game00001.rcs") ||
        !Listed("game00003.rcs") || !Listed("game00004.rcs"))
        return Fail("listing after an interrupted commit");
    if (strcmp(Content(".\\gamesave\\game00003.rcs"), "KEPT") != 0)
        return Fail("the backup was restored");
    if (strcmp(Content(".\\gamesave\\game00004.rcs"), "LIVE") != 0)
        return Fail("a stale backup does not replace the live file");
    if (List("*") != 4 || Exists(DRAWER "/game00002.rcs.tmp") || Exists(DRAWER "/game00004.rcs.bak") ||
        Exists(DRAWER "/game00003.rcs.bak"))
        return Fail("the residue was cleaned up");

    /* A name whose staging name cannot be listed is refused, not written. */
    if (Sys_OpenFile(".\\gamesave\\123456789012345678901234.rcs", SYS_FILE_WRITE) != NULL)
        return Fail("a name too long to stage is refused");

    /* Other directories keep the plain create-and-truncate behaviour. */
    file = Sys_OpenFile(".\\pps\\abcd.pps", SYS_FILE_WRITE);
    if (file == NULL)
        return Fail("open plain write");
    if (Sys_WriteFile(file, "PLAIN", 5) != 5)
        return Fail("write plain");
    if (!Exists(USER "/pps/abcd.pps") || Exists(USER "/pps/abcd.pps.tmp"))
        return Fail("a plain write is visible at once and not staged");
    Sys_CloseFile(file);

    Sys_Log("save_file: OK");
    printf("save_file: OK\n");
    AmigaSys_Shutdown();
    return 0;
}
