/*
 * mjpeg_container.h — reader for YouOS's .ymjp Motion JPEG container
 * (see tools/mkmjpeg.py for the format spec and packer). Each frame is
 * an independent baseline JPEG (own tables), decoded via the existing
 * jpeg_decode() — no new pixel-format or transform code, this file is
 * purely container framing + a playback-position/pacing helper.
 *
 * v2: adds an optional embedded mono PCM audio track. Format version
 * bumped to 2 and required (v1 files rejected cleanly via
 * MJPEG_ERR_BAD_VERSION) — simpler than supporting both, and nothing
 * outside this dev/test setup depends on v1 files existing.
 *
 * Same conventions as jpeg_decoder.h/png_decoder.h: u8/u32 types
 * (typedef'd in desktop.c before this file is #included), no
 * memset/memcpy, no libc.
 */
#ifndef MJPEG_CONTAINER_H
#define MJPEG_CONTAINER_H

/* AC97's ac97_stream_start() hard-rejects more than this many total
 * samples (MAX_WAV_SAMPLES in ac97.c) — mono at a modest sample rate
 * is what makes a several-second clip fit at all. Not enforced by the
 * reader (that's mkmjpeg.py's job at pack time, so a bad file fails
 * loudly when you try to create it, not silently at playback time),
 * but documented here since it's the reason audio is mono/16kHz. */
#define MJPEG_AC97_MAX_SAMPLES 122880

typedef struct {
    u32 width, height;
    u32 frame_count;
    u32 frame_duration_ms;
    const u8* frame_index_base; /* points at first frame's [u32 len][data] */
    u32 file_len;                /* total container length, for bounds checks */

    u32 audio_sample_rate;       /* 0 if no audio track */
    u8  audio_channels;          /* always 1 (mono) when audio present */
    u32 audio_sample_count;      /* 0 if no audio track */
    const u8* audio_data;        /* raw s16le PCM, audio_sample_count*2 bytes; 0 if no audio */
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

/* Parses the 36-byte v2 header and validates it. Does NOT walk/
 * validate every frame's length up front (that would mean a full
 * pass over a possibly-multi-MB file just to open it) —
 * mjpeg_get_frame() bounds-checks each frame lazily, on the access
 * that actually needs it.
 *
 * Header layout (little-endian):
 *   char[4]  magic              "YMJP"
 *   u8       version             2
 *   u8[3]    reserved            0
 *   u32      width
 *   u32      height
 *   u32      frame_count
 *   u32      frame_duration_ms
 *   u32      audio_sample_rate   (0 if no audio)
 *   u8       audio_channels      (0 if no audio, else 1)
 *   u8[3]    reserved
 *   u32      audio_sample_count  (0 if no audio)
 * Then: audio_sample_count*2 bytes of raw s16le PCM (absent if 0),
 * then frame_count entries of [u32 frame_len][frame_len bytes JPEG]. */
static u8 mjpeg_open(const u8* data, u32 len, mjpeg_info_t* out) {
    out->width = 0; out->height = 0; out->frame_count = 0;
    out->frame_duration_ms = 0; out->frame_index_base = 0; out->file_len = len;
    out->audio_sample_rate = 0; out->audio_channels = 0;
    out->audio_sample_count = 0; out->audio_data = 0;

    if (len < 36) return MJPEG_ERR_TRUNCATED;
    if (data[0] != 'Y' || data[1] != 'M' || data[2] != 'J' || data[3] != 'P') return MJPEG_ERR_BADMAGIC;
    if (data[4] != 2) return MJPEG_ERR_BAD_VERSION;

    u32 w   = (u32)data[8]  | ((u32)data[9]  << 8) | ((u32)data[10] << 16) | ((u32)data[11] << 24);
    u32 h   = (u32)data[12] | ((u32)data[13] << 8) | ((u32)data[14] << 16) | ((u32)data[15] << 24);
    u32 fc  = (u32)data[16] | ((u32)data[17] << 8) | ((u32)data[18] << 16) | ((u32)data[19] << 24);
    u32 fd  = (u32)data[20] | ((u32)data[21] << 8) | ((u32)data[22] << 16) | ((u32)data[23] << 24);
    u32 ar  = (u32)data[24] | ((u32)data[25] << 8) | ((u32)data[26] << 16) | ((u32)data[27] << 24);
    u8  ach = data[28];
    u32 asc = (u32)data[32] | ((u32)data[33] << 8) | ((u32)data[34] << 16) | ((u32)data[35] << 24);

    u32 audio_bytes = asc * 2u; /* s16le mono */
    if (36u + audio_bytes > len) return MJPEG_ERR_TRUNCATED;

    out->width = w; out->height = h; out->frame_count = fc; out->frame_duration_ms = fd;
    out->audio_sample_rate = ar; out->audio_channels = ach; out->audio_sample_count = asc;
    out->audio_data = (asc > 0) ? (data + 36) : 0;
    out->frame_index_base = data + 36 + audio_bytes;
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
    u32 header_and_audio = 36u + info->audio_sample_count * 2u;
    const u8* end = info->frame_index_base + (info->file_len - header_and_audio);
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

/* Pacing helper (silent clips / fallback): given elapsed wall-clock ms
 * since playback started and the frame duration, returns which frame
 * index should be showing now (clamped to frame_count-1). Pure
 * function of (info, elapsed_ms) — used directly for clips with no
 * audio track; for clips WITH audio, prefer
 * mjpeg_frame_for_played_samples() below, which derives the same
 * thing from the actual audio playback position instead of a
 * software tick count, so video can't drift relative to audio. */
static u32 mjpeg_frame_for_elapsed(const mjpeg_info_t* info, u32 elapsed_ms) {
    if (info->frame_duration_ms == 0 || info->frame_count == 0) return 0;
    u32 idx = elapsed_ms / info->frame_duration_ms;
    if (idx >= info->frame_count) idx = info->frame_count - 1;
    return idx;
}

/* Audio-as-sync-master pacing: derives the target video frame index
 * directly from how many audio samples have actually played (per
 * ac97_stream_played_samples()), rather than from a wall-clock tick
 * count. This is what keeps video locked to audio over a whole clip —
 * sys_ticks()-based pacing has no way to know if the audio driver
 * ever fell behind or got ahead, since it's a completely independent
 * clock; deriving frame index from the audio position itself makes
 * that impossible by construction. Only meaningful when the container
 * actually has an audio track (audio_sample_rate > 0) — caller should
 * fall back to mjpeg_frame_for_elapsed() otherwise. */
static u32 mjpeg_frame_for_played_samples(const mjpeg_info_t* info, u32 played_samples) {
    if (info->audio_sample_rate == 0 || info->frame_duration_ms == 0 || info->frame_count == 0) return 0;
    u32 elapsed_ms = (u32)(((u64)played_samples * 1000u) / info->audio_sample_rate);
    u32 idx = elapsed_ms / info->frame_duration_ms;
    if (idx >= info->frame_count) idx = info->frame_count - 1;
    return idx;
}

#endif /* MJPEG_CONTAINER_H */
