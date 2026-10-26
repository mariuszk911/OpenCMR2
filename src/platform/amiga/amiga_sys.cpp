// System services on AmigaOS: see port/sys.h.
//
// This slice covers the clock, sleep, logging, files and game paths. Settings,
// window/IDCMP and message boxes land with their M1 components, so the game
// cannot boot on this file alone yet.
#include "port/sys.h"
#include "port/sys_steps.h"
#include "platform/amiga/amiga_keys.h"
#include "platform/amiga/amiga_paths.h"
#include "platform/amiga/amiga_settings.h"
#include "platform/amiga/amiga_sys.h"
#include "platform/amiga/amiga_sys_time.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/timer.h>

#include <map>
#include <new>
#include <string>

// proto/timer.h declares these; libnix supplies SysBase and DOSBase.
struct Device *TimerBase;
struct IntuitionBase *IntuitionBase;

// The IDCMP classes the input pump understands (see Sys_PollEvent).
static const ULONG kInputIDCMP = IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS |
                                 IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                                 IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW;

namespace {

struct MsgPort *s_timerPort;
struct timerequest s_timerRequest;
uint32_t s_eclockFrequency;
uint64_t s_eclockStart;
int s_timeState; // 0 = not tried, 1 = ready, -1 = failed to open

BPTR s_logHandle;
int s_logTried;
char s_commandLine[1024];

char s_dataDir[256] = "PROGDIR:";
char s_userDir[256] = "PROGDIR:";
char s_lastMissing[512];

std::map<std::string, std::string> *s_settings;
bool s_settingsLoaded;

const int kMaxListedFiles = 256;
const int kListedNameSize = AMIGA_PATH_PART_MAX;
const size_t kNativePathSize = 640;
bool s_listFull;
bool s_logWriteFailed;
struct Window *s_inputWindow;
struct Window *s_ownedInputWindow;
struct MsgPort *s_inputPort;
struct Screen *s_inputScreen;
unsigned char s_keyState[256];

int EClockNow(uint64_t *value)
{
    struct EClockVal clock;
    if (ReadEClock(&clock) == 0)
        return 0;
    *value = ((uint64_t)clock.ev_hi << 32) | clock.ev_lo;
    return 1;
}

int InitTime(void)
{
    if (s_timeState != 0)
        return s_timeState > 0;
    s_timeState = -1;

    s_timerPort = CreateMsgPort();
    if (s_timerPort == NULL) {
        Sys_Log("timer.device: CreateMsgPort failed");
        return 0;
    }
    memset(&s_timerRequest, 0, sizeof(s_timerRequest));
    s_timerRequest.tr_node.io_Message.mn_ReplyPort = s_timerPort;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&s_timerRequest, 0) != 0) {
        Sys_Log("timer.device: OpenDevice failed (io_Error %ld)",
                (long)s_timerRequest.tr_node.io_Error);
        DeleteMsgPort(s_timerPort);
        s_timerPort = NULL;
        return 0;
    }

    TimerBase = s_timerRequest.tr_node.io_Device;
    struct EClockVal clock;
    s_eclockFrequency = ReadEClock(&clock);
    if (s_eclockFrequency == 0) {
        Sys_Log("timer.device: ReadEClock is not available");
        CloseDevice((struct IORequest *)&s_timerRequest);
        DeleteMsgPort(s_timerPort);
        TimerBase = NULL;
        s_timerPort = NULL;
        return 0;
    }
    s_eclockStart = ((uint64_t)clock.ev_hi << 32) | clock.ev_lo;
    s_timeState = 1;
    return 1;
}

void CloseTime(void)
{
    if (s_timerPort == NULL)
        return;
    CloseDevice((struct IORequest *)&s_timerRequest);
    DeleteMsgPort(s_timerPort);
    s_timerPort = NULL;
    TimerBase = NULL;
    s_timeState = -1;
}

void CopyPath(char *destination, size_t size, const char *source)
{
    if (size == 0)
        return;
    if (source == NULL)
        source = "";
    strncpy(destination, source, size - 1);
    destination[size - 1] = '\0';
}

int RootFile(char *out, size_t size, const char *root, const char *name)
{
    const size_t rootLen = strlen(root);
    const size_t nameLen = strlen(name);
    const bool needsSlash = rootLen > 0 && root[rootLen - 1] != ':' && root[rootLen - 1] != '/';

    if (rootLen + (needsSlash ? 1u : 0u) + nameLen + 1 > size)
        return 0;
    memcpy(out, root, rootLen);
    size_t length = rootLen;
    if (needsSlash)
        out[length++] = '/';
    memcpy(out + length, name, nameLen + 1);
    return 1;
}

void RememberMissing(const char *path)
{
    CopyPath(s_lastMissing, sizeof(s_lastMissing), path);
}

int ResolveUnder(const char *root, const char *gamePath, char *out, size_t size)
{
    AmigaGamePath parts;
    BPTR lock;

    if (!AmigaPath_Split(gamePath, &parts))
        return 0;
    if (!AmigaPath_Join(out, size, root, &parts, 0, parts.count))
        return 0;
    lock = Lock((STRPTR)out, SHARED_LOCK);
    if (lock == 0)
        return 0;
    UnLock(lock);
    return 1;
}

int CreateParentDirs(const AmigaGamePath *parts)
{
    char path[kNativePathSize];

    for (int i = 1; i < parts->count; ++i) {
        if (!AmigaPath_Join(path, sizeof(path), s_userDir, parts, 0, i))
            return 0;
        BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
        if (lock != 0) {
            UnLock(lock);
            continue;
        }
        if (!CreateDir((STRPTR)path)) {
            const LONG error = IoErr();
            lock = Lock((STRPTR)path, SHARED_LOCK);
            if (lock == 0) {
                Sys_Log("CreateDir %s failed (IoErr %ld)", path, (long)error);
                return 0;
            }
            struct FileInfoBlock info OPENCMR2_PORT_ALIGN4;
            const int isDirectory = Examine(lock, &info) && info.fib_DirEntryType > 0;
            UnLock(lock);
            if (!isDirectory) {
                Sys_Log("%s is not a directory", path);
                return 0;
            }
        }
    }
    return 1;
}

int AsciiEqual(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (AmigaPath_Lower(*a) != AmigaPath_Lower(*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

int NameLess(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        const char lowerA = AmigaPath_Lower(*a);
        const char lowerB = AmigaPath_Lower(*b);
        if (lowerA != lowerB)
            return lowerA < lowerB;
        ++a;
        ++b;
    }
    return *a == '\0' && *b != '\0';
}

void AddListedName(char names[][kListedNameSize], int *count, const char *name)
{
    for (int i = 0; i < *count; ++i) {
        if (AsciiEqual(names[i], name))
            return;
    }
    if (*count >= kMaxListedFiles) {
        s_listFull = true;
        return;
    }
    strncpy(names[*count], name, kListedNameSize - 1);
    names[*count][kListedNameSize - 1] = '\0';
    ++*count;
}

void CollectDir(const char *directory, const char *pattern,
                char names[][kListedNameSize], int *count)
{
    struct FileInfoBlock info OPENCMR2_PORT_ALIGN4;
    BPTR lock = Lock((STRPTR)directory, SHARED_LOCK);

    if (lock == 0)
        return;
    if (Examine(lock, &info) && info.fib_DirEntryType > 0) {
        while (ExNext(lock, &info)) {
            if (info.fib_DirEntryType < 0) {
                char name[108];
                unsigned int length = 0;
                while (length < sizeof(name) - 1 && info.fib_FileName[length] != '\0')
                    ++length;
                memcpy(name, info.fib_FileName, length);
                name[length] = '\0';
                if (length >= kListedNameSize) {
                    Sys_Log("ListFiles: name too long: %s", name);
                    continue;
                }
                if (AmigaPath_Match(name, pattern))
                    AddListedName(names, count, name);
            }
        }
        const LONG error = IoErr();
        if (error != ERROR_NO_MORE_ENTRIES)
            Sys_Log("ExNext %s failed (IoErr %ld)", directory, (long)error);
    }
    UnLock(lock);
}

void SortNames(char names[][kListedNameSize], int count)
{
    for (int i = 1; i < count; ++i) {
        char current[kListedNameSize];
        strcpy(current, names[i]);
        int j = i;
        while (j > 0 && NameLess(current, names[j - 1])) {
            strcpy(names[j], names[j - 1]);
            --j;
        }
        strcpy(names[j], current);
    }
}

bool ReadTextFile(const char *path, std::string *contents)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return false;
    }
    rewind(file);
    contents->resize((size_t)size);
    size_t read = 0;
    if (size > 0)
        read = fread(&(*contents)[0], 1, (size_t)size, file);
    const int closed = fclose(file);
    return closed == 0 && read == (size_t)size;
}

bool WriteTextFile(const std::string &path, const std::string &contents)
{
    FILE *file = fopen(path.c_str(), "wb");
    if (file == NULL)
        return false;
    size_t written = 0;
    if (!contents.empty())
        written = fwrite(contents.data(), 1, contents.size(), file);
    const int closed = fclose(file);
    return closed == 0 && written == contents.size();
}

bool SettingsPath(std::string *path)
{
    char native[kNativePathSize];
    if (!RootFile(native, sizeof(native), s_userDir, "opencmr2.ini"))
        return false;
    *path = native;
    return true;
}

void EnsureSettingsLoaded(void)
{
    if (s_settingsLoaded)
        return;
    s_settingsLoaded = true;
    if (s_settings == NULL)
        s_settings = new std::map<std::string, std::string>();
    std::string path;
    std::string contents;
    if (!SettingsPath(&path) || !ReadTextFile(path.c_str(), &contents))
        return;
    AmigaSettings::Parse(contents, *s_settings);
}

const char *GetSettingText(const char *name)
{
    if (name == NULL)
        return NULL;
    EnsureSettingsLoaded();
    const std::map<std::string, std::string>::const_iterator it =
        s_settings->find(AmigaSettings::Lower(name));
    return it != s_settings->end() ? it->second.c_str() : NULL;
}

bool SaveSetting(const std::string &section, const std::string &key, const std::string &value)
{
    std::string path;
    if (!SettingsPath(&path))
        return false;
    std::string contents;
    if (!ReadTextFile(path.c_str(), &contents)) {
        Sys_Log("settings: cannot read %s (IoErr %ld)", path.c_str(), (long)IoErr());
        return false;
    }
    std::string output;
    if (!AmigaSettings::Rewrite(contents, section, key, value, &output)) {
        Sys_Log("settings: value contains a line break");
        return false;
    }
    const std::string temporary = path + ".tmp";
    if (!WriteTextFile(temporary, output)) {
        Sys_Log("settings: cannot write %s (IoErr %ld)", temporary.c_str(), (long)IoErr());
        DeleteFile((STRPTR)temporary.c_str());
        return false;
    }
    if (!Rename((STRPTR)temporary.c_str(), (STRPTR)path.c_str())) {
        const LONG error = IoErr();
        if (error != ERROR_OBJECT_EXISTS) {
            Sys_Log("settings: cannot rename %s (IoErr %ld)", temporary.c_str(), (long)error);
            DeleteFile((STRPTR)temporary.c_str());
            return false;
        }
        if (!DeleteFile((STRPTR)path.c_str())) {
            Sys_Log("settings: cannot replace %s (IoErr %ld)", path.c_str(), (long)IoErr());
            DeleteFile((STRPTR)temporary.c_str());
            return false;
        }
        if (!Rename((STRPTR)temporary.c_str(), (STRPTR)path.c_str())) {
            Sys_Log("settings: cannot rename %s (IoErr %ld)", temporary.c_str(), (long)IoErr());
            DeleteFile((STRPTR)temporary.c_str());
            return false;
        }
    }
    return true;
}

int EnsureIntuition(void)
{
    if (IntuitionBase != NULL)
        return 1;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (IntuitionBase == NULL)
        Sys_Log("intuition.library: OpenLibrary failed");
    return IntuitionBase != NULL;
}

int BuildBodyText(char *body, struct IntuiText *lines, int maxLines)
{
    int count = 0;
    char *line = body;
    while (line != NULL && count < maxLines) {
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        lines[count].FrontPen = 1;
        lines[count].BackPen = 0;
        lines[count].DrawMode = JAM2;
        lines[count].LeftEdge = 0;
        lines[count].TopEdge = (SHORT)(count * 8);
        lines[count].IText = (UBYTE *)line;
        lines[count].NextText = NULL;
        if (count > 0)
            lines[count - 1].NextText = &lines[count];
        ++count;
        line = end != NULL ? end + 1 : NULL;
    }
    return count;
}

} // namespace

struct SysFile {
    BPTR handle;
    char path[kNativePathSize];
};

extern "C" void AmigaSys_SetCommandLine(const char *line)
{
    if (line == NULL)
        line = "";
    strncpy(s_commandLine, line, sizeof(s_commandLine) - 1);
    s_commandLine[sizeof(s_commandLine) - 1] = '\0';
}

extern "C" void AmigaSys_SetDataDir(const char *path)
{
    if (path != NULL && path[0] != '\0')
        CopyPath(s_dataDir, sizeof(s_dataDir), path);
}

extern "C" void AmigaSys_SetUserDir(const char *path)
{
    if (path != NULL && path[0] != '\0') {
        CopyPath(s_userDir, sizeof(s_userDir), path);
        s_settingsLoaded = false;
    }
}

extern "C" const char *AmigaSys_GetDataDir(void)
{
    return s_dataDir;
}

extern "C" const char *AmigaSys_GetUserDir(void)
{
    return s_userDir;
}

extern "C" int AmigaSys_EnsureSettingsFile(void)
{
    std::string path;
    if (!SettingsPath(&path))
        return 0;
    BPTR lock = Lock((STRPTR)path.c_str(), SHARED_LOCK);
    if (lock != 0) {
        UnLock(lock);
        return 1;
    }
    static const char defaults[] =
        "; OpenCMR2 settings\n"
        "\n"
        "[install]\n"
        "language = English\n"
        "sku = EUROPE\n"
        "\n"
        "[video]\n"
        "fullscreen = 1\n"
        "vsync = 1\n";
    if (!WriteTextFile(path, defaults)) {
        Sys_Log("settings: cannot create %s (IoErr %ld)", path.c_str(), (long)IoErr());
        return 0;
    }
    s_settingsLoaded = false;
    return 1;
}

extern "C" uint32_t Sys_GetTicks(void)
{
    uint64_t now;
    if (!InitTime() || !EClockNow(&now))
        return 0;
    // The E-clock is monotonic; the subtraction wraps safely in 64 bits.
    return AmigaSys_Milliseconds(now - s_eclockStart, s_eclockFrequency);
}

extern "C" double Sys_GetTicksPrecise(void)
{
    uint64_t now;
    if (!InitTime() || !EClockNow(&now))
        return 0.0;
    return AmigaSys_MillisecondsPrecise(now - s_eclockStart, s_eclockFrequency);
}

extern "C" unsigned int Sys_GetUnixTime(void)
{
    struct DateStamp stamp;
    DateStamp(&stamp);
    return AmigaSys_UnixSeconds(stamp.ds_Days, stamp.ds_Minute, stamp.ds_Tick);
}

extern "C" int Sys_TakeSteps(SysStepClock *clock, int rate, int max)
{
    return Sys_TakeStepsAt(clock, Sys_GetTicks(), rate, max);
}

// ---- files ------------------------------------------------------------------

int ResolveReading(const char *path, char *out, size_t size)
{
    if (ResolveUnder(s_userDir, path, out, size))
        return 1;
    if (ResolveUnder(s_dataDir, path, out, size))
        return 1;
    return 0;
}

// Native Amiga paths (volume/assign: or /...) come from the game's install
// paths and must bypass the data-directory join. Backslashes are normalized.
int HasSuffix(const char *path, const char *suffix)
{
    const size_t pathLen = strlen(path);
    const size_t suffixLen = strlen(suffix);
    if (pathLen < suffixLen)
        return 0;
    for (size_t i = 0; i < suffixLen; ++i) {
        if (AmigaPath_Lower(path[pathLen - suffixLen + i]) != AmigaPath_Lower(suffix[i]))
            return 0;
    }
    return 1;
}

int IsNativePath(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return 0;
    if (path[0] == '/')
        return 1;
    const char *colon = strchr(path, ':');
    return colon != NULL && colon != path && colon - path > 1;
}

int NormalizeNativePath(const char *path, char *out, size_t size)
{
    const size_t length = strlen(path);
    if (length + 1 > size)
        return 0;
    for (size_t i = 0; i <= length; ++i)
        out[i] = path[i] == '\\' ? '/' : path[i];
    return 1;
}

int CreateNativeParentDirs(char *path)
{
    for (char *p = path + 1; *p != '\0'; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
        if (lock != 0) {
            UnLock(lock);
        } else if (!CreateDir((STRPTR)path)) {
            const LONG error = IoErr();
            lock = Lock((STRPTR)path, SHARED_LOCK);
            if (lock == 0) {
                *p = '/';
                Sys_Log("CreateDir %s failed (IoErr %ld)", path, (long)error);
                return 0;
            }
            UnLock(lock);
        }
        *p = '/';
    }
    return 1;
}

extern "C" int Sys_ResolvePath(const char *path, int forWriting, char *out, size_t outSize)
{
    AmigaGamePath parts;

    if (path == NULL || out == NULL || outSize == 0)
        return 0;
    char native[kNativePathSize];
    if (IsNativePath(path)) {
        if (!NormalizeNativePath(path, native, sizeof(native))) {
            if (!forWriting)
                RememberMissing(path);
            return 0;
        }
        if (forWriting) {
            if (!CreateNativeParentDirs(native))
                return 0;
            CopyPath(out, outSize, native);
            return 1;
        }
        BPTR lock = Lock((STRPTR)native, SHARED_LOCK);
        if (lock == 0) {
            RememberMissing(path);
            return 0;
        }
        UnLock(lock);
        CopyPath(out, outSize, native);
        if (HasSuffix(path, ".bfl"))
            Sys_Log("file: archive %s", native);
        return 1;
    }
    if (!AmigaPath_Split(path, &parts)) {
        if (!forWriting)
            RememberMissing(path);
        return 0;
    }
    if (forWriting) {
        if (!AmigaPath_Join(out, outSize, s_userDir, &parts, 0, parts.count))
            return 0;
        return CreateParentDirs(&parts);
    }
    if (ResolveReading(path, out, outSize)) {
        if (HasSuffix(path, ".bfl"))
            Sys_Log("file: archive %s", out);
        return 1;
    }
    Sys_Log("file: not found: %s", path);
    RememberMissing(path);
    return 0;
}

extern "C" const char *Sys_GetLastMissingFile(void)
{
    return s_lastMissing;
}

extern "C" SysFile *Sys_OpenFile(const char *path, int mode)
{
    char native[kNativePathSize];
    const int writing = mode == SYS_FILE_WRITE;

    if (!Sys_ResolvePath(path, writing, native, sizeof(native)))
        return NULL;
    BPTR handle = Open((STRPTR)native, writing ? MODE_NEWFILE : MODE_OLDFILE);
    if (handle == 0) {
        if (!writing)
            RememberMissing(path);
        Sys_Log("Open %s failed (IoErr %ld)", native, (long)IoErr());
        return NULL;
    }
    SysFile *file = new (std::nothrow) SysFile;
    if (file == NULL) {
        Close(handle);
        Sys_Log("Sys_OpenFile: out of memory");
        return NULL;
    }
    file->handle = handle;
    CopyPath(file->path, sizeof(file->path), native);
    return file;
}

extern "C" void Sys_CloseFile(SysFile *file)
{
    if (file == NULL)
        return;
    if (!Close(file->handle))
        Sys_Log("Close %s failed (IoErr %ld)", file->path, (long)IoErr());
    delete file;
}

extern "C" uint32_t Sys_ReadFile(SysFile *file, void *buffer, uint32_t size)
{
    if (file == NULL || (buffer == NULL && size != 0))
        return 0;
    const LONG read = Read(file->handle, buffer, (LONG)size);
    if (read < 0) {
        Sys_Log("Read %s failed (IoErr %ld)", file->path, (long)IoErr());
        return 0;
    }
    return (uint32_t)read;
}

extern "C" uint32_t Sys_WriteFile(SysFile *file, const void *buffer, uint32_t size)
{
    if (file == NULL || (buffer == NULL && size != 0))
        return 0;
    const LONG written = Write(file->handle, (APTR)buffer, (LONG)size);
    if (written < 0) {
        Sys_Log("Write %s failed (IoErr %ld)", file->path, (long)IoErr());
        return 0;
    }
    return (uint32_t)written;
}

extern "C" uint32_t Sys_SeekFile(SysFile *file, int32_t offset, int origin)
{
    LONG whence;
    if (file == NULL)
        return 0xffffffffu;
    switch (origin) {
    case SYS_SEEK_SET: whence = OFFSET_BEGINNING; break;
    case SYS_SEEK_CUR: whence = OFFSET_CURRENT; break;
    case SYS_SEEK_END: whence = OFFSET_END; break;
    default: return 0xffffffffu;
    }
    // AmigaDOS returns the position before the seek; ask again for the new one.
    if (Seek(file->handle, (LONG)offset, whence) < 0)
        return 0xffffffffu;
    const LONG position = Seek(file->handle, 0, OFFSET_CURRENT);
    return position < 0 ? 0xffffffffu : (uint32_t)position;
}

extern "C" uint32_t Sys_GetFileSize(SysFile *file)
{
    if (file == NULL)
        return 0xffffffffu;
    const LONG position = Seek(file->handle, 0, OFFSET_CURRENT);
    if (position < 0)
        return 0xffffffffu;
    if (Seek(file->handle, 0, OFFSET_END) < 0)
        return 0xffffffffu;
    const LONG size = Seek(file->handle, 0, OFFSET_CURRENT);
    if (Seek(file->handle, position, OFFSET_BEGINNING) < 0)
        return 0xffffffffu;
    return size < 0 ? 0xffffffffu : (uint32_t)size;
}

extern "C" void Sys_FlushFile(SysFile *file)
{
    if (file == NULL)
        return;
    if (!Flush(file->handle))
        Sys_Log("Flush %s failed (IoErr %ld)", file->path, (long)IoErr());
}

extern "C" int Sys_FileExists(const char *path)
{
    char native[kNativePathSize];
    return path != NULL && ResolveReading(path, native, sizeof(native));
}

extern "C" int Sys_DeleteFile(const char *path)
{
    AmigaGamePath parts;
    char native[kNativePathSize];

    if (path == NULL)
        return 0;
    if (IsNativePath(path)) {
        if (!NormalizeNativePath(path, native, sizeof(native)))
            return 0;
        return DeleteFile((STRPTR)native) ? 1 : 0;
    }
    if (!AmigaPath_Split(path, &parts))
        return 0;
    if (!AmigaPath_Join(native, sizeof(native), s_userDir, &parts, 0, parts.count))
        return 0;
    if (DeleteFile((STRPTR)native))
        return 1;
    const LONG error = IoErr();
    if (error != ERROR_OBJECT_NOT_FOUND)
        Sys_Log("DeleteFile %s failed (IoErr %ld)", native, (long)error);
    return 0;
}

extern "C" void Sys_ListFiles(const char *directory, const char *pattern,
                              SysListCallback callback, void *context)
{
    // The game is single-threaded; a static buffer keeps 8 KB off the stack.
    static char names[kMaxListedFiles][kListedNameSize];
    char rootPath[kNativePathSize];
    AmigaGamePath parts;
    const char *match = pattern != NULL ? pattern : "*";
    int count = 0;
    int hasParts = 0;

    if (callback == NULL)
        return;
    s_listFull = false;
    if (directory != NULL && IsNativePath(directory)) {
        char native[kNativePathSize];
        if (!NormalizeNativePath(directory, native, sizeof(native)))
            return;
        CollectDir(native, match, names, &count);
        SortNames(names, count);
        for (int i = 0; i < count; ++i) {
            if (!callback(names[i], context))
                break;
        }
        return;
    }
    if (directory != NULL && directory[0] != '\0') {
        if (!AmigaPath_Split(directory, &parts))
            return;
        hasParts = 1;
    }
    // The user directory wins when both contain the same name.
    for (int pass = 0; pass < 2; ++pass) {
        const char *root = pass == 0 ? s_userDir : s_dataDir;
        if (hasParts) {
            if (!AmigaPath_Join(rootPath, sizeof(rootPath), root, &parts, 0, parts.count))
                continue;
        } else {
            CopyPath(rootPath, sizeof(rootPath), root);
        }
        CollectDir(rootPath, match, names, &count);
    }
    if (s_listFull)
        Sys_Log("ListFiles: %s reached the %d-entry limit", directory != NULL ? directory : "", kMaxListedFiles);
    SortNames(names, count);
    for (int i = 0; i < count; ++i) {
        if (!callback(names[i], context))
            break;
    }
}

extern "C" const char *Sys_GetInstallSetting(const char *name)
{
    const std::string key = AmigaSettings::Lower(name != NULL ? name : "");
    if (key == "game_hdpath" || key == "game_cdpath")
        return ".";
    if (key == "install_version")
        return "Full";
    if (key == "language") {
        const char *value = GetSettingText("install.language");
        return value != NULL ? value : "English";
    }
    if (key == "sku_type") {
        const char *value = GetSettingText("install.sku");
        return value != NULL ? value : "EUROPE";
    }
    return "";
}

extern "C" int Sys_GetOption(const char *name, int fallback)
{
    const char *value = GetSettingText(name);
    return value != NULL ? (int)strtol(value, NULL, 0) : fallback;
}

extern "C" const char *Sys_GetOptionText(const char *name, const char *fallback)
{
    const char *value = GetSettingText(name);
    return value != NULL ? value : fallback;
}

extern "C" int Sys_SetOption(const char *name, int value)
{
    if (name == NULL)
        return 0;
    const std::string key = AmigaSettings::Lower(name);
    const size_t dot = key.find('.');
    if (dot == std::string::npos)
        return 0;
    char text[32];
    snprintf(text, sizeof(text), "%d", value);
    EnsureSettingsLoaded();
    const bool saved = SaveSetting(key.substr(0, dot), key.substr(dot + 1), text);
    (*s_settings)[key] = text;
    return saved ? 1 : 0;
}

extern "C" void Sys_Sleep(uint32_t ms)
{
    if (ms == 0 || !InitTime())
        return;
    s_timerRequest.tr_node.io_Command = TR_ADDREQUEST;
    s_timerRequest.tr_time.tv_secs = ms / 1000u;
    s_timerRequest.tr_time.tv_micro = (ms % 1000u) * 1000u;
    if (DoIO((struct IORequest *)&s_timerRequest) != 0)
        Sys_Log("timer.device: TR_ADDREQUEST failed (io_Error %ld)",
                (long)s_timerRequest.tr_node.io_Error);
}

// Releases everything AmigaSys_SetWindow owns and detaches our port from a
// window we did not create. One teardown path for window changes and exit.
static void CloseInputState(void)
{
    if (s_ownedInputWindow != NULL) {
        CloseWindow(s_ownedInputWindow);
        s_ownedInputWindow = NULL;
        s_inputWindow = NULL;
    }
    if (s_inputScreen != NULL) {
        UnlockPubScreen(NULL, s_inputScreen);
        s_inputScreen = NULL;
    }
    if (s_inputPort != NULL) {
        if (s_inputWindow != NULL && s_inputWindow->UserPort == s_inputPort) {
            ModifyIDCMP(s_inputWindow, 0);
            s_inputWindow->UserPort = NULL;
        }
        DeleteMsgPort(s_inputPort);
        s_inputPort = NULL;
    }
    s_inputWindow = NULL;
}

// Windowed mode, after reVC: the application owns the visible window, so the
// same window carries the close gadget, receives focus and owns the IDCMP port
// the game polls. MiniGL renders into it with mglCreateContextFromWindow; the
// render size is the window's inner area.
extern "C" void *AmigaSys_CreateGameWindow(int width, int height, const char *title)
{
    struct Screen *screen;
    struct Window *window;
    int left;
    int top;

    if (width <= 0 || height <= 0 || !EnsureIntuition())
        return NULL;
    CloseInputState();
    screen = LockPubScreen(NULL);
    if (screen == NULL) {
        Sys_Log("input: LockPubScreen failed");
        return NULL;
    }
    left = ((int)screen->Width - width) / 2;
    top = ((int)screen->Height - height) / 2;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    {
        struct TagItem windowTags[] = {
            { WA_PubScreen, (ULONG)screen },
            { WA_Left, (ULONG)left },
            { WA_Top, (ULONG)top },
            { WA_InnerWidth, (ULONG)width },
            { WA_InnerHeight, (ULONG)height },
            { WA_AutoAdjust, TRUE },
            { WA_Title, (ULONG)(title != NULL ? title : "OpenCMR2") },
            { WA_DragBar, TRUE },
            { WA_DepthGadget, TRUE },
            { WA_CloseGadget, TRUE },
            { WA_Activate, TRUE },
            { WA_ReportMouse, TRUE },
            { WA_IDCMP, kInputIDCMP },
            { WA_Flags, WFLG_ACTIVATE | WFLG_REPORTMOUSE | WFLG_RMBTRAP },
            { TAG_DONE, 0 }
        };
        window = OpenWindowTagList(NULL, windowTags);
    }
    if (window == NULL) {
        Sys_Log("input: OpenWindowTagList failed");
        UnlockPubScreen(NULL, screen);
        return NULL;
    }
    s_ownedInputWindow = window;
    s_inputScreen = screen;
    s_inputWindow = window;
    WindowToFront(window);
    ActivateWindow(window);
    Sys_Log("input: game window %lx client %dx%d at %d,%d",
            (unsigned long)window,
            (int)(window->Width - window->BorderLeft - window->BorderRight),
            (int)(window->Height - window->BorderTop - window->BorderBottom),
            (int)window->LeftEdge, (int)window->TopEdge);
    return window;
}

extern "C" void AmigaSys_SetWindow(void *window)
{
    struct Window *base = (struct Window *)window;

    CloseInputState();
    if (base != NULL && base->UserPort != NULL) {
        // PORT: AMIGA: a window that already owns an IDCMP port is the one the
        // events arrive on (windowed MiniGL, test harnesses). Adopt it rather
        // than replacing its port. Only touch IDCMP on a real Intuition
        // window; the test harness passes a bare fake Window.
        if (base->WScreen != NULL)
            ModifyIDCMP(base, base->IDCMPFlags | kInputIDCMP);
        s_inputWindow = base;
        return;
    }
    // PORT: AMIGA: follow the working reVC input path. MiniGL's fullscreen
    // backdrop has no UserPort here, so open our own borderless, activated
    // IDCMP window on the same screen instead of borrowing the render window.
    if (base != NULL && base->WScreen != NULL) {
        struct TagItem tags[] = {
            { WA_CustomScreen, (ULONG)base->WScreen },
            { WA_Left, 0 },
            { WA_Top, 0 },
            { WA_Width, 1 },
            { WA_Height, 1 },
            { WA_Flags, WFLG_ACTIVATE | WFLG_BORDERLESS | WFLG_BACKDROP | WFLG_REPORTMOUSE },
            { WA_IDCMP, kInputIDCMP },
            { WA_ReportMouse, TRUE },
            { TAG_DONE, 0 }
        };
        s_ownedInputWindow = OpenWindowTagList(NULL, tags);
        if (s_ownedInputWindow != NULL) {
            s_inputWindow = s_ownedInputWindow;
            WindowToFront(s_ownedInputWindow);
            ActivateWindow(s_ownedInputWindow);
        }
    }
    // Fallback: put a port on MiniGL's window if opening our own failed.
    if (s_inputWindow == NULL && base != NULL && base->UserPort == NULL) {
        s_inputPort = CreateMsgPort();
        if (s_inputPort != NULL) {
            base->UserPort = s_inputPort;
            ModifyIDCMP(base, kInputIDCMP);
            s_inputWindow = base;
            WindowToFront(base);
            ActivateWindow(base);
        }
    }
}

// Consumes every queued IDCMP message, keeping the polled input state current.
// Returns 1 with the first event the game understands. Releases, mouse
// messages and unmapped keys only update state and are not events.
static int PollEvent(SysEvent *event, int *consumed)
{
    struct IntuiMessage *message;

    if (consumed != NULL)
        *consumed = 0;
    if (event == NULL || s_inputWindow == NULL || s_inputWindow->UserPort == NULL)
        return 0;
    while ((message = (struct IntuiMessage *)GetMsg(s_inputWindow->UserPort)) != NULL) {
        const ULONG messageClass = message->Class;
        int handled = 0;

        if (consumed != NULL)
            *consumed = 1;
        if (messageClass == IDCMP_CLOSEWINDOW) {
            Sys_Log("input: close window");
            event->type = SYS_EVENT_QUIT;
            event->key = 0;
            event->character = 0;
            event->repeat = 0;
            handled = 1;
        } else if (messageClass == IDCMP_ACTIVEWINDOW) {
            // Releases may have been delivered while inactive; start clean.
            Sys_Log("input: active window");
            memset(s_keyState, 0, sizeof(s_keyState));
            event->type = SYS_EVENT_FOCUS_GAINED;
            event->key = 0;
            event->character = 0;
            event->repeat = 0;
            handled = 1;
        } else if (messageClass == IDCMP_INACTIVEWINDOW) {
            Sys_Log("input: inactive window");
            memset(s_keyState, 0, sizeof(s_keyState));
            event->type = SYS_EVENT_FOCUS_LOST;
            event->key = 0;
            event->character = 0;
            event->repeat = 0;
            handled = 1;
        } else if (messageClass == IDCMP_REFRESHWINDOW) {
            BeginRefresh(s_inputWindow);
            EndRefresh(s_inputWindow, TRUE);
        } else if (messageClass == IDCMP_RAWKEY) {
            // Releases are not events: the game polls the held-key state.
            const int dik = AmigaKey_UpdateState(s_keyState, message->Code);
            if (dik != 0 && (message->Qualifier & IEQUALIFIER_REPEAT) == 0)
                Sys_Log("input: rawkey %02lx %s dik %d",
                        (unsigned long)(message->Code & 0x7f),
                        (message->Code & 0x80) != 0 ? "up" : "down", dik);
            if (dik != 0 && (message->Code & 0x80) == 0) {
                event->type = SYS_EVENT_KEY_DOWN;
                event->key = dik;
                event->character = 0;
                event->repeat = (message->Qualifier & IEQUALIFIER_REPEAT) != 0;
                handled = 1;
            }
        } else if (messageClass == IDCMP_VANILLAKEY) {
            event->type = SYS_EVENT_TEXT;
            event->key = 0;
            event->character = message->Code;
            event->repeat = 0;
            handled = 1;
        }
        ReplyMsg((struct Message *)message);
        if (handled)
            return 1;
    }
    return 0;
}

extern "C" int Sys_PollEvent(SysEvent *event)
{
    return PollEvent(event, NULL);
}

extern "C" void AmigaSys_GetKeyState(unsigned char keys[256])
{
    if (keys != NULL)
        memcpy(keys, s_keyState, sizeof(s_keyState));
}

extern "C" int AmigaSys_IsKeyDown(int dik)
{
    return dik >= 0 && dik < 256 && (s_keyState[dik] & 0x80) != 0;
}

extern "C" int Sys_WaitEvent(SysEvent *event)
{
    if (event == NULL)
        return 0;
    if (s_inputWindow == NULL || s_inputWindow->UserPort == NULL) {
        // No MiniGL window yet: idle instead of spinning the event loop.
        Sys_Sleep(20);
        return 0;
    }
    int consumed = 0;
    if (PollEvent(event, &consumed))
        return 1;
    // A batch that only carried releases or unmapped keys still has to let the
    // frame run, otherwise holding a key cannot be observed by the game.
    if (consumed)
        return 0;
    // PORT: AMIGA: WaitPort blocks the only render loop while the window is
    // active, which freezes the frontend matrix/animations. Poll and sleep one
    // frame instead; input latency stays under one frame.
    Sys_Sleep(16);
    return Sys_PollEvent(event);
}

extern "C" int Sys_CreateWindow(const char *title, int fullscreen)
{
    // MiniGL owns the window and screen; the renderer creates them when it
    // initializes the context. The game only needs to know it may continue.
    (void)title;
    (void)fullscreen;
    return 1;
}

extern "C" void Sys_DestroyWindow(void)
{
    // Do not close a window owned by MiniGL; just stop reading its port.
    s_inputWindow = NULL;
}

extern "C" void Sys_ShowCursor(int show)
{
    // The MiniGL/input slice owns the pointer; hiding it here would need the
    // window's pointer sprite and is deferred with mouse capture.
    (void)show;
}

extern "C" void Sys_GetDesktopSize(int *width, int *height)
{
    if (width == NULL || height == NULL)
        return;
    if (!EnsureIntuition()) {
        *width = 640;
        *height = 480;
        return;
    }
    struct Screen *screen = LockPubScreen(NULL);
    if (screen == NULL) {
        Sys_Log("intuition: no default public screen");
        *width = 640;
        *height = 480;
        return;
    }
    *width = screen->Width;
    *height = screen->Height;
    UnlockPubScreen(NULL, screen);
}

extern "C" int Sys_MessageBox(const char *title, const char *text, int buttons)
{
    char body[kNativePathSize];
    struct IntuiText lines[16];
    struct IntuiText positive;
    struct IntuiText negative;

    if (title == NULL)
        title = "OpenCMR2";
    if (text == NULL)
        text = "";
    if (!EnsureIntuition())
        return buttons == SYS_MESSAGEBOX_RETRY_CANCEL ? SYS_MESSAGEBOX_RESULT_CANCEL
                                                       : SYS_MESSAGEBOX_RESULT_OK;
    CopyPath(body, sizeof(body), text);
    const int lineCount = BuildBodyText(body, lines, 16);
    if (lineCount == 0)
        return SYS_MESSAGEBOX_RESULT_OK;
    const int height = lineCount * 9 + 20;

    memset(&positive, 0, sizeof(positive));
    positive.FrontPen = 1;
    positive.DrawMode = JAM2;
    positive.IText = (UBYTE *)"Retry";
    memset(&negative, 0, sizeof(negative));
    negative.FrontPen = 1;
    negative.DrawMode = JAM2;
    negative.IText = (UBYTE *)"Cancel";

    if (buttons == SYS_MESSAGEBOX_RETRY_CANCEL) {
        const BOOL retry = AutoRequest(NULL, &lines[0], &positive, &negative, 0, 0, 400, height);
        return retry ? SYS_MESSAGEBOX_RESULT_RETRY : SYS_MESSAGEBOX_RESULT_CANCEL;
    }
    AutoRequest(NULL, &lines[0], NULL, NULL, 0, 0, 400, height);
    return SYS_MESSAGEBOX_RESULT_OK;
}

extern "C" void Sys_Log(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
    if (!s_logTried) {
        s_logTried = 1;
        char logPath[kNativePathSize];
        if (RootFile(logPath, sizeof(logPath), s_userDir, "opencmr2.log")) {
            /* Append when possible: MODE_NEWFILE fails with IoErr 202 while a
               previous run still holds the file. A unique name is the last
               resort so the diagnostic trail is never lost. */
            s_logHandle = Open((STRPTR)logPath, MODE_OLDFILE);
            if (s_logHandle == 0)
                s_logHandle = Open((STRPTR)logPath, MODE_NEWFILE);
            if (s_logHandle == 0) {
                char fallback[kNativePathSize + 16];
                for (int i = 1; i <= 3 && s_logHandle == 0; ++i) {
                    snprintf(fallback, sizeof(fallback), "%s.%d", logPath, i);
                    s_logHandle = Open((STRPTR)fallback, MODE_OLDFILE);
                    if (s_logHandle == 0)
                        s_logHandle = Open((STRPTR)fallback, MODE_NEWFILE);
                }
            }
            if (s_logHandle != 0)
                Seek(s_logHandle, 0, OFFSET_END);
            else
                fprintf(stderr, "opencmr2: cannot open %s (IoErr %ld)\n",
                        logPath, (long)IoErr());
        }
    }
    if (s_logHandle != 0) {
        char newline = '\n';
        const LONG wrote = Write(s_logHandle, line, (LONG)strlen(line));
        const LONG newlineWrote = Write(s_logHandle, &newline, 1);
        const LONG flushed = Flush(s_logHandle);
        if ((wrote < 0 || newlineWrote < 0 || !flushed) && !s_logWriteFailed) {
            s_logWriteFailed = true;
            fprintf(stderr, "opencmr2: log write failed (IoErr %ld)\n", (long)IoErr());
        }
    }
}

extern "C" void Sys_Fatal(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    Sys_Log("fatal: %s", line);
    Sys_MessageBox("OpenCMR2", line, SYS_MESSAGEBOX_OK);
    Sys_Exit(1);
}

extern "C" void AmigaSys_Shutdown(void)
{
    CloseTime();
    CloseInputState();
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    // The log is flushed per line; closing its dos handle on the normal return
    // path is left to the OS.
}

extern "C" void Sys_Exit(int code)
{
    AmigaSys_Shutdown();
    exit(code);
}

extern "C" const char *Sys_GetCommandLine(void)
{
    return s_commandLine;
}
