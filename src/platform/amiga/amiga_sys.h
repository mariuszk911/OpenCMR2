/* Fork-owned internal API between main_amiga.cpp and the Domain B Sys glue. */
#ifndef OPENCMR2_AMIGA_SYS_H
#define OPENCMR2_AMIGA_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Stores the command line returned by Sys_GetCommandLine. */
void AmigaSys_SetCommandLine(const char *line);

/* Game data and user directories. Defaults: PROGDIR:Data and PROGDIR:. */
void AmigaSys_SetDataDir(const char *path);
void AmigaSys_SetUserDir(const char *path);
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

#ifdef __cplusplus
}
#endif

#endif
