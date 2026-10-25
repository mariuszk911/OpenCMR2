// AmigaOS platform lifecycle: data discovery, user directory and defaults.
//
// There is no SDL here and no ASL picker in v1: --data, paths.data,
// PROGDIR:Data and PROGDIR: are the only data locations.
#include "platform/amiga/amiga_platform.h"
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

namespace {

const char *const kRequiredFiles[] = {
    "Game/Bigfiles/common.bfl",
    "FrontEnd/common.bfl",
    "Sounds/surface.bfl",
};
const char *const kRequiredDirs[] = {
    "CountrySpecific",
    "SetupRep",
};

char s_userDir[256];

void CopyPath(char *destination, size_t size, const char *source)
{
    if (size == 0)
        return;
    if (source == NULL)
        source = "";
    strncpy(destination, source, size - 1);
    destination[size - 1] = '\0';
}

int JoinNative(char *out, size_t size, const char *root, const char *relative)
{
    const size_t rootLen = strlen(root);
    const size_t relativeLen = strlen(relative);
    const int needsSlash = rootLen > 0 && root[rootLen - 1] != ':' && root[rootLen - 1] != '/';

    if (rootLen + (needsSlash ? 1u : 0u) + relativeLen + 1 > size)
        return 0;
    memcpy(out, root, rootLen);
    size_t length = rootLen;
    if (needsSlash)
        out[length++] = '/';
    memcpy(out + length, relative, relativeLen + 1);
    return 1;
}

int HasFile(const char *dir, const char *relative)
{
    char path[512];
    if (!JoinNative(path, sizeof(path), dir, relative))
        return 0;
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock == 0)
        return 0;
    struct FileInfoBlock info OPENCMR2_PORT_ALIGN4;
    const int found = Examine(lock, &info) && info.fib_DirEntryType < 0 && info.fib_Size > 0;
    UnLock(lock);
    return found;
}

int DirHasEntries(const char *dir, const char *relative)
{
    char path[512];
    if (!JoinNative(path, sizeof(path), dir, relative))
        return 0;
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock == 0)
        return 0;
    struct FileInfoBlock info OPENCMR2_PORT_ALIGN4;
    int found = Examine(lock, &info) && info.fib_DirEntryType > 0 && ExNext(lock, &info) != 0;
    UnLock(lock);
    return found;
}

int IsGameDataDir(const char *dir)
{
    for (size_t i = 0; i < sizeof(kRequiredFiles) / sizeof(kRequiredFiles[0]); ++i) {
        if (!HasFile(dir, kRequiredFiles[i]))
            return 0;
    }
    for (size_t i = 0; i < sizeof(kRequiredDirs) / sizeof(kRequiredDirs[0]); ++i) {
        if (!DirHasEntries(dir, kRequiredDirs[i]))
            return 0;
    }
    return 1;
}

int EnsureUserDir(const char *dir)
{
    BPTR lock = Lock((STRPTR)dir, SHARED_LOCK);
    if (lock != 0) {
        UnLock(lock);
        return 1;
    }
    if (!CreateDir((STRPTR)dir)) {
        Sys_Log("user directory: cannot create %s (IoErr %ld)", dir, (long)IoErr());
        return 0;
    }
    return 1;
}

void BuildCommandLine(int argc, char **argv, char *out, size_t size)
{
    out[0] = '\0';
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 || strcmp(argv[i], "--user-dir") == 0) {
            ++i; // skip the option's value
            continue;
        }
        const size_t used = strlen(out);
        const size_t length = strlen(argv[i]);
        if (used + (used != 0 ? 1u : 0u) + length + 1 > size)
            break;
        if (used != 0)
            out[used] = ' ';
        strcpy(out + used + (used != 0 ? 1u : 0u), argv[i]);
    }
}

} // namespace

extern "C" int AmigaPlatform_Init(int argc, char **argv)
{
    const char *dataArgument = NULL;
    const char *userArgument = NULL;

    s_userDir[0] = '\0';
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            dataArgument = argv[++i];
        else if (strcmp(argv[i], "--user-dir") == 0 && i + 1 < argc)
            userArgument = argv[++i];
    }

    if (userArgument != NULL) {
        CopyPath(s_userDir, sizeof(s_userDir), userArgument);
        if (!EnsureUserDir(s_userDir))
            return 0;
    } else {
        CopyPath(s_userDir, sizeof(s_userDir), "PROGDIR:");
    }
    AmigaSys_SetUserDir(s_userDir);

    if (!AmigaSys_EnsureSettingsFile()) {
        Sys_Log("settings: cannot create the default opencmr2.ini");
        return 0;
    }

    const char *dataDir = NULL;
    if (dataArgument != NULL) {
        if (!IsGameDataDir(dataArgument)) {
            Sys_Log("game data: %s is not a complete installation", dataArgument);
            return 0;
        }
        dataDir = dataArgument;
    } else {
        const char *setting = Sys_GetOptionText("paths.data", NULL);
        static const char *const fallbacks[] = { "PROGDIR:Data", "PROGDIR:" };
        if (setting != NULL && IsGameDataDir(setting)) {
            dataDir = setting;
        } else {
            for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
                if (IsGameDataDir(fallbacks[i])) {
                    dataDir = fallbacks[i];
                    break;
                }
            }
        }
        if (dataDir == NULL) {
            Sys_Log("game data: no complete installation found; use --data "
                    "or set paths.data in opencmr2.ini");
            return 0;
        }
    }
    AmigaSys_SetDataDir(dataDir);

    char commandLine[1024];
    BuildCommandLine(argc, argv, commandLine, sizeof(commandLine));
    AmigaSys_SetCommandLine(commandLine);

    Sys_Log("game data: %s", dataDir);
    Sys_Log("user directory: %s", s_userDir);
    return 1;
}

extern "C" void AmigaPlatform_Shutdown(void)
{
    // MiniGL owns the window; its lifetime is closed by the renderer slice.
}

extern "C" const char *AmigaPlatform_GetDataDir(void)
{
    return AmigaSys_GetDataDir();
}

extern "C" const char *AmigaPlatform_GetUserDir(void)
{
    return AmigaSys_GetUserDir();
}

extern "C" int AmigaPlatform_WasSetupCanceled(void)
{
    return 0;
}
