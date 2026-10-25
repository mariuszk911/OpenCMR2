/* Fork-owned AmigaOS platform lifecycle, called from main_amiga.cpp. */
#ifndef OPENCMR2_AMIGA_PLATFORM_H
#define OPENCMR2_AMIGA_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Parses --data/--user-dir, sets the Sys directories, creates the default
   settings file and locates a complete game installation. Returns 0 after
   logging a diagnostic when the installation cannot be used. */
int AmigaPlatform_Init(int argc, char **argv);
void AmigaPlatform_Shutdown(void);
const char *AmigaPlatform_GetDataDir(void);
const char *AmigaPlatform_GetUserDir(void);
/* There is no ASL picker in v1, so this is always 0. */
int AmigaPlatform_WasSetupCanceled(void);

#ifdef __cplusplus
}
#endif

#endif
