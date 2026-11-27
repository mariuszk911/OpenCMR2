/* Fork-owned internal API between main_amiga.cpp and the Domain B Sys glue. */
#ifndef OPENCMR2_AMIGA_SYS_H
#define OPENCMR2_AMIGA_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Stores the command line returned by Sys_GetCommandLine. */
void AmigaSys_SetCommandLine(const char *line);

/* Game data and user directories. Default and supported layout: PROGDIR:. */
void AmigaSys_SetDataDir(const char *path);
void AmigaSys_SetUserDir(const char *path);

/* A function the platform layer calls from the game's own task while the game
   is busy: on every event poll and every file read, at most once per 50 ms
   (what keeps the network session answering during a stage load). NULL
   removes it. The call comes from the task that registered it. */
void AmigaSys_SetBackgroundPump(void (*pump)(void));
/* Longest time between two calls of the pump since it was registered, in
   milliseconds (a measurement, logged by the network layer). */
unsigned long AmigaSys_GetPumpGapMax(void);

/* Runs once at the start of AmigaSys_Shutdown, before the timer, the log and
   the libraries are closed: for subsystems that own OS resources of their own
   (the network session closes its sockets and bsdsocket.library). */
void AmigaSys_SetShutdownHook(void (*hook)(void));
#if defined(OPENCMR2_WARPOS)
/* One-shot ENV: switch for remote sessions: 1 when the global variable exists,
   which is then deleted. A 68k call: callers poll it at most once a second. */
int AmigaSys_TakeEnvFlag(const char *name);
unsigned long AmigaSys_TakePumpMicros(void);
unsigned long AmigaSys_TakeFileMicros(unsigned long *calls, unsigned long *bytes);
unsigned long AmigaSys_TakeAudioMicros(unsigned long *calls);
#endif
const char *AmigaSys_GetDataDir(void);
const char *AmigaSys_GetUserDir(void);
/* Creates opencmr2.ini in the user directory with the v1 defaults if it is
   missing. Returns 1 when the file exists or was written. */
int AmigaSys_EnsureSettingsFile(void);

/* Closes the timer, Intuition and log without terminating the process. Normal
   exits return from main; Sys_Exit reserves libnix's exit path for fatal paths. */
void AmigaSys_Shutdown(void);

/* Registers the Intuition window whose IDCMP port feeds Sys_PollEvent/
   Sys_WaitEvent. The MiniGL executor calls this after creating its context. */
void AmigaSys_SetWindow(void *window);

/* Fork-owned input.device adapter: installs the key down/up handler that
   maintains the DIK state, latches sub-frame taps and ignores keys while the
   game window is not active. Called when the game opens the keyboard. */
int AmigaSys_InputInit(void);
void AmigaSys_InputShutdown(void);

/* Windowed mode: opens the visible game window on the Workbench screen with a
   close gadget and makes it the input window, after reVC. Returns the Window*
   for mglCreateContextFromWindow, or NULL on failure (the executor then uses
   the MiniGL screen and its input backdrop). */
void *AmigaSys_CreateGameWindow(int width, int height, const char *title);

/* Held-key state indexed by DIK_*, 0x80 while the key is down. It comes from
   the input.device adapter; IDCMP remains the fallback for tests and when
   input.device cannot be opened. Focus loss clears it. */
void AmigaSys_GetKeyState(unsigned char keys[256]);
int AmigaSys_IsKeyDown(int dik);

/* Free memory of any type in bytes (AvailMem(MEMF_ANY)), for leak checks in
   the logs. */
unsigned long AmigaSys_GetFreeMemory(void);

#ifdef __cplusplus
}
#endif

#endif
