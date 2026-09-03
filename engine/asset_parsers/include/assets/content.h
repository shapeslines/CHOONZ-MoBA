#pragma once

#include <stddef.h>
#include <stdint.h>

#include "assets/asset_id.h"
#include "serialize/byte_io.h"

// Typed game-content records (docs/slate-moba-proto-design.md section 3.3).
// POD-only, fixed capacity, explicit little-endian serialization through
// eng::serialize in the documented field order. ActionDef and EffectDef nest inside
// HeroDef exactly as the design nests them; one .mba record is one top-level
// HeroDef, ObjectiveDef, or EconomyRule. Map records stay opaque .mapdesc bytes
// here (eng::sim decodes them; the asset modules may not include sim headers).

constexpr uint32_t CONTENT_SCHEMA_VERSION = 1u;
constexpr uint32_t CONTENT_MAX_ACTIONS = 8u;
constexpr uint32_t CONTENT_MAX_EFFECTS = 4u;

typedef enum ContentEffectType {
    CONTENT_EFFECT_NONE = 0,
    CONTENT_EFFECT_PROJECTILE_DAMAGE = 1,
    CONTENT_EFFECT_SELF_HEAL = 2,
    CONTENT_EFFECT_AREA_SLOW = 3,
} ContentEffectType;

typedef enum ContentTargetMode {
    CONTENT_TARGET_NONE = 0,
    CONTENT_TARGET_POINT = 1,
    CONTENT_TARGET_ENTITY = 2,
    CONTENT_TARGET_SELF = 3,
    CONTENT_TARGET_AREA = 4,
} ContentTargetMode;

typedef enum ContentObjectiveKind {
    CONTENT_OBJECTIVE_NONE = 0,
    CONTENT_OBJECTIVE_TOWER = 1,
    CONTENT_OBJECTIVE_CORE = 2,
} ContentObjectiveKind;

typedef enum ContentTargetPolicy {
    CONTENT_TARGET_POLICY_NEAREST = 0,
    CONTENT_TARGET_POLICY_LOWEST_HEALTH = 1,
    CONTENT_TARGET_POLICY_HERO_FIRST = 2,
} ContentTargetPolicy;

typedef enum ContentAwardKind {
    CONTENT_AWARD_NONE = 0,
    CONTENT_AWARD_GOLD = 1,
    CONTENT_AWARD_XP = 2,
} ContentAwardKind;

typedef enum ContentSourceKind {
    CONTENT_SOURCE_NONE = 0,
    CONTENT_SOURCE_HERO_KILL = 1,
    CONTENT_SOURCE_OBJECTIVE_DAMAGE = 2,
    CONTENT_SOURCE_OBJECTIVE_DESTROY = 3,
} ContentSourceKind;

typedef struct EffectDef {
    uint8_t  effect_type;
    uint8_t  damage_type;     // reserved for the single unified pipeline; zero in v1
    uint16_t duration_ticks;
    int32_t  magnitude;
    int32_t  radius_q16;
    int32_t  scalar_q16;
} EffectDef;

typedef struct ActionDef {
    AssetId   action_id;
    uint8_t   slot;
    uint8_t   target_mode;
    uint16_t  effect_count;
    uint32_t  cooldown_ticks;
    uint32_t  cast_time_ticks;
    uint32_t  resource_cost;
    int32_t   range_q16;
    int32_t   projectile_speed_q16;
    EffectDef effects[CONTENT_MAX_EFFECTS];
} ActionDef;

typedef struct HeroDef {
    uint32_t  schema_version;
    AssetId   hero_def_id;
    int32_t   max_health;
    int32_t   move_speed_q16;
    int32_t   attack_range_q16;
    uint16_t  action_count;
    uint16_t  reserved;
    ActionDef actions[CONTENT_MAX_ACTIONS];
} HeroDef;

typedef struct ObjectiveDef {
    AssetId  objective_id;
    uint8_t  owner_team_id;
    uint8_t  objective_kind;
    uint16_t reserved;
    int32_t  max_health;
    int32_t  armor;
    uint8_t  target_policy;
    uint8_t  reserved2[3];
} ObjectiveDef;

typedef struct EconomyRule {
    uint8_t  award_kind;
    uint8_t  source_kind;
    uint16_t reserved;
    uint32_t award_amount;
} EconomyRule;

constexpr size_t CONTENT_EFFECT_ENCODED_BYTES = 16u;
constexpr size_t CONTENT_ACTION_BASE_ENCODED_BYTES = 32u;
constexpr size_t CONTENT_HERO_BASE_ENCODED_BYTES = 28u;
constexpr size_t CONTENT_OBJECTIVE_ENCODED_BYTES = 24u;
constexpr size_t CONTENT_ECONOMY_ENCODED_BYTES = 8u;
constexpr size_t CONTENT_MAP_MIN_ENCODED_BYTES = 28u; // .mapdesc header (MAPD)

// Semantic validity (ids non-null, enums known, reserved zero, counts within
// capacity, slots ascending from zero, magnitudes non-negative). Encode refuses an
// invalid record; decode accepts only a valid one.
bool content_validate_hero(const HeroDef* hero);
bool content_validate_objective(const ObjectiveDef* objective);
bool content_validate_economy(const EconomyRule* rule);

// Serialized size of a valid record (arrays only up to their counts).
size_t content_hero_encoded_bytes(const HeroDef* hero);

// Writers fail (and poison the writer) if the record is invalid or does not fit.
bool content_encode_hero(ByteWriter* writer, const HeroDef* hero);
bool content_encode_objective(ByteWriter* writer, const ObjectiveDef* objective);
bool content_encode_economy(ByteWriter* writer, const EconomyRule* rule);

// Readers work on a cursor copy and commit only on success; the destination and
// the reader are untouched on failure. They do not require the reader to end at
// the record (the container checks trailing bytes).
bool content_decode_hero(ByteReader* reader, HeroDef* out);
bool content_decode_objective(ByteReader* reader, ObjectiveDef* out);
bool content_decode_economy(ByteReader* reader, EconomyRule* out);

// Opaque map payload: MAPD magic, schema 1, non-zero dimensions, positive cell
// size, cell_count == width * height, and enough bytes for the declared cells.
bool content_map_bytes_valid(const uint8_t* bytes, size_t size);
