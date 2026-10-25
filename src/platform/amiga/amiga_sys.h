/* Fork-owned internal API between main_amiga.cpp and the Domain B Sys glue. */
#ifndef OPENCMR2_AMIGA_SYS_H
#define OPENCMR2_AMIGA_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Stores the command line returned by Sys_GetCommandLine. */
void AmigaSys_SetCommandLine(const char *line);

#ifdef __cplusplus
}
#endif

#endif
