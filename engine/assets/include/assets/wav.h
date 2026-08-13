#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/mem.h"

typedef struct WavInfo {
    size_t   pcm_bytes;
    uint32_t sample_rate;
    uint32_t frame_count;
    uint16_t channels;
    uint16_t bits_per_sample;
} WavInfo;

typedef struct WavPcm {
    uint8_t* pcm;
    size_t   pcm_bytes;
    uint32_t sample_rate;
    uint32_t frame_count;
    uint16_t channels;
    uint16_t bits_per_sample;
} WavPcm;

// Strict RIFF/WAVE PCM: one fmt chunk, one data chunk, 1-2 channels, integer
// 8/16/24/32-bit samples, and internally consistent byte/block rates. Unknown
// padded chunks are skipped. Failure is allocation-free and leaves outputs intact.
bool wav_inspect_pcm(const void* bytes, size_t size, WavInfo* out);
bool wav_decode_pcm(const void* bytes, size_t size, Allocator alloc, WavPcm* out);
