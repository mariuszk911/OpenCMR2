/* PPC exception record for WarpOS (wos_diag.c). Plain C types only. */
#ifndef OPENCMR2_WOS_DIAG_H
#define OPENCMR2_WOS_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Installs the task's PPC exception handler and logs where its record is. */
void WosDiag_Install(void);
/* Removes it; called once at shutdown. */
void WosDiag_Remove(void);

#ifdef __cplusplus
}
#endif

#endif
