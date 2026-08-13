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
        *transform.position_x = fix_add_wrap(*transform.position_x, dx);
        *transform.position_y = fix_add_wrap(*transform.position_y, dy);
    }
    return true;
}

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

    for (uint32_t i = 0u; i < events.count; ++i) {
        const DamageEvent& event = events.events[i];
        HealthView health{};
        bool found = health_pool_get(&world->health, event.target, &health);
        ENSURE(found);
        if (event.amount >= *health.current) *health.current = 0;
        else *health.current -= event.amount;
        if (event.amount > 0) *health.damage_cooldown = 3u;
    }
    return true;
}

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
