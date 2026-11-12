// The Amiga build's qsort (src/platform/amiga/amiga_qsort.c), compiled for the
// host under the name AmigaQsort:
// - it stays inside the array whatever the comparator returns (libnix's ran
//   off the two player ids when the Amiga hosted a network race);
// - it sorts, and it is stable;
// - on glibc it makes the same choices as the host's qsort, so the Linux and
//   Amiga builds order equal and inconsistently compared elements alike.
#include "platform/amiga/amiga_qsort.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int failures;

void Check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The array being sorted: every pointer the comparator gets must be one of
// its elements.
const unsigned char *g_lo;
const unsigned char *g_hi;
size_t g_size;
bool g_outside;

void Bounds(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    if (b < g_lo || b >= g_hi || (size_t)(b - g_lo) % g_size != 0)
        g_outside = true;
}

void Watch(const void *base, size_t n, size_t size)
{
    g_lo = (const unsigned char *)base;
    g_hi = g_lo + n * size;
    g_size = size;
    g_outside = false;
}

// The game's NetPlayers_ComparePlayerIDs: never 0.
int CompareNeverEqual(const void *a, const void *b)
{
    Bounds(a);
    Bounds(b);
    return *(const uint32_t *)a > *(const uint32_t *)b ? 1 : -1;
}

int CompareKey(const void *a, const void *b)
{
    Bounds(a);
    Bounds(b);
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

// Answers from a fixed pseudo-random sequence: as inconsistent as a
// comparator can be. Two sorts that make the same comparisons in the same
// order end with the same permutation.
uint32_t g_answers;
int CompareRandom(const void *a, const void *b)
{
    Bounds(a);
    Bounds(b);
    g_answers = g_answers * 1103515245u + 12345u;
    return (int)((g_answers >> 16) % 3) - 1;
}

uint32_t g_rng = 12345;
uint32_t Next()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

// Elements of `size` bytes: a 32-bit key first, then the original index in
// the next 4 bytes when there is room, then filler.
std::vector<unsigned char> Make(size_t n, size_t size, uint32_t keys)
{
    std::vector<unsigned char> v(n * size);
    for (size_t i = 0; i < n; i++) {
        unsigned char *e = &v[i * size];
        for (size_t j = 0; j < size; j++)
            e[j] = (unsigned char)Next();
        if (size >= 4) {
            uint32_t key = keys != 0 ? Next() % keys : Next();
            std::memcpy(e, &key, 4);
        }
        if (size >= 8) {
            uint32_t index = (uint32_t)i;
            std::memcpy(e + 4, &index, 4);
        }
    }
    return v;
}

// The libnix failure: two ids already in order, zeros around them as in BSS.
void TwoPlayerIds()
{
    uint32_t mem[16] = {};
    uint32_t *ids = mem + 8;
    ids[0] = 0x10001;
    ids[1] = 0x20001;
    Watch(ids, 2, 4);
    AmigaQsort(ids, 2, 4, CompareNeverEqual);
    Check(!g_outside, "player ids: compared outside the array");
    Check(ids[0] == 0x10001 && ids[1] == 0x20001, "player ids in order stay in order");
    bool guards = true;
    for (int i = 0; i < 16; i++)
        if (i != 8 && i != 9 && mem[i] != 0)
            guards = false;
    Check(guards, "player ids: memory around the array untouched");

    ids[0] = 0x20001;
    ids[1] = 0x10001;
    AmigaQsort(ids, 2, 4, CompareNeverEqual);
    Check(ids[0] == 0x10001 && ids[1] == 0x20001, "player ids out of order get sorted");
}

// Sorted by key, equal keys in their original order, and nothing outside the
// array touched (guard bytes on both sides).
void SortedAndStable(size_t n, size_t size, uint32_t keys)
{
    const size_t guard = 64;
    std::vector<unsigned char> data = Make(n, size, keys);
    std::vector<unsigned char> buffer(guard + data.size() + guard, 0xa5);
    std::memcpy(&buffer[guard], data.data(), data.size());
    unsigned char *base = &buffer[guard];

    Watch(base, n, size);
    AmigaQsort(base, n, size, CompareKey);
    Check(!g_outside, "sort: compared outside the array");

    bool ok = true;
    for (size_t i = 1; i < n; i++) {
        uint32_t k0, k1, i0, i1;
        std::memcpy(&k0, base + (i - 1) * size, 4);
        std::memcpy(&k1, base + i * size, 4);
        if (k0 > k1)
            ok = false;
        if (size >= 8 && k0 == k1) {
            std::memcpy(&i0, base + (i - 1) * size + 4, 4);
            std::memcpy(&i1, base + i * size + 4, 4);
            if (i0 > i1)
                ok = false;
        }
    }
    Check(ok, "sort: keys in order, equal keys in their original order");

    bool guards = true;
    for (size_t i = 0; i < guard; i++)
        if (buffer[i] != 0xa5 || buffer[guard + data.size() + i] != 0xa5)
            guards = false;
    Check(guards, "sort: guard bytes untouched");

    // The same elements, permuted: compare sorted copies of both.
    if (size >= 8) {
        std::vector<int> seen(n, 0);
        for (size_t i = 0; i < n; i++) {
            uint32_t index;
            std::memcpy(&index, base + i * size + 4, 4);
            if (index < n && std::memcmp(base + i * size, &data[index * size], size) == 0)
                seen[index]++;
        }
        bool permutation = true;
        for (size_t i = 0; i < n; i++)
            if (seen[i] != 1)
                permutation = false;
        Check(permutation, "sort: the result is a permutation of the input");
    }
}

// Any comparator, however inconsistent, keeps every access in bounds.
void InBounds(size_t n, size_t size, int (*compare)(const void *, const void *))
{
    std::vector<unsigned char> v = Make(n, size, 4);
    g_answers = 99;
    Watch(v.data(), n, size);
    AmigaQsort(v.data(), n, size, compare);
    Check(!g_outside, "inconsistent comparator: compared outside the array");
}

#if defined(__GLIBC__)
// The host's qsort and AmigaQsort end with the same bytes.
void SameAsHost(size_t n, size_t size, uint32_t keys, int (*compare)(const void *, const void *))
{
    std::vector<unsigned char> a = Make(n, size, keys);
    std::vector<unsigned char> b = a;
    g_answers = 7;
    Watch(a.data(), n, size);
    std::qsort(a.data(), n, size, compare);
    g_answers = 7;
    Watch(b.data(), n, size);
    AmigaQsort(b.data(), n, size, compare);
    char what[96];
    std::snprintf(what, sizeof(what), "same order as the host's qsort (n %zu, size %zu)", n, size);
    Check(a == b, what);
}
#endif

} // namespace

int main()
{
    TwoPlayerIds();

    const size_t counts[] = { 0, 1, 2, 3, 7, 8, 32, 100, 1000, 5000 };
    const size_t sizes[] = { 4, 8, 0x18, 0x1c, 40 };
    for (size_t n : counts)
        for (size_t size : sizes) {
            SortedAndStable(n, size, 0);
            SortedAndStable(n, size, 5);
            InBounds(n, size, CompareNeverEqual);
            InBounds(n, size, CompareRandom);
        }
    // Elements smaller than a key: only the bounds matter.
    for (size_t n : counts) {
        std::vector<unsigned char> v = Make(n, 3, 0);
        g_answers = 3;
        Watch(v.data(), n, 3);
        AmigaQsort(v.data(), n, 3, CompareRandom);
        Check(!g_outside, "3-byte elements: compared outside the array");
    }

#if defined(__GLIBC__)
    for (size_t n : counts)
        for (size_t size : sizes) {
            SameAsHost(n, size, 5, CompareKey);
            SameAsHost(n, size, 5, CompareNeverEqual);
            SameAsHost(n, size, 0, CompareRandom);
        }
#endif

    if (failures != 0) {
        std::fprintf(stderr, "amiga_qsort: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("amiga_qsort: OK\n");
    return 0;
}
