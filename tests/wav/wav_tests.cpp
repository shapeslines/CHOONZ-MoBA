#include "test.h"

#include "assets/wav.h"
#include "core/mem.h"

#include <cstring>

static void put_u16(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static size_t make_wav(uint8_t* out, uint16_t channels, uint32_t sample_rate,
                       uint16_t bits, const uint8_t* pcm, uint32_t pcm_bytes,
                       bool add_odd_junk) {
    size_t cursor = 0u;
    std::memcpy(out + cursor, "RIFF", 4u); cursor += 4u;
    const size_t riff_size_offset = cursor; cursor += 4u;
    std::memcpy(out + cursor, "WAVE", 4u); cursor += 4u;
    std::memcpy(out + cursor, "fmt ", 4u); cursor += 4u;
    put_u32(out + cursor, 16u); cursor += 4u;
    put_u16(out + cursor, 1u); cursor += 2u;
    put_u16(out + cursor, channels); cursor += 2u;
    put_u32(out + cursor, sample_rate); cursor += 4u;
    const uint16_t block_align = (uint16_t)(channels * (bits / 8u));
    put_u32(out + cursor, sample_rate * block_align); cursor += 4u;
    put_u16(out + cursor, block_align); cursor += 2u;
    put_u16(out + cursor, bits); cursor += 2u;
    if (add_odd_junk) {
        std::memcpy(out + cursor, "JUNK", 4u); cursor += 4u;
        put_u32(out + cursor, 3u); cursor += 4u;
        out[cursor++] = 7u; out[cursor++] = 8u; out[cursor++] = 9u;
        out[cursor++] = 0u;                         // RIFF pad byte
    }
    std::memcpy(out + cursor, "data", 4u); cursor += 4u;
    put_u32(out + cursor, pcm_bytes); cursor += 4u;
    std::memcpy(out + cursor, pcm, pcm_bytes); cursor += pcm_bytes;
    if ((pcm_bytes & 1u) != 0u) out[cursor++] = 0u;
    put_u32(out + riff_size_offset, (uint32_t)(cursor - 8u));
    return cursor;
}

static Allocator wav_test_allocator(Arena* arena, uint8_t* storage, size_t size) {
    arena_init_fixed(arena, storage, size);
    return arena_allocator(arena);
}

TEST(wav, pcm_header_and_samples_roundtrip) {
    const uint8_t pcm[] = {0x01,0x02, 0x03,0x04, 0x05,0x06, 0x07,0x08};
    uint8_t blob[128] = {};
    const size_t size = make_wav(blob, 2u, 48000u, 16u, pcm, sizeof(pcm), true);

    WavInfo info{};
    CHECK(wav_inspect_pcm(blob, size, &info));
    CHECK(info.sample_rate == 48000u && info.channels == 2u &&
          info.bits_per_sample == 16u && info.frame_count == 2u &&
          info.pcm_bytes == sizeof(pcm));

    alignas(16) uint8_t storage[256];
    Arena arena; Allocator alloc = wav_test_allocator(&arena, storage, sizeof(storage));
    WavPcm decoded{};
    CHECK(wav_decode_pcm(blob, size, alloc, &decoded));
    CHECK(decoded.sample_rate == info.sample_rate && decoded.frame_count == info.frame_count);
    CHECK(decoded.pcm != pcm && std::memcmp(decoded.pcm, pcm, sizeof(pcm)) == 0);
}

TEST(wav, odd_sized_pcm_requires_and_ignores_pad_byte) {
    const uint8_t pcm[] = {1u, 2u, 3u};
    uint8_t blob[96] = {};
    const size_t size = make_wav(blob, 1u, 22050u, 8u, pcm, sizeof(pcm), false);
    WavInfo info{};
    CHECK(wav_inspect_pcm(blob, size, &info));
    CHECK(info.frame_count == 3u && info.pcm_bytes == 3u);
    CHECK(!wav_inspect_pcm(blob, size - 1u, &info));
}

TEST(wav, malformed_headers_reject_before_allocation) {
    const uint8_t pcm[] = {0u,0u, 1u,0u};
    uint8_t blob[96] = {};
    const size_t size = make_wav(blob, 1u, 44100u, 16u, pcm, sizeof(pcm), false);

    alignas(16) uint8_t storage[128];
    Arena arena; Allocator alloc = wav_test_allocator(&arena, storage, sizeof(storage));
    WavPcm output{};
    output.sample_rate = 123u;
    output.pcm = (uint8_t*)(uintptr_t)0xBEEFu;

    CHECK(!wav_decode_pcm(nullptr, size, alloc, &output));
    CHECK(!wav_decode_pcm(blob, size - 1u, alloc, &output));
    CHECK(arena.offset == 0u);

    uint8_t corrupt[96];
    std::memcpy(corrupt, blob, size);
    put_u16(corrupt + 20u, 3u);                    // IEEE float, not PCM
    CHECK(!wav_decode_pcm(corrupt, size, alloc, &output));
    std::memcpy(corrupt, blob, size);
    put_u16(corrupt + 32u, 3u);                    // wrong block align
    CHECK(!wav_decode_pcm(corrupt, size, alloc, &output));
    std::memcpy(corrupt, blob, size);
    put_u32(corrupt + 28u, 1u);                    // wrong byte rate
    CHECK(!wav_decode_pcm(corrupt, size, alloc, &output));
    std::memcpy(corrupt, blob, size);
    put_u16(corrupt + 22u, 3u);                    // more than stereo
    CHECK(!wav_decode_pcm(corrupt, size, alloc, &output));

    CHECK(arena.offset == 0u);
    CHECK(output.sample_rate == 123u && output.pcm == (uint8_t*)(uintptr_t)0xBEEFu);
}
