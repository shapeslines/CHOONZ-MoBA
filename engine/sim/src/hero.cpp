#include "sim/hero.h"

#include <cstring>

// ---------------------------------------------------------------- schema validation

static bool effect_is_zero(const SimEffectDef* effect) {
    return effect->effect_type == 0u && effect->damage_type == 0u &&
           effect->duration_ticks == 0u && effect->magnitude == 0 && effect->radius == 0 &&
           effect->scalar == 0;
}

bool sim_effect_def_valid(const SimEffectDef* effect) {
    if (!effect) return false;
    switch (effect->effect_type) {
        case SIM_EFFECT_NONE:
            return effect_is_zero(effect);
        case SIM_EFFECT_PROJECTILE_DAMAGE:
        case SIM_EFFECT_SELF_HEAL:
        case SIM_EFFECT_AREA_SLOW:
            break;
        default:
            return false;
    }
    if (effect->damage_type > SIM_MAX_DAMAGE_TYPE) return false;
    if (effect->magnitude < 0 || effect->radius < 0 || effect->scalar < 0) return false;
    // A slow with no duration or a full-speed scalar is a data mistake, not a
    // silently ignored effect. scalar is the surviving fraction of movement.
    if (effect->effect_type == SIM_EFFECT_AREA_SLOW &&
        (effect->duration_ticks == 0u || effect->radius <= 0 || effect->scalar >= FIX_ONE))
        return false;
    return true;
}

static bool action_is_zero(const SimActionDef* action) {
    if (action->action_id != 0u || action->slot != 0u || action->target_mode != 0u ||
        action->effect_count != 0u || action->cooldown_ticks != 0u ||
        action->cast_time_ticks != 0u || action->resource_cost != 0u || action->range != 0 ||
        action->projectile_speed != 0) return false;
    for (uint16_t i = 0u; i < SIM_MAX_EFFECTS_PER_ACTION; ++i) {
        if (!effect_is_zero(&action->effects[i])) return false;
    }
    return true;
}

bool sim_action_def_valid(const SimActionDef* action) {
    if (!action || action->action_id == 0u) return false;
    if (action->slot >= SIM_MAX_ACTION_SLOTS) return false;
    if (action->target_mode > SIM_TARGET_AREA) return false;
    if (action->effect_count == 0u || action->effect_count > SIM_MAX_EFFECTS_PER_ACTION)
        return false;
    if (action->range < 0 || action->projectile_speed < 0) return false;
    for (uint16_t i = 0u; i < SIM_MAX_EFFECTS_PER_ACTION; ++i) {
        const SimEffectDef* effect = &action->effects[i];
        if (i < action->effect_count) {
            if (!sim_effect_def_valid(effect) || effect->effect_type == SIM_EFFECT_NONE)
                return false;
        } else if (!effect_is_zero(effect)) {
            return false;
        }
    }
    return true;
}

bool sim_hero_def_valid(const SimHeroDef* def) {
    if (!def || def->hero_def_id == 0u) return false;
    if (def->max_health <= 0) return false;
    if (def->move_speed < 0 || def->attack_range < 0) return false;
    if (def->action_count > SIM_MAX_ACTION_SLOTS) return false;
    for (uint16_t i = 0u; i < SIM_MAX_ACTION_SLOTS; ++i) {
        const SimActionDef* action = &def->actions[i];
        if (i < def->action_count) {
            if (!sim_action_def_valid(action)) return false;
            for (uint16_t previous = 0u; previous < i; ++previous) {
                if (def->actions[previous].slot == action->slot) return false;
            }
        } else if (!action_is_zero(action)) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- def table

size_t sim_hero_def_table_memory_required(uint16_t capacity) {
    if (capacity == 0u) return 0u;
    size_t bytes = sizeof(SimHeroDef) * static_cast<size_t>(capacity);
    if (bytes > SIZE_MAX - (alignof(SimHeroDef) - 1u)) return 0u;
    return bytes + alignof(SimHeroDef) - 1u;
}

bool sim_hero_def_table_init(SimHeroDefTable* table, Arena* arena, uint16_t capacity) {
    size_t required = sim_hero_def_table_memory_required(capacity);
    if (!table || !arena || !arena->base || arena->offset > arena->reserved ||
        required == 0u || required > arena->reserved - arena->offset) return false;

    SimHeroDefTable staged{};
    staged.defs = static_cast<SimHeroDef*>(arena_push_zero(
        arena, sizeof(SimHeroDef) * static_cast<size_t>(capacity), alignof(SimHeroDef)));
    if (!staged.defs) return false;
    staged.capacity = capacity;
    *table = staged;
    return true;
}

bool sim_hero_def_table_valid(const SimHeroDefTable* table) {
    if (!table) return false;
    if (table->capacity == 0u) return !table->defs && table->count == 0u;
    if (!table->defs || table->count > table->capacity) return false;
    for (uint16_t i = 0u; i < table->count; ++i) {
        if (!sim_hero_def_valid(&table->defs[i])) return false;
    }
    return true;
}

const SimHeroDef* sim_hero_def_table_get(const SimHeroDefTable* table, uint16_t index) {
    if (!table || !table->defs || index >= table->count) return nullptr;
    return &table->defs[index];
}

bool sim_hero_def_table_install(SimHeroDefTable* table, const SimHeroDef* def,
                                uint16_t* out_index) {
    if (!sim_hero_def_table_valid(table) || !sim_hero_def_valid(def)) return false;
    if (table->count >= table->capacity) return false;
    for (uint16_t i = 0u; i < table->count; ++i) {
        if (table->defs[i].hero_def_id == def->hero_def_id) return false;
    }
    uint16_t index = table->count;
    table->defs[index] = *def;
    table->count = static_cast<uint16_t>(index + 1u);
    if (out_index) *out_index = index;
    return true;
}
