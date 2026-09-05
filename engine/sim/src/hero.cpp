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

static const SimActionDef* action_at_slot(const SimHeroDef* def, uint8_t slot) {
    for (uint16_t i = 0u; i < def->action_count; ++i) {
        if (def->actions[i].slot == slot) return &def->actions[i];
    }
    return nullptr;
}

static bool transform_position_of(const SimWorld* world, EntityId entity,
                                  mm::fix* out_x, mm::fix* out_y) {
    ConstTransformView view{};
    if (!transform_pool_get_const(&world->transforms, entity, &view)) return false;
    *out_x = *view.position_x;
    *out_y = *view.position_y;
    return true;
}

// A projectile-delivered damage effect spawns a projectile entity instead of
// appending immediately; the impact re-enters the same pipeline in sys_projectiles.
// Everything else resolves this tick.
static bool hero_cast_effects(SimWorld* world, EntityId caster, uint16_t def_index,
                              const SimActionDef* action, EntityId target,
                              mm::fix point_x, mm::fix point_y, bool commit,
                              uint32_t* damage_needed, uint32_t* sim_needed) {
    bool projectile_delivery = action->target_mode == SIM_TARGET_ENTITY &&
                               action->projectile_speed > 0;
    for (uint16_t i = 0u; i < action->effect_count; ++i) {
        const SimEffectDef* effect = &action->effects[i];
        if (projectile_delivery && effect->effect_type == SIM_EFFECT_PROJECTILE_DAMAGE) {
            if (!commit) continue;
            // A full projectile pool makes the delivery a no-op rather than a failed
            // tick: bounded state is a design limit, not a malformed input.
            if (world->projectiles.membership.count >= world->config.projectile_capacity)
                continue;
            mm::fix origin_x = 0;
            mm::fix origin_y = 0;
            if (!transform_position_of(world, caster, &origin_x, &origin_y)) continue;
            EntityId shot = entity_manager_create(&world->entities);
            if (shot.h == HANDLE_NULL) continue;
            if (!projectile_pool_add(&world->projectiles, shot, caster, target, def_index,
                                     action->slot, static_cast<uint8_t>(i), origin_x,
                                     origin_y, action->projectile_speed,
                                     SIM_PROJECTILE_MAX_LIFETIME_TICKS)) return false;
            continue;
        }
        if (!commit) {
            if (!resolve_effect_measure(world, caster, target, effect, point_x, point_y,
                                        damage_needed, sim_needed)) return false;
            continue;
        }
        if (!resolve_effect(world, caster, target, effect, point_x, point_y)) return false;
    }
    return true;
}

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

    if (*hero.pending_kind != SIM_HERO_PENDING_CAST) return true;
    const SimActionDef* action = action_at_slot(def, *hero.pending_slot);
    if (!action) return true;
    uint8_t slot = *hero.pending_slot;
    if (hero.action_cooldown[slot] != 0u) return true;
    if (*hero.resource < static_cast<int32_t>(action->resource_cost)) return true;

    // target_mode comes from the def table, never from the wire, so the meaning of
    // pending_target / pending_point is unambiguous without a canonicality lookup.
    EntityId resolve_target = entity;
    mm::fix point_x = 0;
    mm::fix point_y = 0;
    if (action->target_mode == SIM_TARGET_ENTITY) {
        resolve_target = *hero.pending_target;
        if (!entity_manager_is_alive(&world->entities, resolve_target)) return true;
        int64_t distance_squared = 0;
        if (!sim_distance_squared(world, entity, resolve_target, &distance_squared)) return true;
        if (distance_squared > sim_range_squared(action->range)) return true;
        if (!transform_position_of(world, resolve_target, &point_x, &point_y)) return true;
    } else if (action->target_mode == SIM_TARGET_SELF) {
        if (!transform_position_of(world, entity, &point_x, &point_y)) return true;
    } else {
        point_x = *hero.pending_point_x;
        point_y = *hero.pending_point_y;
        int64_t distance_squared = 0;
        if (!sim_distance_squared_point(world, entity, point_x, point_y, &distance_squared))
            return true;
        if (distance_squared > sim_range_squared(action->range)) return true;
    }

    if (!hero_cast_effects(world, entity, *hero.def_index, action, resolve_target,
                           point_x, point_y, commit, damage_needed, sim_needed))
        return false;
    if (!commit) return true;
    *hero.resource -= static_cast<int32_t>(action->resource_cost);
    hero.action_cooldown[slot] = action->cooldown_ticks;
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

// ---------------------------------------------------------------- sys_projectiles

// Advances one projectile. Measurement and commit walk the same code, and a hit
// re-enters resolve_effect so a projectile has no mutation path of its own.
static bool projectile_step(SimWorld* world, EntityId entity, bool commit,
                            uint32_t* damage_needed, uint32_t* sim_needed) {
    ProjectileView shot{};
    if (!projectile_pool_get(&world->projectiles, entity, &shot)) return true;
    if (!entity_manager_is_alive(&world->entities, entity)) return true;

    EntityId target = *shot.target;
    bool expired = *shot.remaining_ticks == 0u ||
                   !entity_manager_is_alive(&world->entities, target) ||
                   !transform_pool_has(&world->transforms, target);
    if (expired) {
        if (commit && !sim_destroy_deferred(world, entity)) return false;
        return true;
    }

    mm::fix target_x = 0;
    mm::fix target_y = 0;
    if (!transform_position_of(world, target, &target_x, &target_y)) return true;
    int64_t dx = static_cast<int64_t>(target_x) - static_cast<int64_t>(*shot.position_x);
    int64_t dy = static_cast<int64_t>(target_y) - static_cast<int64_t>(*shot.position_y);
    mm::fix step = mm::fix_mul(*shot.speed, SIM_DT_FIXED);
    int64_t distance_squared = dx * dx + dy * dy;
    int64_t step_squared = static_cast<int64_t>(step) * static_cast<int64_t>(step);

    if (distance_squared <= step_squared) {
        const SimHeroDef* def = sim_hero_def_table_get(&world->hero_defs, *shot.def_index);
        const SimActionDef* action = def ? action_at_slot(def, *shot.action_slot) : nullptr;
        if (!action || *shot.effect_index >= action->effect_count) {
            if (commit && !sim_destroy_deferred(world, entity)) return false;
            return true;
        }
        const SimEffectDef* effect = &action->effects[*shot.effect_index];
        if (!commit) {
            return resolve_effect_measure(world, *shot.source, target, effect, target_x,
                                          target_y, damage_needed, sim_needed);
        }
        if (!resolve_effect(world, *shot.source, target, effect, target_x, target_y))
            return false;
        return sim_destroy_deferred(world, entity);
    }

    if (!commit) return true;
    // Exact integer distance: isqrt of a Q32.32 square is Q16.16, so the step is
    // deterministic on every compiler and optimization level.
    int64_t distance = static_cast<int64_t>(mm::fix_isqrt64(static_cast<uint64_t>(distance_squared)));
    if (distance <= 0) return true;
    *shot.position_x = static_cast<mm::fix>(
        static_cast<int64_t>(*shot.position_x) + (dx * static_cast<int64_t>(step)) / distance);
    *shot.position_y = static_cast<mm::fix>(
        static_cast<int64_t>(*shot.position_y) + (dy * static_cast<int64_t>(step)) / distance);
    --*shot.remaining_ticks;
    return true;
}

bool sys_projectiles(SimWorld* world) {
    if (!world) return false;
    if (world->config.projectile_capacity == 0u) return true;
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->projectiles.membership, &ordered)) return false;

    uint32_t damage_needed = 0u;
    uint32_t sim_needed = 0u;
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        if (!projectile_step(world, ordered.entities[i], false, &damage_needed, &sim_needed))
            return false;
    }
    if (damage_needed > damage_event_queue_write_room(&world->damage_events)) return false;
    if (sim_needed > 0u && sim_needed > sim_event_queue_write_room(&world->sim_events))
        return false;
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        if (!projectile_step(world, ordered.entities[i], true, nullptr, nullptr)) return false;
    }
    return true;
}

// ---------------------------------------------------------------- sys_status

bool sys_status(SimWorld* world) {
    if (!world) return false;
    if (world->config.status_capacity == 0u) return true;
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->statuses.membership, &ordered)) return false;

    uint32_t expiring = 0u;
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        StatusView status{};
        if (!status_pool_get(&world->statuses, ordered.entities[i], &status)) continue;
        if (*status.remaining_ticks <= 1u) ++expiring;
    }
    if (expiring > 0u && expiring > sim_event_queue_write_room(&world->sim_events))
        return false;

    for (uint32_t i = 0u; i < ordered.count; ++i) {
        EntityId entity = ordered.entities[i];
        StatusView status{};
        if (!status_pool_get(&world->statuses, entity, &status)) continue;
        if (*status.remaining_ticks > 1u) {
            --*status.remaining_ticks;
            continue;
        }
        SimEvent event = sim_event_make_status_expired(world->tick, entity,
                                                       *status.effect_type);
        if (!sim_event_queue_append(&world->sim_events, &event)) return false;
        if (!status_pool_remove(&world->statuses, entity)) return false;
    }
    return true;
}
