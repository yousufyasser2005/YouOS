/*
 * Minimal PNG decoder — chunk parsing + scanline filter reconstruction,
 * built on the tested inflate.h DEFLATE core.
 *
 * Supports: 8-bit color types 2 (RGB) and 6 (RGBA), non-interlaced only.
 * These cover ordinary screenshots/photos exported by any normal tool.
 * Deliberately NOT supported (real, documented limitations, same
 * spirit as the WAV_MAX_SAMPLES cap): palette (type 3), grayscale
 * (types 0/4), 16-bit depth, interlaced (Adam7). A PNG outside this
 * subset is rejected with a clear reason rather than silently
 * producing garbage.
 *
 * Self-tested in a host sandbox against real libpng/Pillow-produced
 * PNG files before being handed off for the freestanding build.
 */
#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include "inflate.h"

#define PNG_MAX_W 1024
#define PNG_MAX_H 768

typedef struct {
    u32 width, height;
    u8 error; /* 0 = ok, nonzero = see png_error_str() */
} png_info_t;

enum {
    PNG_OK = 0,
    PNG_ERR_SIG,
    PNG_ERR_NO_IHDR,
    PNG_ERR_UNSUPPORTED_TYPE,
    PNG_ERR_INTERLACED,
    PNG_ERR_TOO_LARGE,
    PNG_ERR_NO_IDAT,
    PNG_ERR_INFLATE_FAILED,
    PNG_ERR_ZLIB_HEADER,
    PNG_ERR_BAD_FILTER,
    PNG_ERR_TRUNCATED
};

static const char* png_error_str(u8 e) {
    switch (e) {
        case PNG_OK: return "ok";
        case PNG_ERR_SIG: return "not a PNG file (bad signature)";
        case PNG_ERR_NO_IHDR: return "missing IHDR chunk";
        case PNG_ERR_UNSUPPORTED_TYPE: return "unsupported PNG type (need 8-bit RGB or RGBA)";
        case PNG_ERR_INTERLACED: return "interlaced PNGs not supported";
        case PNG_ERR_TOO_LARGE: return "image too large for viewer";
        case PNG_ERR_NO_IDAT: return "missing image data";
        case PNG_ERR_INFLATE_FAILED: return "decompression failed (corrupt file?)";
        case PNG_ERR_ZLIB_HEADER: return "bad zlib header in image data";
        case PNG_ERR_BAD_FILTER: return "bad scanline filter byte";
        case PNG_ERR_TRUNCATED: return "file truncated / not enough data";
        default: return "unknown error";
    }
}

static u32 png_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u8 paeth_predictor(u8 a, u8 b, u8 c) {
    /* a = left, b = above, c = upper-left */
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/*
 * Decodes a PNG from a full in-memory buffer (caller already read the
 * whole file — matches the WAV decoder's "read whole file, then
 * process" convention). Output is RGBA8 into `out_rgba`
 * (PNG_MAX_W*PNG_MAX_H*4 bytes, caller-allocated static buffer),
 * row-major, top-to-bottom — same orientation as the framebuffer
 * `px()` convention (NOT bottom-up like the BMP loader).
 *
 * `inflate_scratch` must be a caller-provided buffer at least
 * (width*bytes_per_pixel + 1) * height bytes — the decompressed
 * "filtered" scanline data before un-filtering. Caller sizes this
 * (e.g. a static PNG_MAX_W*PNG_MAX_H*4+PNG_MAX_H buffer) to avoid
 * this file needing its own huge static allocation on top of the
 * inflate output buffer.
 */
static png_info_t png_decode(const u8* data, u32 len, u8* out_rgba,
                              u8* inflate_scratch, u32 inflate_scratch_cap) {
    png_info_t info; info.width = 0; info.height = 0; info.error = PNG_OK;

    static const u8 SIG[8] = {0x89,'P','N','G','\r','\n',0x1A,'\n'};
    if (len < 8) { info.error = PNG_ERR_TRUNCATED; return info; }
    for (int i = 0; i < 8; i++) if (data[i] != SIG[i]) { info.error = PNG_ERR_SIG; return info; }

    u32 pos = 8;
    u8 have_ihdr = 0;
    u8 bitdepth = 0, colortype = 0, interlace = 0;
    u32 width = 0, height = 0;

    /* IDAT chunks can be split across multiple chunks; concatenate
     * them into one contiguous buffer before inflating. We reuse the
     * tail of inflate_scratch as scratch space for this concatenation
     * isn't safe (inflate output needs the whole buffer) — so the
     * caller must also give us room via a second pass: first pass
     * measures total IDAT size, second pass copies. Simpler: require
     * IDAT bytes to fit within `out_rgba`'s backing area temporarily
     * is fragile; instead we do two scans of the chunk stream. */

    /* Pass 1: find IHDR, validate, and measure total IDAT length. */
    u32 idat_total = 0;
    u32 scan = pos;
    while (scan + 8 <= len) {
        u32 clen = png_be32(data + scan);
        const u8* ctype = data + scan + 4;
        if (scan + 8 + clen + 4 > len) { info.error = PNG_ERR_TRUNCATED; return info; }
        if (ctype[0]=='I'&&ctype[1]=='H'&&ctype[2]=='D'&&ctype[3]=='R') {
            if (clen < 13) { info.error = PNG_ERR_TRUNCATED; return info; }
            const u8* c = data + scan + 8;
            width = png_be32(c);
            height = png_be32(c + 4);
            bitdepth = c[8];
            colortype = c[9];
            interlace = c[12];
            have_ihdr = 1;
        } else if (ctype[0]=='I'&&ctype[1]=='D'&&ctype[2]=='A'&&ctype[3]=='T') {
            idat_total += clen;
        } else if (ctype[0]=='I'&&ctype[1]=='E'&&ctype[2]=='N'&&ctype[3]=='D') {
            break;
        }
        scan += 8 + clen + 4;
    }

    if (!have_ihdr) { info.error = PNG_ERR_NO_IHDR; return info; }
    if (interlace != 0) { info.error = PNG_ERR_INTERLACED; return info; }
    if (bitdepth != 8 || (colortype != 2 && colortype != 6)) {
        info.error = PNG_ERR_UNSUPPORTED_TYPE; return info;
    }
    if (width == 0 || height == 0 || width > PNG_MAX_W || height > PNG_MAX_H) {
        info.error = PNG_ERR_TOO_LARGE; return info;
    }
    if (idat_total == 0) { info.error = PNG_ERR_NO_IDAT; return info; }

    int channels = (colortype == 6) ? 4 : 3;
    u32 stride = width * (u32)channels;
    u32 filtered_len = (stride + 1) * height; /* +1 filter-type byte per row */

    /* We need: (a) a buffer to concatenate raw IDAT bytes into, (b) a
     * buffer for inflate's output (the filtered scanline data). The
     * caller-provided inflate_scratch is used for (b); we reuse
     * out_rgba's backing memory temporarily for (a) since IDAT bytes
     * (compressed) are always significantly smaller than the final
     * RGBA output for any real image, and we overwrite out_rgba with
     * real pixel data afterward anyway — same "reuse the big buffer
     * as scratch before its final use" pattern the WAV code used for
     * wav_chunk. */
    if (idat_total > (PNG_MAX_W * PNG_MAX_H * 4)) { info.error = PNG_ERR_TOO_LARGE; return info; }
    if (filtered_len > inflate_scratch_cap) { info.error = PNG_ERR_TOO_LARGE; return info; }

    u8* idat_concat = out_rgba; /* temporary reuse, see comment above */
    u32 idat_pos = 0;
    scan = pos;
    while (scan + 8 <= len) {
        u32 clen = png_be32(data + scan);
        const u8* ctype = data + scan + 4;
        if (ctype[0]=='I'&&ctype[1]=='D'&&ctype[2]=='A'&&ctype[3]=='T') {
            for (u32 i = 0; i < clen; i++) idat_concat[idat_pos + i] = data[scan + 8 + i];
            idat_pos += clen;
        } else if (ctype[0]=='I'&&ctype[1]=='E'&&ctype[2]=='N'&&ctype[3]=='D') {
            break;
        }
        scan += 8 + clen + 4;
    }

    /* Strip 2-byte zlib header, ignore 4-byte Adler32 trailer (we
     * don't verify the checksum — a real gap, but matches "don't
     * over-engineer" for a first working version; a corrupt file
     * would likely fail inflate itself before reaching this point). */
    if (idat_pos < 6) { info.error = PNG_ERR_ZLIB_HEADER; return info; }
    const u8* deflate_data = idat_concat + 2;
    u32 deflate_len = idat_pos - 2 - 4;

    u32 got = inflate_raw(deflate_data, deflate_len, inflate_scratch, inflate_scratch_cap);
    if (got == (u32)-1 || got != filtered_len) { info.error = PNG_ERR_INFLATE_FAILED; return info; }

    /* Un-filter each scanline (RFC 2083 §6) directly into out_rgba,
     * converting RGB->RGBA (alpha=255) if needed as we go. Now safe
     * to write into out_rgba since we're done reading idat_concat
     * (which aliased the same memory) — inflate_scratch holds the
     * filtered bytes independently. */
    u8 prevrow[PNG_MAX_W * 4];
    for (u32 i = 0; i < stride; i++) prevrow[i] = 0;

    for (u32 y = 0; y < height; y++) {
        const u8* rowsrc = inflate_scratch + y * (stride + 1);
        u8 ftype = rowsrc[0];
        const u8* fdata = rowsrc + 1;
        u8 currow[PNG_MAX_W * 4];

        for (u32 x = 0; x < stride; x++) {
            u8 raw = fdata[x];
            u8 a = (x >= (u32)channels) ? currow[x - channels] : 0;
            u8 b = prevrow[x];
            u8 c = (x >= (u32)channels) ? prevrow[x - channels] : 0;
            u8 recon;
            switch (ftype) {
                case 0: recon = raw; break;                              /* None */
                case 1: recon = (u8)(raw + a); break;                    /* Sub */
                case 2: recon = (u8)(raw + b); break;                    /* Up */
                case 3: recon = (u8)(raw + (u8)(((u32)a + (u32)b) / 2)); break; /* Average */
                case 4: recon = (u8)(raw + paeth_predictor(a, b, c)); break;    /* Paeth */
                default: info.error = PNG_ERR_BAD_FILTER; return info;
            }
            currow[x] = recon;
        }

        /* Write this row into out_rgba as RGBA, top-to-bottom. */
        u8* dst = out_rgba + y * width * 4;
        for (u32 x = 0; x < width; x++) {
            u8 r = currow[x*channels + 0];
            u8 g = currow[x*channels + 1];
            u8 b2 = currow[x*channels + 2];
            u8 al = (channels == 4) ? currow[x*channels + 3] : 255;
            dst[x*4 + 0] = r; dst[x*4 + 1] = g; dst[x*4 + 2] = b2; dst[x*4 + 3] = al;
        }

        for (u32 i = 0; i < stride; i++) prevrow[i] = currow[i];
    }

    info.width = width;
    info.height = height;
    return info;
}

#endif /* PNG_DECODER_H */
