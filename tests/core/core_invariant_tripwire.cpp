#include "core/array.h"
#include "core/hashmap.h"
#include "core/mem.h"

#include <cstdint>
#include <cstring>

static bool reject_commit(void*, size_t) { return false; }

int main(int argc, char** argv) {
    if (argc != 2) return 64;
    const char* mode = argv[1];

    if (std::strcmp(mode, "array-length") == 0) {
        uint32_t slot = 0;
        Array<uint32_t> a{&slot, UINT32_MAX, UINT32_MAX, {}};
        array_push(&a, 1u);
    } else if (std::strcmp(mode, "array-capacity") == 0) {
        uint32_t slot = 0;
        Array<uint32_t> a{&slot, 0u, 0x80000000u, {}};
        array_reserve(&a, UINT32_MAX);
    } else if (std::strcmp(mode, "array-bytes") == 0) {
        (void)array__allocation_bytes(2u, SIZE_MAX);
    } else if (std::strcmp(mode, "hashmap-capacity") == 0) {
        using Map = HashMap<uint32_t, uint32_t>;
        Map::Slot slot{};
        Map m{&slot, 0x80000000u, 0x7fffffffu, 0x70000000u, {}};
        hashmap_set(&m, 1u, 2u);
    } else if (std::strcmp(mode, "hashmap-bytes") == 0) {
        (void)hashmap__allocation_bytes(2u, SIZE_MAX);
    } else if (std::strcmp(mode, "arena-init-state") == 0) {
        Arena a{};
        uint8_t buffer[8]{};
        arena_init(&a, buffer, sizeof(buffer), sizeof(buffer) + 1u, nullptr, 0u);
    } else if (std::strcmp(mode, "arena-init-address") == 0) {
        Arena a{};
        arena_init(&a, reinterpret_cast<void*>(UINTPTR_MAX - 3u), 8u, 0u, nullptr, 0u);
    } else if (std::strcmp(mode, "arena-uninitialized") == 0) {
        Arena a{};
        (void)arena_push(&a, 1u, 1u);
    } else if (std::strcmp(mode, "arena-state-order") == 0) {
        uint8_t buffer[8]{};
        Arena a{buffer, 9u, 8u, 8u, 9u, nullptr, 0u};
        (void)arena_push(&a, 1u, 1u);
    } else if (std::strcmp(mode, "arena-address") == 0) {
        Arena a{reinterpret_cast<uint8_t*>(UINTPTR_MAX - 3u), 0u, 4u, 4u, 0u, nullptr, 0u};
        (void)arena_push(&a, 1u, 1u);
    } else if (std::strcmp(mode, "arena-alignment") == 0) {
        Arena a{reinterpret_cast<uint8_t*>(UINTPTR_MAX - 7u), 0u, 7u, 7u, 0u, nullptr, 0u};
        (void)arena_push(&a, 1u, 16u);
    } else if (std::strcmp(mode, "arena-end") == 0) {
        uint8_t buffer[8]{};
        Arena a{buffer, 0u, sizeof(buffer), sizeof(buffer), 0u, nullptr, 0u};
        (void)arena_push(&a, sizeof(buffer) + 1u, 1u);
    } else if (std::strcmp(mode, "arena-rounding") == 0) {
        Arena a{reinterpret_cast<uint8_t*>(1u), 0u, 0u, SIZE_MAX - 1u, 0u,
                reject_commit, 4u};
        (void)arena_push(&a, SIZE_MAX - 1u, 1u);
    } else {
        return 64;
    }

    return 0;
}
