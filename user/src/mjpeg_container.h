/*
 * mjpeg_container.h — reader for YouOS's .ymjp Motion JPEG container
 * (see tools/mkmjpeg.py for the format spec and packer). Each frame is
 * an independent baseline JPEG (own tables), decoded via the existing
 * jpeg_decode() — no new pixel-format or transform code, this file is
 * purely container framing + a playback-position/pacing helper.
 *
 * Same conventions as jpeg_decoder.h/png_decoder.h: u8/u32 types
 * (typedef'd in desktop.c before this file is #included), no
 * memset/memcpy, no libc.
 */
#ifndef MJPEG_CONTAINER_H
#define MJPEG_CONTAINER_H

typedef struct {
    u32 width, height;
    u32 frame_count;
    u32 frame_duration_ms;
    const u8* frame_index_base; /* points at first frame's [u32 len][data] */
    u32 file_len;                /* total container length, for bounds checks */
} mjpeg_info_t;

enum {
    MJPEG_OK = 0,
    MJPEG_ERR_BADMAGIC,
    MJPEG_ERR_TRUNCATED,
    MJPEG_ERR_BAD_VERSION,
};

static const char* mjpeg_error_str(u8 e) {
    switch (e) {
        case MJPEG_OK: return "ok";
        case MJPEG_ERR_BADMAGIC: return "not a .ymjp file (bad magic)";
        case MJPEG_ERR_TRUNCATED: return "file truncated / not enough data";
        case MJPEG_ERR_BAD_VERSION: return "unsupported .ymjp version";
        default: return "unknown error";
    }
}

/* Parses the 24-byte header and validates it. Does NOT walk/validate
 * every frame's length up front (that would mean a full pass over a
 * possibly-multi-MB file just to open it) — mjpeg_get_frame() bounds-
 * checks each frame lazily, on the access that actually needs it. */
static u8 mjpeg_open(const u8* data, u32 len, mjpeg_info_t* out) {
    out->width = 0; out->height = 0; out->frame_count = 0;
    out->frame_duration_ms = 0; out->frame_index_base = 0; out->file_len = len;

    if (len < 24) return MJPEG_ERR_TRUNCATED;
    if (data[0] != 'Y' || data[1] != 'M' || data[2] != 'J' || data[3] != 'P') return MJPEG_ERR_BADMAGIC;
    if (data[4] != 1) return MJPEG_ERR_BAD_VERSION;

    u32 w  = (u32)data[8]  | ((u32)data[9]  << 8) | ((u32)data[10] << 16) | ((u32)data[11] << 24);
    u32 h  = (u32)data[12] | ((u32)data[13] << 8) | ((u32)data[14] << 16) | ((u32)data[15] << 24);
    u32 fc = (u32)data[16] | ((u32)data[17] << 8) | ((u32)data[18] << 16) | ((u32)data[19] << 24);
    u32 fd = (u32)data[20] | ((u32)data[21] << 8) | ((u32)data[22] << 16) | ((u32)data[23] << 24);

    out->width = w; out->height = h; out->frame_count = fc; out->frame_duration_ms = fd;
    out->frame_index_base = data + 24;
    return MJPEG_OK;
}

/* Walks the frame chain to frame index `idx` (0-based), returning a
 * pointer to that frame's raw JPEG bytes and its length via out_len.
 * Returns 0 (with out_len untouched) on out-of-range idx or a length
 * that would run past the container's end (corrupt/truncated file).
 *
 * O(idx) — walks from the start each call, since frames are variable-
 * length and there's no separate offset table (keeps the container/
 * packer simple; playback only ever asks for "current" or "current+1"
 * frame, so this is never actually a hot loop in practice). If a seek
 * bar / scrubbing is added later, worth adding a real offset index. */
static const u8* mjpeg_get_frame(const mjpeg_info_t* info, u32 idx, u32* out_len) {
    if (idx >= info->frame_count) return 0;
    const u8* p = info->frame_index_base;
    const u8* end = info->frame_index_base + (info->file_len - 24);
    for (u32 i = 0; i < info->frame_count; i++) {
        if (p + 4 > end) return 0;
        u32 flen = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
        p += 4;
        if (p + flen > end) return 0;
        if (i == idx) { *out_len = flen; return p; }
        p += flen;
    }
    return 0;
}

/* Pacing helper: given elapsed wall-clock ms since playback started
 * and the frame duration, returns which frame index should be showing
 * now (clamped to frame_count-1, so the last frame holds rather than
 * looping — caller decides whether to loop/stop at that point). Pure
 * function of (info, elapsed_ms) — the real integration computes
 * elapsed_ms from sys_ticks() (100Hz PIT, confirmed in
 * kernel_main.c, so 10ms/tick) polled once per redraw, same pattern
 * the File Manager's double-click timing already uses. No IRQ hook
 * needed — video playback is ordinary userspace polling, unlike
 * WAV's sample-accurate AC97 feed. */
static u32 mjpeg_frame_for_elapsed(const mjpeg_info_t* info, u32 elapsed_ms) {
    if (info->frame_duration_ms == 0 || info->frame_count == 0) return 0;
    u32 idx = elapsed_ms / info->frame_duration_ms;
    if (idx >= info->frame_count) idx = info->frame_count - 1;
    return idx;
}

#endif /* MJPEG_CONTAINER_H */
