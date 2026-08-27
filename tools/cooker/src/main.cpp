#include "assets/asset_id.h"
#include "assets/mba.h"
#include "assets/tga.h"
#include "core/mem.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct CookerOptions {
    const char* input = nullptr;
    const char* asset_path = nullptr;
    const char* output = nullptr;
};

static void cooker_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " --input source.tga --asset canonical/asset.mba --output baked.mba\n";
}

static bool cooker_parse_options(int argc, char** argv, CookerOptions* out) {
    if (!out || argc != 7) return false;
    CookerOptions options{};
    for (int i = 1; i < argc; i += 2) {
        const char* option = argv[i];
        const char* value = argv[i + 1];
        if (!option || !value || value[0] == '\0') return false;
        if (std::strcmp(option, "--input") == 0 && !options.input) options.input = value;
        else if (std::strcmp(option, "--asset") == 0 && !options.asset_path)
            options.asset_path = value;
        else if (std::strcmp(option, "--output") == 0 && !options.output)
            options.output = value;
        else return false;
    }
    if (!options.input || !options.asset_path || !options.output) return false;
    *out = options;
    return true;
}

static bool cooker_read_file(const char* path, std::vector<uint8_t>* out) {
    if (!path || !out) return false;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const std::streamoff end = input.tellg();
    if (end <= 0 || (uintmax_t)end > (uintmax_t)std::numeric_limits<size_t>::max() ||
        (uintmax_t)end > (uintmax_t)std::numeric_limits<std::streamsize>::max())
        return false;
    const size_t size = (size_t)end;
    std::vector<uint8_t> data(size);
    input.seekg(0, std::ios::beg);
    if (!input.read((char*)data.data(), (std::streamsize)size)) return false;
    *out = std::move(data);
    return true;
}

static bool cooker_write_file(const char* path, const uint8_t* bytes, size_t size) {
    if (!path || !bytes || size == 0u) return false;
    std::error_code error;
    const std::filesystem::path output(path);
    const std::filesystem::path parent = output.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    if (error) return false;
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write((const char*)bytes, (std::streamsize)size);
    return stream.good();
}

int main(int argc, char** argv) {
    CookerOptions options{};
    if (!cooker_parse_options(argc, argv, &options)) {
        cooker_usage(argc > 0 && argv[0] ? argv[0] : "cooker");
        return 2;
    }

    AssetId asset_id = ASSET_ID_NULL;
    if (!asset_id_from_path(options.asset_path, &asset_id)) {
        std::cerr << "Invalid canonical asset path: " << options.asset_path << "\n";
        return 1;
    }

    std::vector<uint8_t> source_bytes;
    if (!cooker_read_file(options.input, &source_bytes)) {
        std::cerr << "Unable to read TGA source: " << options.input << "\n";
        return 1;
    }

    uint32_t width = 0u;
    uint32_t height = 0u;
    size_t rgba8_bytes = 0u;
    if (!tga_inspect(source_bytes.data(), source_bytes.size(), &width, &height,
                     &rgba8_bytes)) {
        std::cerr << "Unsupported or malformed TGA: " << options.input << "\n";
        return 1;
    }

    std::vector<uint8_t> rgba8(rgba8_bytes);
    Arena decode_arena{};
    arena_init_fixed(&decode_arena, rgba8.data(), rgba8.size());
    TgaImage image{};
    if (!tga_decode(source_bytes.data(), source_bytes.size(), arena_allocator(&decode_arena),
                    &image)) {
        std::cerr << "TGA decode failed: " << options.input << "\n";
        return 1;
    }

    const MbaTextureSource texture{image.rgba8, rgba8_bytes, image.width, image.height};
    size_t baked_bytes = 0u;
    if (!mba_texture_measure(&texture, &baked_bytes)) {
        std::cerr << "Invalid decoded texture dimensions\n";
        return 1;
    }
    std::vector<uint8_t> baked(baked_bytes);
    size_t written_bytes = 0u;
    if (!mba_encode_texture(baked.data(), baked.size(), asset_id, &texture, &written_bytes) ||
        written_bytes != baked.size() ||
        !cooker_write_file(options.output, baked.data(), baked.size())) {
        std::cerr << "Unable to write baked asset: " << options.output << "\n";
        return 1;
    }

    std::cout << "cooked asset=" << options.asset_path << " width=" << width
              << " height=" << height << " bytes=" << baked.size() << "\n";
    return 0;
}
