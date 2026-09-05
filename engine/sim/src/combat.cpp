#include "sim/combat.h"

#include "core/assert.h"
#include "sim/systems.h"

// ---------------------------------------------------------------- geometry

int64_t sim_range_squared(mm::fix range) {
    int64_t widened = static_cast<int64_t>(range);
    return widened * widened;
}

static bool transform_position(const SimWorld* world, EntityId entity,
                               mm::fix* out_x, mm::fix* out_y) {
    ConstTransformView view{};
    if (!world || !transform_pool_get_const(&world->transforms, entity, &view)) return false;
    *out_x = *view.position_x;
    *out_y = *view.position_y;
    return true;
}

static int64_t squared_length(mm::fix dx, mm::fix dy) {
    int64_t x = static_cast<int64_t>(dx);
    int64_t y = static_cast<int64_t>(dy);
    return x * x + y * y;
}

bool sim_distance_squared(const SimWorld* world, EntityId a, EntityId b, int64_t* out) {
    mm::fix ax = 0, ay = 0, bx = 0, by = 0;
    if (!out || !transform_position(world, a, &ax, &ay) ||
        !transform_position(world, b, &bx, &by)) return false;
    *out = squared_length(static_cast<mm::fix>(bx - ax), static_cast<mm::fix>(by - ay));
    return true;
}

bool sim_distance_squared_point(const SimWorld* world, EntityId entity,
                                mm::fix point_x, mm::fix point_y, int64_t* out) {
    mm::fix ex = 0, ey = 0;
    if (!out || !transform_position(world, entity, &ex, &ey)) return false;
    *out = squared_length(static_cast<mm::fix>(point_x - ex),
                          static_cast<mm::fix>(point_y - ey));
    return true;
}

// ---------------------------------------------------------------- pipeline

SimEffectDef sim_basic_attack_effect(void) {
    SimEffectDef effect{};
    effect.effect_type = SIM_EFFECT_PROJECTILE_DAMAGE;
    effect.magnitude = SIM_BASIC_ATTACK_MAGNITUDE;
    return effect;
}

// Stage 4 of the pipeline. Armor is a v1 placeholder: damage_type is stored,
// range-validated, and hashed, but selects nothing until objectives land.
static int32_t apply_mitigation(int32_t magnitude) {
    const int32_t armor = 0;
    int32_t mitigated = magnitude - armor;
    return mitigated < 0 ? 0 : mitigated;
}

static bool effect_target_is_damageable(const SimWorld* world, EntityId target) {
    return entity_manager_is_alive(&world->entities, target) &&
           health_pool_has(&world->health, target);
}

// The single walk. commit == false measures and mutates nothing; commit == true
// appends. Both traversals are literally the same code, so a pre-flight count can
// never disagree with the commit.
static bool resolve_effect_walk(SimWorld* world, EntityId source, EntityId target,
                                const SimEffectDef* effect, mm::fix point_x, mm::fix point_y,
                                bool commit, uint32_t* damage_events, uint32_t* sim_events) {
    if (!world || !effect || !sim_effect_def_valid(effect)) return false;
    if (!entity_manager_is_alive(&world->entities, source)) return false;

    switch (effect->effect_type) {
        case SIM_EFFECT_NONE:
            return true;
        case SIM_EFFECT_PROJECTILE_DAMAGE: {
            if (!effect_target_is_damageable(world, target)) return false;
            if (!commit) {
                ++*damage_events;
                return true;
            }
            DamageEvent event{source, target, apply_mitigation(effect->magnitude)};
            return damage_event_queue_append(&world->damage_events, &event);
        }
        case SIM_EFFECT_SELF_HEAL: {
            if (!effect_target_is_damageable(world, target)) return false;
            if (!commit) {
                ++*sim_events;
                return true;
            }
            SimEvent event = sim_event_make_heal(world->tick, target, effect->magnitude);
            return sim_event_queue_append(&world->sim_events, &event);
        }
        case SIM_EFFECT_AREA_SLOW: {
            // Ordered by ascending entity index, never dense order, so the append
            // order of a multi-target effect is a property of identity alone.
            ComponentPoolOrderedView ordered{};
            if (!component_pool_ordered_view(&world->transforms.membership, &ordered))
                return false;
            int64_t radius_squared = sim_range_squared(effect->radius);
            for (uint32_t i = 0u; i < ordered.count; ++i) {
                EntityId candidate = ordered.entities[i];
                int64_t distance_squared = 0;
                if (!sim_distance_squared_point(world, candidate, point_x, point_y,
                                                &distance_squared)) continue;
                if (distance_squared > radius_squared) continue;
                if (!entity_manager_is_alive(&world->entities, candidate)) continue;
                if (!commit) {
                    ++*sim_events;
                    continue;
                }
                SimEvent event = sim_event_make_status_applied(
                    world->tick, candidate, SIM_EFFECT_AREA_SLOW, effect->duration_ticks,
                    effect->magnitude, effect->scalar);
                if (!sim_event_queue_append(&world->sim_events, &event)) return false;
            }
            return true;
        }
        default:
            return false;
    }
}

bool resolve_effect(SimWorld* world, EntityId source, EntityId target,
                    const SimEffectDef* effect, mm::fix point_x, mm::fix point_y) {
    return resolve_effect_walk(world, source, target, effect, point_x, point_y, true,
                               nullptr, nullptr);
}

bool resolve_effect_measure(const SimWorld* world, EntityId source, EntityId target,
                            const SimEffectDef* effect, mm::fix point_x, mm::fix point_y,
                            uint32_t* damage_events, uint32_t* sim_events) {
    if (!damage_events || !sim_events) return false;
    return resolve_effect_walk(const_cast<SimWorld*>(world), source, target, effect,
                               point_x, point_y, false, damage_events, sim_events);
}

// ---------------------------------------------------------------- committers

bool sys_combat_resolve(SimWorld* world) {
    if (!world) return false;
    DamageEventView events{};
    if (!damage_event_queue_read(&world->damage_events, &events)) return false;

    // Validate the whole read phase before applying any damage. This also makes
    // a future next-tick policy surface stale-target handling at one seam.
    for (uint32_t i = 0u; i < events.count; ++i) {
        const DamageEvent& event = events.events[i];
        if (!damage_event_is_canonical(&event) ||
            !entity_manager_is_alive(&world->entities, event.target) ||
            !health_pool_has(&world->health, event.target) ||
            (event.source.h != HANDLE_NULL &&
             !entity_manager_is_alive(&world->entities, event.source))) return false;
    }

    // M5.3: a damaged objective is a damage-commit observation, so its event is
    // emitted from here, the one place that commits damage. Pre-flight the whole
    // read phase so an envelope overflow fails the source operation before any
    // health moves (ADR-0014).
    if (world->config.objective_capacity > 0u && world->config.sim_event_capacity > 0u) {
        uint32_t objective_hits = 0u;
        for (uint32_t i = 0u; i < events.count; ++i) {
            if (events.events[i].amount > 0 &&
                objective_pool_has(&world->objectives, events.events[i].target))
                ++objective_hits;
        }
        if (sim_event_queue_write_room(&world->sim_events) < objective_hits) return false;
    }

    for (uint32_t i = 0u; i < events.count; ++i) {
        const DamageEvent& event = events.events[i];
        HealthView health{};
        bool found = health_pool_get(&world->health, event.target, &health);
        ENSURE(found);
        if (event.amount >= *health.current) *health.current = 0;
        else *health.current -= event.amount;
        if (event.amount > 0) *health.damage_cooldown = 3u;
        // The sole kill-credit input, written on the damage commit and nowhere else.
        if (event.amount > 0) *health.last_damage_source = event.source;
        if (event.amount > 0 && world->config.objective_capacity > 0u &&
            world->config.sim_event_capacity > 0u &&
            objective_pool_has(&world->objectives, event.target)) {
            SimEvent damaged = sim_event_make_objective_damaged(
                world->tick, event.target, event.source, event.amount, *health.current);
            if (!sim_event_queue_append(&world->sim_events, &damaged)) return false;
        }
    }
    return true;
}

// Commits the published envelope, then reports hero deaths. Health is only ever
// written here and in sys_combat_resolve; status rows are only ever written here
// and expired by sys_status.
bool sys_effects_resolve(SimWorld* world) {
    if (!world) return false;
    if (world->config.sim_event_capacity == 0u) return true;
    SimEventView events{};
    if (!sim_event_queue_read(&world->sim_events, &events)) return false;

    // Validate the whole read phase before committing any of it, the same seam
    // discipline sys_combat_resolve uses.
    for (uint32_t i = 0u; i < events.count; ++i) {
        const SimEvent& event = events.events[i];
        if (!sim_event_is_canonical(&event)) return false;
        EntityId target{sim_event_payload_u32(&event, 0u)};
        if (event.event_kind == SIM_EVENT_HEAL &&
            (!entity_manager_is_alive(&world->entities, target) ||
             !health_pool_has(&world->health, target))) return false;
    }

    for (uint32_t i = 0u; i < events.count; ++i) {
        const SimEvent& event = events.events[i];
        EntityId target{sim_event_payload_u32(&event, 0u)};
        if (event.event_kind == SIM_EVENT_HEAL) {
            HealthView health{};
            bool found = health_pool_get(&world->health, target, &health);
            ENSURE(found);
            int32_t amount = apply_mitigation(sim_event_payload_i32(&event, 4u));
            int32_t headroom = *health.maximum - *health.current;
            *health.current += amount < headroom ? amount : headroom;
        } else if (event.event_kind == SIM_EVENT_STATUS_APPLIED) {
            if (!entity_manager_is_alive(&world->entities, target)) continue;
            bool applied = status_pool_add(&world->statuses, target,
                                           sim_event_payload_u8(&event, 4u),
                                           sim_event_payload_u16(&event, 6u),
                                           sim_event_payload_i32(&event, 8u),
                                           sim_event_payload_i32(&event, 12u));
            if (!applied) return false;
        }
    }

    // M5.3: the death verdict moved to sys_death in objectives.cpp, where it is
    // generalized over heroes, minions, and objectives and carries a killer. This
    // step now commits heals and statuses only.
    return true;
}
