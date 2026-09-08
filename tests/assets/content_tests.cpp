#include "test.h"

#include "assets/content.h"
#include "assets/mba.h"

#include <cstring>

// ADR-0016 typed record payloads: content codecs, container record path, goldens
// produced by an independent Python encoder (tests/assets/content_golden.py).
#include "content_golden.inc"

static HeroDef fixture_hero() {
    HeroDef h{};
    h.schema_version = CONTENT_SCHEMA_VERSION;
    h.hero_def_id = asset_id("heroes/mirror.hero");
    h.max_health = 600;
    h.move_speed_q16 = 229376;
    h.attack_range_q16 = 327680;
    h.action_count = 3u;
    ActionDef* a = &h.actions[0];
    a->action_id = asset_id("actions/mirror/bolt"); a->slot = 0u; a->target_mode = CONTENT_TARGET_ENTITY;
    a->effect_count = 1u; a->cooldown_ticks = 180u; a->cast_time_ticks = 6u; a->resource_cost = 40u;
    a->range_q16 = 524288; a->projectile_speed_q16 = 786432;
    a->effects[0] = EffectDef{CONTENT_EFFECT_PROJECTILE_DAMAGE, 0u, 0u, 90, 0, 0};
    a = &h.actions[1];
    a->action_id = asset_id("actions/mirror/mend"); a->slot = 1u; a->target_mode = CONTENT_TARGET_SELF;
    a->effect_count = 1u; a->cooldown_ticks = 360u; a->resource_cost = 60u;
    a->effects[0] = EffectDef{CONTENT_EFFECT_SELF_HEAL, 0u, 90u, 120, 0, 0};
    a = &h.actions[2];
    a->action_id = asset_id("actions/mirror/mire"); a->slot = 2u; a->target_mode = CONTENT_TARGET_AREA;
    a->effect_count = 1u; a->cooldown_ticks = 540u; a->cast_time_ticks = 12u; a->resource_cost = 80u;
    a->range_q16 = 393216;
    a->effects[0] = EffectDef{CONTENT_EFFECT_AREA_SLOW, 0u, 120u, 40, 196608, 32768};
    return h;
}

static ObjectiveDef fixture_tower() {
    ObjectiveDef o{};
    o.objective_id = asset_id("objectives/lane/tower_a");
    o.owner_team_id = 0u;
    o.objective_kind = CONTENT_OBJECTIVE_TOWER;
    o.max_health = 1800;
    o.armor = 20;
    o.target_policy = CONTENT_TARGET_POLICY_HERO_FIRST;
    return o;
}

static EconomyRule fixture_gold() {
    EconomyRule r{};
    r.award_kind = CONTENT_AWARD_GOLD;
    r.source_kind = CONTENT_SOURCE_HERO_KILL;
    r.award_amount = 300u;
    return r;
}

// ---------------------------------------------------------------- codecs

TEST(content, hero_round_trips_and_matches_the_independent_golden) {
    HeroDef h = fixture_hero();
    CHECK(content_validate_hero(&h));
    CHECK(content_hero_encoded_bytes(&h) == 172u);
    uint8_t body[512];
    ByteWriter w;
    byte_writer_init(&w, body, sizeof(body));
    CHECK(content_encode_hero(&w, &h));
    CHECK(byte_writer_size(&w) == 172u);
    // Container around it must equal the Python golden byte for byte.
    uint8_t baked[512];
    size_t baked_bytes = 0u;
    CHECK(mba_encode_record(baked, sizeof(baked), asset_id("hero_test.mba"), MBA_ASSET_TYPE_HERO,
                            CONTENT_SCHEMA_VERSION, body, 172u, &baked_bytes));
    CHECK(baked_bytes == sizeof(CONTENT_GOLDEN_HERO_TEST));
    CHECK(std::memcmp(baked, CONTENT_GOLDEN_HERO_TEST, sizeof(CONTENT_GOLDEN_HERO_TEST)) == 0);

    ByteReader r;
    byte_reader_init(&r, body, 172u);
    HeroDef d{};
    CHECK(content_decode_hero(&r, &d));
    CHECK(byte_reader_remaining(&r) == 0u);
    CHECK(d.hero_def_id == h.hero_def_id && d.action_count == 3u && d.max_health == 600);
    CHECK(d.actions[2].effects[0].effect_type == CONTENT_EFFECT_AREA_SLOW &&
          d.actions[2].effects[0].radius_q16 == 196608);
    CHECK(std::memcmp(&d, &h, sizeof(HeroDef)) == 0);
}

TEST(content, objective_and_economy_round_trip_and_match_goldens) {
    ObjectiveDef o = fixture_tower();
    EconomyRule e = fixture_gold();
    uint8_t body[64];
    ByteWriter w;
    byte_writer_init(&w, body, sizeof(body));
    CHECK(content_encode_objective(&w, &o) && byte_writer_size(&w) == CONTENT_OBJECTIVE_ENCODED_BYTES);
    uint8_t baked[128];
    size_t baked_bytes = 0u;
    CHECK(mba_encode_record(baked, sizeof(baked), asset_id("objectives/tower_a.mba"),
                            MBA_ASSET_TYPE_OBJECTIVE, CONTENT_SCHEMA_VERSION, body,
                            CONTENT_OBJECTIVE_ENCODED_BYTES, &baked_bytes));
    CHECK(baked_bytes == sizeof(CONTENT_GOLDEN_TOWER_TEST));
    CHECK(std::memcmp(baked, CONTENT_GOLDEN_TOWER_TEST, sizeof(CONTENT_GOLDEN_TOWER_TEST)) == 0);
    ByteReader r;
    byte_reader_init(&r, body, CONTENT_OBJECTIVE_ENCODED_BYTES);
    ObjectiveDef od{};
    CHECK(content_decode_objective(&r, &od) && std::memcmp(&od, &o, sizeof(o)) == 0);

    byte_writer_init(&w, body, sizeof(body));
    CHECK(content_encode_economy(&w, &e) && byte_writer_size(&w) == CONTENT_ECONOMY_ENCODED_BYTES);
    CHECK(mba_encode_record(baked, sizeof(baked), asset_id("economy/gold_hero_kill.mba"),
                            MBA_ASSET_TYPE_ECONOMY, CONTENT_SCHEMA_VERSION, body,
                            CONTENT_ECONOMY_ENCODED_BYTES, &baked_bytes));
    CHECK(baked_bytes == sizeof(CONTENT_GOLDEN_GOLD_RULE));
    CHECK(std::memcmp(baked, CONTENT_GOLDEN_GOLD_RULE, sizeof(CONTENT_GOLDEN_GOLD_RULE)) == 0);
    byte_reader_init(&r, body, CONTENT_ECONOMY_ENCODED_BYTES);
    EconomyRule ed{};
    CHECK(content_decode_economy(&r, &ed) && std::memcmp(&ed, &e, sizeof(e)) == 0);
}

TEST(content, every_validation_rule_rejects_once_and_encode_refuses_invalid) {
    uint8_t body[512];
    ByteWriter w;
    HeroDef h = fixture_hero();
    #define REJECT_HERO(mutate) do { HeroDef x = fixture_hero(); mutate; CHECK(!content_validate_hero(&x)); \
        byte_writer_init(&w, body, sizeof(body)); CHECK(!content_encode_hero(&w, &x)); } while (0)
    REJECT_HERO(x.schema_version = 2u);
    REJECT_HERO(x.hero_def_id = ASSET_ID_NULL);
    REJECT_HERO(x.max_health = 0);
    REJECT_HERO(x.move_speed_q16 = -1);
    REJECT_HERO(x.reserved = 1u);
    REJECT_HERO(x.action_count = CONTENT_MAX_ACTIONS + 1u);
    REJECT_HERO(x.actions[1].slot = 0u);                       // slots must ascend from zero
    REJECT_HERO(x.actions[0].action_id = ASSET_ID_NULL);
    REJECT_HERO(x.actions[0].target_mode = 5u);
    REJECT_HERO(x.actions[0].effect_count = CONTENT_MAX_EFFECTS + 1u);
    REJECT_HERO(x.actions[0].range_q16 = -1);
    REJECT_HERO(x.actions[0].effects[0].effect_type = 0u);
    REJECT_HERO(x.actions[0].effects[0].damage_type = 1u);
    REJECT_HERO(x.actions[0].effects[0].magnitude = -1);
    #undef REJECT_HERO
    CHECK(content_validate_hero(&h));

    ObjectiveDef o = fixture_tower();
    o.objective_kind = 3u;
    CHECK(!content_validate_objective(&o));
    o = fixture_tower(); o.reserved2[1] = 1u;
    CHECK(!content_validate_objective(&o));
    o = fixture_tower(); o.target_policy = 3u;
    CHECK(!content_validate_objective(&o));
    o = fixture_tower(); o.max_health = 0;
    CHECK(!content_validate_objective(&o));
    EconomyRule e = fixture_gold();
    e.award_amount = 0u;
    CHECK(!content_validate_economy(&e));
    e = fixture_gold(); e.source_kind = 4u;
    CHECK(!content_validate_economy(&e));
    e = fixture_gold(); e.reserved = 1u;
    CHECK(!content_validate_economy(&e));
}

TEST(content, malformed_streams_reject_with_destination_and_reader_untouched) {
    HeroDef h = fixture_hero();
    uint8_t body[512];
    ByteWriter w;
    byte_writer_init(&w, body, sizeof(body));
    CHECK(content_encode_hero(&w, &h));
    const size_t size = byte_writer_size(&w);

    HeroDef sentinel;
    std::memset(&sentinel, 0xA5, sizeof(sentinel));
    const HeroDef before = sentinel;
    ByteReader r;

    // truncated at several boundaries
    static const size_t cuts[] = {0u, 10u, 27u, 28u, 50u, 100u, 171u};
    for (size_t i = 0u; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
        byte_reader_init(&r, body, cuts[i]);
        CHECK(!content_decode_hero(&r, &sentinel));
        CHECK(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
        CHECK(r.offset == 0u);
    }
    // impossible action count
    uint8_t mutated[512];
    std::memcpy(mutated, body, size);
    mutated[24] = 9u;   // action_count low byte
    byte_reader_init(&r, mutated, size);
    CHECK(!content_decode_hero(&r, &sentinel) && r.offset == 0u);
    // bad enum inside an effect
    std::memcpy(mutated, body, size);
    mutated[28u + 32u] = 7u;   // first action's first effect type
    byte_reader_init(&r, mutated, size);
    CHECK(!content_decode_hero(&r, &sentinel) && r.offset == 0u);
    // reserved word set
    std::memcpy(mutated, body, size);
    mutated[26] = 1u;
    byte_reader_init(&r, mutated, size);
    CHECK(!content_decode_hero(&r, &sentinel) && r.offset == 0u);
    CHECK(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
    // the untouched stream still decodes
    byte_reader_init(&r, body, size);
    CHECK(content_decode_hero(&r, &sentinel) && r.offset == size);

    // objective / economy truncation
    ObjectiveDef o = fixture_tower();
    byte_writer_init(&w, body, sizeof(body));
    CHECK(content_encode_objective(&w, &o));
    byte_reader_init(&r, body, CONTENT_OBJECTIVE_ENCODED_BYTES - 1u);
    ObjectiveDef od{};
    CHECK(!content_decode_objective(&r, &od) && r.offset == 0u);
    EconomyRule e = fixture_gold();
    byte_writer_init(&w, body, sizeof(body));
    CHECK(content_encode_economy(&w, &e));
    byte_reader_init(&r, body, CONTENT_ECONOMY_ENCODED_BYTES - 1u);
    EconomyRule ed{};
    CHECK(!content_decode_economy(&r, &ed) && r.offset == 0u);
}

// ---------------------------------------------------------------- container record path

static void write_u32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8u); p[2] = (uint8_t)(v >> 16u); p[3] = (uint8_t)(v >> 24u);
}

TEST(content, container_record_payload_is_validated_fully_before_a_view_is_published) {
    uint8_t baked[512];
    std::memcpy(baked, CONTENT_GOLDEN_HERO_TEST, sizeof(CONTENT_GOLDEN_HERO_TEST));
    const size_t size = sizeof(CONTENT_GOLDEN_HERO_TEST);
    MbaAssetView view{};
    CHECK(mba_inspect(baked, size, &view));
    CHECK(view.header.type == MBA_ASSET_TYPE_HERO && view.record.record_kind == MBA_ASSET_TYPE_HERO);
    CHECK(view.record.schema_version == CONTENT_SCHEMA_VERSION && view.record.bytes_count == 172u);
    CHECK(view.record.bytes == baked + MBA_HEADER_BYTES + MBA_RECORD_PAYLOAD_HEADER_BYTES);

    MbaAssetView untouched;
    std::memset(&untouched, 0xA5, sizeof(untouched));
    const MbaAssetView before = untouched;
    uint8_t m[512];
    // record_kind disagrees with the container tag
    std::memcpy(m, baked, size); write_u32le(m + 32u, MBA_ASSET_TYPE_OBJECTIVE);
    CHECK(!mba_inspect(m, size, &untouched));
    // unknown schema
    std::memcpy(m, baked, size); write_u32le(m + 36u, 2u);
    CHECK(!mba_inspect(m, size, &untouched));
    // record_bytes disagrees with payload_bytes
    std::memcpy(m, baked, size); write_u32le(m + 40u, 171u);
    CHECK(!mba_inspect(m, size, &untouched));
    // reserved word set
    std::memcpy(m, baked, size); write_u32le(m + 44u, 1u);
    CHECK(!mba_inspect(m, size, &untouched));
    // semantically invalid body (effect type 0) behind a perfectly shaped container
    std::memcpy(m, baked, size); m[48u + 28u + 32u] = 0u;
    CHECK(!mba_inspect(m, size, &untouched));
    // trailing byte
    std::memcpy(m, baked, size); m[size] = 0u;
    CHECK(!mba_inspect(m, size + 1u, &untouched));
    // short record header
    std::memcpy(m, baked, size); write_u32le(m + 12u, 8u);
    CHECK(!mba_inspect(m, MBA_HEADER_BYTES + 8u, &untouched));
    // unknown container tag (mesh is reserved, not a record)
    std::memcpy(m, baked, size); write_u32le(m + 8u, MBA_ASSET_TYPE_MESH); write_u32le(m + 32u, MBA_ASSET_TYPE_MESH);
    CHECK(!mba_inspect(m, size, &untouched));
    CHECK(std::memcmp(&untouched, &before, sizeof(untouched)) == 0);

    // The encoder refuses a body its codec would reject, leaving the destination alone.
    uint8_t bad_body[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    uint8_t dest[64];
    std::memset(dest, 0xA5, sizeof(dest));
    uint8_t dest_before[64];
    std::memcpy(dest_before, dest, sizeof(dest));
    size_t written = 999u;
    CHECK(!mba_encode_record(dest, sizeof(dest), asset_id("x.mba"), MBA_ASSET_TYPE_ECONOMY,
                             CONTENT_SCHEMA_VERSION, bad_body, sizeof(bad_body), &written));
    CHECK(std::memcmp(dest, dest_before, sizeof(dest)) == 0 && written == 999u);
    CHECK(!mba_encode_record(dest, sizeof(dest), asset_id("x.mba"), MBA_ASSET_TYPE_MESH,
                             CONTENT_SCHEMA_VERSION, baked, 8u, &written));
}

TEST(content, map_record_is_opaque_but_header_checked) {
    // Minimal .mapdesc: MAPD, schema 1, id, 2x2 cells of 4 bytes, lane count 0.
    uint8_t map[28u + 16u + 2u];
    std::memset(map, 0, sizeof(map));
    std::memcpy(map, "MAPD", 4u);
    write_u32le(map + 4u, 1u);
    write_u32le(map + 8u, 0x12345678u); write_u32le(map + 12u, 0u);
    map[16] = 2u; map[18] = 2u;                 // width, height (u16 LE)
    write_u32le(map + 20u, 32768u);              // cell size 0.5
    write_u32le(map + 24u, 4u);                  // cell_count
    CHECK(content_map_bytes_valid(map, sizeof(map)));
    uint8_t baked[128];
    size_t baked_bytes = 0u;
    CHECK(mba_encode_record(baked, sizeof(baked), asset_id("maps/lane_test.mba"), MBA_ASSET_TYPE_MAP,
                            CONTENT_SCHEMA_VERSION, map, sizeof(map), &baked_bytes));
    MbaAssetView view{};
    CHECK(mba_inspect(baked, baked_bytes, &view));
    CHECK(view.record.record_kind == MBA_ASSET_TYPE_MAP && view.record.bytes_count == sizeof(map));
    CHECK(std::memcmp(view.record.bytes, map, sizeof(map)) == 0);

    uint8_t bad[sizeof(map)];
    std::memcpy(bad, map, sizeof(map)); bad[0] = 'X';
    CHECK(!content_map_bytes_valid(bad, sizeof(bad)));
    std::memcpy(bad, map, sizeof(map)); write_u32le(bad + 24u, 5u);   // cell_count != w*h
    CHECK(!content_map_bytes_valid(bad, sizeof(bad)));
    std::memcpy(bad, map, sizeof(map)); write_u32le(bad + 20u, 0u);   // cell size 0
    CHECK(!content_map_bytes_valid(bad, sizeof(bad)));
    CHECK(!content_map_bytes_valid(map, 28u + 8u));                   // fewer bytes than declared cells
}
