// Input on AmigaOS: the device model is present but polling is not wired to
// the keyboard/joystick hardware yet. The keyboard events still reach the game
// through Sys_PollEvent, so menus can be added before the polling slice.
#include "port/input.h"

#include <string.h>

struct InputDevice {
    int kind;
};

namespace {
InputDevice s_keyboard = { 1 };
InputDevice s_mouse = { 2 };
}

extern "C" int Input_GetJoystickCount(void)
{
    return 0;
}

extern "C" int Input_JoysticksChanged(void)
{
    return 0;
}

extern "C" InputDevice *Input_OpenKeyboard(void)
{
    return &s_keyboard;
}

extern "C" InputDevice *Input_OpenMouse(void)
{
    return &s_mouse;
}

extern "C" InputDevice *Input_OpenJoystick(int index)
{
    (void)index;
    return NULL;
}

extern "C" int Input_IsGamepad(InputDevice *device)
{
    (void)device;
    return 0;
}

extern "C" void Input_CloseDevice(InputDevice *device)
{
    (void)device;
}

extern "C" const char *Input_GetInstanceName(InputDevice *device)
{
    return device != NULL && device->kind == 2 ? "Amiga mouse" : "Amiga keyboard";
}

extern "C" const char *Input_GetProductName(InputDevice *device)
{
    return Input_GetInstanceName(device);
}

extern "C" void Input_GetCaps(InputDevice *device, InputCaps *caps)
{
    (void)device;
    if (caps != NULL)
        memset(caps, 0, sizeof(*caps));
}

extern "C" int Input_GetObjectName(InputDevice *device, DWORD offset, char *name, size_t size)
{
    (void)device;
    (void)offset;
    if (name != NULL && size != 0)
        name[0] = '\0';
    return 0;
}

extern "C" int Input_HasAxis(InputDevice *device, DWORD offset)
{
    (void)device;
    (void)offset;
    return 0;
}

extern "C" void Input_SetAxisRange(InputDevice *device, DWORD offset, LONG min, LONG max)
{
    (void)device; (void)offset; (void)min; (void)max;
}

extern "C" void Input_SetAxisDeadzone(InputDevice *device, DWORD offset, DWORD deadzone)
{
    (void)device; (void)offset; (void)deadzone;
}

extern "C" void Input_SetAxisSaturation(InputDevice *device, DWORD offset, DWORD saturation)
{
    (void)device; (void)offset; (void)saturation;
}

extern "C" void Input_SetMouseExclusive(InputDevice *device, int exclusive)
{
    (void)device;
    (void)exclusive;
}

extern "C" int Input_ReadKeyboard(InputDevice *device, BYTE keys[256])
{
    (void)device;
    if (keys != NULL)
        memset(keys, 0, 256);
    return device != NULL;
}

extern "C" int Input_ReadMouse(InputDevice *device, InputMouseState *state)
{
    (void)device;
    if (state != NULL)
        memset(state, 0, sizeof(*state));
    return device != NULL;
}

extern "C" int Input_ReadJoystick(InputDevice *device, InputJoystickState *state)
{
    (void)device;
    (void)state;
    return 0;
}

extern "C" int Input_IsKeyDown(int key)
{
    (void)key;
    return 0;
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
    (void)key;
    return "Unknown";
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
