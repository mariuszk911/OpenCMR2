/*
 * ogles2.library client glue: see ogles2_glue.h.
 *
 * The behaviour follows the SDK's documented OGLES2Open (doc/ogles2.doc): try
 * the installed library, then PROGDIR:libs/ogles2.library and
 * PROGDIR:ogles2.library, taking the first that opens, is new enough and has
 * this dispatch ABI.
 */
#include "render/gles2/ogles2_glue.h"

#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/ogles2.h>

#include <string.h>

struct Library *OGLES2Base = NULL;
const struct OGLES2Dispatch *OGLES2Dispatch = NULL;
struct OGLES2Context *OGLES2CurrentContext = NULL;

/* Library vector -30: struct OGLES2Dispatch *GetDispatchTable(void), library
   base in a6, the result in d0 (no other register is an input). */
extern const struct OGLES2Dispatch *ogles2_glue_get_table(struct Library *base);
__asm__(
    ".text\n"
    ".even\n"
    ".globl _ogles2_glue_get_table\n"
    "_ogles2_glue_get_table:\n"
    "    move.l a6,-(sp)\n"
    "    move.l 8(sp),a6\n"
    "    jsr -30(a6)\n"
    "    move.l (sp)+,a6\n"
    "    rts\n");

static const char *s_error = "";
static const char *s_from = "";

const char *OGLES2OpenError(void)
{
    return s_error;
}

const char *OGLES2OpenedFrom(void)
{
    return s_from;
}

/* ULONG-sized so the table checks are in one place. */
static int table_usable(const struct OGLES2Dispatch *table)
{
    if (table == NULL) {
        s_error = "the library returned no dispatch table";
        return 0;
    }
    if (table->abiVersion != OGLES2_DISPATCH_ABI_VERSION) {
        s_error = "the library has another dispatch ABI";
        return 0;
    }
    if (table->structSize < sizeof(struct OGLES2Dispatch)) {
        s_error = "the library is older than the headers (short dispatch table)";
        return 0;
    }
    return 1;
}

static int try_open(const char *name)
{
    struct Library *base = OpenLibrary((CONST_STRPTR)name, OGLES2_VERSION);
    if (base == NULL) {
        s_error = "ogles2.library not found";
        return 0;
    }
    const struct OGLES2Dispatch *table = ogles2_glue_get_table(base);
    if (!table_usable(table)) {
        CloseLibrary(base);
        return 0;
    }
    OGLES2Base = base;
    OGLES2Dispatch = table;
    s_from = name;
    s_error = "";
    return 1;
}

BOOL OGLES2Open(void)
{
    if (OGLES2Base != NULL)
        return TRUE;
    /* A library in memory that is too old makes exec return it for any path,
       so unlike the SDK's own import library this does not try to expunge it:
       it reports the failure and the renderer falls back. */
    if (try_open("ogles2.library") || try_open("PROGDIR:libs/ogles2.library") ||
        try_open("PROGDIR:ogles2.library"))
        return TRUE;
    return FALSE;
}

void OGLES2Close(void)
{
    if (OGLES2Base == NULL)
        return;
    OGLES2CurrentContext = NULL;
    OGLES2Dispatch = NULL;
    CloseLibrary(OGLES2Base);
    OGLES2Base = NULL;
    s_from = "";
}

void *aglCreateContext2(ULONG *errcode, struct TagItem *tags)
{
    ULONG scratch;
    if (OGLES2Dispatch == NULL) {
        if (errcode != NULL)
            *errcode = OGLES2_CCE_NO_GPU;
        return NULL;
    }
    return OGLES2Dispatch->aglCreateContext2(errcode != NULL ? errcode : &scratch, tags);
}

void aglDestroyContext(void *context)
{
    if (OGLES2Dispatch == NULL)
        return;
    if (context == (void *)OGLES2CurrentContext)
        OGLES2CurrentContext = NULL;
    OGLES2Dispatch->aglDestroyContext((struct OGLES2Context *)context);
}

void aglMakeCurrent(void *context)
{
    if (OGLES2Dispatch == NULL)
        return;
    OGLES2CurrentContext = (struct OGLES2Context *)context;
    OGLES2Dispatch->aglMakeCurrent((struct OGLES2Context *)context);
}

void aglSwapBuffers(void)
{
    if (OGLES2Dispatch != NULL)
        OGLES2Dispatch->aglSwapBuffers(OGLES2CurrentContext);
}

void aglSetParams2(struct TagItem *tags)
{
    if (OGLES2Dispatch != NULL)
        OGLES2Dispatch->aglSetParams2(OGLES2CurrentContext, tags);
}

void aglSetBitmap(struct BitMap *bitmap)
{
    if (OGLES2Dispatch != NULL)
        OGLES2Dispatch->aglSetBitmap(OGLES2CurrentContext, bitmap);
}
