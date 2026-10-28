// System services on AmigaOS: see port/sys.h.
//
// This slice covers the clock, sleep, logging, files and game paths. Settings,
// window/IDCMP and message boxes land with their M1 components, so the game
// cannot boot on this file alone yet.
#include "port/sys.h"
#include "port/sys_steps.h"
#include "port/endian.h"
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
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/keymap.h>
#include <exec/interrupts.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/keymap.h>

#include <map>
#include <new>
#include <string>

// proto/timer.h declares these; libnix supplies SysBase and DOSBase.
struct Device *TimerBase;
struct IntuitionBase *IntuitionBase;
struct Library *KeymapBase;

// The IDCMP classes the input pump understands (see Sys_PollEvent).
static const ULONG kInputIDCMP = IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS |
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

// Small cache for the per-frame option lookups (HUD aspect/scale, vsync).
// Keys are short literals; the value pointers stay valid until the settings
// are reparsed.
struct SettingCacheEntry {
    char name[48];
    const char *value;
};
static SettingCacheEntry s_settingCache[4];
static int s_settingCacheCount;
static int s_settingCacheNext;

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

/* input.device adapter. The handler is the authoritative key down/up source:
 * a sampled IDCMP state can lose a release or a tap shorter than one frame.
 * It runs at interrupt level, so it only touches these arrays and the window
 * flag; all device-management calls happen in task context below. An IDCMP
 * release also clears the held state (see PollEvent) so a synthetic or
 * otherwise missed handler release cannot leave a control held forever; the
 * tap latch is left alone to expire normally. */
struct MsgPort *s_inputDevicePort;
struct IOStdReq *s_inputDeviceRequest;
struct Interrupt s_inputDeviceHandler;
int s_inputDeviceState; /* 0 = not tried, 1 = open, 2 = handler installed, -1 = failed */
static int s_inputExitRegistered;
volatile unsigned char s_inputHeld[256];
volatile unsigned char s_inputLatch[256];

/* Text input: MapRawKey translates a RAWKEY through the user's keymap. The
 * resulting characters are queued because Sys_PollEvent returns one event at
 * a time; Sys_PollEvent/Sys_WaitEvent drain this queue before the IDCMP. */
static unsigned char s_pendingText[32];
static unsigned int s_pendingTextCount;
static int s_keymapTried;

static int EnsureKeymap(void)
{
    if (KeymapBase != NULL)
        return 1;
    if (!s_keymapTried) {
        s_keymapTried = 1;
        KeymapBase = OpenLibrary((STRPTR)"keymap.library", 36);
        if (KeymapBase == NULL)
            Sys_Log("keymap.library: OpenLibrary failed; text input unavailable");
    }
    return KeymapBase != NULL;
}

static void QueueTextByte(unsigned char c)
{
    if (s_pendingTextCount < sizeof(s_pendingText))
        s_pendingText[s_pendingTextCount++] = c;
}

static int TakePendingText(SysEvent *event)
{
    if (s_pendingTextCount == 0)
        return 0;
    memset(event, 0, sizeof(*event));
    event->type = SYS_EVENT_TEXT;
    event->character = s_pendingText[0];
    memmove(s_pendingText, s_pendingText + 1, --s_pendingTextCount);
    return 1;
}

static void ResetKeyState(void)
{
    int i;
    Disable();
    for (i = 0; i < 256; i++) {
        s_keyState[i] = 0;
        s_inputHeld[i] = 0;
        s_inputLatch[i] = 0;
    }
    Enable();
}

/* RAWKEY carries the DIK scan code; bit 7 (IECODE_UP_PREFIX) is key-up. A
 * tap is latched for two game samples so the game's frame-to-frame edge
 * (DeviceInfo.field_0x8) still sees it; a held key stays in s_inputHeld. */
static struct InputEvent *InputDeviceHandler(register struct InputEvent *events __asm("a0"),
                                            register APTR data __asm("a1"))
{
    struct InputEvent *event;
    struct Window *window = s_inputWindow;

    (void)data;
    if (window == NULL || (window->Flags & WFLG_WINDOWACTIVE) == 0)
        return events;
    for (event = events; event != NULL; event = event->ie_NextEvent) {
        int dik;
        if (event->ie_Class != IECLASS_RAWKEY)
            continue;
        dik = AmigaKey_ToDIK(event->ie_Code);
        if (dik == 0)
            continue;
        if ((event->ie_Code & IECODE_UP_PREFIX) != 0) {
            s_inputHeld[dik] = 0;
        } else if (s_inputHeld[dik] == 0) {
            s_inputHeld[dik] = 1;
            s_inputLatch[dik] = 2;
        }
    }
    return events;
}

static void CloseInputDevice(void)
{
    if (s_inputDeviceState == 2 && s_inputDeviceRequest != NULL) {
        int attempt;
        BYTE result = 0;
        for (attempt = 0; attempt < 3; ++attempt) {
            s_inputDeviceRequest->io_Command = IND_REMHANDLER;
            s_inputDeviceRequest->io_Data = (APTR)&s_inputDeviceHandler;
            s_inputDeviceRequest->io_Length = 0;
            result = DoIO((struct IORequest *)s_inputDeviceRequest);
            if (result == 0 && s_inputDeviceRequest->io_Error == 0)
                break;
            Sys_Log("input.device: IND_REMHANDLER failed (IoErr %ld), attempt %d",
                    (long)(result != 0 ? result : s_inputDeviceRequest->io_Error), attempt + 1);
        }
        if (result != 0 || s_inputDeviceRequest->io_Error != 0) {
            Sys_Log("input.device: handler still installed; keeping request and port");
            return;
        }
        s_inputDeviceState = 1;
    }
    if (s_inputDeviceState == 1 && s_inputDeviceRequest != NULL) {
        CloseDevice((struct IORequest *)s_inputDeviceRequest);
        s_inputDeviceState = 0;
    }
    if (s_inputDeviceRequest != NULL) {
        DeleteIORequest((struct IORequest *)s_inputDeviceRequest);
        s_inputDeviceRequest = NULL;
    }
    if (s_inputDevicePort != NULL) {
        DeleteMsgPort(s_inputDevicePort);
        s_inputDevicePort = NULL;
    }
    s_inputDeviceState = 0;
}

extern "C" int AmigaSys_InputInit(void)
{
    if (s_inputDeviceState == 2)
        return 1;
    if (s_inputDeviceState != 0)
        return 0;
    s_inputDeviceState = -1;
    s_inputDevicePort = CreateMsgPort();
    if (s_inputDevicePort == NULL) {
        Sys_Log("input.device: CreateMsgPort failed");
        s_inputDeviceState = 0;
        return 0;
    }
    s_inputDeviceRequest = (struct IOStdReq *)CreateIORequest(s_inputDevicePort, sizeof(struct IOStdReq));
    if (s_inputDeviceRequest == NULL) {
        Sys_Log("input.device: CreateIORequest failed");
        CloseInputDevice();
        return 0;
    }
    if (OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)s_inputDeviceRequest, 0) != 0) {
        Sys_Log("input.device: OpenDevice failed (IoErr %ld)", (long)IoErr());
        CloseInputDevice();
        return 0;
    }
    s_inputDeviceState = 1;
    s_inputDeviceHandler.is_Node.ln_Type = NT_INTERRUPT;
    s_inputDeviceHandler.is_Node.ln_Pri = 100;
    s_inputDeviceHandler.is_Node.ln_Name = (char *)"OpenCMR2 input handler";
    s_inputDeviceHandler.is_Code = (void (*)())InputDeviceHandler;
    s_inputDeviceHandler.is_Data = NULL;
    s_inputDeviceRequest->io_Command = IND_ADDHANDLER;
    s_inputDeviceRequest->io_Data = (APTR)&s_inputDeviceHandler;
    s_inputDeviceRequest->io_Length = 0;
    if (DoIO((struct IORequest *)s_inputDeviceRequest) != 0) {
        Sys_Log("input.device: IND_ADDHANDLER failed (IoErr %ld)", (long)IoErr());
        CloseInputDevice();
        return 0;
    }
    s_inputDeviceState = 2;
    ResetKeyState();
    if (!s_inputExitRegistered) {
        if (atexit(AmigaSys_InputShutdown) == 0)
            s_inputExitRegistered = 1;
        else
            Sys_Log("input.device: atexit registration failed");
    }
    Sys_Log("input: device handler installed");
    return 1;
}

extern "C" void AmigaSys_InputShutdown(void)
{
    /* Direct exit() paths do not run CloseInputState; make sure an in-flight
     * handler returns before the window is torn down by the OS. */
    s_inputWindow = NULL;
    ResetKeyState();
    CloseInputDevice();
}

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
        BPTR created = CreateDir((STRPTR)path);
        if (created != 0) {
            UnLock(created);
            continue;
        }
        {
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
    char lockPath[kNativePathSize];
    size_t length = 0;
    BPTR lock;

    if (directory != NULL) {
        while (directory[length] != '\0' && length + 1 < sizeof(lockPath)) {
            lockPath[length] = directory[length];
            ++length;
        }
    }
    if (length > 0 && lockPath[length - 1] != '/' && lockPath[length - 1] != ':') {
        if (length + 1 >= sizeof(lockPath))
            return;
        lockPath[length++] = '/';
    }
    lockPath[length] = '\0';
    lock = Lock((STRPTR)lockPath, SHARED_LOCK);

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
    s_settingCacheCount = 0;
    s_settingCacheNext = 0;
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
    std::map<std::string, std::string>::const_iterator it;
    int i;

    if (name == NULL)
        return NULL;
    EnsureSettingsLoaded();
    for (i = 0; i < s_settingCacheCount; i++) {
        if (strcmp(s_settingCache[i].name, name) == 0)
            return s_settingCache[i].value;
    }
    it = s_settings->find(AmigaSettings::Lower(name));
    if (strlen(name) >= sizeof(s_settingCache[0].name))
        return it != s_settings->end() ? it->second.c_str() : NULL;
    i = s_settingCacheCount < 4 ? s_settingCacheCount++ : s_settingCacheNext;
    s_settingCacheNext = (i + 1) % 4;
    strcpy(s_settingCache[i].name, name);
    s_settingCache[i].value = it != s_settings->end() ? it->second.c_str() : NULL;
    return s_settingCache[i].value;
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
        const std::string backup = path + ".bak";
        DeleteFile((STRPTR)backup.c_str());
        if (!Rename((STRPTR)path.c_str(), (STRPTR)backup.c_str())) {
            Sys_Log("settings: cannot back up %s (IoErr %ld)", path.c_str(), (long)IoErr());
            DeleteFile((STRPTR)temporary.c_str());
            return false;
        }
        if (!Rename((STRPTR)temporary.c_str(), (STRPTR)path.c_str())) {
            const LONG replaceError = IoErr();
            Sys_Log("settings: cannot rename %s (IoErr %ld)", temporary.c_str(), (long)replaceError);
            if (!Rename((STRPTR)backup.c_str(), (STRPTR)path.c_str()))
                Sys_Log("settings: cannot restore %s (IoErr %ld)", path.c_str(), (long)IoErr());
            DeleteFile((STRPTR)temporary.c_str());
            return false;
        }
        DeleteFile((STRPTR)backup.c_str());
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
    // A save interrupted between the live rename and the temp rename leaves
    // only the backup. Recover it before creating defaults.
    const std::string backup = path + ".bak";
    BPTR backupLock = Lock((STRPTR)backup.c_str(), SHARED_LOCK);
    if (backupLock != 0) {
        UnLock(backupLock);
        if (Rename((STRPTR)backup.c_str(), (STRPTR)path.c_str())) {
            s_settingsLoaded = false;
            return 1;
        }
        Sys_Log("settings: cannot restore %s (IoErr %ld)", backup.c_str(), (long)IoErr());
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
        } else {
            BPTR created = CreateDir((STRPTR)path);
            if (created != 0) {
                UnLock(created);
            } else {
                const LONG error = IoErr();
                lock = Lock((STRPTR)path, SHARED_LOCK);
                if (lock == 0) {
                    *p = '/';
                    Sys_Log("CreateDir %s failed (IoErr %ld)", path, (long)error);
                    return 0;
                }
                UnLock(lock);
            }
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

/*
 * PORT: AMIGA: the controller and game-info files are raw native structures.
 * The keyboard binding bytes are endian-independent, but the multi-byte
 * fields and the 4-byte controller slot map are not. A PC-written file passes
 * the 0x11a4 size check and would be read by the game without conversion; the
 * slot map [0,1] would become [0,256] and ApplyControllerButtonBindings(1)
 * would index m_controllerInfo[256]. Log the byte order and the critical
 * fields so the next target run proves which format is present.
 */
static void LogConfigFile(const char *native, BPTR handle)
{
    BYTE head[0x150];
    BYTE slot[4];
    char name[32];
    LONG size;
    LONG got;
    int slotGot = 0;

    if (strstr(native, "Controller.rcf") == NULL && strstr(native, "GameInfo.rcf") == NULL)
        return;
    memset(head, 0, sizeof(head));
    /* AmigaDOS Seek returns the previous position, so the second call is the
     * offset at EOF. The old one-call form logged size=0 for every file and
     * skipped the slot-map read. */
    if (Seek(handle, 0, OFFSET_END) < 0)
        size = 0;
    else
        size = Seek(handle, 0, OFFSET_CURRENT);
    Seek(handle, 0, OFFSET_BEGINNING);
    got = Read(handle, head, sizeof(head));
    Seek(handle, 0, OFFSET_BEGINNING);
    if (got < 0)
        got = 0;
    if (strstr(native, "Controller.rcf") != NULL) {
        name[0] = '\0';
        if (got > 8) {
            size_t nameLen = (size_t)(got - 8);
            if (nameLen >= sizeof(name))
                nameLen = sizeof(name) - 1;
            memcpy(name, head + 8, nameLen);
            name[nameLen] = '\0';
        }
        memset(slot, 0, sizeof(slot));
        if (size >= 0x11a4) {
            Seek(handle, 0x11a0, OFFSET_BEGINNING);
            slotGot = (int)Read(handle, slot, sizeof(slot));
            Seek(handle, 0, OFFSET_BEGINNING);
        }
        Sys_Log("file: config Controller size=%ld first=%08lx/%08lx name=%s bind0=%02lx%02lx%02lx%02lx%02lx%02lx%02lx%02lx%02lx slot=%02lx%02lx%02lx%02lx be=%u,%u le=%u,%u read=%d",
                (long)size,
                got >= 4 ? (unsigned long)Swap32(ReadLE32(head)) : 0UL,
                got >= 4 ? (unsigned long)ReadLE32(head) : 0UL,
                name,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 0] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 1] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 2] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 3] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 4] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 5] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 6] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 7] : 0,
                got > 0x13e + 8 ? (unsigned long)head[0x13e + 8] : 0,
                (unsigned long)slot[0], (unsigned long)slot[1],
                (unsigned long)slot[2], (unsigned long)slot[3],
                (unsigned int)Swap16(ReadLE16(slot)),
                (unsigned int)Swap16(ReadLE16(slot + 2)),
                (unsigned int)ReadLE16(slot),
                (unsigned int)ReadLE16(slot + 2),
                slotGot);
    } else {
        Sys_Log("file: config GameInfo size=%ld magic=%08lx/%08lx lang=%02lx read=%ld",
                (long)size,
                got >= 0x14 ? (unsigned long)Swap32(ReadLE32(head + 0x10)) : 0,
                got >= 0x14 ? (unsigned long)ReadLE32(head + 0x10) : 0,
                got >= 0x15 ? (unsigned long)head[0x14] : 0,
                (long)got);
    }
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
    if (!writing)
        LogConfigFile(native, handle);
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
    s_settingCacheCount = 0;
    s_settingCacheNext = 0;
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
        struct Window *window = s_ownedInputWindow;
        s_ownedInputWindow = NULL;
        /* Stop the input handler from touching the window before it dies. */
        s_inputWindow = NULL;
        CloseWindow(window);
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
    ResetKeyState();
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
            ResetKeyState();
            event->type = SYS_EVENT_FOCUS_GAINED;
            event->key = 0;
            event->character = 0;
            event->repeat = 0;
            handled = 1;
        } else if (messageClass == IDCMP_INACTIVEWINDOW) {
            Sys_Log("input: inactive window");
            ResetKeyState();
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
            if (dik != 0 && (message->Code & 0x80) != 0) {
                // Fallback for a release the input.device handler did not
                // observe (for example a synthetic or dropped event).
                Disable();
                s_inputHeld[dik] = 0;
                Enable();
            }
            if (dik != 0 && (message->Code & 0x80) == 0) {
                if (EnsureKeymap()) {
                    struct InputEvent ie;
                    char text[8];
                    LONG n;
                    LONG k;
                    memset(&ie, 0, sizeof(ie));
                    ie.ie_Class = IECLASS_RAWKEY;
                    ie.ie_Code = message->Code & 0x7f;
                    ie.ie_Qualifier = message->Qualifier;
                    n = MapRawKey(&ie, text, sizeof(text), NULL);
                    for (k = 0; k < n; ++k) {
                        const unsigned char c = (unsigned char)text[k];
                        if (c >= 0x20 && c != 0x7f)
                            QueueTextByte(c);
                    }
                }
                event->type = SYS_EVENT_KEY_DOWN;
                event->key = dik;
                event->character = 0;
                event->repeat = (message->Qualifier & IEQUALIFIER_REPEAT) != 0;
                handled = 1;
            }
        }
        ReplyMsg((struct Message *)message);
        if (handled)
            return 1;
    }
    return 0;
}

extern "C" int Sys_PollEvent(SysEvent *event)
{
    if (event == NULL)
        return 0;
    if (TakePendingText(event))
        return 1;
    return PollEvent(event, NULL);
}

extern "C" void AmigaSys_GetKeyState(unsigned char keys[256])
{
    int i;
    if (keys == NULL)
        return;
    if (s_inputDeviceState == 2) {
        /* One sample: held keys plus taps latched for this frame. */
        Disable();
        for (i = 0; i < 256; i++) {
            keys[i] = (s_inputHeld[i] || s_inputLatch[i]) ? 0x80 : 0;
            if (s_inputHeld[i])
                s_inputLatch[i] = 0;
            else if (s_inputLatch[i] != 0)
                s_inputLatch[i]--;
        }
        Enable();
        return;
    }
    /* Fallback for the target tests (fake window, no input.device) and for a
     * machine where input.device could not be opened. */
    memcpy(keys, s_keyState, sizeof(s_keyState));
}

extern "C" int AmigaSys_IsKeyDown(int dik)
{
    if (dik < 0 || dik >= 256)
        return 0;
    if (s_inputDeviceState == 2)
        return s_inputHeld[dik] != 0 || s_inputLatch[dik] != 0;
    return (s_keyState[dik] & 0x80) != 0;
}

extern "C" int Sys_WaitEvent(SysEvent *event)
{
    if (event == NULL)
        return 0;
    if (TakePendingText(event))
        return 1;
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
    AmigaSys_InputShutdown();
    if (KeymapBase != NULL) {
        CloseLibrary(KeymapBase);
        KeymapBase = NULL;
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    // AmigaOS does not close a process's dos handles at exit; an open log stays
    // locked until reboot (the IoErr 202 fallback in Sys_Log). s_logTried stays
    // set, so a later Sys_Log (atexit handlers) writes to stderr only.
    if (s_logHandle != 0) {
        if (!Close(s_logHandle))
            fprintf(stderr, "opencmr2: closing the log failed (IoErr %ld)\n", (long)IoErr());
        s_logHandle = 0;
    }
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
