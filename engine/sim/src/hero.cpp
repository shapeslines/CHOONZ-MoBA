#include "sim/hero.h"

#include "sim/combat.h"
#include "sim/systems.h"

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

// ---------------------------------------------------------------- sys_hero_actions

// Two passes over the same code: pass one measures the event slots every pending
// action would consume, pass two commits. Neither pass changes anything the other
// reads, so a queue that cannot hold the tick fails the source operation before a
// single byte of state moves (ADR-0014, proto-design section 5 event-overflow).
static bool hero_action_run(SimWorld* world, EntityId entity, bool commit,
                            uint32_t* damage_needed, uint32_t* sim_needed) {
    HeroView hero{};
    if (!hero_pool_get(&world->heroes, entity, &hero)) return false;
    if (*hero.pending_kind == SIM_HERO_PENDING_NONE) return true;

    const SimHeroDef* def = sim_hero_def_table_get(&world->hero_defs, *hero.def_index);
    if (!def) return true;

    if (*hero.pending_kind == SIM_HERO_PENDING_ATTACK) {
        // Cooldown and range are not tick failures: M5.1 defined
        // COMMAND_REJECT_COOLDOWN / COMMAND_REJECT_RANGE for command_intake_run, in
        // front of this seam, so a stale intent is a no-op here.
        if (*hero.basic_attack_cooldown != 0u) return true;
        EntityId target = *hero.pending_target;
        if (!entity_manager_is_alive(&world->entities, target) ||
            !health_pool_has(&world->health, target)) return true;
        int64_t distance_squared = 0;
        if (!sim_distance_squared(world, entity, target, &distance_squared)) return true;
        if (distance_squared > sim_range_squared(def->attack_range)) return true;

        SimEffectDef effect = sim_basic_attack_effect();
        if (!commit) return resolve_effect_measure(world, entity, target, &effect, 0, 0,
                                                   damage_needed, sim_needed);
        if (!resolve_effect(world, entity, target, &effect, 0, 0)) return false;
        *hero.basic_attack_cooldown = SIM_BASIC_ATTACK_COOLDOWN_TICKS;
        return true;
    }
    return true;
}

bool sys_hero_actions(SimWorld* world) {
    if (!world) return false;
    if (world->config.hero_capacity == 0u) return true;
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->heroes.membership, &ordered)) return false;

    uint32_t damage_needed = 0u;
    uint32_t sim_needed = 0u;
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        if (!hero_action_run(world, ordered.entities[i], false, &damage_needed, &sim_needed))
            return false;
    }
    if (damage_needed > damage_event_queue_write_room(&world->damage_events)) return false;
    if (sim_needed > 0u && sim_needed > sim_event_queue_write_room(&world->sim_events))
        return false;

    for (uint32_t i = 0u; i < ordered.count; ++i) {
        if (!hero_action_run(world, ordered.entities[i], true, nullptr, nullptr)) return false;
    }

    // Tick-local intent is authoritative only between sys_apply_commands and here,
    // so it is cleared unconditionally and in the same ordered walk.
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        HeroView hero{};
        if (!hero_pool_get(&world->heroes, ordered.entities[i], &hero)) return false;
        *hero.pending_kind = SIM_HERO_PENDING_NONE;
        *hero.pending_slot = 0u;
        *hero.pending_target = EntityId{HANDLE_NULL};
        *hero.pending_point_x = 0;
        *hero.pending_point_y = 0;
    }
    return true;
}
