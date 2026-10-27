#include "assets/content.h"

#include <string.h>

// ---------------------------------------------------------------- validation

static bool effect_valid(const EffectDef* e) {
    if (e->effect_type < CONTENT_EFFECT_PROJECTILE_DAMAGE ||
        e->effect_type > CONTENT_EFFECT_AREA_SLOW) return false;
    if (e->damage_type != 0u) return false;
    if (e->magnitude < 0 || e->radius_q16 < 0 || e->scalar_q16 < 0) return false;
    return true;
}

static bool action_valid(const ActionDef* a, uint32_t expected_slot) {
    if (a->action_id == ASSET_ID_NULL) return false;
    if (a->slot != expected_slot) return false;
    if (a->target_mode < CONTENT_TARGET_POINT || a->target_mode > CONTENT_TARGET_AREA) return false;
    if (a->effect_count > CONTENT_MAX_EFFECTS) return false;
    if (a->range_q16 < 0 || a->projectile_speed_q16 < 0) return false;
    for (uint32_t i = 0u; i < a->effect_count; ++i) {
        if (!effect_valid(&a->effects[i])) return false;
    }
    return true;
}

bool content_validate_hero(const HeroDef* h) {
    if (!h || h->schema_version != CONTENT_SCHEMA_VERSION) return false;
    if (h->hero_def_id == ASSET_ID_NULL || h->max_health <= 0) return false;
    if (h->move_speed_q16 < 0 || h->attack_range_q16 < 0) return false;
    if (h->action_count > CONTENT_MAX_ACTIONS || h->reserved != 0u) return false;
    for (uint32_t i = 0u; i < h->action_count; ++i) {
        if (!action_valid(&h->actions[i], i)) return false;
    }
    return true;
}

bool content_validate_objective(const ObjectiveDef* o) {
    if (!o || o->objective_id == ASSET_ID_NULL) return false;
    if (o->objective_kind < CONTENT_OBJECTIVE_TOWER || o->objective_kind > CONTENT_OBJECTIVE_CORE)
        return false;
    if (o->reserved != 0u || o->max_health <= 0 || o->armor < 0) return false;
    if (o->target_policy > CONTENT_TARGET_POLICY_HERO_FIRST) return false;
    if (o->reserved2[0] != 0u || o->reserved2[1] != 0u || o->reserved2[2] != 0u) return false;
    return true;
}

bool content_validate_economy(const EconomyRule* r) {
    if (!r) return false;
    if (r->award_kind < CONTENT_AWARD_GOLD || r->award_kind > CONTENT_AWARD_XP) return false;
    if (r->source_kind < CONTENT_SOURCE_HERO_KILL || r->source_kind > CONTENT_SOURCE_OBJECTIVE_DESTROY)
        return false;
    if (r->reserved != 0u || r->award_amount == 0u) return false;
    return true;
}

size_t content_hero_encoded_bytes(const HeroDef* h) {
    if (!content_validate_hero(h)) return 0u;
    size_t total = CONTENT_HERO_BASE_ENCODED_BYTES;
    for (uint32_t i = 0u; i < h->action_count; ++i) {
        total += CONTENT_ACTION_BASE_ENCODED_BYTES +
                 (size_t)h->actions[i].effect_count * CONTENT_EFFECT_ENCODED_BYTES;
    }
    return total;
}

// ---------------------------------------------------------------- encode

static bool writer_has(ByteWriter* w, size_t size) {
    if (!w || !w->ok || w->offset > w->capacity || size > w->capacity - w->offset) {
        if (w) w->ok = false;
        return false;
    }
    return true;
}

static void write_effect(ByteWriter* w, const EffectDef* e) {
    byte_writer_write_u8(w, e->effect_type);
    byte_writer_write_u8(w, e->damage_type);
    byte_writer_write_u16(w, e->duration_ticks);
    byte_writer_write_i32(w, e->magnitude);
    byte_writer_write_i32(w, e->radius_q16);
    byte_writer_write_i32(w, e->scalar_q16);
}

bool content_encode_hero(ByteWriter* w, const HeroDef* h) {
    size_t total = content_hero_encoded_bytes(h);
    if (total == 0u || !writer_has(w, total)) return false;
    byte_writer_write_u32(w, h->schema_version);
    byte_writer_write_u64(w, h->hero_def_id);
    byte_writer_write_i32(w, h->max_health);
    byte_writer_write_i32(w, h->move_speed_q16);
    byte_writer_write_i32(w, h->attack_range_q16);
    byte_writer_write_u16(w, h->action_count);
    byte_writer_write_u16(w, h->reserved);
    for (uint32_t i = 0u; i < h->action_count; ++i) {
        const ActionDef* a = &h->actions[i];
        byte_writer_write_u64(w, a->action_id);
        byte_writer_write_u8(w, a->slot);
        byte_writer_write_u8(w, a->target_mode);
        byte_writer_write_u16(w, a->effect_count);
        byte_writer_write_u32(w, a->cooldown_ticks);
        byte_writer_write_u32(w, a->cast_time_ticks);
        byte_writer_write_u32(w, a->resource_cost);
        byte_writer_write_i32(w, a->range_q16);
        byte_writer_write_i32(w, a->projectile_speed_q16);
        for (uint32_t j = 0u; j < a->effect_count; ++j) write_effect(w, &a->effects[j]);
    }
    return byte_writer_ok(w);
}

bool content_encode_objective(ByteWriter* w, const ObjectiveDef* o) {
    if (!content_validate_objective(o) || !writer_has(w, CONTENT_OBJECTIVE_ENCODED_BYTES)) return false;
    byte_writer_write_u64(w, o->objective_id);
    byte_writer_write_u8(w, o->owner_team_id);
    byte_writer_write_u8(w, o->objective_kind);
    byte_writer_write_u16(w, o->reserved);
    byte_writer_write_i32(w, o->max_health);
    byte_writer_write_i32(w, o->armor);
    byte_writer_write_u8(w, o->target_policy);
    byte_writer_write_bytes(w, o->reserved2, 3u);
    return byte_writer_ok(w);
}

bool content_encode_economy(ByteWriter* w, const EconomyRule* r) {
    if (!content_validate_economy(r) || !writer_has(w, CONTENT_ECONOMY_ENCODED_BYTES)) return false;
    byte_writer_write_u8(w, r->award_kind);
    byte_writer_write_u8(w, r->source_kind);
    byte_writer_write_u16(w, r->reserved);
    byte_writer_write_u32(w, r->award_amount);
    return byte_writer_ok(w);
}

// ---------------------------------------------------------------- decode

static bool read_effect(ByteReader* r, EffectDef* e) {
    return byte_reader_read_u8(r, &e->effect_type) && byte_reader_read_u8(r, &e->damage_type) &&
           byte_reader_read_u16(r, &e->duration_ticks) && byte_reader_read_i32(r, &e->magnitude) &&
           byte_reader_read_i32(r, &e->radius_q16) && byte_reader_read_i32(r, &e->scalar_q16);
}

bool content_decode_hero(ByteReader* reader, HeroDef* out) {
    if (!reader || !out || !reader->ok) return false;
    ByteReader cursor = *reader;
    HeroDef h{};
    if (!byte_reader_read_u32(&cursor, &h.schema_version) ||
        !byte_reader_read_u64(&cursor, &h.hero_def_id) ||
        !byte_reader_read_i32(&cursor, &h.max_health) ||
        !byte_reader_read_i32(&cursor, &h.move_speed_q16) ||
        !byte_reader_read_i32(&cursor, &h.attack_range_q16) ||
        !byte_reader_read_u16(&cursor, &h.action_count) ||
        !byte_reader_read_u16(&cursor, &h.reserved)) return false;
    if (h.action_count > CONTENT_MAX_ACTIONS) return false;
    if ((size_t)h.action_count * CONTENT_ACTION_BASE_ENCODED_BYTES > byte_reader_remaining(&cursor))
        return false;
    for (uint32_t i = 0u; i < h.action_count; ++i) {
        ActionDef* a = &h.actions[i];
        if (!byte_reader_read_u64(&cursor, &a->action_id) ||
            !byte_reader_read_u8(&cursor, &a->slot) ||
            !byte_reader_read_u8(&cursor, &a->target_mode) ||
            !byte_reader_read_u16(&cursor, &a->effect_count) ||
            !byte_reader_read_u32(&cursor, &a->cooldown_ticks) ||
            !byte_reader_read_u32(&cursor, &a->cast_time_ticks) ||
            !byte_reader_read_u32(&cursor, &a->resource_cost) ||
            !byte_reader_read_i32(&cursor, &a->range_q16) ||
            !byte_reader_read_i32(&cursor, &a->projectile_speed_q16)) return false;
        if (a->effect_count > CONTENT_MAX_EFFECTS) return false;
        if ((size_t)a->effect_count * CONTENT_EFFECT_ENCODED_BYTES > byte_reader_remaining(&cursor))
            return false;
        for (uint32_t j = 0u; j < a->effect_count; ++j) {
            if (!read_effect(&cursor, &a->effects[j])) return false;
        }
    }
    if (!content_validate_hero(&h)) return false;
    *out = h;
    *reader = cursor;
    return true;
}

bool content_decode_objective(ByteReader* reader, ObjectiveDef* out) {
    if (!reader || !out || !reader->ok) return false;
    ByteReader cursor = *reader;
    ObjectiveDef o{};
    if (!byte_reader_read_u64(&cursor, &o.objective_id) ||
        !byte_reader_read_u8(&cursor, &o.owner_team_id) ||
        !byte_reader_read_u8(&cursor, &o.objective_kind) ||
        !byte_reader_read_u16(&cursor, &o.reserved) ||
        !byte_reader_read_i32(&cursor, &o.max_health) ||
        !byte_reader_read_i32(&cursor, &o.armor) ||
        !byte_reader_read_u8(&cursor, &o.target_policy) ||
        !byte_reader_read_bytes(&cursor, o.reserved2, 3u)) return false;
    if (!content_validate_objective(&o)) return false;
    *out = o;
    *reader = cursor;
    return true;
}

bool content_decode_economy(ByteReader* reader, EconomyRule* out) {
    if (!reader || !out || !reader->ok) return false;
    ByteReader cursor = *reader;
    EconomyRule r{};
    if (!byte_reader_read_u8(&cursor, &r.award_kind) ||
        !byte_reader_read_u8(&cursor, &r.source_kind) ||
        !byte_reader_read_u16(&cursor, &r.reserved) ||
        !byte_reader_read_u32(&cursor, &r.award_amount)) return false;
    if (!content_validate_economy(&r)) return false;
    *out = r;
    *reader = cursor;
    return true;
}

// ---------------------------------------------------------------- map (opaque)

bool content_map_bytes_valid(const uint8_t* bytes, size_t size) {
    if (!bytes || size < CONTENT_MAP_MIN_ENCODED_BYTES) return false;
    ByteReader reader;
    byte_reader_init(&reader, bytes, size);
    uint8_t magic[4];
    uint32_t schema_version = 0u;
    uint64_t map_id = 0u;
    uint16_t width = 0u, height = 0u;
    int32_t cell_size = 0;
    uint32_t cell_count = 0u;
    if (!byte_reader_read_bytes(&reader, magic, 4u) || !byte_reader_read_u32(&reader, &schema_version) ||
        !byte_reader_read_u64(&reader, &map_id) || !byte_reader_read_u16(&reader, &width) ||
        !byte_reader_read_u16(&reader, &height) || !byte_reader_read_i32(&reader, &cell_size) ||
        !byte_reader_read_u32(&reader, &cell_count)) return false;
    if (memcmp(magic, "MAPD", 4u) != 0 || schema_version != 1u) return false;
    if (width == 0u || height == 0u || cell_size <= 0) return false;
    if (cell_count != (uint32_t)width * (uint32_t)height) return false;
    // Every cell is 4 bytes and a lane count follows the cell block.
    size_t remaining = byte_reader_remaining(&reader);
    if ((size_t)cell_count > (remaining - 2u) / 4u) return false;
    return true;
}
