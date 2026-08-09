#!/usr/bin/env python3
"""
mkmjpeg.py — Pack a video (or an ffmpeg-readable input) into YouOS's
.ymjp Motion JPEG container: a fixed-rate sequence of independent
baseline JPEG frames, each self-contained (own quant/Huffman tables),
so the existing jpeg_decode() can decode any frame with no persistent
state between frames — matches the "get it working, iterate later"
spirit of the rest of the media effort (no B/P-frame prediction, no
audio track, no seeking beyond linear frame index — YouOS's own
sys_fread has no seek syscall either, so this matches what the
freestanding side can actually do).

Usage: python3 tools/mkmjpeg.py input.mp4 output.ymjp [--fps 15] [--width 320] [--height 240] [--quality 5]

Container format (all integers little-endian):
  Header (24 bytes):
    char[4]  magic            "YMJP"
    u8       version          1
    u8[3]    reserved         0
    u32      width
    u32      height
    u32      frame_count
    u32      frame_duration_ms   (1000/fps, fixed rate)
  Then frame_count entries, each:
    u32      frame_len
    u8[frame_len]  jpeg_data   (baseline SOF0, self-contained)

Requires ffmpeg on PATH. width/height must fit within JPEG_MAX_W/H
(1024x768) — same bound as the image viewer's still-JPEG support.
"""
import struct, sys, os, subprocess, tempfile, shutil, argparse

MAGIC = b"YMJP"
VERSION = 1
JPEG_MAX_W = 1024
JPEG_MAX_H = 768


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


def pack(frame_paths, width, height, fps, out_path):
    frame_duration_ms = round(1000 / fps)
    header = struct.pack("<4sBBBBIII", MAGIC, VERSION, 0, 0, 0, width, height, len(frame_paths))
    header += struct.pack("<I", frame_duration_ms)
    assert len(header) == 24, f"header size drifted: {len(header)}"

    with open(out_path, "wb") as out:
        out.write(header)
        total_jpeg_bytes = 0
        for idx, fp in enumerate(frame_paths):
            with open(fp, "rb") as f:
                data = f.read()
            verify_baseline(data, idx)
            out.write(struct.pack("<I", len(data)))
            out.write(data)
            total_jpeg_bytes += len(data)

    return frame_duration_ms, total_jpeg_bytes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--quality", type=int, default=5, help="ffmpeg -q:v (2=best/largest .. 31=worst/smallest)")
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
        frame_duration_ms, total_jpeg_bytes = pack(frames, args.width, args.height, args.fps, args.output)
        out_size = os.path.getsize(args.output)
        print(f"wrote {args.output}: {len(frames)} frames, {args.width}x{args.height} @ {args.fps}fps "
              f"({frame_duration_ms}ms/frame), {out_size} bytes total ({total_jpeg_bytes} bytes of JPEG data)")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main()
