/*
 * qsort for the AmigaOS build, replacing libnix's.
 *
 * libnix's qsort is a quicksort whose scans stop only when the comparator
 * returns 0 for the middle element compared with itself. The game's
 * comparators come from MSVC code and several never return 0
 * (NetPlayers_ComparePlayerIDs answers -1 for equal ids). With such a
 * comparator libnix's scan runs off the array and its swaps scramble the
 * memory on both sides: sorting the two player ids [0x10001, 0x20001] when
 * the Amiga hosts a network race walked over the program's BSS, the AHI
 * sound hook among it, and the machine froze or Gurued in the AHI task
 * (docs/NETWORK.md, "Amiga host crash at race start"). The original ran on
 * MSVC's qsort and the Linux build runs on glibc's without this fault.
 *
 * This one is a top-down merge sort: halves of n / 2 and n - n / 2, and a
 * merge that takes the left element unless the comparator says it is
 * greater (> 0). It only ever reads, compares and writes elements of the
 * array and of its scratch buffer, so no comparator can make it leave them.
 * It is stable, and it makes the same comparisons and the same choices as
 * the merge sort of the glibc qsort the Linux build uses, so both builds
 * order equal and inconsistently compared elements alike (tests/amiga_qsort
 * checks it against the host's qsort).
 *
 * Scratch: sorts of up to kStackScratch bytes merge through a stack buffer
 * (every network table the game sorts); larger ones through a heap buffer
 * that grows to the largest sort seen and is kept, so a per-frame sort does
 * not allocate every frame. If that buffer cannot grow, the array is sorted
 * by a stable insertion sort of adjacent swaps (slower, still in bounds).
 * The game calls qsort from its own task only; the kept buffer is not
 * shared between tasks.
 */
#include "platform/amiga/amiga_qsort.h"

#include <stdlib.h>
#include <string.h>

typedef int (*Compare)(const void *, const void *);

enum { kStackScratch = 1024 };

static char *s_scratch;
static size_t s_scratchSize;

/* Sorts n elements at base, merging through tmp (at least n * size bytes). */
static void MergeSort(char *base, size_t n, size_t size, Compare compar, char *tmp)
{
    size_t n1, n2;
    char *b1, *b2, *out;

    if (n <= 1)
        return;
    n1 = n / 2;
    n2 = n - n1;
    b1 = base;
    b2 = base + n1 * size;
    MergeSort(b1, n1, size, compar, tmp);
    MergeSort(b2, n2, size, compar, tmp);

    out = tmp;
    while (n1 > 0 && n2 > 0) {
        if (compar(b1, b2) <= 0) {
            memcpy(out, b1, size);
            b1 += size;
            n1--;
        } else {
            memcpy(out, b2, size);
            b2 += size;
            n2--;
        }
        out += size;
    }
    /* What is left of the right half is already in place. */
    if (n1 > 0)
        memcpy(out, b1, n1 * size);
    memcpy(base, tmp, (n - n2) * size);
}

static void SwapBytes(char *a, char *b, size_t size)
{
    while (size-- > 0) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* Stable and in bounds without scratch memory. */
static void InsertionSort(char *base, size_t n, size_t size, Compare compar)
{
    size_t i, j;

    for (i = 1; i < n; i++)
        for (j = i; j > 0 && compar(base + (j - 1) * size, base + j * size) > 0; j--)
            SwapBytes(base + (j - 1) * size, base + j * size, size);
}

/* A scratch buffer of at least bytes, or NULL. */
static char *Scratch(size_t bytes)
{
    char *grown;

    if (bytes <= s_scratchSize)
        return s_scratch;
    grown = (char *)realloc(s_scratch, bytes);
    if (grown == NULL)
        return NULL;
    s_scratch = grown;
    s_scratchSize = bytes;
    return s_scratch;
}

void AmigaQsort(void *base, size_t nmemb, size_t size, Compare compar)
{
    char stackScratch[kStackScratch];
    char *tmp;
    size_t bytes;

    if (nmemb <= 1 || size == 0)
        return;
    if (nmemb > (size_t)-1 / size) {
        InsertionSort((char *)base, nmemb, size, compar);
        return;
    }
    bytes = nmemb * size;
    tmp = bytes <= sizeof(stackScratch) ? stackScratch : Scratch(bytes);
    if (tmp == NULL) {
        InsertionSort((char *)base, nmemb, size, compar);
        return;
    }
    MergeSort((char *)base, nmemb, size, compar, tmp);
}

#ifndef AMIGA_QSORT_HOST_TEST
/* Linked ahead of libnix, so libnix's qsort.o is never pulled in. */
void qsort(void *base, size_t nmemb, size_t size, Compare compar)
{
    AmigaQsort(base, nmemb, size, compar);
}
#endif
