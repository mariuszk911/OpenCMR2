/* Bounds-safe qsort for the AmigaOS build (see amiga_qsort.c). */
#ifndef OPENCMR2_AMIGA_QSORT_H
#define OPENCMR2_AMIGA_QSORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sorts like qsort, stable, and touches only the nmemb elements at base
   whatever the comparator returns. The Amiga build's qsort calls it. */
void AmigaQsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif
