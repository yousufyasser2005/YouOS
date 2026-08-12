#!/usr/bin/env python3
"""
mkmjpeg.py — Pack a video (or an ffmpeg-readable input) into YouOS's
.ymjp Motion JPEG container: a fixed-rate sequence of independent
baseline JPEG frames, each self-contained (own quant/Huffman tables),
so the existing jpeg_decode() can decode any frame with no persistent
state between frames — matches the "get it working, iterate later"
spirit of the rest of the media effort (no B/P-frame prediction, no
seeking beyond linear frame index).

v2 adds an optional embedded mono PCM audio track, muxed automatically
if the source has an audio stream (use --no-audio to force silent).
Audio drives playback as the sync master on-device (see
mjpeg_frame_for_played_samples() in mjpeg_container.h) — video frame
selection is derived from actual audio playback position, not a
separate wall-clock timer, so it can't drift.

Usage: python3 tools/mkmjpeg.py input.mp4 output.ymjp [--fps 15] [--width 320] [--height 240] [--quality 5] [--audio-rate 16000] [--no-audio]

Container format (all integers little-endian):
  Header (36 bytes):
    char[4]  magic              "YMJP"
    u8       version             2
    u8[3]    reserved            0
    u32      width
    u32      height
    u32      frame_count
    u32      frame_duration_ms   (1000/fps, fixed rate)
    u32      audio_sample_rate   (0 if no audio track)
    u8       audio_channels      (0 if no audio, else 1 — mono only)
    u8[3]    reserved
    u32      audio_sample_count  (0 if no audio track)
  Then audio_sample_count*2 bytes of raw s16le PCM (absent if 0),
  then frame_count entries, each:
    u32      frame_len
    u8[frame_len]  jpeg_data     (baseline SOF0, self-contained)

Requires ffmpeg on PATH. width/height must fit within JPEG_MAX_W/H
(1024x768) — same bound as the image viewer's still-JPEG support.
Audio is capped at MJPEG_AC97_MAX_SAMPLES (122880 samples — the
freestanding AC97 driver's ac97_stream_start() hard limit); mono at a
modest sample rate (16kHz default) is what makes several seconds of
audio fit at all. A clip whose audio would exceed the cap fails
loudly here at pack time rather than misbehaving on-device.
"""
import struct, sys, os, subprocess, tempfile, shutil, argparse

MAGIC = b"YMJP"
VERSION = 2
JPEG_MAX_W = 1024
JPEG_MAX_H = 768
AC97_MAX_SAMPLES = 122880


def extract_frames(input_path, tmpdir, fps, width, height, quality):
    pattern = os.path.join(tmpdir, "frame_%06d.jpg")
    scale = f"scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2"
    cmd = [
        "ffmpeg", "-y", "-i", input_path,
        "-vf", f"fps={fps},{scale}",
        "-q:v", str(quality),
        "-pix_fmt", "yuvj420p",   # 4:2:0, matches jpeg_decoder.h's tested/primary path
        pattern,
    ]
    subprocess.run(cmd, check=True, capture_output=True)
    files = sorted(f for f in os.listdir(tmpdir) if f.endswith(".jpg"))
    if not files:
        raise RuntimeError("ffmpeg produced no frames — check input path/codec support")
    return [os.path.join(tmpdir, f) for f in files]


def probe_has_audio(input_path):
    cmd = ["ffprobe", "-v", "error", "-select_streams", "a",
           "-show_entries", "stream=index", "-of", "csv=p=0", input_path]
    try:
        out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
        return len(out.strip()) > 0
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False  # no ffprobe, or it errored — treat as "no audio", not fatal


def extract_audio(input_path, tmpdir, sample_rate):
    """Raw s16le mono PCM at the given sample rate. Returns bytes, or
    b"" if the source has no audio track (checked via ffprobe first,
    so this doesn't fail the whole pack for a silent source)."""
    if not probe_has_audio(input_path):
        return b""
    out_path = os.path.join(tmpdir, "audio.raw")
    cmd = [
        "ffmpeg", "-y", "-i", input_path,
        "-vn", "-ac", "1", "-ar", str(sample_rate), "-f", "s16le",
        out_path,
    ]
    subprocess.run(cmd, check=True, capture_output=True)
    with open(out_path, "rb") as f:
        return f.read()


def verify_baseline(jpeg_bytes, frame_idx):
    """Reject progressive frames early with a clear error, rather than
    letting them silently fail at decode time on-device — matches
    jpeg_decoder.h's own JPEG_ERR_UNSUPPORTED philosophy."""
    i = 2
    n = len(jpeg_bytes)
    while i + 4 <= n:
        if jpeg_bytes[i] != 0xFF:
            i += 1
            continue
        marker = jpeg_bytes[i + 1]
        if marker == 0xD8:
            i += 2
            continue
        if marker == 0xD9 or marker == 0xDA:
            break
        if marker in (0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            raise RuntimeError(f"frame {frame_idx}: non-baseline SOF marker 0x{marker:02X} "
                                f"(ffmpeg produced a progressive/other JPEG — pass -q:v with a "
                                f"plain mjpeg-capable ffmpeg build, or check pixel format)")
        if marker == 0xC0:
            return
        seglen = struct.unpack(">H", jpeg_bytes[i+2:i+4])[0]
        i += 2 + seglen
    raise RuntimeError(f"frame {frame_idx}: no SOF0 marker found (corrupt frame?)")


def pack(frame_paths, width, height, fps, audio_bytes, audio_rate, out_path):
    frame_duration_ms = round(1000 / fps)
    audio_sample_count = len(audio_bytes) // 2
    audio_channels = 1 if audio_sample_count > 0 else 0
    effective_rate = audio_rate if audio_sample_count > 0 else 0

    header = struct.pack("<4sBBBBIIII", MAGIC, VERSION, 0, 0, 0,
                          width, height, len(frame_paths), frame_duration_ms)
    header += struct.pack("<IBBBBI", effective_rate, audio_channels, 0, 0, 0, audio_sample_count)
    assert len(header) == 36, f"header size drifted: {len(header)}"

    with open(out_path, "wb") as out:
        out.write(header)
        out.write(audio_bytes)
        total_jpeg_bytes = 0
        for idx, fp in enumerate(frame_paths):
            with open(fp, "rb") as f:
                data = f.read()
            verify_baseline(data, idx)
            out.write(struct.pack("<I", len(data)))
            out.write(data)
            total_jpeg_bytes += len(data)

    return frame_duration_ms, total_jpeg_bytes, audio_sample_count, effective_rate


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--quality", type=int, default=5, help="ffmpeg -q:v (2=best/largest .. 31=worst/smallest)")
    ap.add_argument("--audio-rate", type=int, default=16000, help="mono PCM sample rate for the embedded audio track")
    ap.add_argument("--no-audio", action="store_true", help="force a silent .ymjp even if the source has audio")
    args = ap.parse_args()

    if args.width > JPEG_MAX_W or args.height > JPEG_MAX_H:
        print(f"error: {args.width}x{args.height} exceeds JPEG_MAX_W/H ({JPEG_MAX_W}x{JPEG_MAX_H})", file=sys.stderr)
        sys.exit(1)
    if args.width % 2 or args.height % 2:
        print("error: width/height must be even (4:2:0 subsampling)", file=sys.stderr)
        sys.exit(1)

    tmpdir = tempfile.mkdtemp(prefix="mkmjpeg_")
    try:
        frames = extract_frames(args.input, tmpdir, args.fps, args.width, args.height, args.quality)

        audio_bytes = b"" if args.no_audio else extract_audio(args.input, tmpdir, args.audio_rate)
        audio_sample_count = len(audio_bytes) // 2
        if audio_sample_count > AC97_MAX_SAMPLES:
            max_secs = AC97_MAX_SAMPLES / args.audio_rate
            got_secs = audio_sample_count / args.audio_rate
            print(f"error: audio track is {got_secs:.2f}s ({audio_sample_count} samples at "
                  f"{args.audio_rate}Hz mono), exceeds the AC97 driver's hard cap of "
                  f"{AC97_MAX_SAMPLES} samples ({max_secs:.2f}s max). Trim the source, lower "
                  f"--audio-rate, or pass --no-audio for a silent clip.", file=sys.stderr)
            sys.exit(1)

        frame_duration_ms, total_jpeg_bytes, asc, arate = pack(
            frames, args.width, args.height, args.fps, audio_bytes, args.audio_rate, args.output)
        out_size = os.path.getsize(args.output)
        audio_desc = f"{asc} samples @ {arate}Hz mono ({asc/arate:.2f}s)" if asc > 0 else "none (silent)"
        print(f"wrote {args.output}: {len(frames)} frames, {args.width}x{args.height} @ {args.fps}fps "
              f"({frame_duration_ms}ms/frame), audio: {audio_desc}, {out_size} bytes total "
              f"({total_jpeg_bytes} bytes JPEG, {len(audio_bytes)} bytes audio)")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main()
