// System services on AmigaOS: see port/sys.h.
//
// This slice covers the clock, sleep, logging, files and game paths. Settings,
// window/IDCMP and message boxes land with their M1 components, so the game
// cannot boot on this file alone yet.
#include "port/sys.h"
#include "port/sys_steps.h"
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
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <map>
#include <new>
#include <string>

// proto/timer.h declares these; libnix supplies SysBase and DOSBase.
struct Device *TimerBase;

namespace {

struct MsgPort *s_timerPort;
struct timerequest s_timerRequest;
uint32_t s_eclockFrequency;
uint64_t s_eclockStart;
int s_timeState; // 0 = not tried, 1 = ready, -1 = failed to open

BPTR s_logHandle;
int s_logTried;
char s_commandLine[1024];

char s_dataDir[256] = "PROGDIR:Data";
char s_userDir[256] = "PROGDIR:";
char s_lastMissing[512];

std::map<std::string, std::string> *s_settings;
bool s_settingsLoaded;

const int kMaxListedFiles = 256;
const int kListedNameSize = AMIGA_PATH_PART_MAX;
const size_t kNativePathSize = 640;
bool s_listFull;
bool s_logWriteFailed;

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

extern "C" int Sys_ResolvePath(const char *path, int forWriting, char *out, size_t outSize)
{
    AmigaGamePath parts;

    if (path == NULL || out == NULL || outSize == 0)
        return 0;
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
    if (ResolveReading(path, out, outSize))
        return 1;
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

    if (path == NULL || !AmigaPath_Split(path, &parts))
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

extern "C" void Sys_Log(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "%s\n", line);
    if (!s_logTried) {
        s_logTried = 1;
        char logPath[kNativePathSize];
        if (RootFile(logPath, sizeof(logPath), s_userDir, "opencmr2.log")) {
            s_logHandle = Open((STRPTR)logPath, MODE_NEWFILE);
            if (s_logHandle == 0)
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
    // The requester lands with the Intuition/IDCMP slice of M1.
    Sys_Exit(1);
}

extern "C" void Sys_Exit(int code)
{
    CloseTime();
    if (s_logHandle != 0) {
        Close(s_logHandle);
        s_logHandle = 0;
    }
    exit(code);
}

extern "C" const char *Sys_GetCommandLine(void)
{
    return s_commandLine;
}
