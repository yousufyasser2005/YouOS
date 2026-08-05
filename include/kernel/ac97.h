#pragma once
#include <stdint.h>

/*
 * ac97.c — AC97 audio driver (Phase 3.5 media-support effort, stage 1:
 * audio driver, prerequisite for WAV/MP3 playback).
 *
 * Detected via PCI (vendor 0x8086 Intel, device 0x2415 — the ICH
 * 82801AA AC97 controller QEMU's -device AC97 emulates). Two I/O-space
 * BARs: NAM (mixer — reset, volume) and NABM (bus master — DMA buffer
 * descriptor list, playback control, interrupts).
 *
 * PCM out only (no recording, no mic input) — this is a playback-only
 * driver, matching what WAV/MP3 stages actually need.
 */

int ac97_init(void);

/* Queues a raw PCM buffer for playback: 16-bit signed samples,
 * interleaved if stereo. sample_rate/channels describe the data (AC97
 * itself is commonly fixed at 48kHz internally on real hardware, but
 * QEMU's model accepts whatever rate is requested via the mixer rate
 * registers — set here to match). Blocks until the whole buffer has
 * been handed to hardware (not until playback finishes) — no async
 * queueing yet, one buffer at a time. Returns 0 on success. */
int ac97_play_pcm(const int16_t* samples, uint32_t sample_count,
                   uint32_t sample_rate, uint8_t channels);

/* Non-blocking: 1 if the last submitted buffer has finished playing
 * (i.e. the whole ring has fully drained — nothing left in flight). */
int ac97_is_done(void);

/* Non-blocking: 1 if there is a free ring slot to accept another
 * chunk right now without stalling. Callers doing streamed/chunked
 * playback (e.g. WAV) should submit their next chunk as soon as this
 * is true, rather than waiting for ac97_is_done() — that's what
 * keeps a second buffer queued ahead of the hardware and avoids the
 * per-chunk stop/reset/restart cycle. */
int ac97_can_submit(void);

/* Streaming API (replaces per-chunk userspace polling). Copies the
 * ENTIRE PCM buffer into kernel memory and plays it back, refilling
 * the ring from the AC97 completion interrupt itself rather than a
 * userspace poll loop. Returns 0 on success. */
int ac97_stream_start(const int16_t* samples, uint32_t total_samples,
                       uint32_t sample_rate, uint8_t channels);
/* Non-blocking: 1 while a stream is still playing (including ring
 * drain of the final buffer), 0 once fully finished. */
int ac97_stream_is_playing(void);
/* Called from the timer IRQ each tick to drive paced refills. */
void ac97_stream_tick(void);

/* Debug only: packed (restart_count<<16)|fastpath_count, for
 * verifying the double-buffering fast path is actually being taken
 * instead of falling back to a cold restart every chunk. */
uint32_t ac97_debug_path_counts(void);

/* Debug only: WHERE restarts happened, see ac97.c for detail. */
void ac97_debug_restart_log_reset(void);
uint32_t ac97_debug_restart_log_get(uint32_t idx);
uint32_t ac97_debug_cold_start_duration(void);
