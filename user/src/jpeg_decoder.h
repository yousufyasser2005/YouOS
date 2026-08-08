/*
 * Baseline (sequential DCT, Huffman) JPEG decoder — same shape as
 * png_decoder.h: chunk/marker parsing + a tested transform core, built
 * to the same "read whole file, decode into caller's out_rgba buffer"
 * convention.
 *
 * Supports: baseline sequential DCT (SOF0), 8-bit precision, 1
 * (grayscale) or 3 (YCbCr) component images, chroma subsampling
 * 4:4:4/4:2:2/4:2:0 (any Hi/Vi 1..4), Huffman coding, restart markers.
 * Deliberately NOT supported (same spirit as PNG's documented gaps):
 * progressive (SOF2), lossless, arithmetic coding, extended (SOF1) —
 * rejected with a clear reason rather than silently producing garbage.
 *
 * u8/u16/u32/u64/s64 are already typedef'd in desktop.c before this
 * file is #included — do NOT redefine them here (same rule inflate.h
 * follows). s32 comes from inflate.h, which we pull in explicitly so
 * this file doesn't depend on include order.
 *
 * No memset/memcpy anywhere: this codebase has no libc, and even an
 * innocent `struct x = {0};` can get lowered to an implicit memset call
 * by the compiler at -O1/-O2 in a freestanding build — a link error
 * with nothing to catch it until final link. Every zero-fill/copy here
 * is an explicit loop, matching png_decoder.h's own convention.
 *
 * Self-tested in a host sandbox against real Pillow-produced JPEGs
 * (49 fixtures across 4:4:4/4:2:2/4:2:0/grayscale, sizes 8x8 up to
 * non-MCU-aligned dimensions, plus restart markers, progressive
 * rejection, and truncated/garbage input) before being handed off for
 * the freestanding build. IDCT is fixed-point/integer-only (no FPU/SSE
 * dependency — this kernel has no CR0/clts/#NM handling).
 */
#ifndef JPEG_DECODER_H
#define JPEG_DECODER_H

#include "inflate.h" /* for s32; see note above */

#define JPEG_MAX_W 1024
#define JPEG_MAX_H 768

/* Worst case is 4:2:0: component base planes (luma + 2 quarter-size
 * chroma) = 1.5*W*H, plus per-chroma vertical-upsample intermediate
 * (0.5*W*H each, x2) plus horizontal-upsample final (W*H each, x2) =
 * 1.5WH + WH + 2WH = 4.5*W*H. JPEG_MAX_W/H are both multiples of 16
 * (the largest possible MCU size), so no image within bounds can pad
 * past this — the formula is a true, non-approximate upper bound, not
 * a margin estimate. +4096 is just bookkeeping slack; the allocator
 * below is bounds-checked regardless, so any shortfall fails cleanly
 * with JPEG_ERR_NOMEM rather than overrunning the buffer. */
#define JPEG_SCRATCH_BYTES (9 * JPEG_MAX_W * JPEG_MAX_H / 2 + 4096)

typedef struct {
    u32 width, height;
    u8 error; /* 0 = ok, nonzero = see jpeg_error_str() */
} jpeg_info_t;

enum {
    JPEG_OK = 0,
    JPEG_ERR_BADMARKER,
    JPEG_ERR_TRUNCATED,
    JPEG_ERR_UNSUPPORTED,      /* progressive / arithmetic / SOF1 etc */
    JPEG_ERR_TOO_LARGE,
    JPEG_ERR_BAD_HUFFMAN,
    JPEG_ERR_BAD_COMPONENTS,
    JPEG_ERR_NOMEM,            /* scratch buffer too small (shouldn't
                                 * happen within JPEG_MAX_W/H bounds) */
};

static const char* jpeg_error_str(u8 e) {
    switch (e) {
        case JPEG_OK: return "ok";
        case JPEG_ERR_BADMARKER: return "not a JPEG file (bad marker)";
        case JPEG_ERR_TRUNCATED: return "file truncated / not enough data";
        case JPEG_ERR_UNSUPPORTED: return "unsupported JPEG type (need baseline sequential)";
        case JPEG_ERR_TOO_LARGE: return "image too large for viewer";
        case JPEG_ERR_BAD_HUFFMAN: return "bad Huffman data (corrupt file?)";
        case JPEG_ERR_BAD_COMPONENTS: return "unsupported component layout";
        case JPEG_ERR_NOMEM: return "out of scratch memory";
        default: return "unknown error";
    }
}

/* ============================= internals ============================= */

#define JPEG__MAXCOMP 4

typedef struct {
    u8 bits[17];                /* bits[i] = # codes of length i (1..16) */
    u8 vals[256];
    int mincode[17];
    int maxcode[17];
    int valptr[17];
} jpeg__huff_t;

typedef struct {
    int id;
    int h, v;                   /* sampling factors */
    int tq;                     /* quant table index */
    int td, ta;                 /* huffman table indices (DC, AC) */
    int dc_pred;
} jpeg__comp_t;

typedef struct {
    const u8* data;
    u32 len;
    u32 pos;

    u32 width, height;
    int ncomp;
    jpeg__comp_t comp[JPEG__MAXCOMP];
    int max_h, max_v;

    u16 qtab[4][64];
    jpeg__huff_t huff_dc[4];
    jpeg__huff_t huff_ac[4];
    int huff_dc_present[4];
    int huff_ac_present[4];

    int restart_interval;

    u32 bitbuf;
    int bitcnt;
    int marker_hit;             /* set when a real 0xFF marker (not a
                                  * stuffed 0xFF00) is seen while filling */

    /* bump allocator into caller-supplied scratch — no free(), the
     * caller reclaims it in bulk after decode returns */
    u8* scratch;
    u32 scratch_cap;
    u32 scratch_used;
} jpeg__ctx_t;

static u8* jpeg__alloc(jpeg__ctx_t* c, u32 size) {
    if (c->scratch_used + size > c->scratch_cap) return 0;
    u8* p = c->scratch + c->scratch_used;
    c->scratch_used += size;
    return p;
}

/* ---------------------------- byte/bit IO ---------------------------- */

static int jpeg__byte(jpeg__ctx_t* c) {
    if (c->pos >= c->len) return -1;
    return c->data[c->pos++];
}

static int jpeg__u16(jpeg__ctx_t* c) {
    int hi = jpeg__byte(c);
    int lo = jpeg__byte(c);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 8) | lo;
}

/* Fill bit buffer, handling 0xFF00 byte-stuffing. Stops (without
 * consuming) at a real marker. */
static void jpeg__bit_fill(jpeg__ctx_t* c) {
    while (c->bitcnt <= 24) {
        if (c->marker_hit || c->pos >= c->len) {
            c->bitbuf |= (0xFFu << (24 - c->bitcnt));
            c->bitcnt += 8;
            continue;
        }
        u8 b = c->data[c->pos];
        if (b == 0xFF) {
            if (c->pos + 1 < c->len) {
                u8 b2 = c->data[c->pos + 1];
                if (b2 == 0x00) {
                    c->pos += 2;
                    c->bitbuf |= ((u32)b << (24 - c->bitcnt));
                    c->bitcnt += 8;
                    continue;
                } else {
                    c->marker_hit = 1;
                    continue;
                }
            } else {
                c->marker_hit = 1;
                continue;
            }
        }
        c->pos++;
        c->bitbuf |= ((u32)b << (24 - c->bitcnt));
        c->bitcnt += 8;
    }
}

static int jpeg__get_bit(jpeg__ctx_t* c) {
    if (c->bitcnt <= 0) jpeg__bit_fill(c);
    if (c->bitcnt <= 0) return 0;
    int bit = (c->bitbuf >> 31) & 1;
    c->bitbuf <<= 1;
    c->bitcnt--;
    return bit;
}

static int jpeg__get_bits(jpeg__ctx_t* c, int n) {
    if (n == 0) return 0;
    if (c->bitcnt < n) jpeg__bit_fill(c);
    int v = (int)(c->bitbuf >> (32 - n));
    c->bitbuf <<= n;
    c->bitcnt -= n;
    if (c->bitcnt < 0) c->bitcnt = 0;
    return v;
}

static int jpeg__extend(int v, int t) {
    if (t == 0) return 0;
    int vt = 1 << (t - 1);
    if (v < vt) return v - (1 << t) + 1;
    return v;
}

/* ---------------------------- Huffman decode ---------------------------- */

static void jpeg__build_huff(jpeg__huff_t* h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        if (h->bits[l] == 0) {
            h->valptr[l] = 0;
            h->mincode[l] = 0;
            h->maxcode[l] = -1;
        } else {
            h->valptr[l] = k;
            h->mincode[l] = code;
            code += h->bits[l];
            k += h->bits[l];
            h->maxcode[l] = code - 1;
        }
        code <<= 1;
    }
}

static int jpeg__decode_huff(jpeg__ctx_t* c, jpeg__huff_t* h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | jpeg__get_bit(c);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) {
            int idx = h->valptr[l] + (code - h->mincode[l]);
            if (idx < 0 || idx > 255) return -1;
            return h->vals[idx];
        }
    }
    return -1;
}

/* ---------------------------- IDCT (fixed-point, integer-only) ---------------------------- */

/* No FPU/SSE dependency — see file header. Cu(u)*cos((2x+1)u*pi/16) is
 * baked into a Q13 fixed-point table (SCALE=8192), computed once at
 * build time (not called at runtime, since this environment has no
 * libm either). Accumulates in s64 to avoid overflow (block values can
 * reach ~2^19 after dequantization; two passes against a 13-bit table
 * need > 32 bits of headroom). Final descale is an exact power-of-2
 * shift (SCALE^2*4 == 1<<28) — a plain arithmetic shift with a
 * rounding bias, no division. */
#define JPEG__IDCT_DESCALE_BITS 28

static const int jpeg__cosb_fx[8][8] = {
    {5793, 8035, 7568, 6811, 5793, 4551, 3135, 1598},
    {5793, 6811, 3135, -1598, -5793, -8035, -7568, -4551},
    {5793, 4551, -3135, -8035, -5793, 1598, 7568, 6811},
    {5793, 1598, -7568, -4551, 5793, 6811, -3135, -8035},
    {5793, -1598, -7568, 4551, 5793, -6811, -3135, 8035},
    {5793, -4551, -3135, 8035, -5793, -1598, 7568, -6811},
    {5793, -6811, 3135, 1598, -5793, 8035, -7568, 4551},
    {5793, -8035, 7568, -6811, 5793, -4551, 3135, -1598},
};

static void jpeg__idct_8x8(const int* in, u8* out, int stride) {
    s64 tmp[64];
    for (int v = 0; v < 8; v++) {
        for (int x = 0; x < 8; x++) {
            s64 sum = 0;
            for (int u = 0; u < 8; u++)
                sum += (s64)in[v * 8 + u] * jpeg__cosb_fx[x][u];
            tmp[v * 8 + x] = sum;
        }
    }
    const s64 bias = (s64)1 << (JPEG__IDCT_DESCALE_BITS - 1);
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            s64 sum = 0;
            for (int v = 0; v < 8; v++)
                sum += tmp[v * 8 + x] * jpeg__cosb_fx[y][v];
            int iv = (int)(((sum + bias) >> JPEG__IDCT_DESCALE_BITS) + 128);
            if (iv < 0) iv = 0;
            if (iv > 255) iv = 255;
            out[y * stride + x] = (u8)iv;
        }
    }
}

static const int jpeg__zigzag[64] = {
     0, 1, 8,16, 9, 2, 3,10,
    17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,
    27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,
    29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,
    53,60,61,54,47,55,62,63
};

/* ---------------------------- marker segment parsing ---------------------------- */

static int jpeg__read_dqt(jpeg__ctx_t* c, int seglen) {
    u32 end = c->pos + (u32)seglen - 2;
    while (c->pos < end) {
        int pq_tq = jpeg__byte(c);
        if (pq_tq < 0) return JPEG_ERR_TRUNCATED;
        int pq = pq_tq >> 4, tq = pq_tq & 0xF;
        if (tq > 3) return JPEG_ERR_BADMARKER;
        for (int i = 0; i < 64; i++) {
            int v;
            if (pq) { v = jpeg__u16(c); if (v < 0) return JPEG_ERR_TRUNCATED; }
            else    { v = jpeg__byte(c); if (v < 0) return JPEG_ERR_TRUNCATED; }
            c->qtab[tq][jpeg__zigzag[i]] = (u16)v;
        }
    }
    return JPEG_OK;
}

static int jpeg__read_dht(jpeg__ctx_t* c, int seglen) {
    u32 end = c->pos + (u32)seglen - 2;
    while (c->pos < end) {
        int tc_th = jpeg__byte(c);
        if (tc_th < 0) return JPEG_ERR_TRUNCATED;
        int tc = tc_th >> 4, th = tc_th & 0xF;
        if (th > 3) return JPEG_ERR_BADMARKER;
        jpeg__huff_t* h = tc ? &c->huff_ac[th] : &c->huff_dc[th];
        int total = 0;
        h->bits[0] = 0;
        for (int i = 1; i <= 16; i++) {
            int b = jpeg__byte(c);
            if (b < 0) return JPEG_ERR_TRUNCATED;
            h->bits[i] = (u8)b;
            total += b;
        }
        if (total > 256) return JPEG_ERR_BAD_HUFFMAN;
        for (int i = 0; i < total; i++) {
            int v = jpeg__byte(c);
            if (v < 0) return JPEG_ERR_TRUNCATED;
            h->vals[i] = (u8)v;
        }
        jpeg__build_huff(h);
        if (tc) c->huff_ac_present[th] = 1; else c->huff_dc_present[th] = 1;
    }
    return JPEG_OK;
}

static int jpeg__read_sof0(jpeg__ctx_t* c, int seglen) {
    (void)seglen;
    int prec = jpeg__byte(c);
    if (prec != 8) return JPEG_ERR_UNSUPPORTED;
    int h = jpeg__u16(c);
    int w = jpeg__u16(c);
    if (h < 0 || w < 0) return JPEG_ERR_TRUNCATED;
    if (w > JPEG_MAX_W || h > JPEG_MAX_H || w == 0 || h == 0) return JPEG_ERR_TOO_LARGE;
    c->height = (u32)h; c->width = (u32)w;
    int nc = jpeg__byte(c);
    if (nc != 1 && nc != 3) return JPEG_ERR_BAD_COMPONENTS;
    c->ncomp = nc;
    c->max_h = 1; c->max_v = 1;
    for (int i = 0; i < nc; i++) {
        int id = jpeg__byte(c);
        int hv = jpeg__byte(c);
        int tq = jpeg__byte(c);
        if (id < 0 || hv < 0 || tq < 0) return JPEG_ERR_TRUNCATED;
        c->comp[i].id = id;
        c->comp[i].h = hv >> 4;
        c->comp[i].v = hv & 0xF;
        c->comp[i].tq = tq;
        c->comp[i].dc_pred = 0;
        if (c->comp[i].h < 1 || c->comp[i].h > 4 || c->comp[i].v < 1 || c->comp[i].v > 4)
            return JPEG_ERR_BAD_COMPONENTS;
        if (c->comp[i].h > c->max_h) c->max_h = c->comp[i].h;
        if (c->comp[i].v > c->max_v) c->max_v = c->comp[i].v;
    }
    return JPEG_OK;
}

static int jpeg__read_sos_header(jpeg__ctx_t* c, int seglen) {
    (void)seglen;
    int ns = jpeg__byte(c);
    if (ns < 0 || ns != c->ncomp) return JPEG_ERR_BAD_COMPONENTS;
    for (int i = 0; i < ns; i++) {
        int cs = jpeg__byte(c);
        int tdta = jpeg__byte(c);
        if (cs < 0 || tdta < 0) return JPEG_ERR_TRUNCATED;
        int found = -1;
        for (int j = 0; j < c->ncomp; j++) if (c->comp[j].id == cs) { found = j; break; }
        if (found < 0) return JPEG_ERR_BAD_COMPONENTS;
        c->comp[found].td = tdta >> 4;
        c->comp[found].ta = tdta & 0xF;
    }
    int ss = jpeg__byte(c), se = jpeg__byte(c), ahal = jpeg__byte(c);
    (void)ss; (void)se; (void)ahal;
    return JPEG_OK;
}

/* ---------------------------- scan decode ---------------------------- */

typedef struct {
    u32 comp_planes_w[JPEG__MAXCOMP];
    u32 comp_planes_h[JPEG__MAXCOMP];
    u8* plane[JPEG__MAXCOMP];
} jpeg__planes_t;

static int jpeg__decode_scan(jpeg__ctx_t* c, jpeg__planes_t* pl) {
    int mcus_x = (int)((c->width + 8 * c->max_h - 1) / (8 * c->max_h));
    int mcus_y = (int)((c->height + 8 * c->max_v - 1) / (8 * c->max_v));

    c->bitbuf = 0; c->bitcnt = 0; c->marker_hit = 0;
    int restarts_left = c->restart_interval;

    for (int my = 0; my < mcus_y; my++) {
        for (int mx = 0; mx < mcus_x; mx++) {
            for (int ci = 0; ci < c->ncomp; ci++) {
                jpeg__comp_t* comp = &c->comp[ci];
                if (!c->huff_dc_present[comp->td] || !c->huff_ac_present[comp->ta])
                    return JPEG_ERR_BAD_HUFFMAN;
                for (int by = 0; by < comp->v; by++) {
                    for (int bx = 0; bx < comp->h; bx++) {
                        int block[64];
                        for (int zi = 0; zi < 64; zi++) block[zi] = 0;

                        int t = jpeg__decode_huff(c, &c->huff_dc[comp->td]);
                        if (t < 0) return JPEG_ERR_BAD_HUFFMAN;
                        int diff = 0;
                        if (t > 0) {
                            int v = jpeg__get_bits(c, t);
                            diff = jpeg__extend(v, t);
                        }
                        comp->dc_pred += diff;
                        block[0] = comp->dc_pred * c->qtab[comp->tq][0];

                        int k = 1;
                        while (k < 64) {
                            int rs = jpeg__decode_huff(c, &c->huff_ac[comp->ta]);
                            if (rs < 0) return JPEG_ERR_BAD_HUFFMAN;
                            int run = rs >> 4, size = rs & 0xF;
                            if (size == 0) {
                                if (run == 15) { k += 16; continue; }
                                break;
                            }
                            k += run;
                            if (k >= 64) return JPEG_ERR_BAD_HUFFMAN;
                            int v = jpeg__get_bits(c, size);
                            int coeff = jpeg__extend(v, size);
                            int zz = jpeg__zigzag[k];
                            block[zz] = coeff * c->qtab[comp->tq][zz];
                            k++;
                        }

                        u32 plane_w = pl->comp_planes_w[ci];
                        int ox = (mx * comp->h + bx) * 8;
                        int oy = (my * comp->v + by) * 8;
                        u8 tmp[64];
                        jpeg__idct_8x8(block, tmp, 8);
                        for (int yy = 0; yy < 8; yy++) {
                            int py = oy + yy;
                            if ((u32)py >= pl->comp_planes_h[ci]) continue;
                            for (int xx = 0; xx < 8; xx++) {
                                int px_ = ox + xx;
                                if ((u32)px_ >= plane_w) continue;
                                pl->plane[ci][(u32)py * plane_w + (u32)px_] = tmp[yy * 8 + xx];
                            }
                        }
                    }
                }
            }
            if (c->restart_interval && --restarts_left == 0 && !(my == mcus_y - 1 && mx == mcus_x - 1)) {
                c->bitbuf = 0; c->bitcnt = 0; c->marker_hit = 0;
                while (c->pos + 1 < c->len && !(c->data[c->pos] == 0xFF && c->data[c->pos+1] != 0x00 && c->data[c->pos+1] != 0xFF)) {
                    c->pos++;
                }
                if (c->pos + 1 < c->len && c->data[c->pos] == 0xFF &&
                    c->data[c->pos+1] >= 0xD0 && c->data[c->pos+1] <= 0xD7) {
                    c->pos += 2;
                }
                for (int ci = 0; ci < c->ncomp; ci++) c->comp[ci].dc_pred = 0;
                restarts_left = c->restart_interval;
            }
        }
    }
    return JPEG_OK;
}

/* ---------------------------- upsample + color convert ---------------------------- */

static u8 jpeg__clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (u8)v); }

/* "Fancy" (libjpeg-matching) triangle-filter chroma upsampling. See
 * the original design notes: `stride` is the source row pitch (block-
 * padded plane width); `logical_w`/`logical_h` are the true
 * "downsampled dimensions" (ceil(image_dim*comp_factor/max_factor)) —
 * libjpeg's notion of where a line's real edge is, as opposed to where
 * MCU block-padding happens to end. Getting this distinction right is
 * what makes partial-MCU image edges match libjpeg's output instead of
 * drifting toward invisible padding samples. */
static void jpeg__upsample_v2(const u8* in, int stride, int logical_w, int logical_h, u8* out) {
    for (int y = 0; y < logical_h; y++) {
        const u8* cur = in + (u32)y * stride;
        const u8* above = in + (u32)(y > 0 ? y - 1 : 0) * stride;
        const u8* below = in + (u32)(y < logical_h - 1 ? y + 1 : logical_h - 1) * stride;
        u8* top = out + (u32)(2 * y) * stride;
        u8* bot = out + (u32)(2 * y + 1) * stride;
        for (int x = 0; x < logical_w; x++) {
            top[x] = (u8)((3 * cur[x] + above[x] + 2) >> 2);
            bot[x] = (u8)((3 * cur[x] + below[x] + 2) >> 2);
        }
    }
}

/* Matches libjpeg's h2v1_fancy_upsample bit-for-bit: edge samples are a
 * direct copy on their outward side, interior samples use a 3:1 blend
 * with asymmetric rounding (+1 leaning left, +2 leaning right). */
static void jpeg__upsample_h2(const u8* in, int in_stride, int logical_w, int h, u8* out) {
    int out_stride = in_stride * 2;
    for (int y = 0; y < h; y++) {
        const u8* row = in + (u32)y * in_stride;
        u8* orow = out + (u32)y * out_stride;
        if (logical_w == 1) { orow[0] = orow[1] = row[0]; continue; }

        orow[0] = row[0];
        orow[1] = (u8)((row[0] * 3 + row[1] + 2) >> 2);
        for (int x = 1; x < logical_w - 1; x++) {
            int v3 = row[x] * 3;
            orow[2 * x] = (u8)((v3 + row[x - 1] + 1) >> 2);
            orow[2 * x + 1] = (u8)((v3 + row[x + 1] + 2) >> 2);
        }
        orow[2 * (logical_w - 1)] = (u8)((row[logical_w - 1] * 3 + row[logical_w - 2] + 1) >> 2);
        orow[2 * (logical_w - 1) + 1] = row[logical_w - 1];
    }
}

/* Returns a plane at full (max_h/max_v) resolution for component ci:
 * the original plane if already full-res, or a newly bump-allocated
 * upsampled copy. Returns 0 on scratch exhaustion (caller must check).
 * image_w/image_h are the true JPEG image dimensions. */
static u8* jpeg__get_full_plane(jpeg__ctx_t* c, jpeg__planes_t* pl, int ci,
                                 u32 image_w, u32 image_h, u32* out_w, u32* out_h) {
    int fh = c->max_h / c->comp[ci].h;
    int fv = c->max_v / c->comp[ci].v;
    u32 pw = pl->comp_planes_w[ci], ph = pl->comp_planes_h[ci];
    if (fh <= 1 && fv <= 1) {
        *out_w = pw; *out_h = ph;
        return pl->plane[ci];
    }
    int logical_w = (int)((image_w * (u32)c->comp[ci].h + (u32)c->max_h - 1) / (u32)c->max_h);
    int logical_h = (int)((image_h * (u32)c->comp[ci].v + (u32)c->max_v - 1) / (u32)c->max_v);

    u8* cur = pl->plane[ci];
    int stride = (int)pw;
    int cur_h = logical_h;

    if (fv >= 2) {
        u8* v = jpeg__alloc(c, pw * ph * 2);
        if (!v) return 0;
        jpeg__upsample_v2(cur, stride, stride, logical_h, v);
        cur = v; cur_h = logical_h * 2;
    }
    if (fh >= 2) {
        u8* h = jpeg__alloc(c, (u32)(stride * 2) * (u32)cur_h);
        if (!h) return 0;
        jpeg__upsample_h2(cur, stride, logical_w, cur_h, h);
        cur = h; stride *= 2; logical_w *= 2;
    }
    int cur_w = logical_w;
    /* fh/fv of 3 or 4 (non-standard, essentially never seen from real
     * encoders): fall back to nearest replication for any remaining
     * factor beyond the 2x fancy pass already applied. */
    while ((u32)cur_w < image_w || (u32)cur_h < image_h) {
        int rw = ((u32)cur_w < image_w) ? 2 : 1;
        int rh = ((u32)cur_h < image_h) ? 2 : 1;
        u8* r = jpeg__alloc(c, (u32)(cur_w * rw) * (u32)(cur_h * rh));
        if (!r) return 0;
        for (int y = 0; y < cur_h * rh; y++)
            for (int x = 0; x < cur_w * rw; x++)
                r[y * (cur_w * rw) + x] = cur[(y / rh) * stride + (x / rw)];
        cur = r; cur_w *= rw; cur_h *= rh; stride = cur_w;
    }
    *out_w = (u32)stride; *out_h = (u32)cur_h;
    return cur;
}

static void jpeg__ycbcr_to_rgb(int Y, int Cb, int Cr, u8* out) {
    Cb -= 128; Cr -= 128;
    int r = Y + ((91881 * Cr) >> 16);
    int g = Y - ((22554 * Cb + 46802 * Cr) >> 16);
    int b = Y + ((116130 * Cb) >> 16);
    out[0] = jpeg__clamp255(r);
    out[1] = jpeg__clamp255(g);
    out[2] = jpeg__clamp255(b);
}

/* ============================= public entry ============================= */

/*
 * Decodes a JPEG from a full in-memory buffer into `out_rgba`
 * (JPEG_MAX_W*JPEG_MAX_H*4 bytes, caller-allocated), RGBA8,
 * row-major, top-to-bottom — identical convention to png_decode
 * (alpha is always 255; JPEG has no alpha channel). `scratch` must be
 * at least JPEG_SCRATCH_BYTES bytes — used as a bump allocator for
 * component planes and upsampling intermediates; the caller reclaims
 * it in bulk (no per-allocation free()).
 */
static jpeg_info_t jpeg_decode(const u8* data, u32 len, u8* out_rgba,
                                u8* scratch, u32 scratch_cap) {
    jpeg_info_t info; info.width = 0; info.height = 0; info.error = JPEG_OK;

    jpeg__ctx_t ctx;
    ctx.data = data; ctx.len = len; ctx.pos = 0;
    ctx.scratch = scratch; ctx.scratch_cap = scratch_cap; ctx.scratch_used = 0;
    ctx.restart_interval = 0;
    ctx.bitbuf = 0; ctx.bitcnt = 0; ctx.marker_hit = 0;
    ctx.width = 0; ctx.height = 0; ctx.ncomp = 0; ctx.max_h = 1; ctx.max_v = 1;
    for (int i = 0; i < 4; i++) { ctx.huff_dc_present[i] = 0; ctx.huff_ac_present[i] = 0; }

    if (len < 4) { info.error = JPEG_ERR_TRUNCATED; return info; }
    if (data[0] != 0xFF || data[1] != 0xD8) { info.error = JPEG_ERR_BADMARKER; return info; }
    ctx.pos = 2;

    int sof_seen = 0;
    int sos_done = 0;

    while (!sos_done) {
        int m0 = jpeg__byte(&ctx);
        if (m0 < 0) { info.error = JPEG_ERR_TRUNCATED; return info; }
        if (m0 != 0xFF) continue;
        int marker;
        do {
            marker = jpeg__byte(&ctx);
            if (marker < 0) { info.error = JPEG_ERR_TRUNCATED; return info; }
        } while (marker == 0xFF);
        if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (marker == 0xD9) { info.error = JPEG_ERR_TRUNCATED; return info; }

        int seglen = jpeg__u16(&ctx);
        if (seglen < 2) { info.error = JPEG_ERR_TRUNCATED; return info; }
        u32 seg_start = ctx.pos;

        int rc = JPEG_OK;
        switch (marker) {
            case 0xDB: rc = jpeg__read_dqt(&ctx, seglen); break;
            case 0xC4: rc = jpeg__read_dht(&ctx, seglen); break;
            case 0xC0: rc = jpeg__read_sof0(&ctx, seglen); sof_seen = 1; break;
            case 0xC1: case 0xC2:
            case 0xC3: case 0xC5: case 0xC6: case 0xC7:
            case 0xC9: case 0xCA: case 0xCB:
            case 0xCD: case 0xCE: case 0xCF:
                info.error = JPEG_ERR_UNSUPPORTED; return info;
            case 0xDD: {
                int ri = jpeg__u16(&ctx);
                if (ri < 0) { info.error = JPEG_ERR_TRUNCATED; return info; }
                ctx.restart_interval = ri;
                break;
            }
            case 0xDA: {
                if (!sof_seen) { info.error = JPEG_ERR_BADMARKER; return info; }
                rc = jpeg__read_sos_header(&ctx, seglen);
                sos_done = 1;
                break;
            }
            default: break; /* APPn, COM, etc: skip */
        }
        if (rc != JPEG_OK) { info.error = (u8)rc; return info; }
        if (!sos_done) ctx.pos = seg_start + (u32)seglen - 2;
    }

    jpeg__planes_t pl;
    int mcus_x = (int)((ctx.width + 8 * (u32)ctx.max_h - 1) / (8 * (u32)ctx.max_h));
    int mcus_y = (int)((ctx.height + 8 * (u32)ctx.max_v - 1) / (8 * (u32)ctx.max_v));
    for (int ci = 0; ci < ctx.ncomp; ci++) {
        u32 pw = (u32)(mcus_x * 8 * ctx.comp[ci].h);
        u32 ph = (u32)(mcus_y * 8 * ctx.comp[ci].v);
        pl.comp_planes_w[ci] = pw;
        pl.comp_planes_h[ci] = ph;
        pl.plane[ci] = jpeg__alloc(&ctx, pw * ph);
        if (!pl.plane[ci]) { info.error = JPEG_ERR_NOMEM; return info; }
    }

    int rc = jpeg__decode_scan(&ctx, &pl);
    if (rc != JPEG_OK) { info.error = (u8)rc; return info; }

    if (ctx.ncomp == 1) {
        u32 pw = pl.comp_planes_w[0];
        for (u32 y = 0; y < ctx.height; y++) {
            const u8* src = pl.plane[0] + y * pw;
            u8* dst = out_rgba + y * ctx.width * 4;
            for (u32 x = 0; x < ctx.width; x++) {
                u8 g = src[x];
                dst[x*4+0] = g; dst[x*4+1] = g; dst[x*4+2] = g; dst[x*4+3] = 255;
            }
        }
    } else {
        u8* full[3]; u32 full_w[3], full_h[3];
        for (int ci = 0; ci < 3; ci++) {
            full[ci] = jpeg__get_full_plane(&ctx, &pl, ci, ctx.width, ctx.height, &full_w[ci], &full_h[ci]);
            if (!full[ci]) { info.error = JPEG_ERR_NOMEM; return info; }
        }
        for (u32 y = 0; y < ctx.height; y++) {
            u8* dst = out_rgba + y * ctx.width * 4;
            for (u32 x = 0; x < ctx.width; x++) {
                int sample[3];
                for (int ci = 0; ci < 3; ci++) sample[ci] = full[ci][y * full_w[ci] + x];
                u8 rgb[3];
                jpeg__ycbcr_to_rgb(sample[0], sample[1], sample[2], rgb);
                dst[x*4+0] = rgb[0]; dst[x*4+1] = rgb[1]; dst[x*4+2] = rgb[2]; dst[x*4+3] = 255;
            }
        }
    }

    info.width = ctx.width;
    info.height = ctx.height;
    return info;
}

#endif /* JPEG_DECODER_H */
