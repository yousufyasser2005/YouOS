/*
 * Minimal DEFLATE (RFC 1951) decompressor — "inflate".
 * Freestanding, no libc, no dynamic allocation — everything is
 * static buffers, matching this codebase's conventions (see
 * WAV_MAX_SAMPLES / wallpaper_pixels for the same pattern).
 *
 * Used by the PNG decoder (PNG's IDAT stream is zlib-wrapped
 * DEFLATE data — see png_decoder.h for the zlib header handling
 * and PNG-specific chunk parsing).
 *
 * This file is self-tested against real zlib-compressed data in a
 * host sandbox before being handed off for the freestanding build,
 * since ~500 lines of bit-level decompression logic is exactly the
 * kind of code that should be verified against ground truth before
 * trusting it blind in an environment with no compiler feedback
 * loop of its own.
 */
#ifndef INFLATE_H
#define INFLATE_H

/* u8/u16/u32/u64/s64 are already typedef'd in desktop.c before this
 * file is #included — do NOT redefine them here, that would collide.
 * s32 is genuinely new (desktop.c only has s64), safe to add. */
typedef signed int     s32;

/* ---- Bit reader: DEFLATE packs bits LSB-first within each byte ---- */
typedef struct {
    const u8* data;
    u32 len;      /* total input bytes available */
    u32 pos;      /* current byte position */
    u32 bitbuf;   /* bit accumulator */
    u32 bitcnt;   /* number of valid bits currently in bitbuf */
} inflate_bitreader_t;

static void ibr_init(inflate_bitreader_t* br, const u8* data, u32 len) {
    br->data = data; br->len = len; br->pos = 0; br->bitbuf = 0; br->bitcnt = 0;
}

/* Returns -1 on out-of-data (caller must check before relying on result). */
static s32 ibr_getbits(inflate_bitreader_t* br, int n) {
    while (br->bitcnt < (u32)n) {
        if (br->pos >= br->len) return -1;
        br->bitbuf |= ((u32)br->data[br->pos++]) << br->bitcnt;
        br->bitcnt += 8;
    }
    u32 val = br->bitbuf & ((1u << n) - 1);
    br->bitbuf >>= n;
    br->bitcnt -= (u32)n;
    return (s32)val;
}

/* Discard any partial bits, align to next byte boundary. */
static void ibr_align(inflate_bitreader_t* br) {
    br->bitbuf = 0;
    br->bitcnt = 0;
}

/* ---- Canonical Huffman decoding ----
 * DEFLATE Huffman codes are "canonical": fully described by an array
 * of code LENGTHS (one per symbol). We build a simple lookup: for
 * each possible code length, the first code value and the range of
 * symbol indices — then decode bit-by-bit (MSB-first per-code, even
 * though the bitstream itself is LSB-first byte-packing; this is a
 * DEFLATE-specific quirk: within a Huffman code, bits are read and
 * compared MSB-first).
 */
#define MAX_HUFF_SYMBOLS 288
#define MAX_HUFF_BITS 15

typedef struct {
    u16 counts[MAX_HUFF_BITS + 1]; /* counts[n] = number of codes of length n */
    u16 symbols[MAX_HUFF_SYMBOLS]; /* symbols sorted by (length, symbol value) */
} huff_tree_t;

static void huff_build(huff_tree_t* t, const u8* lengths, int nsyms) {
    int i;
    for (i = 0; i <= MAX_HUFF_BITS; i++) t->counts[i] = 0;
    for (i = 0; i < nsyms; i++) t->counts[lengths[i]]++;
    t->counts[0] = 0; /* length 0 = symbol unused */

    u16 offs[MAX_HUFF_BITS + 2];
    offs[1] = 0;
    for (i = 1; i <= MAX_HUFF_BITS; i++) offs[i + 1] = offs[i] + t->counts[i];

    for (i = 0; i < nsyms; i++) {
        if (lengths[i]) t->symbols[offs[lengths[i]]++] = (u16)i;
    }
}

/* Decode one symbol. Returns -1 on error/out-of-data. */
static s32 huff_decode(inflate_bitreader_t* br, const huff_tree_t* t) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_HUFF_BITS; len++) {
        s32 bit = ibr_getbits(br, 1);
        if (bit < 0) return -1;
        code |= bit;
        int count = t->counts[len];
        if (code - first < count) return t->symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* ---- Length/distance extra-bit tables (RFC 1951 §3.2.5) ---- */
static const u16 LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const u8 LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const u16 DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const u8 DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
/* Order in which code-length-code lengths themselves are stored
 * (dynamic Huffman block header) — this ordering is fixed by spec. */
static const u8 CLC_ORDER[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

/* ---- Output sink: caller-supplied buffer + running length ----
 * Back-references need to copy from EARLIER IN THIS SAME OUTPUT
 * buffer, so the whole decompressed output must be addressable —
 * matches this codebase's "decode whole file into one static
 * buffer" pattern used for WAV. */
typedef struct {
    u8* buf;
    u32 cap;
    u32 len;
} inflate_out_t;

static int out_put(inflate_out_t* o, u8 b) {
    if (o->len >= o->cap) return -1;
    o->buf[o->len++] = b;
    return 0;
}

static int inflate_block_stored(inflate_bitreader_t* br, inflate_out_t* out) {
    ibr_align(br);
    if (br->pos + 4 > br->len) return -1;
    u16 lenv = (u16)(br->data[br->pos] | (br->data[br->pos+1] << 8));
    /* NLEN (one's complement of LEN) follows but we don't need to
     * validate it strictly for a first working version */
    br->pos += 4;
    if (br->pos + lenv > br->len) return -1;
    for (u16 i = 0; i < lenv; i++) {
        if (out_put(out, br->data[br->pos + i]) != 0) return -1;
    }
    br->pos += lenv;
    return 0;
}

static void huff_build_fixed(huff_tree_t* lit, huff_tree_t* dist) {
    u8 lens[288];
    int i;
    for (i = 0;   i <= 143; i++) lens[i] = 8;
    for (i = 144; i <= 255; i++) lens[i] = 9;
    for (i = 256; i <= 279; i++) lens[i] = 7;
    for (i = 280; i <= 287; i++) lens[i] = 8;
    huff_build(lit, lens, 288);
    u8 dlens[30];
    for (i = 0; i < 30; i++) dlens[i] = 5;
    huff_build(dist, dlens, 30);
}

static int inflate_block_huffman(inflate_bitreader_t* br, inflate_out_t* out,
                                  const huff_tree_t* lit, const huff_tree_t* dist) {
    for (;;) {
        s32 sym = huff_decode(br, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (out_put(out, (u8)sym) != 0) return -1;
        } else if (sym == 256) {
            return 0; /* end of block */
        } else {
            int lidx = sym - 257;
            if (lidx >= 29) return -1;
            s32 extra = LEN_EXTRA[lidx] ? ibr_getbits(br, LEN_EXTRA[lidx]) : 0;
            if (extra < 0) return -1;
            u32 length = LEN_BASE[lidx] + (u32)extra;

            s32 dsym = huff_decode(br, dist);
            if (dsym < 0 || dsym >= 30) return -1;
            s32 dextra = DIST_EXTRA[dsym] ? ibr_getbits(br, DIST_EXTRA[dsym]) : 0;
            if (dextra < 0) return -1;
            u32 distance = DIST_BASE[dsym] + (u32)dextra;

            if (distance > out->len) return -1; /* back-ref before start of output */
            u32 srcpos = out->len - distance;
            for (u32 k = 0; k < length; k++) {
                if (out_put(out, out->buf[srcpos + k]) != 0) return -1;
            }
        }
    }
}

static int inflate_block_dynamic(inflate_bitreader_t* br, inflate_out_t* out) {
    s32 hlit  = ibr_getbits(br, 5); if (hlit  < 0) return -1; hlit  += 257;
    s32 hdist = ibr_getbits(br, 5); if (hdist < 0) return -1; hdist += 1;
    s32 hclen = ibr_getbits(br, 4); if (hclen < 0) return -1; hclen += 4;

    u8 clc_lengths[19];
    for (int i = 0; i < 19; i++) clc_lengths[i] = 0;
    for (int i = 0; i < hclen; i++) {
        s32 v = ibr_getbits(br, 3);
        if (v < 0) return -1;
        clc_lengths[CLC_ORDER[i]] = (u8)v;
    }
    huff_tree_t clc_tree;
    huff_build(&clc_tree, clc_lengths, 19);

    u8 lengths[288 + 32];
    int total = hlit + hdist;
    int n = 0;
    while (n < total) {
        s32 sym = huff_decode(br, &clc_tree);
        if (sym < 0) return -1;
        if (sym < 16) {
            lengths[n++] = (u8)sym;
        } else if (sym == 16) {
            if (n == 0) return -1;
            s32 rep = ibr_getbits(br, 2); if (rep < 0) return -1; rep += 3;
            u8 prev = lengths[n - 1];
            while (rep-- > 0 && n < total) lengths[n++] = prev;
        } else if (sym == 17) {
            s32 rep = ibr_getbits(br, 3); if (rep < 0) return -1; rep += 3;
            while (rep-- > 0 && n < total) lengths[n++] = 0;
        } else { /* 18 */
            s32 rep = ibr_getbits(br, 7); if (rep < 0) return -1; rep += 11;
            while (rep-- > 0 && n < total) lengths[n++] = 0;
        }
    }

    huff_tree_t lit_tree, dist_tree;
    huff_build(&lit_tree, lengths, hlit);
    huff_build(&dist_tree, lengths + hlit, hdist);

    return inflate_block_huffman(br, out, &lit_tree, &dist_tree);
}

/* Top-level: decompress a raw DEFLATE stream (NOT zlib-wrapped — the
 * caller strips the 2-byte zlib header and 4-byte Adler32 trailer
 * before calling this, see png_decoder.h). Returns decompressed
 * length, or (u32)-1 on error. */
static u32 inflate_raw(const u8* src, u32 srclen, u8* dst, u32 dstcap) {
    inflate_bitreader_t br;
    ibr_init(&br, src, srclen);
    inflate_out_t out;
    out.buf = dst; out.cap = dstcap; out.len = 0;

    for (;;) {
        s32 bfinal = ibr_getbits(&br, 1);
        if (bfinal < 0) return (u32)-1;
        s32 btype = ibr_getbits(&br, 2);
        if (btype < 0) return (u32)-1;

        int rc;
        if (btype == 0) {
            rc = inflate_block_stored(&br, &out);
        } else if (btype == 1) {
            huff_tree_t lit, dist;
            huff_build_fixed(&lit, &dist);
            rc = inflate_block_huffman(&br, &out, &lit, &dist);
        } else if (btype == 2) {
            rc = inflate_block_dynamic(&br, &out);
        } else {
            return (u32)-1; /* btype 3 is reserved/invalid */
        }
        if (rc != 0) return (u32)-1;
        if (bfinal) break;
    }
    return out.len;
}

#endif /* INFLATE_H */
