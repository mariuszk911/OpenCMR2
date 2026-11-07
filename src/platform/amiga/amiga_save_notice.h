/*
 * What the player is told when a saved game is refused (see
 * SavedImageDisk_SetRejectHandler). Pure text selection, so the wording and
 * the one-notice-per-kind policy are host-testable; the requester itself is
 * Sys_MessageBox. Lines are short enough for the 400-pixel requester.
 */
#ifndef OPENCMR2_AMIGA_SAVE_NOTICE_H
#define OPENCMR2_AMIGA_SAVE_NOTICE_H

#include "port/save_image_disk.h"

/* The requester text for a refused file, or NULL when there is none. */
static inline const char *AmigaSaveNotice_Text(SavedImageStatus status)
{
    switch (status) {
    case SAVED_IMAGE_FOREIGN:
        return "Saved games from the PC version\n"
               "cannot be used by the Amiga version.\n"
               "They were ignored and left untouched.\n"
               "Save a new game to create an Amiga save.";
    case SAVED_IMAGE_UNSUPPORTED:
        return "A saved game was made by a newer\n"
               "version of OpenCMR2 and was ignored.\n"
               "Update OpenCMR2 to use it.";
    case SAVED_IMAGE_CORRUPT:
        return "A saved game is damaged and was\n"
               "ignored. It was left untouched.";
    default:
        return NULL;
    }
}

/* Each kind of refusal is announced once per run, so a drawer full of PC
   saves does not raise a requester for every file and every menu visit. */
static inline int AmigaSaveNotice_ShouldShow(SavedImageStatus status, unsigned *announced)
{
    if (AmigaSaveNotice_Text(status) == NULL)
        return 0;
    const unsigned bit = 1u << (unsigned)status;
    if ((*announced & bit) != 0)
        return 0;
    *announced |= bit;
    return 1;
}

#endif
