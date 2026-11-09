// Input on AmigaOS: the IDCMP event loop (amiga_sys.cpp) is the source of
// truth. It keeps the held-key state current while Sys_PollEvent drains the
// window port; this layer turns that state into the action interface of
// port/input.h for the two keyboard players, and exposes the raw keyboard
// for text entry. There are no joysticks or mice yet.
//
// The bindings are DirectInput scan codes (INPUT_KEY_* / DIK_*), two per
// action. They live in "controls.amiga" in the user directory, as one line of
// hexadecimal key pairs per keyboard player.
#include "port/input.h"
#include "port/sys.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "platform/amiga/amiga_keys.h"
#include "platform/amiga/amiga_sys.h"

struct InputDevice {
    int kind;
};

namespace {
InputDevice s_keyboard = { 1 };
InputDevice s_mouse = { 2 };

const int KEYS_PER_ACTION = 2;
struct Bindings {
    unsigned char key[INPUT_ACTION_COUNT][KEYS_PER_ACTION];
};

// DIK codes of the two keyboard players (the defaults of the SDL build).
const Bindings s_defaults[INPUT_PLAYERS] = {
    { {
        { 0xCB, 0 }, { 0xCD, 0 }, { 0xC8, 0 }, { 0xD0, 0 }, { 0x39, 0 },
        { 0x1B, 0 }, { 0x1A, 0 }, { 0x2E, 0 }, { 0x13, 0 }, { 0x01, 0 },
        { 0xCB, 0 }, { 0xCD, 0 }, { 0xC8, 0 }, { 0xD0, 0 }, { 0x1C, 0x9C }, { 0x01, 0 },
    } },
    { {
        { 0x4B, 0 }, { 0x4D, 0 }, { 0x48, 0 }, { 0x50, 0 }, { 0x51, 0 },
        { 0xC9, 0 }, { 0xD1, 0 }, { 0x49, 0 }, { 0x47, 0 }, { 0x01, 0 },
        { 0xCB, 0 }, { 0xCD, 0 }, { 0xC8, 0 }, { 0xD0, 0 }, { 0x1C, 0x9C }, { 0x01, 0 },
    } },
};

Bindings s_bindings[INPUT_PLAYERS];
Bindings s_saved[INPUT_PLAYERS];
bool s_loaded = false;
bool s_fileExists = false;
bool s_changed = false;
int s_localPlayers = 2;
bool s_textEntryOpen = false;

// Capture of the next key for an action.
bool s_capturing = false;
int s_capturePlayer = 0;
int s_captureAction = 0;
unsigned char s_captureBase[256];
clock_t s_captureStart = 0;

bool UserPath(char *path, size_t size)
{
    const char *dir = AmigaSys_GetUserDir();
    if (dir == NULL || dir[0] == '\0')
        return false;
    size_t length = strlen(dir);
    const char *separator = (dir[length - 1] == ':' || dir[length - 1] == '/') ? "" : "/";
    return snprintf(path, size, "%s%scontrols.amiga", dir, separator) < (int)size;
}

void Load(void)
{
    if (s_loaded)
        return;
    s_loaded = true;
    memcpy(s_bindings, s_defaults, sizeof(s_bindings));
    char path[512];
    if (UserPath(path, sizeof(path))) {
        if (FILE *file = fopen(path, "r")) {
            Bindings loaded[INPUT_PLAYERS];
            memcpy(loaded, s_bindings, sizeof(loaded));
            bool ok = true;
            for (int player = 0; player < INPUT_PLAYERS && ok; player++) {
                for (int action = 0; action < INPUT_ACTION_COUNT && ok; action++) {
                    for (int k = 0; k < KEYS_PER_ACTION && ok; k++) {
                        unsigned value = 0;
                        if (fscanf(file, "%x", &value) != 1 || value > 0xFF)
                            ok = false;
                        else
                            loaded[player].key[action][k] = (unsigned char)value;
                    }
                }
            }
            fclose(file);
            if (ok) {
                memcpy(s_bindings, loaded, sizeof(loaded));
                s_fileExists = true;
            }
        }
    }
    memcpy(s_saved, s_bindings, sizeof(s_saved));
}

bool ActionDown(const Bindings &bindings, int action)
{
    for (int k = 0; k < KEYS_PER_ACTION; k++) {
        if (bindings.key[action][k] != 0 && AmigaSys_IsKeyDown(bindings.key[action][k]))
            return true;
    }
    return false;
}
}

extern "C" void Input_Update(void)
{
    Load();
}

extern "C" void Input_SetLocalPlayers(int count)
{
    s_localPlayers = count < 1 ? 1 : count > INPUT_PLAYERS ? INPUT_PLAYERS : count;
}

extern "C" void Input_SetTextEntryOpen(BOOL open)
{
    s_textEntryOpen = open != 0;
}

extern "C" int Input_GetAction(int player, int action)
{
    Load();
    if (s_textEntryOpen || player < 0 || player >= INPUT_PLAYERS || action < 0 || action >= INPUT_ACTION_COUNT)
        return 0;
    if (ActionDown(s_bindings[player], action))
        return INPUT_FULL;
    // With one local player, player 1 also takes the keys of player 2.
    if (player == 0 && s_localPlayers == 1 && ActionDown(s_bindings[1], action))
        return INPUT_FULL;
    return 0;
}

extern "C" BOOL Input_IsAnalogSteering(int player)
{
    (void)player;
    return FALSE;
}

extern "C" BOOL Input_IsAnalogPedals(int player)
{
    (void)player;
    return FALSE;
}

extern "C" BOOL Input_IsSmoothSteering(int player)
{
    (void)player;
    return FALSE;
}

extern "C" BOOL Input_DevicesChanged(void)
{
    return FALSE;
}

extern "C" InputDevice *Input_GetFeedbackDevice(int player)
{
    (void)player;
    return NULL;
}

extern "C" void Input_SetRumble(int player, int low, int high)
{
    (void)player; (void)low; (void)high;
}

extern "C" void Input_ImportKeyboardKeys(int player, const BYTE keys[9])
{
    Load();
    if (s_fileExists || player < 0 || player >= INPUT_PLAYERS)
        return;
    for (int action = 0; action < 9; action++) {
        if (keys[action] != 0) {
            s_bindings[player].key[action][0] = keys[action];
            s_bindings[player].key[action][1] = 0;
        }
    }
    s_changed = true;
}

extern "C" void Input_SaveBindings(void)
{
    char path[512];
    if (!s_loaded || !s_changed || !UserPath(path, sizeof(path)))
        return;
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        Sys_Log("controls: cannot write %s", path);
        return;
    }
    for (int player = 0; player < INPUT_PLAYERS; player++) {
        for (int action = 0; action < INPUT_ACTION_COUNT; action++)
            fprintf(file, "%02x %02x%c", s_bindings[player].key[action][0], s_bindings[player].key[action][1],
                    action + 1 == INPUT_ACTION_COUNT ? '\n' : ' ');
    }
    fclose(file);
    memcpy(s_saved, s_bindings, sizeof(s_saved));
    s_changed = false;
    s_fileExists = true;
}

// ---- controls pages: the two keyboard players -----------------------------------

extern "C" int Input_GetProfileCount(void)
{
    return INPUT_PLAYERS;
}

extern "C" int Input_GetProfileKind(int profile)
{
    return profile == 1 ? INPUT_PROFILE_KEYBOARD2 : INPUT_PROFILE_KEYBOARD;
}

extern "C" int Input_GetLastUsedProfile(void)
{
    return -1;
}

extern "C" const char *Input_GetProfileName(int profile)
{
    (void)profile;
    return "";
}

extern "C" void Input_GetBindingText(int profile, int action, char *text, int size)
{
    Load();
    if (text == NULL || size <= 0)
        return;
    text[0] = '\0';
    if (profile < 0 || profile >= INPUT_PLAYERS || action < 0 || action >= INPUT_ACTION_COUNT)
        return;
    for (int k = 0; k < KEYS_PER_ACTION; k++) {
        unsigned char key = s_bindings[profile].key[action][k];
        if (key == 0)
            continue;
        const char *name = AmigaKey_DIKName(key);
        size_t used = strlen(text);
        snprintf(text + used, (size_t)size - used, "%s%s", used ? " / " : "", name ? name : "?");
    }
}

extern "C" int Input_GetSetting(int profile, int setting)
{
    (void)profile; (void)setting;
    return 0;
}

extern "C" void Input_SetSetting(int profile, int setting, int value)
{
    (void)profile; (void)setting; (void)value;
}

extern "C" void Input_RestoreDefaults(int profile)
{
    Load();
    if (profile < 0 || profile >= INPUT_PLAYERS)
        return;
    s_bindings[profile] = s_defaults[profile];
    s_changed = true;
}

extern "C" void Input_BeginCapture(int profile, int action)
{
    Load();
    s_capturing = profile >= 0 && profile < INPUT_PLAYERS && action >= 0 && action < INPUT_ACTION_COUNT;
    s_capturePlayer = profile;
    s_captureAction = action;
    s_captureStart = clock();
    AmigaSys_GetKeyState(s_captureBase);
}

extern "C" int Input_UpdateCapture(void)
{
    if (!s_capturing)
        return -1;
    unsigned char now[256];
    AmigaSys_GetKeyState(now);
    for (int key = 1; key < 256; key++) {
        if (!now[key]) {
            s_captureBase[key] = 0;
            continue;
        }
        if (s_captureBase[key])
            continue;   // held since the capture began
        Bindings &bindings = s_bindings[s_capturePlayer];
        // A key bound to another driving action trades places with this one.
        unsigned char previous[KEYS_PER_ACTION];
        memcpy(previous, bindings.key[s_captureAction], sizeof(previous));
        for (int other = 0; other < 9; other++) {
            if (other == s_captureAction)
                continue;
            for (int k = 0; k < KEYS_PER_ACTION; k++) {
                if (bindings.key[other][k] == key)
                    bindings.key[other][k] = previous[0];
            }
        }
        bindings.key[s_captureAction][0] = (unsigned char)key;
        bindings.key[s_captureAction][1] = 0;
        s_changed = true;
        s_capturing = false;
        return 1;
    }
    if ((clock() - s_captureStart) / CLOCKS_PER_SEC >= 10) {
        s_capturing = false;
        return -1;
    }
    return 0;
}

extern "C" void Input_ReloadBindings(void)
{
    Load();
    memcpy(s_bindings, s_saved, sizeof(s_bindings));
    s_changed = false;
}

// ---- raw keyboard and mouse ---------------------------------------------------------

extern "C" InputDevice *Input_OpenKeyboard(void)
{
    AmigaSys_InputInit();
    return &s_keyboard;
}

extern "C" InputDevice *Input_OpenMouse(void)
{
    return &s_mouse;
}

extern "C" void Input_CloseDevice(InputDevice *device)
{
    (void)device;
}

extern "C" void Input_SetMouseExclusive(InputDevice *device, int exclusive)
{
    (void)device;
    (void)exclusive;
}

extern "C" int Input_ReadKeyboard(InputDevice *device, BYTE keys[256])
{
    if (device == NULL || keys == NULL || device->kind != 1)
        return 0;
    AmigaSys_GetKeyState(keys);
    return 1;
}

extern "C" int Input_ReadMouse(InputDevice *device, InputMouseState *state)
{
    (void)device;
    if (state != NULL)
        memset(state, 0, sizeof(*state));
    return device != NULL;
}

extern "C" int Input_IsKeyDown(int key)
{
    return AmigaSys_IsKeyDown(key);
}

extern "C" void Input_GetKeyRepeat(DWORD *delay, DWORD *interval)
{
    if (delay != NULL)
        *delay = 500;
    if (interval != NULL)
        *interval = 33;
}

extern "C" const char *Input_GetKeyName(int key)
{
    return AmigaKey_DIKName(key);
}

extern "C" InputEffect *Input_CreateEffect(InputDevice *device, const InputEffectParams *params)
{
    (void)device;
    (void)params;
    return NULL;
}

extern "C" void Input_UpdateEffect(InputEffect *effect, const InputEffectParams *params)
{
    (void)effect;
    (void)params;
}

extern "C" void Input_StartEffect(InputEffect *effect)
{
    (void)effect;
}

extern "C" int Input_IsEffectPlaying(InputEffect *effect)
{
    (void)effect;
    return 0;
}

extern "C" void Input_DestroyEffect(InputEffect *effect)
{
    (void)effect;
}

extern "C" void Input_StopAllEffects(InputDevice *device)
{
    (void)device;
}

extern "C" void Input_SetAutocenter(InputDevice *device, int enable)
{
    (void)device;
    (void)enable;
}
