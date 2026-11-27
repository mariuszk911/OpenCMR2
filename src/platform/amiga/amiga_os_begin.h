/*
 * Opens a block of AmigaOS headers; amiga_os_end.h closes it.
 *
 *   #include "platform/amiga/amiga_os_begin.h"
 *   #include <exec/types.h>
 *   ...
 *   #include "platform/amiga/amiga_os_end.h"
 *
 * AmigaOS structures are laid out for the 68k: 2-byte alignment. The 68k
 * compiler (Domain B, no -malign-int) does that by itself; PowerPC GCC aligns
 * to 4, so on WarpOS the block is read inside #pragma pack(push, 2). Only OS
 * headers go inside: never the program's own, nor a PPC-native library's
 * (the PPC MiniGL's structures have PPC layout).
 *
 * No include guard: every block opens it again.
 */
#if defined(OPENCMR2_WARPOS)
#pragma pack(push, 2)
#endif
