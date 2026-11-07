/*
 * Bink video decoder
 * Copyright (c) 2009 Konstantin Shishkov
 * Copyright (C) 2011 Peter Ross <pross@xvid.org>
 * Audio format reference: Copyright (c) 2007-2011 Peter Ross,
 * Copyright (c) 2009 Daniel Verkamp (libavcodec/binkaudio.c).
 *
 * Adapted for OpenCMR2 from FFmpeg n6.1 (libavcodec/bink.c).
 * Standalone BIKi implementation; no FFmpeg library dependency.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "bink_decoder.h"
#include "bink_tables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace Bink {
namespace {
// The decoder is built without exceptions. Failure is sticky: the first message
// is kept, later reads return zero and never touch memory past the buffer, and
// callers check Failure() at their existing check points and stop.
class Bits {
    const uint8_t *data;
    size_t size, position = 0;
    const char *failure = nullptr;
public:
    Bits(const uint8_t *bytes, size_t count) : data(bytes), size(count * 8) {}
    size_t Left() const { return size - position; }
    const char *Failure() const { return failure; }
    // Records the failure (the first one wins) and returns false.
    bool Fail(const char *message) {
        if (!failure) failure = message;
        return false;
    }
    uint32_t Read(unsigned count) {
        if (count > 32 || count > Left()) {
            Fail("truncated Bink bitstream");
            position = size;
            return 0;
        }
        uint32_t value = 0;
        for (unsigned shift = 0; shift < count;) {
            unsigned take = std::min(8u - unsigned(position & 7), count - shift);
            value |= uint32_t((data[position / 8] >> (position & 7)) & ((1u << take) - 1)) << shift;
            position += take;
            shift += take;
        }
        return value;
    }
    void Align() { Read(unsigned((-position) & 31)); }
};

int Log2(unsigned value) { int n = 0; while (value >>= 1) ++n; return n; }
uint32_t LE32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

enum Sources {
    BINK_SRC_BLOCK_TYPES, BINK_SRC_SUB_BLOCK_TYPES, BINK_SRC_COLORS,
    BINK_SRC_PATTERN, BINK_SRC_X_OFF, BINK_SRC_Y_OFF,
    BINK_SRC_INTRA_DC, BINK_SRC_INTER_DC, BINK_SRC_RUN, BINK_NB_SRC
};
enum BlockTypes {
    SKIP_BLOCK, SCALED_BLOCK, MOTION_BLOCK, RUN_BLOCK, RESIDUE_BLOCK,
    INTRA_BLOCK, FILL_BLOCK, INTER_BLOCK, PATTERN_BLOCK, RAW_BLOCK
};
struct Tree { unsigned vlc_num; uint8_t syms[16]; };
struct Bundle {
    int len;
    Tree tree;
    uint8_t *data, *data_end, *cur_dec, *cur_ptr;
};
struct Plane {
    int stride = 0, rows = 0;
    std::vector<uint8_t> current, previous;
    void Init(unsigned w, unsigned h) {
        stride = (w + 15) & ~15u;
        rows = (h + 15) & ~15u;
        current.assign(size_t(stride) * rows, 0);
        previous = current;
    }
};
struct VideoState {
    unsigned width = 0, height = 0;
    bool alpha = false;
    Plane planes[4];
    Bundle bundle[BINK_NB_SRC] = {};
    std::vector<uint8_t> bundleStorage;
    Tree col_high[16];
    int col_lastval;
    unsigned mbColumns = 0, mbRows = 0;
    bool first = true;
    std::vector<uint8_t> changed; // one byte per 16x16 macroblock
    void Init(unsigned w, unsigned h, bool hasAlpha) {
        width = w; height = h; alpha = hasAlpha;
        mbColumns = (w + 15) / 16; mbRows = (h + 15) / 16;
        changed.assign(size_t(mbColumns) * mbRows, 1);
        for (int i = 0; i < (alpha ? 4 : 3); ++i)
            planes[i].Init((i == 1 || i == 2) ? (w + 1) / 2 : w,
                           (i == 1 || i == 2) ? (h + 1) / 2 : h);
        size_t bytes = size_t((w + 7) / 8) * ((h + 7) / 8) * 64;
        bundleStorage.resize(bytes * BINK_NB_SRC);
        for (int i = 0; i < BINK_NB_SRC; ++i) {
            bundle[i].data = bundleStorage.data() + bytes * i;
            bundle[i].data_end = bundle[i].data + bytes;
        }
    }
};

uint8_t Huff(Bits *bits, const Tree &tree) {
    if (!tree.vlc_num)
        return tree.syms[bits->Read(4)];
    unsigned code = 0;
    for (unsigned length = 1; length <= 7; ++length) {
        code |= bits->Read(1) << (length - 1);
        for (int symbol = 0; symbol < 16; ++symbol)
            if (bink_tree_lens[tree.vlc_num][symbol] == length &&
                bink_tree_bits[tree.vlc_num][symbol] == code)
                return tree.syms[symbol];
    }
    bits->Fail("invalid Bink Huffman code");
    return 0;
}
#define GET_HUFF(gb, tree) Huff(gb, tree)

static void init_lengths(VideoState *c, int width, int bw)
{
    width = ((width + 7) & ~7);

    c->bundle[BINK_SRC_BLOCK_TYPES].len = Log2((width >> 3) + 511) + 1;

    c->bundle[BINK_SRC_SUB_BLOCK_TYPES].len = Log2((width >> 4) + 511) + 1;

    c->bundle[BINK_SRC_COLORS].len = Log2(bw*64 + 511) + 1;

    c->bundle[BINK_SRC_INTRA_DC].len =
    c->bundle[BINK_SRC_INTER_DC].len =
    c->bundle[BINK_SRC_X_OFF].len =
    c->bundle[BINK_SRC_Y_OFF].len = Log2((width >> 3) + 511) + 1;

    c->bundle[BINK_SRC_PATTERN].len = Log2((bw << 3) + 511) + 1;

    c->bundle[BINK_SRC_RUN].len = Log2(bw*48 + 511) + 1;
}

static void merge(Bits *gb, uint8_t *dst, uint8_t *src, int size)
{
    uint8_t *src2 = src + size;
    int size2 = size;

    do {
        if (!gb->Read(1)) {
            *dst++ = *src++;
            size--;
        } else {
            *dst++ = *src2++;
            size2--;
        }
    } while (size && size2);

    while (size--)
        *dst++ = *src++;
    while (size2--)
        *dst++ = *src2++;
}

static int read_tree(Bits *gb, Tree *tree)
{
    uint8_t tmp1[16] = { 0 }, tmp2[16], *in = tmp1, *out = tmp2;
    int i, t, len;

    if (gb->Left() < 4)
        return -1;

    tree->vlc_num = gb->Read(4);
    if (!tree->vlc_num) {
        for (i = 0; i < 16; i++)
            tree->syms[i] = i;
        return 0;
    }
    if (gb->Read(1)) {
        len = gb->Read(3);
        for (i = 0; i <= len; i++) {
            tree->syms[i] = gb->Read(4);
            tmp1[tree->syms[i]] = 1;
        }
        for (i = 0; i < 16 && len < 16 - 1; i++)
            if (!tmp1[i])
                tree->syms[++len] = i;
    } else {
        len = gb->Read(2);
        for (i = 0; i < 16; i++)
            in[i] = i;
        for (i = 0; i <= len; i++) {
            int size = 1 << i;
            for (t = 0; t < 16; t += size << 1)
                merge(gb, out + t, in + t, size);
            std::swap(in, out);
        }
        memcpy(tree->syms, in, 16);
    }
    return 0;
}

static int read_bundle(Bits *gb, VideoState *c, int bundle_num)
{
    int i;

    if (bundle_num == BINK_SRC_COLORS) {
        for (i = 0; i < 16; i++) {
            int ret = read_tree(gb, &c->col_high[i]);
            if (ret < 0)
                return ret;
        }
        c->col_lastval = 0;
    }
    if (bundle_num != BINK_SRC_INTRA_DC && bundle_num != BINK_SRC_INTER_DC) {
        int ret = read_tree(gb, &c->bundle[bundle_num].tree);
        if (ret < 0)
            return ret;
    }
    c->bundle[bundle_num].cur_dec =
    c->bundle[bundle_num].cur_ptr = c->bundle[bundle_num].data;

    return 0;
}

#define CHECK_READ_VAL(gb, b, t) \
    if (!b->cur_dec || (b->cur_dec > b->cur_ptr)) \
        return 0; \
    t = gb->Read(b->len); \
    if (!t) { \
        b->cur_dec = NULL; \
        return 0; \
    } \

static int read_runs(Bits *gb, Bundle *b)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    if (t > b->data_end - b->cur_dec) {
        return -1;
    }
    dec_end = b->cur_dec + t;
    if (gb->Left() < 1)
        return -1;
    if (gb->Read(1)) {
        v = gb->Read(4);
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end)
            *b->cur_dec++ = GET_HUFF(gb, b->tree);
    }
    return 0;
}

static int read_motion_values(Bits *gb, Bundle *b)
{
    int t, sign, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    if (t > b->data_end - b->cur_dec) {
        return -1;
    }
    dec_end = b->cur_dec + t;
    if (gb->Left() < 1)
        return -1;
    if (gb->Read(1)) {
        v = gb->Read(4);
        if (v) {
            sign = -gb->Read(1);
            v = (v ^ sign) - sign;
        }
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            v = GET_HUFF(gb, b->tree);
            if (v) {
                sign = -gb->Read(1);
                v = (v ^ sign) - sign;
            }
            *b->cur_dec++ = v;
        }
    }
    return 0;
}

static const uint8_t bink_rlelens[4] = { 4, 8, 12, 32 };

static int read_block_types(Bits *gb, Bundle *b)
{
    int t, v;
    int last = 0;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    if (t > b->data_end - b->cur_dec) {
        return -1;
    }
    dec_end = b->cur_dec + t;
    if (gb->Left() < 1)
        return -1;
    if (gb->Read(1)) {
        v = gb->Read(4);
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            v = GET_HUFF(gb, b->tree);
            if (v < 12) {
                last = v;
                *b->cur_dec++ = v;
            } else {
                int run = bink_rlelens[v - 12];

                if (dec_end - b->cur_dec < run)
                    return -1;
                memset(b->cur_dec, last, run);
                b->cur_dec += run;
            }
        }
    }
    return 0;
}

static int read_patterns(Bits *gb, Bundle *b)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    if (t > b->data_end - b->cur_dec) {
        return -1;
    }
    dec_end = b->cur_dec + t;
    while (b->cur_dec < dec_end) {
        if (gb->Left() < 2)
            return -1;
        v  = GET_HUFF(gb, b->tree);
        v |= GET_HUFF(gb, b->tree) << 4;
        *b->cur_dec++ = v;
    }

    return 0;
}

static int read_colors(Bits *gb, Bundle *b, VideoState *c)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    if (t > b->data_end - b->cur_dec) {
        return -1;
    }
    dec_end = b->cur_dec + t;
    if (gb->Left() < 1)
        return -1;
    if (gb->Read(1)) {
        c->col_lastval = GET_HUFF(gb, c->col_high[c->col_lastval]);
        v = GET_HUFF(gb, b->tree);
        v = (c->col_lastval << 4) | v;
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            if (gb->Left() < 2)
                return -1;
            c->col_lastval = GET_HUFF(gb, c->col_high[c->col_lastval]);
            v = GET_HUFF(gb, b->tree);
            v = (c->col_lastval << 4) | v;
            *b->cur_dec++ = v;
        }
    }
    return 0;
}

// DC values are native-endian int16 in the byte storage of the bundle.
static inline void store_dc(uint8_t **dst, int value)
{
    int16_t v = int16_t(value);
    memcpy(*dst, &v, sizeof(v));
    *dst += sizeof(v);
}

static int read_dcs(Bits *gb, Bundle *b,
                    int start_bits, int has_sign)
{
    int i, j, len, len2, bsize, sign, v, v2;
    uint8_t *dst           = b->cur_dec;
    const uint8_t *dst_end = b->data_end;

    CHECK_READ_VAL(gb, b, len);
    if (gb->Left() < unsigned(start_bits - has_sign))
        return -1;
    v = gb->Read(start_bits - has_sign);
    if (v && has_sign) {
        sign = -gb->Read(1);
        v = (v ^ sign) - sign;
    }
    if (dst_end - dst < 2)
        return -1;
    store_dc(&dst, v);
    len--;
    for (i = 0; i < len; i += 8) {
        len2 = std::min(len - i, 8);
        if (dst_end - dst < 2 * len2)
            return -1;
        bsize = gb->Read(4);
        if (bsize) {
            for (j = 0; j < len2; j++) {
                v2 = gb->Read(bsize);
                if (v2) {
                    sign = -gb->Read(1);
                    v2 = (v2 ^ sign) - sign;
                }
                v += v2;
                store_dc(&dst, v);
                if (v < -32768 || v > 32767) {
                    return -1;
                }
            }
        } else {
            for (j = 0; j < len2; j++)
                store_dc(&dst, v);
        }
    }

    b->cur_dec = dst;
    return 0;
}

static inline int get_value(Bits *gb, VideoState *c, int bundle)
{
    Bundle &b = c->bundle[bundle];
    const int bytes = (bundle == BINK_SRC_INTRA_DC || bundle == BINK_SRC_INTER_DC) ? 2 : 1;
    if (!b.cur_dec || b.cur_dec - b.cur_ptr < bytes) {
        gb->Fail("exhausted video bundle");
        return 0;
    }

    if (bundle < BINK_SRC_X_OFF || bundle == BINK_SRC_RUN)
        return *c->bundle[bundle].cur_ptr++;
    if (bundle == BINK_SRC_X_OFF || bundle == BINK_SRC_Y_OFF)
        return (int8_t)*c->bundle[bundle].cur_ptr++;
    int16_t ret;
    memcpy(&ret, c->bundle[bundle].cur_ptr, sizeof(ret));
    c->bundle[bundle].cur_ptr += 2;
    return ret;
}

static int read_dct_coeffs(Bits *gb, int32_t block[64],
                           const uint8_t *scan, int *coef_count_,
                           int coef_idx[64], int q)
{
    int coef_list[128] = {};
    int mode_list[128] = {};
    int i, t, bits, ccoef, mode, sign;
    int list_start = 64, list_end = 64, list_pos;
    int coef_count = 0;
    int quant_idx;

    if (gb->Left() < 4)
        return -1;

    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 1;  mode_list[list_end++] = 3;
    coef_list[list_end] = 2;  mode_list[list_end++] = 3;
    coef_list[list_end] = 3;  mode_list[list_end++] = 3;

    for (bits = gb->Read(4) - 1; bits >= 0; bits--) {
        list_pos = list_start;
        while (list_pos < list_end) {
            if (!(mode_list[list_pos] | coef_list[list_pos]) || !gb->Read(1)) {
                list_pos++;
                continue;
            }
            ccoef = coef_list[list_pos];
            mode  = mode_list[list_pos];
            switch (mode) {
            case 0:
                coef_list[list_pos] = ccoef + 4;
                mode_list[list_pos] = 1;
            case 2:
                if (mode == 2) {
                    coef_list[list_pos]   = 0;
                    mode_list[list_pos++] = 0;
                }
                for (i = 0; i < 4; i++, ccoef++) {
                    if (gb->Read(1)) {
                        if (list_start <= 0) {
                            gb->Fail("coefficient list overflow");
                            return -1;
                        }
                        coef_list[--list_start] = ccoef;
                        mode_list[  list_start] = 3;
                    } else {
                        if (!bits) {
                            t = 1 - (gb->Read(1) << 1);
                        } else {
                            t = gb->Read(bits) | 1 << bits;
                            sign = -gb->Read(1);
                            t = (t ^ sign) - sign;
                        }
                        if (ccoef < 0 || ccoef >= 64 || coef_count >= 64) {
                            gb->Fail("DCT coefficient overflow");
                            return -1;
                        }
                        block[scan[ccoef]] = t;
                        coef_idx[coef_count++] = ccoef;
                    }
                }
                break;
            case 1:
                if (list_end > 128 - 3) {
                    gb->Fail("coefficient list overflow");
                    return -1;
                }
                mode_list[list_pos] = 2;
                for (i = 0; i < 3; i++) {
                    ccoef += 4;
                    coef_list[list_end]   = ccoef;
                    mode_list[list_end++] = 2;
                }
                break;
            case 3:
                if (!bits) {
                    t = 1 - (gb->Read(1) << 1);
                } else {
                    t = gb->Read(bits) | 1 << bits;
                    sign = -gb->Read(1);
                    t = (t ^ sign) - sign;
                }
                if (ccoef < 0 || ccoef >= 64 || coef_count >= 64) {
                    gb->Fail("DCT coefficient overflow");
                    return -1;
                }
                block[scan[ccoef]] = t;
                coef_idx[coef_count++] = ccoef;
                coef_list[list_pos]   = 0;
                mode_list[list_pos++] = 0;
                break;
            }
        }
    }

    if (q == -1) {
        quant_idx = gb->Read(4);
    } else {
        quant_idx = q;
        if (unsigned(quant_idx) > 15) {
            return -1;
        }
    }

    *coef_count_ = coef_count;

    return quant_idx;
}

static void unquantize_dct_coeffs(int32_t block[64], const int32_t quant[64],
                                  int coef_count, int coef_idx[64],
                                  const uint8_t *scan)
{
    int i;
    block[0] = int32_t(uint32_t(block[0]) * uint32_t(quant[0])) >> 11;
    for (i = 0; i < coef_count; i++) {
        int idx = coef_idx[i];
        block[scan[idx]] = int32_t(uint32_t(block[scan[idx]]) * uint32_t(quant[idx])) >> 11;
    }
}

static int read_residue(Bits *gb, int16_t block[64], int masks_count)
{
    int coef_list[128] = {};
    int mode_list[128] = {};
    int i, sign, mask, ccoef, mode;
    int list_start = 64, list_end = 64, list_pos;
    int nz_coeff[64] = {};
    int nz_coeff_count = 0;

    coef_list[list_end] =  4; mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] =  0; mode_list[list_end++] = 2;

    for (mask = 1 << gb->Read(3); mask; mask >>= 1) {
        for (i = 0; i < nz_coeff_count; i++) {
            if (!gb->Read(1))
                continue;
            if (block[nz_coeff[i]] < 0)
                block[nz_coeff[i]] -= mask;
            else
                block[nz_coeff[i]] += mask;
            masks_count--;
            if (masks_count < 0)
                return 0;
        }
        list_pos = list_start;
        while (list_pos < list_end) {
            if (!(coef_list[list_pos] | mode_list[list_pos]) || !gb->Read(1)) {
                list_pos++;
                continue;
            }
            ccoef = coef_list[list_pos];
            mode  = mode_list[list_pos];
            switch (mode) {
            case 0:
                coef_list[list_pos] = ccoef + 4;
                mode_list[list_pos] = 1;
            case 2:
                if (mode == 2) {
                    coef_list[list_pos]   = 0;
                    mode_list[list_pos++] = 0;
                }
                for (i = 0; i < 4; i++, ccoef++) {
                    if (gb->Read(1)) {
                        if (list_start <= 0) {
                            gb->Fail("coefficient list overflow");
                            return -1;
                        }
                        coef_list[--list_start] = ccoef;
                        mode_list[  list_start] = 3;
                    } else {
                        if (ccoef < 0 || ccoef >= 64 || nz_coeff_count >= 64) {
                            gb->Fail("residue coefficient overflow");
                            return -1;
                        }
                        nz_coeff[nz_coeff_count++] = bink_scan[ccoef];
                        sign = -gb->Read(1);
                        block[bink_scan[ccoef]] = (mask ^ sign) - sign;
                        masks_count--;
                        if (masks_count < 0)
                            return 0;
                    }
                }
                break;
            case 1:
                if (list_end > 128 - 3) {
                    gb->Fail("coefficient list overflow");
                    return -1;
                }
                mode_list[list_pos] = 2;
                for (i = 0; i < 3; i++) {
                    ccoef += 4;
                    coef_list[list_end]   = ccoef;
                    mode_list[list_end++] = 2;
                }
                break;
            case 3:
                if (ccoef < 0 || ccoef >= 64 || nz_coeff_count >= 64) {
                    gb->Fail("residue coefficient overflow");
                    return -1;
                }
                nz_coeff[nz_coeff_count++] = bink_scan[ccoef];
                sign = -gb->Read(1);
                block[bink_scan[ccoef]] = (mask ^ sign) - sign;
                coef_list[list_pos]   = 0;
                mode_list[list_pos++] = 0;
                masks_count--;
                if (masks_count < 0)
                    return 0;
                break;
            }
        }
    }

    return 0;
}

#define A1  2896 /* (1/sqrt(2))<<12 */
#define A2  2217
#define A3  3784
#define A4 -5352

#define MUL(X,Y) ((int)((unsigned)(X) * (Y)) >> 11)

#define IDCT_TRANSFORM(dest,s0,s1,s2,s3,s4,s5,s6,s7,d0,d1,d2,d3,d4,d5,d6,d7,munge,src) {\
    const int a0 = (src)[s0] + (src)[s4]; \
    const int a1 = (src)[s0] - (src)[s4]; \
    const int a2 = (src)[s2] + (src)[s6]; \
    const int a3 = MUL(A1, (src)[s2] - (src)[s6]); \
    const int a4 = (src)[s5] + (src)[s3]; \
    const int a5 = (src)[s5] - (src)[s3]; \
    const int a6 = (src)[s1] + (src)[s7]; \
    const int a7 = (src)[s1] - (src)[s7]; \
    const int b0 = a4 + a6; \
    const int b1 = MUL(A3, a5 + a7); \
    const int b2 = MUL(A4, a5) - b0 + b1; \
    const int b3 = MUL(A1, a6 - a4) - b2; \
    const int b4 = MUL(A2, a7) + b3 - b1; \
    (dest)[d0] = munge(a0+a2   +b0); \
    (dest)[d1] = munge(a1+a3-a2+b2); \
    (dest)[d2] = munge(a1-a3+a2+b3); \
    (dest)[d3] = munge(a0-a2   -b4); \
    (dest)[d4] = munge(a0-a2   +b4); \
    (dest)[d5] = munge(a1-a3+a2-b3); \
    (dest)[d6] = munge(a1+a3-a2-b2); \
    (dest)[d7] = munge(a0+a2   -b0); \
}
/* end IDCT_TRANSFORM macro */

#define MUNGE_NONE(x) (x)
#define IDCT_COL(dest,src) IDCT_TRANSFORM(dest,0,8,16,24,32,40,48,56,0,8,16,24,32,40,48,56,MUNGE_NONE,src)

#define MUNGE_ROW(x) (((x) + 0x7F)>>8)
#define IDCT_ROW(dest,src) IDCT_TRANSFORM(dest,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,MUNGE_ROW,src)

static inline void bink_idct_col(int *dest, const int32_t *src)
{
    if ((src[8]|src[16]|src[24]|src[32]|src[40]|src[48]|src[56])==0) {
        dest[0]  =
        dest[8]  =
        dest[16] =
        dest[24] =
        dest[32] =
        dest[40] =
        dest[48] =
        dest[56] = src[0];
    } else {
        IDCT_COL(dest, src);
    }
}

static void bink_idct_c(int32_t *block)
{
    int i;
    int temp[64];

    for (i = 0; i < 8; i++)
        bink_idct_col(&temp[i], &block[i]);
    for (i = 0; i < 8; i++) {
        IDCT_ROW( (&block[8*i]), (&temp[8*i]) );
    }
}

static void bink_idct_add_c(uint8_t *dest, int linesize, int32_t *block)
{
    int i, j;

    bink_idct_c(block);
    for (i = 0; i < 8; i++, dest += linesize, block += 8)
        for (j = 0; j < 8; j++)
             dest[j] += block[j];
}

static void bink_idct_put_c(uint8_t *dest, int linesize, int32_t *block)
{
    int i;
    int temp[64];
    for (i = 0; i < 8; i++)
        bink_idct_col(&temp[i], &block[i]);
    for (i = 0; i < 8; i++) {
        IDCT_ROW( (&dest[i*linesize]), (&temp[8*i]) );
    }
}


void CopyBlock(uint8_t *dst, const uint8_t *src, int stride) {
    for (int y = 0; y < 8; ++y) std::memcpy(dst + y * stride, src + y * stride, 8);
}

// Flags the macroblocks covered by a non-skip 8x8 block at (bx, by) of plane
// `index`. A luma block lies in one macroblock; a chroma block is one
// macroblock wide. A scaled block covers 16x16 plane pixels: one luma
// macroblock, but 2x2 macroblocks of chroma. The alpha plane is not tracked.
void MarkChanged(VideoState &s, int index, int bx, int by, bool scaled) {
    if (index == 3) return;
    const bool chroma = index != 0;
    const int x0 = chroma ? bx : bx >> 1, y0 = chroma ? by : by >> 1;
    const int span = chroma && scaled ? 2 : 1;
    for (int y = y0; y < y0 + span && y < int(s.mbRows); ++y)
        for (int x = x0; x < x0 + span && x < int(s.mbColumns); ++x)
            s.changed[size_t(y) * s.mbColumns + x] = 1;
}

bool DecodePlane(VideoState &s, Bits &bits, int index, bool chroma) {
    Plane &plane = s.planes[index];
    int bw = (s.width + (chroma ? 15 : 7)) >> (chroma ? 4 : 3);
    int bh = (s.height + (chroma ? 15 : 7)) >> (chroma ? 4 : 3);
    init_lengths(&s, std::max(int(s.width >> chroma), 8), bw);
    for (int i = 0; i < BINK_NB_SRC; ++i)
        if (read_bundle(&bits, &s, i) != 0) return bits.Fail("invalid Bink bundle tree");
    auto value = [&](int source) { return get_value(&bits, &s, source); };
    for (int by = 0; by < bh; ++by) {
        if (!(read_block_types(&bits, &s.bundle[BINK_SRC_BLOCK_TYPES]) == 0 &&
              read_block_types(&bits, &s.bundle[BINK_SRC_SUB_BLOCK_TYPES]) == 0 &&
              read_colors(&bits, &s.bundle[BINK_SRC_COLORS], &s) == 0 &&
              read_patterns(&bits, &s.bundle[BINK_SRC_PATTERN]) == 0 &&
              read_motion_values(&bits, &s.bundle[BINK_SRC_X_OFF]) == 0 &&
              read_motion_values(&bits, &s.bundle[BINK_SRC_Y_OFF]) == 0 &&
              read_dcs(&bits, &s.bundle[BINK_SRC_INTRA_DC], 11, 0) == 0 &&
              read_dcs(&bits, &s.bundle[BINK_SRC_INTER_DC], 11, 1) == 0 &&
              read_runs(&bits, &s.bundle[BINK_SRC_RUN]) == 0))
            return bits.Fail("invalid Bink bundle");
        for (int bx = 0; bx < bw; ++bx) {
            int type = value(BINK_SRC_BLOCK_TYPES);
            if (bits.Failure()) return false;
            if (type == SCALED_BLOCK && ((by & 1) || (bx & 1))) {
                if (bx + 1 >= bw) return bits.Fail("scaled block outside plane");
                ++bx;
                continue;
            }
            bool scaled = type == SCALED_BLOCK;
            if (scaled) type = value(BINK_SRC_SUB_BLOCK_TYPES);
            if (scaled && !(type == RUN_BLOCK || type == INTRA_BLOCK || type == FILL_BLOCK ||
                            type == PATTERN_BLOCK || type == RAW_BLOCK))
                return bits.Fail("invalid scaled block type");
            const int stride = plane.stride;
            uint8_t *target = plane.current.data() + by * 8 * stride + bx * 8;
            uint8_t scratch[64] = {};
            uint8_t *dst = scaled ? scratch : target;
            int pitch = scaled ? 8 : stride;
            auto motion = [&] {
                int x = bx * 8 + value(BINK_SRC_X_OFF);
                int y = by * 8 + value(BINK_SRC_Y_OFF);
                if (!(x >= 0 && y >= 0 && x + 8 <= bw * 8 && y + 8 <= bh * 8)) {
                    bits.Fail("Bink motion vector outside plane");
                    return; // nothing is copied from outside the plane
                }
                CopyBlock(dst, plane.previous.data() + y * stride + x, stride);
            };
            switch (type) {
            case SKIP_BLOCK:
                CopyBlock(dst, plane.previous.data() + by * 8 * stride + bx * 8, stride);
                break;
            case MOTION_BLOCK:
                motion();
                break;
            case RESIDUE_BLOCK: {
                motion();
                int16_t block[64] = {};
                if (read_residue(&bits, block, bits.Read(7)) != 0) return bits.Fail("invalid Bink residue");
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x) dst[y * pitch + x] += block[y * 8 + x];
                break;
            }
            case INTRA_BLOCK:
            case INTER_BLOCK: {
                if (type == INTER_BLOCK) motion();
                int32_t block[64] = {};
                int count = 0, indices[64];
                block[0] = value(type == INTRA_BLOCK ? BINK_SRC_INTRA_DC : BINK_SRC_INTER_DC);
                int q = read_dct_coeffs(&bits, block, bink_scan, &count, indices, -1);
                if (q < 0 || q >= 16) return bits.Fail("invalid Bink DCT quantizer");
                unquantize_dct_coeffs(block, type == INTRA_BLOCK ? bink_intra_quant[q] : bink_inter_quant[q],
                                      count, indices, bink_scan);
                if (type == INTRA_BLOCK) bink_idct_put_c(dst, pitch, block);
                else bink_idct_add_c(dst, pitch, block);
                break;
            }
            case FILL_BLOCK: {
                uint8_t color = value(BINK_SRC_COLORS);
                for (int y = 0; y < 8; ++y) std::memset(dst + y * pitch, color, 8);
                break;
            }
            case PATTERN_BLOCK: {
                uint8_t colors[2];
                colors[0] = value(BINK_SRC_COLORS); colors[1] = value(BINK_SRC_COLORS);
                for (int y = 0; y < 8; ++y) {
                    unsigned pattern = value(BINK_SRC_PATTERN);
                    for (int x = 0; x < 8; ++x, pattern >>= 1) dst[y * pitch + x] = colors[pattern & 1];
                }
                break;
            }
            case RAW_BLOCK:
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x) dst[y * pitch + x] = value(BINK_SRC_COLORS);
                break;
            case RUN_BLOCK: {
                const uint8_t *scan = bink_patterns[bits.Read(4)];
                int position = 0;
                while (position < 63) {
                    int count = value(BINK_SRC_RUN) + 1;
                    if (count > 64 - position) {
                        bits.Fail("Bink run outside block");
                        break;
                    }
                    bool constant = bits.Read(1);
                    int color = constant ? value(BINK_SRC_COLORS) : 0;
                    for (int i = 0; i < count; ++i) {
                        unsigned pixel = scan[position++];
                        dst[(pixel >> 3) * pitch + (pixel & 7)] = constant ? color : value(BINK_SRC_COLORS);
                    }
                }
                if (position == 63) {
                    unsigned pixel = scan[position];
                    dst[(pixel >> 3) * pitch + (pixel & 7)] = value(BINK_SRC_COLORS);
                }
                break;
            }
            default: return bits.Fail("unknown Bink block type");
            }
            if (bits.Failure()) return false;
            if (type != SKIP_BLOCK) MarkChanged(s, index, bx, by, scaled);
            if (scaled) {
                if (!(bx + 1 < bw && by + 1 < bh)) return bits.Fail("scaled block outside plane");
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x) target[y * stride + x] = scratch[(y / 2) * 8 + x / 2];
                ++bx;
            }
        }
    }
    bits.Align();
    return !bits.Failure();
}

uint8_t Clamp(int value) { return uint8_t(value < 0 ? 0 : value > 255 ? 255 : value); }
void ConvertBgrx(const VideoState &s, Frame &frame) {
    frame.bgrx.resize(size_t(s.width) * s.height * 4);
    for (unsigned y = 0; y < s.height; ++y) {
        for (unsigned x = 0; x < s.width; ++x) {
            int luma = std::max(int(s.planes[0].current[y * s.planes[0].stride + x]) - 16, 0);
            int u = int(s.planes[1].current[(y / 2) * s.planes[1].stride + x / 2]) - 128;
            int v = int(s.planes[2].current[(y / 2) * s.planes[2].stride + x / 2]) - 128;
            uint8_t *dst = frame.bgrx.data() + (size_t(y) * s.width + x) * 4;
            dst[0] = Clamp((298 * luma + 516 * u + 128) >> 8);
            dst[1] = Clamp((298 * luma - 100 * u - 208 * v + 128) >> 8);
            dst[2] = Clamp((298 * luma + 409 * v + 128) >> 8);
            dst[3] = 255;
        }
    }
}

// Returns nullptr on success or the first failure message.
const char *DecodeVideo(VideoState &s, const uint8_t *data, size_t bytes, Frame &frame,
                        Planes *planes) {
    Bits bits(data, bytes);
    if (planes) frame.bgrx.clear(); // before the plane calls: no scratch register is held across them
    std::fill(s.changed.begin(), s.changed.end(), s.first ? 1 : 0);
    s.first = false;
    if (s.alpha) {
        bits.Read(32);
        if (!DecodePlane(s, bits, 3, false)) return bits.Failure();
    }
    bits.Read(32); // BIKi luma plane size; planes are word aligned.
    if (!DecodePlane(s, bits, 0, false)) return bits.Failure();
    if (!DecodePlane(s, bits, 2, true)) return bits.Failure(); // BIKi stores V before U.
    if (!DecodePlane(s, bits, 1, true)) return bits.Failure();
    if (!planes) ConvertBgrx(s, frame);
    for (Plane &plane : s.planes) plane.previous.swap(plane.current);
    if (planes) {
        // After the swap `previous` holds the frame just decoded.
        planes->y = s.planes[0].previous.data();
        planes->u = s.planes[1].previous.data();
        planes->v = s.planes[2].previous.data();
        planes->yStride = s.planes[0].stride;
        planes->cStride = s.planes[1].stride;
        planes->changed = s.changed.data();
        planes->mbColumns = s.mbColumns;
        planes->mbRows = s.mbRows;
    }
    return nullptr;
}

constexpr double Pi = 3.14159265358979323846;

// Audio decoding keeps every table and scratch buffer in AudioState, sized once
// in Init: no allocation and no cos/sin/exp/ldexp per block (on a 68040/060 the
// transcendental functions trap to the FPSP). The arithmetic is the same
// operations in the same order as the earlier std::complex code (the FFT
// twiddles are the same w *= step recurrence, only stored), so the output is
// bit-identical; docs/VIDEO.md holds the hashes.
struct AudioState {
    unsigned channels = 0, codedChannels = 0, length = 0, overlap = 0;
    bool dct = false, first = true;
    float root = 0;
    std::vector<unsigned> bands;
    std::vector<float> previous[2];
    std::vector<float> block[2];             // decoded blocks, one per coded channel
    std::vector<float> quant;                // per-band quantisers of the block being read
    std::vector<double> re, im;              // transform input/output, fftSize entries
    std::vector<double> twRe, twIm;          // FFT twiddles, stage after stage (fftSize - 1)
    std::vector<uint16_t> bitReverse;        // fftSize entries
    std::vector<double> dctRe, dctIm;        // DCT phase factors, length entries
    float quantTable[96];                    // exp(q * 0.15289...) for q < 96
    float scaleTable[32];                    // 2^(e - 23), the Float() exponent scale
    unsigned fftSize = 0;
    void Init(unsigned rate, unsigned count, bool useDct) {
        channels = count; dct = useDct; first = true;
        length = rate < 22050 ? 512 : (rate < 44100 ? 1024 : 2048);
        codedChannels = dct ? channels : 1;
        unsigned effectiveRate = rate;
        if (!dct) { length *= channels; effectiveRate *= channels; }
        overlap = length / 16;
        root = (dct ? float(length) : 2.0f) / (std::sqrt(float(length)) * 32768.0f);
        static const unsigned frequencies[] = {
            100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720,
            2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 24500
        };
        unsigned half = (effectiveRate + 1) / 2, countBands = 1;
        while (countBands < 25 && half > frequencies[countBands - 1]) ++countBands;
        bands.resize(countBands + 1); bands[0] = 2;
        for (unsigned i = 1; i < countBands; ++i) bands[i] = (frequencies[i - 1] * length / half) & ~1u;
        bands[countBands] = length;
        for (unsigned i = 0; i < codedChannels; ++i) {
            previous[i].assign(overlap, 0);
            block[i].assign(length, 0);
        }
        quant.assign(bands.size() - 1, 0);
        fftSize = dct ? length * 2 : length;
        re.assign(fftSize, 0); im.assign(fftSize, 0);
        // Bit-reversal permutation, as the in-place loop computed it per call.
        bitReverse.assign(fftSize, 0);
        for (size_t i = 1, j = 0; i < fftSize; ++i) {
            size_t bit = fftSize >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            bitReverse[i] = uint16_t(j);
        }
        // Twiddles: per stage the recurrence w = w * step, starting at 1.
        twRe.assign(fftSize, 0); twIm.assign(fftSize, 0);
        size_t entry = 0;
        for (size_t stage = 2; stage <= fftSize; stage <<= 1) {
            double angle = 2 * Pi / stage;
            double stepRe = std::cos(angle), stepIm = std::sin(angle);
            double wRe = 1, wIm = 0;
            for (size_t j = 0; j < stage / 2; ++j) {
                twRe[entry] = wRe; twIm[entry] = wIm; ++entry;
                double nextRe = wRe * stepRe - wIm * stepIm;
                double nextIm = wRe * stepIm + wIm * stepRe;
                wRe = nextRe; wIm = nextIm;
            }
        }
        if (dct) {
            dctRe.assign(length, 0); dctIm.assign(length, 0);
            for (unsigned k = 1; k < length; ++k) {
                double angle = Pi * k / (2 * length);
                dctRe[k] = std::cos(angle); dctIm[k] = std::sin(angle);
            }
        }
        for (unsigned q = 0; q < 96; ++q) quantTable[q] = std::exp(q * 0.15289164787221953823f);
        // 2^(e - 23), exactly and without libm: a power of two or its reciprocal.
        for (int e = 0; e < 32; ++e)
            scaleTable[e] = e >= 23 ? float(1u << (e - 23)) : 1.0f / float(1u << (23 - e));
    }
    float Float(Bits &bits) {
        unsigned exponent = bits.Read(5);
        float value = float(bits.Read(23)) * scaleTable[exponent]; // exact: a power-of-two scale
        return bits.Read(1) ? -value : value;
    }
    // Radix-2 inverse FFT of (re, im), unnormalised, in place.
    void InverseFFT() {
        const size_t n = fftSize;
        for (size_t i = 1; i < n; ++i) {
            size_t j = bitReverse[i];
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        size_t entry = 0;
        for (size_t stage = 2; stage <= n; stage <<= 1) {
            const size_t half = stage / 2;
            for (size_t start = 0; start < n; start += stage) {
                for (size_t j = 0; j < half; ++j) {
                    const double wRe = twRe[entry + j], wIm = twIm[entry + j];
                    const size_t lo = start + j, hi = lo + half;
                    const double oddRe = re[hi] * wRe - im[hi] * wIm;
                    const double oddIm = re[hi] * wIm + im[hi] * wRe;
                    const double evenRe = re[lo], evenIm = im[lo];
                    re[lo] = evenRe + oddRe; im[lo] = evenIm + oddIm;
                    re[hi] = evenRe - oddRe; im[hi] = evenIm - oddIm;
                }
            }
            entry += half;
        }
    }
    // Reads one transform block for a coded channel into block[ch].
    void Coefficients(Bits &bits, unsigned ch) {
        std::vector<float> &c = block[ch];
        std::fill(c.begin(), c.end(), 0.0f);
        c[0] = Float(bits) * root; c[1] = Float(bits) * root;
        for (float &q : quant) q = quantTable[std::min(bits.Read(8), 95u)] * root;
        unsigned band = 0, i = 2;
        float q = quant[0];
        static const unsigned runs[] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};
        while (i < length) {
            unsigned run = bits.Read(1) ? runs[bits.Read(4)] : 1;
            unsigned end = std::min(i + run * 8, length), width = bits.Read(4);
            if (!width) {
                i = end;
                while (bands[band] < i) q = quant[band++];
            } else {
                while (i < end) {
                    if (bands[band] == i) q = quant[band++];
                    unsigned coefficient = bits.Read(width);
                    c[i++] = coefficient ? (bits.Read(1) ? -q : q) * coefficient : 0;
                }
            }
        }
        if (dct) {
            // DCT III via the even, phase-shifted spectrum of a 2N-point FFT.
            std::fill(re.begin(), re.end(), 0.0); std::fill(im.begin(), im.end(), 0.0);
            re[0] = 2 * c[0];
            for (unsigned k = 1; k < length; ++k) {
                re[k] = double(c[k]) * dctRe[k]; im[k] = double(c[k]) * dctIm[k];
                re[2 * length - k] = re[k]; im[2 * length - k] = -im[k];
            }
        } else {
            re[0] = c[0]; im[0] = 0;
            re[length / 2] = c[1]; im[length / 2] = 0;
            for (unsigned k = 1; k < length / 2; ++k) {
                re[k] = c[2 * k]; im[k] = -double(c[2 * k + 1]);
                re[length - k] = re[k]; im[length - k] = -im[k];
            }
        }
        InverseFFT();
        float scale = dct ? 1.0f / length : 0.5f;
        for (unsigned n = 0; n < length; ++n) c[n] = re[n] * scale;
    }
    // Returns nullptr on success or the first failure message.
    const char *Decode(const uint8_t *data, size_t bytes, std::vector<float> &pcm) {
        if (bytes < 4) return "truncated Bink audio packet";
        unsigned reported = LE32(data);
        if (!(reported <= 16 * 1024 * 1024 && reported % (2 * channels) == 0))
            return "invalid Bink audio output size";
        Bits bits(data + 4, bytes - 4);
        pcm.clear();
        pcm.reserve(reported / 2 + size_t(length - overlap) * codedChannels);
        while (bits.Left()) {
            if (pcm.size() >= 8 * 1024 * 1024) return "Bink audio packet too large";
            if (dct) bits.Read(2);
            for (unsigned ch = 0; ch < codedChannels; ++ch) {
                Coefficients(bits, ch);
                if (bits.Failure()) return bits.Failure();
                std::vector<float> &samples = block[ch];
                if (!first) {
                    unsigned total = overlap * codedChannels;
                    for (unsigned i = 0; i < overlap; ++i) {
                        unsigned weight = i * codedChannels + ch;
                        samples[i] = (previous[ch][i] * (total - weight) + samples[i] * weight) / total;
                    }
                }
                std::copy(samples.end() - overlap, samples.end(), previous[ch].begin());
            }
            first = false;
            bits.Align();
            if (bits.Failure()) return bits.Failure();
            for (unsigned i = 0; i < length - overlap; ++i)
                for (unsigned ch = 0; ch < codedChannels; ++ch) {
                    if (!std::isfinite(block[ch][i])) return "invalid Bink audio sample";
                    pcm.push_back(block[ch][i]);
                }
        }
        // Last packets can carry fewer valid samples than a complete transform block.
        if (pcm.size() < reported / 2) return "Bink audio output shorter than reported";
        pcm.resize(reported / 2);
        return nullptr;
    }
};
} // namespace

struct Decoder::Impl {
    Reader reader = {};
    Info info;
    std::string error;
    VideoState video;
    AudioState audio;
    std::vector<uint32_t> offsets;
    unsigned tracks = 0, frame = 0;
    int selected = -1;
    bool ready = false;
    std::vector<uint8_t> packet;

    bool ReadAt(uint32_t offset, void *dst, uint32_t bytes) {
        return offset <= reader.size && bytes <= reader.size - offset &&
               reader.readAt(reader.context, offset, dst, bytes);
    }
    // Open and Decode return nullptr on success or the failure message.
#define REQUIRE(valid, message) do { if (!(valid)) return message; } while (0)
    const char *Open(Reader source, int track) {
        reader = source;
        REQUIRE(reader.readAt && reader.size >= 44, "truncated Bink header");
        uint8_t header[44];
        REQUIRE(ReadAt(0, header, 44), "truncated Bink file");
        REQUIRE(std::memcmp(header, "BIKi", 4) == 0, "unsupported Bink revision (expected BIKi)");
        uint64_t fileSize = uint64_t(LE32(header + 4)) + 8;
        info.frames = LE32(header + 8);
        uint32_t largest = LE32(header + 12);
        info.width = LE32(header + 20); info.height = LE32(header + 24);
        info.fpsNumerator = LE32(header + 28); info.fpsDenominator = LE32(header + 32);
        uint32_t flags = LE32(header + 36);
        tracks = LE32(header + 40);
        REQUIRE(fileSize <= reader.size && fileSize >= 44 && largest <= fileSize &&
                largest <= 64 * 1024 * 1024, "invalid Bink file size");
        REQUIRE(info.frames && info.frames <= 1000000 && tracks <= 256, "invalid Bink frame/track count");
        REQUIRE(info.width && info.height && info.width <= 4096 && info.height <= 4096 &&
                uint64_t(info.width) * info.height <= 4096 * 2160, "invalid Bink dimensions");
        REQUIRE(info.fpsNumerator && info.fpsDenominator &&
                uint64_t(info.fpsNumerator) <= uint64_t(info.fpsDenominator) * 1000 &&
                uint64_t(info.fpsDenominator) <= uint64_t(info.fpsNumerator) * 10 &&
                uint64_t(info.frames) * 1000 * info.fpsDenominator / info.fpsNumerator <= UINT32_MAX,
                "invalid Bink frame rate/duration");
        REQUIRE((flags & ~0x00100000u) == 0, "unsupported Bink video flags");
        uint64_t tableEnd = 44ull + tracks * 12ull + info.frames * 4ull;
        REQUIRE(tableEnd <= fileSize, "truncated Bink frame index");
        std::vector<uint8_t> table(size_t(tableEnd) - 44);
        REQUIRE(ReadAt(44, table.data(), uint32_t(table.size())), "truncated Bink file");
        selected = track < 0 || !tracks ? -1 : 0;
        for (unsigned i = 0; i < tracks; ++i)
            if (track >= 0 && LE32(table.data() + tracks * 8 + i * 4) == uint32_t(track)) selected = i;
        if (selected >= 0) {
            uint32_t format = LE32(table.data() + tracks * 4 + selected * 4);
            info.sampleRate = format & 65535;
            info.channels = format & 0x20000000 ? 2 : 1;
            REQUIRE(info.sampleRate >= 1000 && info.sampleRate <= 48000, "invalid Bink sample rate");
            audio.Init(info.sampleRate, info.channels, format & 0x10000000);
        }
        offsets.resize(info.frames + 1);
        for (unsigned i = 0; i < info.frames; ++i) offsets[i] = LE32(table.data() + tracks * 12 + i * 4) & ~1u;
        offsets.back() = uint32_t(fileSize);
        REQUIRE(offsets.front() >= tableEnd, "Bink frame overlaps header/index");
        for (unsigned i = 0; i < info.frames; ++i)
            REQUIRE(offsets[i + 1] > offsets[i] && offsets[i + 1] - offsets[i] <= largest,
                    "invalid Bink frame offset/size");
        video.Init(info.width, info.height, flags & 0x00100000);
        ready = true;
        return nullptr;
    }
    const char *Decode(Frame &output, Planes *planes) {
        REQUIRE(ready && frame < info.frames, "end of Bink movie");
        packet.resize(offsets[frame + 1] - offsets[frame]);
        REQUIRE(ReadAt(offsets[frame], packet.data(), uint32_t(packet.size())), "truncated Bink file");
        size_t cursor = 0;
        output.audio.clear();
        for (unsigned i = 0; i < tracks; ++i) {
            REQUIRE(packet.size() - cursor >= 4, "truncated Bink audio length");
            uint32_t bytes = LE32(packet.data() + cursor); cursor += 4;
            REQUIRE(bytes <= packet.size() - cursor && (bytes == 0 || bytes >= 4), "invalid Bink audio length");
            if (int(i) == selected && bytes) {
                const char *failure = audio.Decode(packet.data() + cursor, bytes, output.audio);
                if (failure) return failure;
            }
            cursor += bytes;
        }
        if (const char *failure = DecodeVideo(video, packet.data() + cursor, packet.size() - cursor, output, planes))
            return failure;
        ++frame;
        return nullptr;
    }
#undef REQUIRE
};

Decoder::Decoder() : impl(new Impl) {}
Decoder::~Decoder() = default;
bool Decoder::Open(Reader reader, int track) {
    impl.reset(new Impl);
    const char *failure = impl->Open(reader, track);
    if (!failure) return true;
    impl->error = failure;
    impl->ready = false;
    return false;
}
bool Decoder::Decode(Frame &frame) { return Decode(frame, nullptr); }
bool Decoder::Decode(Frame &frame, Planes *planes) {
    const char *failure = impl->Decode(frame, planes);
    if (!failure) return true;
    if (planes) *planes = Planes();
    impl->error = failure;
    impl->ready = false;
    frame.audio.clear();
    frame.bgrx.clear();
    return false;
}
const Info &Decoder::GetInfo() const { return impl->info; }
const std::string &Decoder::Error() const { return impl->error; }
} // namespace Bink
