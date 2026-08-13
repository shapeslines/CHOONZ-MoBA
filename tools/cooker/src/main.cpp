#include "assets/asset_catalog.h"
#include "assets/asset_id.h"
#include "assets/mba.h"
#include "assets/tga.h"
#include "assets/wav.h"
#include "core/mem.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <malloc.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static constexpr size_t COOKER_MAX_MANIFEST_BYTES = 4u * 1024u * 1024u;
static constexpr size_t COOKER_MAX_SOURCE_BYTES = 256u * 1024u * 1024u;
static constexpr size_t COOKER_MAX_BAKED_ASSET_BYTES = 256u * 1024u * 1024u;
static constexpr size_t COOKER_MAX_TOTAL_BAKED_BYTES = 1024u * 1024u * 1024u;
static constexpr size_t COOKER_MAX_ASSETS = 4096u;

struct CookerArgs {
    const char* source_root;
    const char* manifest;
    const char* out_root;
    const char* generated_root;
};

struct ManifestEntry {
    std::string logical_path;
    std::string baked_path;
    std::string symbol;
    AssetId id;
    AssetType type;
};

struct CookedAsset {
    ManifestEntry entry;
    std::vector<uint8_t> bytes;
};

enum class CookerResult {
    Ok,
    IoFailure,
    InvalidInput,
};

static void print_usage(void) {
    std::fprintf(stderr,
        "usage: moba_cooker --source-root <dir> --manifest <file> "
        "--out-root <dir> --generated-root <dir>\n");
}

static bool parse_args(int argc, char** argv, CookerArgs* out) {
    if (!out) return false;
    CookerArgs args{};
    bool source_seen = false;
    bool manifest_seen = false;
    bool out_seen = false;
    bool generated_seen = false;
    for (int i = 1; i < argc; ++i) {
        const char* option = argv[i];
        const bool known = option &&
            (std::strcmp(option, "--source-root") == 0 ||
             std::strcmp(option, "--manifest") == 0 ||
             std::strcmp(option, "--out-root") == 0 ||
             std::strcmp(option, "--generated-root") == 0);
        if (!known || i + 1 >= argc || !argv[i + 1] || argv[i + 1][0] == '\0' ||
            argv[i + 1][0] == '-') return false;
        const char* value = argv[++i];
        if (std::strcmp(option, "--source-root") == 0) {
            if (source_seen) return false;
            source_seen = true;
            args.source_root = value;
        } else if (std::strcmp(option, "--manifest") == 0) {
            if (manifest_seen) return false;
            manifest_seen = true;
            args.manifest = value;
        } else if (std::strcmp(option, "--out-root") == 0) {
            if (out_seen) return false;
            out_seen = true;
            args.out_root = value;
        } else {
            if (generated_seen) return false;
            generated_seen = true;
            args.generated_root = value;
        }
    }
    if (!source_seen || !manifest_seen || !out_seen || !generated_seen) return false;
    *out = args;
    return true;
}

static bool ends_with(const std::string& value, const char* suffix) {
    const size_t suffix_length = std::strlen(suffix);
    return value.size() >= suffix_length &&
        value.compare(value.size() - suffix_length, suffix_length, suffix) == 0;
}

static std::string asset_symbol(const std::string& path) {
    std::string result("ASSET_");
    result.reserve(result.size() + path.size());
    for (char c : path) {
        if (c >= 'a' && c <= 'z') result.push_back((char)(c - 'a' + 'A'));
        else if (c >= '0' && c <= '9') result.push_back(c);
        else result.push_back('_');
    }
    return result;
}

static bool directory_is_plain(const char* path) {
    if (!path || path[0] == '\0') return false;
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(std::filesystem::u8path(path), error);
    return !error && std::filesystem::is_directory(status) &&
        !std::filesystem::is_symlink(status);
}

static bool rooted_parent_is_plain(const char* root, const std::string& relative) {
    if (!directory_is_plain(root)) return false;
    std::filesystem::path current = std::filesystem::u8path(root);
    const std::filesystem::path parent =
        std::filesystem::u8path(relative).parent_path();
    for (const std::filesystem::path& part : parent) {
        current /= part;
        std::error_code error;
        const std::filesystem::file_status status =
            std::filesystem::symlink_status(current, error);
        if (error || !std::filesystem::is_directory(status) ||
            std::filesystem::is_symlink(status)) return false;
    }
    return true;
}

static Allocator cooker_heap_allocator(void);
static void release_platform_file(PlatformFile* file);

static CookerResult parse_manifest_bytes(const void* data, size_t data_bytes,
                                         std::vector<ManifestEntry>* out_entries) {
    if (!data || !out_entries) return CookerResult::InvalidInput;
    std::vector<ManifestEntry> entries;
    std::map<AssetId, std::string> ids;
    std::set<std::string> paths;
    std::set<std::string> baked_paths;
    std::set<std::string> symbols;
    std::string previous;
    size_t line_number = 0u;
    size_t cursor = 0u;
    const char* bytes = static_cast<const char*>(data);
    while (cursor < data_bytes) {
        ++line_number;
        const size_t line_begin = cursor;
        while (cursor < data_bytes && bytes[cursor] != '\n') {
            if (bytes[cursor] == '\0' || cursor - line_begin >= ASSET_PATH_MAX) {
                std::fprintf(stderr, "cooker: invalid manifest line %zu\n", line_number);
                return CookerResult::InvalidInput;
            }
            ++cursor;
        }
        size_t line_bytes = cursor - line_begin;
        if (cursor < data_bytes) ++cursor;
        if (line_bytes != 0u && bytes[line_begin + line_bytes - 1u] == '\r')
            --line_bytes;
        if (line_bytes == 0u || line_bytes >= ASSET_PATH_MAX ||
            entries.size() >= COOKER_MAX_ASSETS) {
            std::fprintf(stderr, "cooker: invalid manifest line %zu\n", line_number);
            return CookerResult::InvalidInput;
        }
        const std::string line(bytes + line_begin, line_bytes);

        char normalized[ASSET_PATH_MAX];
        size_t normalized_length = 0u;
        if (!asset_path_normalize(line.c_str(), normalized, sizeof(normalized),
                                  &normalized_length) ||
            normalized_length != line.size() ||
            std::memcmp(normalized, line.data(), line.size()) != 0) {
            std::fprintf(stderr, "cooker: manifest path is not canonical at line %zu\n",
                         line_number);
            return CookerResult::InvalidInput;
        }
        if (!previous.empty() && previous >= line) {
            std::fprintf(stderr, "cooker: manifest must be strictly sorted at line %zu\n",
                         line_number);
            return CookerResult::InvalidInput;
        }
        previous = line;

        AssetType type = ASSET_TYPE_NONE;
        if (ends_with(line, ".tga")) type = ASSET_TYPE_TEXTURE;
        else if (ends_with(line, ".wav")) type = ASSET_TYPE_SOUND;
        else {
            std::fprintf(stderr, "cooker: unsupported source type '%s'\n", line.c_str());
            return CookerResult::InvalidInput;
        }

        ManifestEntry entry{};
        entry.logical_path = line;
        entry.baked_path = line + ".mba";
        entry.symbol = asset_symbol(line);
        entry.id = asset_id_normalized_bytes(line.data(), line.size());
        entry.type = type;
        if (entry.id == ASSET_ID_NULL || !paths.insert(entry.logical_path).second ||
            !baked_paths.insert(entry.baked_path).second ||
            !symbols.insert(entry.symbol).second) {
            std::fprintf(stderr, "cooker: path/output/symbol collision for '%s'\n",
                         line.c_str());
            return CookerResult::InvalidInput;
        }
        const auto id_result = ids.emplace(entry.id, entry.logical_path);
        if (!id_result.second) {
            std::fprintf(stderr, "cooker: AssetId collision '%s' and '%s'\n",
                         id_result.first->second.c_str(), line.c_str());
            return CookerResult::InvalidInput;
        }
        entries.push_back(entry);
    }
    if (entries.empty()) {
        std::fprintf(stderr, "cooker: manifest contains no supported assets\n");
        return CookerResult::InvalidInput;
    }
    *out_entries = std::move(entries);
    return CookerResult::Ok;
}

static CookerResult read_manifest(const char* path,
                                  std::vector<ManifestEntry>* out_entries) {
    if (!path || !out_entries) return CookerResult::InvalidInput;
    size_t observed_bytes = 0u;
    if (!platform_file_size(path, &observed_bytes)) return CookerResult::IoFailure;
    if (observed_bytes > COOKER_MAX_MANIFEST_BYTES) {
        std::fprintf(stderr, "cooker: manifest exceeds %zu bytes\n",
                     COOKER_MAX_MANIFEST_BYTES);
        return CookerResult::InvalidInput;
    }

    PlatformFile manifest{};
    if (!platform_file_read_bounded(path, COOKER_MAX_MANIFEST_BYTES,
                                    cooker_heap_allocator(), &manifest))
        return CookerResult::IoFailure;
    const CookerResult result = parse_manifest_bytes(
        manifest.data, manifest.size, out_entries);
    release_platform_file(&manifest);
    return result;
}

static void* cooker_heap_allocate(void*, void* ptr, size_t, size_t new_size,
                                  size_t alignment) {
    if (new_size == 0u) {
        if (ptr) _aligned_free(ptr);
        return nullptr;
    }
    if (ptr || alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        return nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    return _aligned_malloc(new_size, alignment);
}

static Allocator cooker_heap_allocator(void) {
    Allocator allocator{cooker_heap_allocate, nullptr, ALLOC_HEAP};
    return allocator;
}

static void release_platform_file(PlatformFile* file) {
    if (!file || !file->data) return;
    mem_free(cooker_heap_allocator(), file->data,
             file->size != 0u ? file->size : 1u);
    file->data = nullptr;
    file->size = 0u;
}

static CookerResult cook_texture(const ManifestEntry& entry,
                                 const PlatformFile& source,
                                 CookedAsset* out) {
    uint32_t width = 0u;
    uint32_t height = 0u;
    size_t rgba_bytes = 0u;
    if (!tga_inspect(source.data, source.size, &width, &height, &rgba_bytes) ||
        rgba_bytes > COOKER_MAX_BAKED_ASSET_BYTES -
                         (MBA_HEADER_BYTES + MBA_TEXTURE_DATA_OFFSET))
        return CookerResult::InvalidInput;

    TgaImage image{};
    Allocator heap = cooker_heap_allocator();
    if (!tga_decode(source.data, source.size, heap, &image))
        return CookerResult::InvalidInput;
    MbaTextureSource texture{image.rgba8, rgba_bytes, width, height};
    size_t output_bytes = 0u;
    if (!mba_texture_measure(&texture, &output_bytes)) {
        mem_free(heap, image.rgba8, rgba_bytes);
        return CookerResult::InvalidInput;
    }
    if (output_bytes > COOKER_MAX_BAKED_ASSET_BYTES) {
        mem_free(heap, image.rgba8, rgba_bytes);
        return CookerResult::InvalidInput;
    }

    CookedAsset cooked{};
    cooked.entry = entry;
    cooked.bytes.resize(output_bytes);
    size_t written = 0u;
    const bool encoded = mba_encode_texture(cooked.bytes.data(), cooked.bytes.size(),
                                            entry.id, &texture, &written) &&
                         written == output_bytes;
    mem_free(heap, image.rgba8, rgba_bytes);
    if (!encoded) return CookerResult::InvalidInput;
    *out = std::move(cooked);
    return CookerResult::Ok;
}

static CookerResult cook_sound(const ManifestEntry& entry,
                               const PlatformFile& source,
                               CookedAsset* out) {
    WavInfo info{};
    if (!wav_inspect_pcm(source.data, source.size, &info))
        return CookerResult::InvalidInput;

    Allocator heap = cooker_heap_allocator();
    WavPcm pcm{};
    if (!wav_decode_pcm(source.data, source.size, heap, &pcm))
        return CookerResult::InvalidInput;
    MbaSoundSource sound{pcm.pcm, pcm.pcm_bytes, pcm.sample_rate,
                         pcm.channels, pcm.bits_per_sample};
    size_t output_bytes = 0u;
    if (!mba_sound_measure(&sound, &output_bytes)) {
        mem_free(heap, pcm.pcm, pcm.pcm_bytes);
        return CookerResult::InvalidInput;
    }
    if (output_bytes > COOKER_MAX_BAKED_ASSET_BYTES) {
        mem_free(heap, pcm.pcm, pcm.pcm_bytes);
        return CookerResult::InvalidInput;
    }

    CookedAsset cooked{};
    cooked.entry = entry;
    cooked.bytes.resize(output_bytes);
    size_t written = 0u;
    const bool encoded = mba_encode_sound(cooked.bytes.data(), cooked.bytes.size(),
                                          entry.id, &sound, &written) &&
                         written == output_bytes;
    mem_free(heap, pcm.pcm, pcm.pcm_bytes);
    if (!encoded) return CookerResult::InvalidInput;
    *out = std::move(cooked);
    return CookerResult::Ok;
}

static CookerResult cook_all(const CookerArgs& args,
                             const std::vector<ManifestEntry>& entries,
                             std::vector<CookedAsset>* out_assets) {
    std::vector<CookedAsset> assets;
    assets.reserve(entries.size());
    size_t total_baked_bytes = 0u;
    for (const ManifestEntry& entry : entries) {
        PlatformFile source{};
        if (!platform_file_read_rooted(args.source_root, entry.logical_path.c_str(),
                                       COOKER_MAX_SOURCE_BYTES,
                                       cooker_heap_allocator(), &source)) {
            std::fprintf(stderr, "cooker: cannot read source '%s'\n",
                         entry.logical_path.c_str());
            return CookerResult::IoFailure;
        }
        CookedAsset cooked{};
        const CookerResult result = entry.type == ASSET_TYPE_TEXTURE
            ? cook_texture(entry, source, &cooked)
            : cook_sound(entry, source, &cooked);
        release_platform_file(&source);
        if (result != CookerResult::Ok) {
            std::fprintf(stderr, "cooker: invalid source '%s'\n",
                         entry.logical_path.c_str());
            return result;
        }
        if (cooked.bytes.size() > COOKER_MAX_TOTAL_BAKED_BYTES - total_baked_bytes) {
            std::fprintf(stderr, "cooker: total baked output budget exceeded\n");
            return CookerResult::InvalidInput;
        }
        total_baked_bytes += cooked.bytes.size();
        assets.push_back(std::move(cooked));
    }
    *out_assets = std::move(assets);
    return CookerResult::Ok;
}

static std::string generate_header(std::vector<ManifestEntry> entries) {
    std::sort(entries.begin(), entries.end(),
        [](const ManifestEntry& a, const ManifestEntry& b) { return a.id < b.id; });
    std::ostringstream output;
    output << "#pragma once\n\n"
           << "// Generated by moba_cooker. Do not edit.\n"
           << "#include \"assets/asset_catalog.h\"\n\n";
    for (const ManifestEntry& entry : entries) {
        output << "constexpr AssetId " << entry.symbol << " = 0x"
               << std::hex << std::setw(16) << std::setfill('0') << entry.id
               << "ULL;\n" << std::dec;
    }
    output << "\nstatic constexpr AssetCatalogEntry MOBA_ASSET_CATALOG_ENTRIES[] = {\n";
    for (const ManifestEntry& entry : entries) {
        output << "    {" << entry.symbol << ", "
               << (entry.type == ASSET_TYPE_TEXTURE
                       ? "ASSET_TYPE_TEXTURE" : "ASSET_TYPE_SOUND")
               << ", \"" << entry.logical_path << "\", \""
               << entry.baked_path << "\"},\n";
    }
    output << "};\n\n"
           << "static constexpr AssetCatalog MOBA_ASSET_CATALOG = {\n"
           << "    MOBA_ASSET_CATALOG_ENTRIES,\n"
           << "    (uint32_t)(sizeof(MOBA_ASSET_CATALOG_ENTRIES) / "
              "sizeof(MOBA_ASSET_CATALOG_ENTRIES[0])),\n"
           << "};\n";
    return output.str();
}

static bool write_if_changed(const char* root, const std::string& relative,
                             const uint8_t* bytes, size_t size,
                             bool* out_written) {
    if (!out_written || (!bytes && size)) return false;
    PlatformFile existing{};
    if (platform_file_read_rooted(root, relative.c_str(), size,
                                  cooker_heap_allocator(), &existing)) {
        const bool equal = existing.size == size &&
            (size == 0u || std::memcmp(existing.data, bytes, size) == 0);
        release_platform_file(&existing);
        if (equal) {
            *out_written = false;
            return true;
        }
    }
    if (!platform_file_write_rooted(root, relative.c_str(), bytes, size))
        return false;
    *out_written = true;
    return true;
}

static CookerResult publish(const CookerArgs& args,
                            const std::vector<CookedAsset>& assets,
                            const std::string& header) {
    uint32_t written_count = 0u;
    uint32_t unchanged_count = 0u;
    for (const CookedAsset& asset : assets) {
        bool written = false;
        if (!write_if_changed(args.out_root, asset.entry.baked_path,
                              asset.bytes.data(), asset.bytes.size(), &written)) {
            std::fprintf(stderr, "cooker: cannot publish '%s'\n",
                         asset.entry.baked_path.c_str());
            return CookerResult::IoFailure;
        }
        if (written) ++written_count;
        else ++unchanged_count;
    }

    static const std::string header_path("assets/asset_ids.gen.h");
    bool header_written = false;
    if (!write_if_changed(args.generated_root, header_path,
                          (const uint8_t*)header.data(), header.size(),
                          &header_written)) {
        std::fprintf(stderr, "cooker: cannot publish generated catalog\n");
        return CookerResult::IoFailure;
    }
    if (header_written) ++written_count;
    else ++unchanged_count;
    std::printf("cooked assets=%zu written=%u unchanged=%u\n",
                assets.size(), written_count, unchanged_count);
    return CookerResult::Ok;
}

int main(int argc, char** argv) {
    CookerArgs args{};
    if (!parse_args(argc, argv, &args)) {
        print_usage();
        return 1;
    }
    if (!directory_is_plain(args.source_root) ||
        !directory_is_plain(args.out_root) ||
        !directory_is_plain(args.generated_root)) {
        std::fprintf(stderr, "cooker: source/output/generated roots must be plain existing directories\n");
        return 1;
    }

    std::vector<ManifestEntry> entries;
    CookerResult result = read_manifest(args.manifest, &entries);
    if (result != CookerResult::Ok)
        return result == CookerResult::IoFailure ? 1 : 2;

    for (const ManifestEntry& entry : entries) {
        if (!rooted_parent_is_plain(args.out_root, entry.baked_path)) {
            std::fprintf(stderr, "cooker: output parent is missing or unsafe for '%s'\n",
                         entry.baked_path.c_str());
            return 1;
        }
    }
    if (!rooted_parent_is_plain(args.generated_root, "assets/asset_ids.gen.h")) {
        std::fprintf(stderr, "cooker: generated assets directory is missing or unsafe\n");
        return 1;
    }

    std::vector<CookedAsset> assets;
    result = cook_all(args, entries, &assets);
    if (result != CookerResult::Ok)
        return result == CookerResult::IoFailure ? 1 : 2;

    const std::string header = generate_header(entries);
    result = publish(args, assets, header);
    return result == CookerResult::Ok ? 0 :
        (result == CookerResult::IoFailure ? 1 : 2);
}
