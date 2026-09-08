#include "sim/systems.h"

#include "core/assert.h"

static mm::fix fix_add_wrap(mm::fix a, mm::fix b) {
    return static_cast<mm::fix>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

bool sys_apply_commands(SimWorld* world, const SimCommandBuffer* commands) {
    // This preserves standalone failure atomicity for the public system seam.
    if (!sim_validate_commands(world, commands)) return false;
    if (!commands) return true;

    for (uint32_t i = 0u; i < commands->count; ++i) {
        const SimCommand& command = commands->commands[i];
        EntityId target = world->unit_entities[command.unit_index];
        if (command.kind == SIM_COMMAND_SET_VELOCITY) {
            VelocityView velocity{};
            bool found = velocity_pool_get(&world->velocities, target, &velocity);
            ENSURE(found);
            *velocity.velocity_x = command.value_x;
            *velocity.velocity_y = command.value_y;
        } else if (command.kind == SIM_COMMAND_ATTACK) {
            // Commands only record intent. sys_hero_actions is the one place that
            // validates cooldown and range and enters the pipeline.
            HeroView hero{};
            bool found = hero_pool_get(&world->heroes, target, &hero);
            ENSURE(found);
            *hero.pending_kind = SIM_HERO_PENDING_ATTACK;
            *hero.pending_slot = 0u;
            *hero.pending_target =
                world->unit_entities[static_cast<uint32_t>(mm::fix_to_int(command.value_x))];
            *hero.pending_point_x = 0;
            *hero.pending_point_y = 0;
        } else if (command.kind == SIM_COMMAND_CAST) {
            HeroView hero{};
            bool found = hero_pool_get(&world->heroes, target, &hero);
            ENSURE(found);
            const SimHeroDef* def =
                sim_hero_def_table_get(&world->hero_defs, *hero.def_index);
            ENSURE(def != nullptr);
            *hero.pending_kind = SIM_HERO_PENDING_CAST;
            *hero.pending_slot = static_cast<uint8_t>(command.amount);
            *hero.pending_target = EntityId{HANDLE_NULL};
            *hero.pending_point_x = command.value_x;
            *hero.pending_point_y = command.value_y;
            for (uint16_t action_index = 0u; action_index < def->action_count; ++action_index) {
                if (def->actions[action_index].slot != *hero.pending_slot) continue;
                if (def->actions[action_index].target_mode != SIM_TARGET_ENTITY) continue;
                uint32_t slot = static_cast<uint32_t>(mm::fix_to_int(command.value_x));
                if (slot < SIM_MAX_UNITS) *hero.pending_target = world->unit_entities[slot];
                *hero.pending_point_x = 0;
                *hero.pending_point_y = 0;
            }
        } else {
            DamageEvent event{EntityId{HANDLE_NULL}, target, command.amount};
            bool appended = damage_event_queue_append(&world->damage_events, &event);
            ENSURE(appended);
        }
    }
    return true;
}

bool sys_movement(SimWorld* world) {
    if (!world) return false;
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->transforms.membership, &ordered)) return false;

    // The flagship/default path is ordered even though independent integration
    // is currently commutative. Future interactions cannot silently inherit a
    // dense-order traversal from this system.
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        EntityId entity = ordered.entities[i];
        TransformView transform{};
        VelocityView velocity{};
        bool found_transform = transform_pool_get(&world->transforms, entity, &transform);
        ENSURE(found_transform);
        if (!velocity_pool_get(&world->velocities, entity, &velocity)) continue;
        mm::fix dx = mm::fix_mul(*velocity.velocity_x, SIM_DT_FIXED);
        mm::fix dy = mm::fix_mul(*velocity.velocity_y, SIM_DT_FIXED);
        // M5.2 area_slow scales the integrated delta. An empty StatusPool leaves the
        // integration byte-identical to every milestone before this one.
        StatusView status{};
        if (world->config.status_capacity > 0u &&
            status_pool_get(&world->statuses, entity, &status) &&
            *status.effect_type == SIM_EFFECT_AREA_SLOW) {
            dx = mm::fix_mul(dx, *status.scalar);
            dy = mm::fix_mul(dy, *status.scalar);
        }
        *transform.position_x = fix_add_wrap(*transform.position_x, dx);
        *transform.position_y = fix_add_wrap(*transform.position_y, dy);
    }
    return true;
}

// sys_combat_resolve and sys_effects_resolve live in combat.cpp: they are the sole
// committers of health, and keeping them beside resolve_effect keeps the one-pipeline
// rule checkable by reading a single file.

bool sys_cooldown_tick(SimWorld* world) {
    if (!world) return false;
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->health.membership, &ordered)) return false;
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        HealthView health{};
        bool found = health_pool_get(&world->health, ordered.entities[i], &health);
        ENSURE(found);
        if (*health.damage_cooldown > 0u) --*health.damage_cooldown;
    }

    // M5.3 minion and tower cooldowns tick here for the same reason hero cooldowns
    // do: sys_cooldown_tick is the sole decrementer of every cooldown in the sim.
    if (world->config.minion_capacity > 0u) {
        ComponentPoolOrderedView minions{};
        if (!component_pool_ordered_view(&world->minions.membership, &minions)) return false;
        for (uint32_t i = 0u; i < minions.count; ++i) {
            MinionView minion{};
            bool found = minion_pool_get(&world->minions, minions.entities[i], &minion);
            ENSURE(found);
            if (*minion.attack_cooldown > 0u) --*minion.attack_cooldown;
        }
    }
    if (world->config.objective_capacity > 0u) {
        ComponentPoolOrderedView objectives{};
        if (!component_pool_ordered_view(&world->objectives.membership, &objectives)) return false;
        for (uint32_t i = 0u; i < objectives.count; ++i) {
            ObjectiveView objective{};
            bool found = objective_pool_get(&world->objectives, objectives.entities[i], &objective);
            ENSURE(found);
            if (*objective.attack_cooldown > 0u) --*objective.attack_cooldown;
        }
    }

    if (world->config.hero_capacity == 0u) return true;
    ComponentPoolOrderedView heroes{};
    if (!component_pool_ordered_view(&world->heroes.membership, &heroes)) return false;
    for (uint32_t i = 0u; i < heroes.count; ++i) {
        HeroView hero{};
        bool found = hero_pool_get(&world->heroes, heroes.entities[i], &hero);
        ENSURE(found);
        if (*hero.basic_attack_cooldown > 0u) --*hero.basic_attack_cooldown;
        for (uint16_t slot = 0u; slot < SIM_MAX_ACTION_SLOTS; ++slot) {
            if (hero.action_cooldown[slot] > 0u) --hero.action_cooldown[slot];
        }
    }
    return true;
}

void sys_rng_advance(SimWorld* world) {
    if (world) (void)mm::pcg32_next(&world->rng);
}

bool sys_flush_destroy(SimWorld* world) {
    if (!world || !world->pending_destroy ||
        world->pending_destroy_count > world->config.max_entities) return false;

    for (uint32_t request = 0u; request < world->pending_destroy_count; ++request) {
        EntityId entity = world->pending_destroy[request];
        if (!entity_manager_is_alive(&world->entities, entity)) return false;
        for (uint32_t previous = 0u; previous < request; ++previous) {
            if (world->pending_destroy[previous].h == entity.h) return false;
        }
    }

    // Removal and release order is authoritative: it defines typed swap-removes
    // and the entity manager's deterministic LIFO free stack.
    for (uint32_t request = 0u; request < world->pending_destroy_count; ++request) {
        EntityId entity = world->pending_destroy[request];
        if (status_pool_has(&world->statuses, entity)) {
            bool removed = status_pool_remove(&world->statuses, entity);
            ENSURE(removed);
        }
        if (projectile_pool_has(&world->projectiles, entity)) {
            bool removed = projectile_pool_remove(&world->projectiles, entity);
            ENSURE(removed);
        }
        if (hero_pool_has(&world->heroes, entity)) {
            bool removed = hero_pool_remove(&world->heroes, entity);
            ENSURE(removed);
        }
        // M5.3. A destroyed objective is never routed here (it stays a hashed
        // DESTROYED row), but the removal is written for completeness so a future
        // caller that does destroy one cannot leave a dangling row behind.
        if (minion_pool_has(&world->minions, entity)) {
            bool removed = minion_pool_remove(&world->minions, entity);
            ENSURE(removed);
        }
        if (objective_pool_has(&world->objectives, entity)) {
            bool removed = objective_pool_remove(&world->objectives, entity);
            ENSURE(removed);
        }
        if (team_pool_has(&world->teams, entity)) {
            bool removed = team_pool_remove(&world->teams, entity);
            ENSURE(removed);
        }
        if (health_pool_has(&world->health, entity)) {
            bool removed = health_pool_remove(&world->health, entity);
            ENSURE(removed);
        }
        if (velocity_pool_has(&world->velocities, entity)) {
            bool removed = velocity_pool_remove(&world->velocities, entity);
            ENSURE(removed);
        }
        if (transform_pool_has(&world->transforms, entity)) {
            bool removed = transform_pool_remove(&world->transforms, entity);
            ENSURE(removed);
        }
        for (uint32_t unit = 0u; unit < SIM_MAX_UNITS; ++unit) {
            if (world->unit_entities[unit].h == entity.h)
                world->unit_entities[unit] = EntityId{HANDLE_NULL};
        }
        bool released = entity_manager_release(&world->entities, entity);
        ENSURE(released);
        world->pending_destroy[request] = EntityId{HANDLE_NULL};
    }
    world->pending_destroy_count = 0u;
    return true;
}
