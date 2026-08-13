#include "assets/wav.h"

#include <string.h>

typedef struct WavParsed {
    WavInfo info;
    const uint8_t* pcm;
} WavParsed;

static uint16_t wav_u16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t wav_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static bool wav_tag(const uint8_t* p, const char* tag) {
    return p[0] == (uint8_t)tag[0] && p[1] == (uint8_t)tag[1] &&
           p[2] == (uint8_t)tag[2] && p[3] == (uint8_t)tag[3];
}

static bool parse_wav(const void* bytes, size_t size, WavParsed* out) {
    if (!bytes || !out || size < 12u) return false;
    const uint8_t* data = (const uint8_t*)bytes;
    if (!wav_tag(data, "RIFF") || !wav_tag(data + 8u, "WAVE")) return false;
    const uint32_t riff_payload = wav_u32(data + 4u);
    if ((uint64_t)riff_payload + 8u != (uint64_t)size) return false;

    bool saw_fmt = false;
    bool saw_data = false;
    uint16_t format = 0u;
    uint16_t channels = 0u;
    uint16_t bits = 0u;
    uint16_t block_align = 0u;
    uint32_t sample_rate = 0u;
    uint32_t byte_rate = 0u;
    const uint8_t* pcm = nullptr;
    size_t pcm_bytes = 0u;

    size_t cursor = 12u;
    while (cursor < size) {
        if (size - cursor < 8u) return false;
        const uint8_t* chunk = data + cursor;
        const uint32_t chunk_size32 = wav_u32(chunk + 4u);
        const size_t chunk_size = chunk_size32;
        cursor += 8u;
        if (chunk_size > size - cursor) return false;
        const uint8_t* payload = data + cursor;

        if (wav_tag(chunk, "fmt ")) {
            if (saw_fmt || chunk_size < 16u) return false;
            saw_fmt = true;
            format = wav_u16(payload + 0u);
            channels = wav_u16(payload + 2u);
            sample_rate = wav_u32(payload + 4u);
            byte_rate = wav_u32(payload + 8u);
            block_align = wav_u16(payload + 12u);
            bits = wav_u16(payload + 14u);
        } else if (wav_tag(chunk, "data")) {
            if (saw_data) return false;
            saw_data = true;
            pcm = payload;
            pcm_bytes = chunk_size;
        }

        cursor += chunk_size;
        if ((chunk_size & 1u) != 0u) {
            if (cursor >= size) return false;
            ++cursor;
        }
    }
    if (cursor != size || !saw_fmt || !saw_data || format != 1u ||
        channels == 0u || channels > 2u || sample_rate == 0u ||
        sample_rate > 384000u ||
        (bits != 8u && bits != 16u && bits != 24u && bits != 32u)) return false;

    const uint32_t bytes_per_sample = bits / 8u;
    const uint32_t expected_align = (uint32_t)channels * bytes_per_sample;
    const uint64_t expected_rate = (uint64_t)sample_rate * expected_align;
    if (expected_align == 0u || expected_align > UINT16_MAX ||
        block_align != expected_align || expected_rate > UINT32_MAX ||
        byte_rate != (uint32_t)expected_rate || pcm_bytes == 0u ||
        pcm_bytes % expected_align != 0u || pcm_bytes / expected_align > UINT32_MAX)
        return false;

    WavParsed parsed{};
    parsed.info.pcm_bytes = pcm_bytes;
    parsed.info.sample_rate = sample_rate;
    parsed.info.frame_count = (uint32_t)(pcm_bytes / expected_align);
    parsed.info.channels = channels;
    parsed.info.bits_per_sample = bits;
    parsed.pcm = pcm;
    *out = parsed;
    return true;
}

bool wav_inspect_pcm(const void* bytes, size_t size, WavInfo* out) {
    if (!out) return false;
    WavParsed parsed{};
    if (!parse_wav(bytes, size, &parsed)) return false;
    *out = parsed.info;
    return true;
}

bool wav_decode_pcm(const void* bytes, size_t size, Allocator alloc, WavPcm* out) {
    if (!out || !alloc.fn) return false;
    WavParsed parsed{};
    if (!parse_wav(bytes, size, &parsed)) return false;
    uint8_t* pcm = (uint8_t*)mem_alloc(alloc, parsed.info.pcm_bytes, MEM_DEFAULT_ALIGN);
    if (!pcm) return false;
    memcpy(pcm, parsed.pcm, parsed.info.pcm_bytes);

    WavPcm result{};
    result.pcm = pcm;
    result.pcm_bytes = parsed.info.pcm_bytes;
    result.sample_rate = parsed.info.sample_rate;
    result.frame_count = parsed.info.frame_count;
    result.channels = parsed.info.channels;
    result.bits_per_sample = parsed.info.bits_per_sample;
    *out = result;
    return true;
}
