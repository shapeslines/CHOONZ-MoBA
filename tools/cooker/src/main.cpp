#include "assets/asset_id.h"
#include "assets/content.h"
#include "assets/mba.h"
#include "assets/tga.h"
#include "core/mem.h"
#include "serialize/byte_io.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Brute-force baker: one input, one output, no incremental state (ADR-0015).
// ADR-0016 adds typed content records: `--kind hero|objective|economy` reads a
// UTF-8 `key = value` text file (cooker-only authored format) and `--kind map`
// copies validated .mapdesc bytes verbatim. The default kind stays `texture`.

struct CookerOptions {
    const char* kind = "texture";
    const char* input = nullptr;
    const char* asset_path = nullptr;
    const char* output = nullptr;
};

static void cooker_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " [--kind texture|hero|objective|economy|map] --input source"
                 " --asset canonical/asset.mba --output baked.mba\n";
}

static bool cooker_parse_options(int argc, char** argv, CookerOptions* out) {
    if (!out || (argc != 7 && argc != 9)) return false;
    CookerOptions options{};
    bool kind_set = false;
    for (int i = 1; i < argc; i += 2) {
        const char* option = argv[i];
        const char* value = argv[i + 1];
        if (!option || !value || value[0] == '\0') return false;
        if (std::strcmp(option, "--input") == 0 && !options.input) options.input = value;
        else if (std::strcmp(option, "--asset") == 0 && !options.asset_path)
            options.asset_path = value;
        else if (std::strcmp(option, "--output") == 0 && !options.output)
            options.output = value;
        else if (std::strcmp(option, "--kind") == 0 && !kind_set) {
            options.kind = value;
            kind_set = true;
        }
        else return false;
    }
    if (!options.input || !options.asset_path || !options.output) return false;
    if (argc == 9 && !kind_set) return false;
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

// ---------------------------------------------------------------- authored text

typedef std::map<std::string, std::string> KeyValues;

static std::string trim(const std::string& s) {
    size_t b = 0u, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1u] == ' ' || s[e - 1u] == '\t' || s[e - 1u] == '\r')) --e;
    return s.substr(b, e - b);
}

// `key = value` per line; `#` starts a comment; duplicate keys are an error.
static bool parse_key_values(const std::vector<uint8_t>& bytes, KeyValues* out) {
    std::string text(bytes.begin(), bytes.end());
    size_t pos = 0u;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1u;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0u, hash);
        line = trim(line);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) return false;
        std::string key = trim(line.substr(0u, eq));
        std::string value = trim(line.substr(eq + 1u));
        if (key.empty() || value.empty() || out->count(key)) return false;
        (*out)[key] = value;
    }
    return true;
}

static bool get_i64(const KeyValues& kv, const std::string& key, int64_t* out) {
    auto it = kv.find(key);
    if (it == kv.end()) return false;
    const std::string& v = it->second;
    size_t i = 0u;
    bool negative = false;
    if (v[i] == '-') { negative = true; ++i; }
    if (i >= v.size()) return false;
    int64_t value = 0;
    for (; i < v.size(); ++i) {
        if (v[i] < '0' || v[i] > '9') return false;
        if (value > (INT64_MAX - (v[i] - '0')) / 10) return false;
        value = value * 10 + (v[i] - '0');
    }
    *out = negative ? -value : value;
    return true;
}

static bool get_u32(const KeyValues& kv, const std::string& key, uint32_t* out, uint32_t fallback,
                    bool required) {
    int64_t v = 0;
    if (!get_i64(kv, key, &v)) { if (required) return false; *out = fallback; return true; }
    if (v < 0 || v > (int64_t)UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static bool get_i32(const KeyValues& kv, const std::string& key, int32_t* out, int32_t fallback,
                    bool required) {
    int64_t v = 0;
    if (!get_i64(kv, key, &v)) { if (required) return false; *out = fallback; return true; }
    if (v < INT32_MIN || v > INT32_MAX) return false;
    *out = (int32_t)v;
    return true;
}

static bool get_enum(const KeyValues& kv, const std::string& key, const char* const* names,
                     uint32_t first_value, uint32_t count, uint8_t* out) {
    auto it = kv.find(key);
    if (it == kv.end()) return false;
    for (uint32_t i = 0u; i < count; ++i) {
        if (it->second == names[i]) { *out = (uint8_t)(first_value + i); return true; }
    }
    int64_t v = 0;
    if (!get_i64(kv, key, &v) || v < (int64_t)first_value || v >= (int64_t)(first_value + count))
        return false;
    *out = (uint8_t)v;
    return true;
}

static bool get_asset_id(const KeyValues& kv, const std::string& key, AssetId* out) {
    auto it = kv.find(key);
    if (it == kv.end()) return false;
    return asset_id_from_path(it->second.c_str(), out) && *out != ASSET_ID_NULL;
}

static const char* const TARGET_MODE_NAMES[] = {"point", "entity", "self", "area"};
static const char* const EFFECT_TYPE_NAMES[] = {"projectile_damage", "self_heal", "area_slow"};
static const char* const OBJECTIVE_KIND_NAMES[] = {"tower", "core"};
static const char* const TARGET_POLICY_NAMES[] = {"nearest", "lowest_health", "hero_first"};
static const char* const AWARD_KIND_NAMES[] = {"gold", "xp"};
static const char* const SOURCE_KIND_NAMES[] = {"hero_kill", "objective_damage", "objective_destroy"};

static bool build_hero(const KeyValues& kv, HeroDef* out) {
    HeroDef h{};
    uint32_t schema = 0u;
    if (!get_u32(kv, "schema_version", &schema, CONTENT_SCHEMA_VERSION, false)) return false;
    h.schema_version = schema;
    if (!get_asset_id(kv, "hero_id", &h.hero_def_id)) return false;
    if (!get_i32(kv, "max_health", &h.max_health, 0, true) ||
        !get_i32(kv, "move_speed_q16", &h.move_speed_q16, 0, true) ||
        !get_i32(kv, "attack_range_q16", &h.attack_range_q16, 0, true)) return false;
    uint32_t action_count = 0u;
    for (uint32_t i = 0u; i < CONTENT_MAX_ACTIONS; ++i) {
        std::string prefix = "action." + std::to_string(i) + ".";
        if (!kv.count(prefix + "id")) break;
        ActionDef* a = &h.actions[i];
        if (!get_asset_id(kv, prefix + "id", &a->action_id)) return false;
        uint32_t slot = 0u;
        if (!get_u32(kv, prefix + "slot", &slot, i, false) || slot > 255u) return false;
        a->slot = (uint8_t)slot;
        if (!get_enum(kv, prefix + "target_mode", TARGET_MODE_NAMES, CONTENT_TARGET_POINT, 4u,
                      &a->target_mode)) return false;
        if (!get_u32(kv, prefix + "cooldown_ticks", &a->cooldown_ticks, 0u, true) ||
            !get_u32(kv, prefix + "cast_time_ticks", &a->cast_time_ticks, 0u, false) ||
            !get_u32(kv, prefix + "resource_cost", &a->resource_cost, 0u, false) ||
            !get_i32(kv, prefix + "range_q16", &a->range_q16, 0, false) ||
            !get_i32(kv, prefix + "projectile_speed_q16", &a->projectile_speed_q16, 0, false))
            return false;
        uint32_t effect_count = 0u;
        for (uint32_t j = 0u; j < CONTENT_MAX_EFFECTS; ++j) {
            std::string ep = prefix + "effect." + std::to_string(j) + ".";
            if (!kv.count(ep + "type")) break;
            EffectDef* e = &a->effects[j];
            if (!get_enum(kv, ep + "type", EFFECT_TYPE_NAMES, CONTENT_EFFECT_PROJECTILE_DAMAGE, 3u,
                          &e->effect_type)) return false;
            uint32_t duration = 0u;
            if (!get_u32(kv, ep + "duration_ticks", &duration, 0u, false) || duration > 65535u)
                return false;
            e->duration_ticks = (uint16_t)duration;
            if (!get_i32(kv, ep + "magnitude", &e->magnitude, 0, true) ||
                !get_i32(kv, ep + "radius_q16", &e->radius_q16, 0, false) ||
                !get_i32(kv, ep + "scalar_q16", &e->scalar_q16, 0, false)) return false;
            ++effect_count;
        }
        a->effect_count = (uint16_t)effect_count;
        ++action_count;
    }
    h.action_count = (uint16_t)action_count;
    if (!content_validate_hero(&h)) return false;
    *out = h;
    return true;
}

static bool build_objective(const KeyValues& kv, ObjectiveDef* out) {
    ObjectiveDef o{};
    uint32_t team = 0u;
    if (!get_asset_id(kv, "objective_id", &o.objective_id)) return false;
    if (!get_u32(kv, "owner_team_id", &team, 0u, true) || team > 255u) return false;
    o.owner_team_id = (uint8_t)team;
    if (!get_enum(kv, "kind", OBJECTIVE_KIND_NAMES, CONTENT_OBJECTIVE_TOWER, 2u, &o.objective_kind))
        return false;
    if (!get_i32(kv, "max_health", &o.max_health, 0, true) ||
        !get_i32(kv, "armor", &o.armor, 0, false)) return false;
    if (!get_enum(kv, "target_policy", TARGET_POLICY_NAMES, CONTENT_TARGET_POLICY_NEAREST, 3u,
                  &o.target_policy)) {
        if (kv.count("target_policy")) return false;
        o.target_policy = CONTENT_TARGET_POLICY_NEAREST;
    }
    if (!content_validate_objective(&o)) return false;
    *out = o;
    return true;
}

static bool build_economy(const KeyValues& kv, EconomyRule* out) {
    EconomyRule r{};
    if (!get_enum(kv, "award_kind", AWARD_KIND_NAMES, CONTENT_AWARD_GOLD, 2u, &r.award_kind) ||
        !get_enum(kv, "source_kind", SOURCE_KIND_NAMES, CONTENT_SOURCE_HERO_KILL, 3u, &r.source_kind) ||
        !get_u32(kv, "award_amount", &r.award_amount, 0u, true)) return false;
    if (!content_validate_economy(&r)) return false;
    *out = r;
    return true;
}

// ---------------------------------------------------------------- bakes

static int bake_texture(const CookerOptions& options, AssetId asset_id,
                        const std::vector<uint8_t>& source_bytes) {
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

static int bake_record(const CookerOptions& options, AssetId asset_id, uint32_t kind,
                       const std::vector<uint8_t>& source_bytes) {
    std::vector<uint8_t> record;
    if (kind == MBA_ASSET_TYPE_MAP) {
        if (!content_map_bytes_valid(source_bytes.data(), source_bytes.size())) {
            std::cerr << "Malformed .mapdesc: " << options.input << "\n";
            return 1;
        }
        record = source_bytes;
    } else {
        KeyValues kv;
        if (!parse_key_values(source_bytes, &kv)) {
            std::cerr << "Malformed authored record text: " << options.input << "\n";
            return 1;
        }
        std::vector<uint8_t> buffer(64u * 1024u);
        ByteWriter writer;
        byte_writer_init(&writer, buffer.data(), buffer.size());
        bool ok = false;
        if (kind == MBA_ASSET_TYPE_HERO) {
            HeroDef h{};
            ok = build_hero(kv, &h) && content_encode_hero(&writer, &h);
        } else if (kind == MBA_ASSET_TYPE_OBJECTIVE) {
            ObjectiveDef o{};
            ok = build_objective(kv, &o) && content_encode_objective(&writer, &o);
        } else if (kind == MBA_ASSET_TYPE_ECONOMY) {
            EconomyRule r{};
            ok = build_economy(kv, &r) && content_encode_economy(&writer, &r);
        }
        if (!ok) {
            std::cerr << "Invalid record fields: " << options.input << "\n";
            return 1;
        }
        record.assign(buffer.begin(), buffer.begin() + (ptrdiff_t)byte_writer_size(&writer));
    }

    size_t baked_bytes = 0u;
    if (!mba_record_measure(kind, record.data(), record.size(), &baked_bytes)) {
        std::cerr << "Record failed container validation: " << options.input << "\n";
        return 1;
    }
    std::vector<uint8_t> baked(baked_bytes);
    size_t written_bytes = 0u;
    if (!mba_encode_record(baked.data(), baked.size(), asset_id, kind, CONTENT_SCHEMA_VERSION,
                           record.data(), record.size(), &written_bytes) ||
        written_bytes != baked.size() ||
        !cooker_write_file(options.output, baked.data(), baked.size())) {
        std::cerr << "Unable to write baked asset: " << options.output << "\n";
        return 1;
    }
    std::cout << "cooked asset=" << options.asset_path << " kind=" << kind
              << " record_bytes=" << record.size() << " bytes=" << baked.size() << "\n";
    return 0;
}

static bool kind_from_name(const char* name, uint32_t* out) {
    if (std::strcmp(name, "texture") == 0) { *out = MBA_ASSET_TYPE_TEXTURE; return true; }
    if (std::strcmp(name, "hero") == 0) { *out = MBA_ASSET_TYPE_HERO; return true; }
    if (std::strcmp(name, "objective") == 0) { *out = MBA_ASSET_TYPE_OBJECTIVE; return true; }
    if (std::strcmp(name, "economy") == 0) { *out = MBA_ASSET_TYPE_ECONOMY; return true; }
    if (std::strcmp(name, "map") == 0) { *out = MBA_ASSET_TYPE_MAP; return true; }
    return false;
}

int main(int argc, char** argv) {
    CookerOptions options{};
    uint32_t kind = 0u;
    if (!cooker_parse_options(argc, argv, &options) || !kind_from_name(options.kind, &kind)) {
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
        std::cerr << "Unable to read source: " << options.input << "\n";
        return 1;
    }

    if (kind == MBA_ASSET_TYPE_TEXTURE) return bake_texture(options, asset_id, source_bytes);
    return bake_record(options, asset_id, kind, source_bytes);
}
