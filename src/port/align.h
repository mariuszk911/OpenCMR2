/*
 * Alignment for structures that cross the game/executor boundary.
 *
 * Domain A is built with -malign-int, so its int and pointer alignment is 4.
 * Domain B keeps the m68k default of 2. The two domains must agree on the
 * type's alignment as well as its size and field offsets, otherwise a value
 * allocated in one domain can reach an access that assumes more alignment in
 * the other. Four bytes matches the original 32-bit layout and the host i386
 * reference. The attribute raises the minimum only; it does not change a
 * field's offset or the structure's size.
 */
#ifndef OPENCMR2_PORT_ALIGN_H
#define OPENCMR2_PORT_ALIGN_H

#if defined(__GNUC__) || defined(__clang__)
#define OPENCMR2_PORT_ALIGN4 __attribute__((aligned(4)))
#else
#define OPENCMR2_PORT_ALIGN4
#endif

#endif
