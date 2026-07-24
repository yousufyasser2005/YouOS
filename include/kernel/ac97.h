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
