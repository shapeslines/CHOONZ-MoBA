#include "sim/sim_hash.h"

#include <cstring>

static const uint64_t FNV1A64_OFFSET = 14695981039346656037ULL;
static const uint64_t FNV1A64_PRIME = 1099511628211ULL;

static uint64_t hash_u8(uint64_t hash, uint8_t value) {
    return (hash ^ value) * FNV1A64_PRIME;
}

static uint64_t hash_u16_le(uint64_t hash, uint16_t value) {
    hash = hash_u8(hash, static_cast<uint8_t>(value));
    return hash_u8(hash, static_cast<uint8_t>(value >> 8u));
}

static uint64_t hash_u32_le(uint64_t hash, uint32_t value) {
    for (uint32_t i = 0u; i < 4u; ++i)
        hash = hash_u8(hash, static_cast<uint8_t>(value >> (i * 8u)));
    return hash;
}

static uint64_t hash_u64_le(uint64_t hash, uint64_t value) {
    for (uint32_t i = 0u; i < 8u; ++i)
        hash = hash_u8(hash, static_cast<uint8_t>(value >> (i * 8u)));
    return hash;
}

static uint32_t i32_bits(int32_t value) {
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool config_valid(SimWorldConfig config) {
    return config.max_entities > 0u && config.max_entities <= HANDLE_INDEX_MASK + 1u &&
           config.initial_unit_count <= SIM_MAX_UNITS &&
           config.initial_unit_count <= config.max_entities &&
           config.damage_event_capacity > 0u;
}

static bool entity_manager_valid(const EntityManager* manager, uint32_t capacity) {
    if (!manager || !manager->generations || !manager->liveness || !manager->free_stack ||
        manager->capacity != capacity || manager->next_fresh > capacity ||
        manager->live_count > manager->next_fresh || manager->free_count > manager->next_fresh ||
        manager->live_count + manager->free_count != manager->next_fresh) return false;

    uint32_t observed_live = 0u;
    for (uint32_t index = 0u; index < manager->next_fresh; ++index) {
        uint16_t generation = manager->generations[index];
        uint8_t live = manager->liveness[index];
        if (generation == 0u || generation > HANDLE_GEN_MASK || live > 1u) return false;
        observed_live += live;
    }
    if (observed_live != manager->live_count) return false;
    for (uint32_t ordinal = 0u; ordinal < manager->free_count; ++ordinal) {
        uint32_t index = manager->free_stack[ordinal];
        if (index >= manager->next_fresh || manager->liveness[index] != 0u) return false;
    }
    return true;
}

static bool membership_valid_sized(const ComponentPool* pool, const EntityManager* manager,
                                   uint32_t entity_capacity, uint32_t capacity) {
    if (!pool || !pool->sparse || !pool->dense_entities ||
        pool->entity_capacity != entity_capacity || pool->capacity != capacity ||
        pool->count > capacity) return false;

    for (uint32_t dense = 0u; dense < pool->count; ++dense) {
        EntityId entity = pool->dense_entities[dense];
        uint32_t index = handle_index(entity.h);
        if (!entity_manager_is_alive(manager, entity) || index >= manager->next_fresh ||
            pool->sparse[index] != dense + 1u) return false;
    }

    uint32_t observed = 0u;
    for (uint32_t index = 0u; index < manager->next_fresh; ++index) {
        uint32_t encoded = pool->sparse[index];
        if (encoded == 0u) continue;
        uint32_t dense = encoded - 1u;
        if (dense >= pool->count || handle_index(pool->dense_entities[dense].h) != index ||
            !entity_manager_is_alive(manager, pool->dense_entities[dense])) return false;
        ++observed;
    }
    return observed == pool->count;
}

static bool membership_valid(const ComponentPool* pool, const EntityManager* manager,
                             uint32_t capacity) {
    return membership_valid_sized(pool, manager, capacity, capacity);
}

// A zero-capacity M5.2 pool is the whole feature being absent: no storage, no rows.
static bool optional_membership_valid(const ComponentPool* pool, const EntityManager* manager,
                                      uint32_t entity_capacity, uint32_t capacity) {
    if (capacity == 0u) {
        return pool && !pool->sparse && !pool->dense_entities && pool->capacity == 0u &&
               pool->entity_capacity == 0u && pool->count == 0u;
    }
    return membership_valid_sized(pool, manager, entity_capacity, capacity);
}

static bool sim_event_queue_valid_for_world(const SimWorld* world) {
    if (world->config.sim_event_capacity == 0u) {
        return !world->sim_events.buffers[0] && !world->sim_events.buffers[1] &&
               world->sim_events.capacity == 0u;
    }
    return sim_event_queue_is_valid(&world->sim_events) &&
           world->sim_events.capacity == world->config.sim_event_capacity;
}

static bool damage_event_valid_for_world(const SimWorld* world, const DamageEvent& event) {
    return world && damage_event_is_canonical(&event) &&
           entity_manager_is_alive(&world->entities, event.target) &&
           health_pool_has(&world->health, event.target) &&
           (event.source.h == HANDLE_NULL ||
            entity_manager_is_alive(&world->entities, event.source));
}

static bool damage_queue_valid_for_world(const SimWorld* world) {
    if (!world || !damage_event_queue_is_valid(&world->damage_events) ||
        world->damage_events.capacity != world->config.damage_event_capacity) return false;
    const uint32_t phases[] = {
        world->damage_events.read_index, world->damage_events.write_index};
    for (uint32_t phase = 0u; phase < 2u; ++phase) {
        uint32_t buffer = phases[phase];
        for (uint32_t ordinal = 0u;
             ordinal < world->damage_events.counts[buffer]; ++ordinal) {
            if (!damage_event_valid_for_world(
                    world, world->damage_events.buffers[buffer][ordinal])) return false;
        }
    }
    return true;
}

static bool canonical_world_valid(const SimWorld* world) {
    if (!world || !config_valid(world->config) ||
        !entity_manager_valid(&world->entities, world->config.max_entities) ||
        !membership_valid(&world->transforms.membership, &world->entities,
                          world->config.max_entities) ||
        !membership_valid(&world->velocities.membership, &world->entities,
                          world->config.max_entities) ||
        !membership_valid(&world->health.membership, &world->entities,
                          world->config.max_entities) ||
        !world->transforms.position_x || !world->transforms.position_y || !world->transforms.facing ||
        !world->velocities.velocity_x || !world->velocities.velocity_y ||
        !world->health.current || !world->health.maximum || !world->health.damage_cooldown ||
        !damage_queue_valid_for_world(world) || !map_valid(&world->map) ||
        !world->pending_destroy || world->pending_destroy_count > world->config.max_entities ||
        !sim_hero_def_table_valid(&world->hero_defs) ||
        world->hero_defs.capacity != world->config.hero_def_capacity ||
        !optional_membership_valid(&world->heroes.membership, &world->entities,
                                   world->config.max_entities, world->config.hero_capacity) ||
        !optional_membership_valid(&world->projectiles.membership, &world->entities,
                                   world->config.max_entities,
                                   world->config.projectile_capacity) ||
        !optional_membership_valid(&world->statuses.membership, &world->entities,
                                   world->config.max_entities, world->config.status_capacity) ||
        !sim_event_queue_valid_for_world(world))
        return false;

    // Every hero row must name an installed def, and every projectile must name a
    // live source and an installed def slot.
    for (uint32_t dense = 0u; dense < world->heroes.membership.count; ++dense) {
        uint16_t def_index = 0u;
        if (!hero_pool_def_index(&world->heroes, world->heroes.membership.dense_entities[dense],
                                 &def_index) ||
            !sim_hero_def_table_get(&world->hero_defs, def_index)) return false;
    }

    for (uint32_t unit = 0u; unit < SIM_MAX_UNITS; ++unit) {
        EntityId entity = world->unit_entities[unit];
        if (entity.h == HANDLE_NULL) continue;
        if (unit >= world->config.initial_unit_count ||
            !entity_manager_is_alive(&world->entities, entity) ||
            !transform_pool_has(&world->transforms, entity) ||
            !velocity_pool_has(&world->velocities, entity) ||
            !health_pool_has(&world->health, entity)) return false;
    }
    for (uint32_t ordinal = 0u; ordinal < world->pending_destroy_count; ++ordinal) {
        EntityId entity = world->pending_destroy[ordinal];
        if (!entity_manager_is_alive(&world->entities, entity)) return false;
        for (uint32_t previous = 0u; previous < ordinal; ++previous) {
            if (world->pending_destroy[previous].h == entity.h) return false;
        }
    }
    return true;
}

static EntityId entity_at(const EntityManager* manager, uint32_t index) {
    return EntityId{handle_make(index, manager->generations[index])};
}

static uint64_t hash_transform(uint64_t hash, const SimWorld* world) {
    hash = hash_u32_le(hash, world->transforms.membership.count);
    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index) {
        EntityId entity = entity_at(&world->entities, index);
        bool present = transform_pool_has(&world->transforms, entity);
        hash = hash_u8(hash, present ? 1u : 0u);
        if (!present) continue;
        uint32_t dense = component_pool_dense_index(&world->transforms.membership, entity);
        hash = hash_u32_le(hash, entity.h);
        hash = hash_u32_le(hash, i32_bits(world->transforms.position_x[dense]));
        hash = hash_u32_le(hash, i32_bits(world->transforms.position_y[dense]));
        hash = hash_u32_le(hash, i32_bits(world->transforms.facing[dense]));
    }
    return hash;
}

static uint64_t hash_velocity(uint64_t hash, const SimWorld* world) {
    hash = hash_u32_le(hash, world->velocities.membership.count);
    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index) {
        EntityId entity = entity_at(&world->entities, index);
        bool present = velocity_pool_has(&world->velocities, entity);
        hash = hash_u8(hash, present ? 1u : 0u);
        if (!present) continue;
        uint32_t dense = component_pool_dense_index(&world->velocities.membership, entity);
        hash = hash_u32_le(hash, entity.h);
        hash = hash_u32_le(hash, i32_bits(world->velocities.velocity_x[dense]));
        hash = hash_u32_le(hash, i32_bits(world->velocities.velocity_y[dense]));
    }
    return hash;
}

static uint64_t hash_health(uint64_t hash, const SimWorld* world) {
    hash = hash_u32_le(hash, world->health.membership.count);
    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index) {
        EntityId entity = entity_at(&world->entities, index);
        bool present = health_pool_has(&world->health, entity);
        hash = hash_u8(hash, present ? 1u : 0u);
        if (!present) continue;
        uint32_t dense = component_pool_dense_index(&world->health.membership, entity);
        hash = hash_u32_le(hash, entity.h);
        hash = hash_u32_le(hash, i32_bits(world->health.current[dense]));
        hash = hash_u32_le(hash, i32_bits(world->health.maximum[dense]));
        hash = hash_u32_le(hash, world->health.damage_cooldown[dense]);
    }
    return hash;
}

static uint64_t hash_damage_event(uint64_t hash, const DamageEvent& event) {
    hash = hash_u32_le(hash, event.source.h);
    hash = hash_u32_le(hash, event.target.h);
    return hash_u32_le(hash, i32_bits(event.amount));
}

static uint64_t hash_damage_events(uint64_t hash, const SimWorld* world) {
    uint32_t read = world->damage_events.read_index;
    hash = hash_u32_le(hash, world->damage_events.counts[read]);
    for (uint32_t ordinal = 0u; ordinal < world->damage_events.counts[read]; ++ordinal)
        hash = hash_damage_event(hash, world->damage_events.buffers[read][ordinal]);

    uint32_t write = world->damage_events.write_index;
    hash = hash_u32_le(hash, world->damage_events.counts[write]);
    for (uint32_t ordinal = 0u; ordinal < world->damage_events.counts[write]; ++ordinal)
        hash = hash_damage_event(hash, world->damage_events.buffers[write][ordinal]);
    return hash;
}

static uint64_t hash_map(uint64_t hash, const SimWorld* world) {
    const MapGrid* map = &world->map;
    hash = hash_u64_le(hash, map->map_id);
    hash = hash_u16_le(hash, map->width_cells);
    hash = hash_u16_le(hash, map->height_cells);
    hash = hash_u32_le(hash, i32_bits(map->cell_size_q16));
    for (uint32_t cell = 0u; cell < map->cell_count; ++cell) {
        hash = hash_u8(hash, map->flags[cell]);
        hash = hash_u8(hash, map->movement_cost[cell]);
        hash = hash_u16_le(hash, static_cast<uint16_t>(map->height_cell[cell]));
    }
    hash = hash_u16_le(hash, map->lane_count);
    for (uint32_t lane = 0u; lane < map->lane_count; ++lane) {
        const MapLane& def = map->lanes[lane];
        const uint16_t* waypoints = map_lane_waypoints(map, static_cast<uint16_t>(lane));
        hash = hash_u8(hash, def.lane_id);
        hash = hash_u8(hash, def.waypoint_count);
        hash = hash_u16_le(hash, def.wave_interval_ticks);
        hash = hash_u32_le(hash, def.first_wave_tick);
        hash = hash_u64_le(hash, def.creep_archetype_id);
        for (uint32_t i = 0u; i < def.waypoint_count; ++i)
            hash = hash_u16_le(hash, waypoints[i]);
    }
    return hash;
}

// ------------------------------------------------------------------ M5.2 hero block

static uint64_t hash_hero_def(uint64_t hash, const SimHeroDef* def) {
    hash = hash_u64_le(hash, def->hero_def_id);
    hash = hash_u32_le(hash, i32_bits(def->max_health));
    hash = hash_u32_le(hash, i32_bits(def->move_speed));
    hash = hash_u32_le(hash, i32_bits(def->attack_range));
    hash = hash_u16_le(hash, def->action_count);
    for (uint16_t a = 0u; a < def->action_count; ++a) {
        const SimActionDef& action = def->actions[a];
        hash = hash_u64_le(hash, action.action_id);
        hash = hash_u8(hash, action.slot);
        hash = hash_u8(hash, action.target_mode);
        hash = hash_u16_le(hash, action.effect_count);
        hash = hash_u32_le(hash, action.cooldown_ticks);
        hash = hash_u32_le(hash, action.cast_time_ticks);
        hash = hash_u32_le(hash, action.resource_cost);
        hash = hash_u32_le(hash, i32_bits(action.range));
        hash = hash_u32_le(hash, i32_bits(action.projectile_speed));
        for (uint16_t e = 0u; e < action.effect_count; ++e) {
            const SimEffectDef& effect = action.effects[e];
            hash = hash_u8(hash, effect.effect_type);
            hash = hash_u8(hash, effect.damage_type);
            hash = hash_u16_le(hash, effect.duration_ticks);
            hash = hash_u32_le(hash, i32_bits(effect.magnitude));
            hash = hash_u32_le(hash, i32_bits(effect.radius));
            hash = hash_u32_le(hash, i32_bits(effect.scalar));
        }
    }
    return hash;
}

static uint64_t hash_hero_defs(uint64_t hash, const SimWorld* world) {
    hash = hash_u16_le(hash, world->hero_defs.count);
    for (uint16_t i = 0u; i < world->hero_defs.count; ++i)
        hash = hash_hero_def(hash, &world->hero_defs.defs[i]);
    return hash;
}

static uint64_t hash_heroes(uint64_t hash, const SimWorld* world) {
    hash = hash_u32_le(hash, world->heroes.membership.count);
    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index) {
        EntityId entity = entity_at(&world->entities, index);
        bool present = hero_pool_has(&world->heroes, entity);
        hash = hash_u8(hash, present ? 1u : 0u);
        if (!present) continue;
        uint32_t dense = component_pool_dense_index(&world->heroes.membership, entity);
        hash = hash_u32_le(hash, entity.h);
        hash = hash_u16_le(hash, world->heroes.def_index[dense]);
        hash = hash_u32_le(hash, i32_bits(world->heroes.resource[dense]));
        hash = hash_u32_le(hash, world->heroes.basic_attack_cooldown[dense]);
        for (uint32_t slot = 0u; slot < SIM_MAX_ACTION_SLOTS; ++slot) {
            hash = hash_u32_le(
                hash, world->heroes.action_cooldown[dense * SIM_MAX_ACTION_SLOTS + slot]);
        }
        hash = hash_u8(hash, world->heroes.pending_kind[dense]);
        hash = hash_u8(hash, world->heroes.pending_slot[dense]);
        hash = hash_u32_le(hash, world->heroes.pending_target[dense].h);
        hash = hash_u32_le(hash, i32_bits(world->heroes.pending_point_x[dense]));
        hash = hash_u32_le(hash, i32_bits(world->heroes.pending_point_y[dense]));
    }
    return hash;
}

static uint64_t hash_projectiles(uint64_t hash, const SimWorld* world) {
    hash = hash_u32_le(hash, world->projectiles.membership.count);
    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index) {
        EntityId entity = entity_at(&world->entities, index);
        bool present = projectile_pool_has(&world->projectiles, entity);
        hash = hash_u8(hash, present ? 1u : 0u);
        if (!present) continue;
        uint32_t dense = component_pool_dense_index(&world->projectiles.membership, entity);
        hash = hash_u32_le(hash, entity.h);
        hash = hash_u32_le(hash, world->projectiles.source[dense].h);
        hash = hash_u32_le(hash, world->projectiles.target[dense].h);
        hash = hash_u16_le(hash, world->projectiles.def_index[dense]);
        hash = hash_u8(hash, world->projectiles.action_slot[dense]);
        hash = hash_u8(hash, world->projectiles.effect_index[dense]);
        hash = hash_u32_le(hash, i32_bits(world->projectiles.position_x[dense]));
        hash = hash_u32_le(hash, i32_bits(world->projectiles.position_y[dense]));
        hash = hash_u32_le(hash, i32_bits(world->projectiles.speed[dense]));
        hash = hash_u16_le(hash, world->projectiles.remaining_ticks[dense]);
    }
    return hash;
}

static uint64_t hash_statuses(uint64_t hash, const SimWorld* world) {
    hash = hash_u32_le(hash, world->statuses.membership.count);
    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index) {
        EntityId entity = entity_at(&world->entities, index);
        bool present = status_pool_has(&world->statuses, entity);
        hash = hash_u8(hash, present ? 1u : 0u);
        if (!present) continue;
        uint32_t dense = component_pool_dense_index(&world->statuses.membership, entity);
        hash = hash_u32_le(hash, entity.h);
        hash = hash_u8(hash, world->statuses.effect_type[dense]);
        hash = hash_u8(hash, world->statuses.stack_count[dense]);
        hash = hash_u16_le(hash, world->statuses.remaining_ticks[dense]);
        hash = hash_u32_le(hash, i32_bits(world->statuses.magnitude[dense]));
        hash = hash_u32_le(hash, i32_bits(world->statuses.scalar[dense]));
    }
    return hash;
}

// Exactly payload_size bytes: the tail padding is storage, not state.
static uint64_t hash_sim_event(uint64_t hash, const SimEvent& event) {
    hash = hash_u64_le(hash, event.tick);
    hash = hash_u16_le(hash, event.event_kind);
    hash = hash_u16_le(hash, event.payload_size);
    hash = hash_u32_le(hash, event.append_ordinal);
    for (uint16_t i = 0u; i < event.payload_size; ++i) hash = hash_u8(hash, event.payload[i]);
    return hash;
}

static uint64_t hash_sim_events(uint64_t hash, const SimWorld* world) {
    if (world->config.sim_event_capacity == 0u) {
        hash = hash_u32_le(hash, 0u);
        return hash_u32_le(hash, 0u);
    }
    const uint32_t phases[] = {world->sim_events.read_index, world->sim_events.write_index};
    for (uint32_t phase = 0u; phase < 2u; ++phase) {
        uint32_t buffer = phases[phase];
        hash = hash_u32_le(hash, world->sim_events.counts[buffer]);
        for (uint32_t ordinal = 0u; ordinal < world->sim_events.counts[buffer]; ++ordinal)
            hash = hash_sim_event(hash, world->sim_events.buffers[buffer][ordinal]);
    }
    return hash;
}

uint64_t sim_hash_state(const SimWorld* world) {
    if (!canonical_world_valid(world)) return 0u;

    uint64_t hash = FNV1A64_OFFSET;
    hash = hash_u64_le(hash, world->tick);
    hash = hash_u32_le(hash, world->config.max_entities);
    hash = hash_u32_le(hash, world->config.initial_unit_count);
    hash = hash_u32_le(hash, world->config.damage_event_capacity);
    hash = hash_u64_le(hash, world->rng.state);
    hash = hash_u64_le(hash, world->rng.inc);
    hash = hash_u32_le(hash, world->entities.capacity);
    hash = hash_u32_le(hash, world->entities.live_count);
    hash = hash_u32_le(hash, world->entities.next_fresh);
    hash = hash_u32_le(hash, world->entities.free_count);

    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index)
        hash = hash_u16_le(hash, world->entities.generations[index]);
    for (uint32_t index = 0u; index < world->entities.next_fresh; ++index)
        hash = hash_u8(hash, world->entities.liveness[index]);
    for (uint32_t ordinal = 0u; ordinal < world->entities.free_count; ++ordinal)
        hash = hash_u32_le(hash, world->entities.free_stack[ordinal]);
    for (uint32_t unit = 0u; unit < SIM_MAX_UNITS; ++unit)
        hash = hash_u32_le(hash, world->unit_entities[unit].h);

    hash = hash_u32_le(hash, world->pending_destroy_count);
    for (uint32_t ordinal = 0u; ordinal < world->pending_destroy_count; ++ordinal)
        hash = hash_u32_le(hash, world->pending_destroy[ordinal].h);

    hash = hash_damage_events(hash, world);

    hash = hash_transform(hash, world);
    hash = hash_velocity(hash, world);
    hash = hash_health(hash, world);
    hash = hash_map(hash, world);

    hash = hash_hero_defs(hash, world);
    hash = hash_heroes(hash, world);
    hash = hash_projectiles(hash, world);
    hash = hash_statuses(hash, world);
    return hash_sim_events(hash, world);
}

static void set_diff(SimStateDiff* diff, SimStateField field, uint32_t index,
                     uint64_t expected, uint64_t actual) {
    diff->field = field;
    diff->index = index;
    diff->expected_value = expected;
    diff->actual_value = actual;
}

#define DIFF_SCALAR(member_expected, member_actual, field_name) do {                    \
    if ((member_expected) != (member_actual)) {                                         \
        set_diff(out_diff, field_name, SIM_STATE_DIFF_NO_INDEX,                         \
                 static_cast<uint64_t>(member_expected),                                \
                 static_cast<uint64_t>(member_actual));                                 \
        return true;                                                                     \
    }                                                                                    \
} while (0)

static bool diff_damage_event_record(const DamageEvent& expected, const DamageEvent& actual,
                                     uint32_t ordinal, SimStateField source_field,
                                     SimStateField target_field, SimStateField amount_field,
                                     SimStateDiff* out_diff) {
    if (expected.source.h != actual.source.h) {
        set_diff(out_diff, source_field, ordinal, expected.source.h, actual.source.h);
        return true;
    }
    if (expected.target.h != actual.target.h) {
        set_diff(out_diff, target_field, ordinal, expected.target.h, actual.target.h);
        return true;
    }
    if (expected.amount != actual.amount) {
        set_diff(out_diff, amount_field, ordinal,
                 i32_bits(expected.amount), i32_bits(actual.amount));
        return true;
    }
    return false;
}

static bool diff_damage_events(const SimWorld* expected, const SimWorld* actual,
                               SimStateDiff* out_diff) {
    uint32_t expected_read = expected->damage_events.read_index;
    uint32_t actual_read = actual->damage_events.read_index;
    DIFF_SCALAR(expected->damage_events.counts[expected_read],
                actual->damage_events.counts[actual_read],
                SIM_STATE_FIELD_DAMAGE_EVENT_READ_COUNT);
    for (uint32_t ordinal = 0u;
         ordinal < expected->damage_events.counts[expected_read]; ++ordinal) {
        if (diff_damage_event_record(
                expected->damage_events.buffers[expected_read][ordinal],
                actual->damage_events.buffers[actual_read][ordinal], ordinal,
                SIM_STATE_FIELD_DAMAGE_EVENT_READ_SOURCE,
                SIM_STATE_FIELD_DAMAGE_EVENT_READ_TARGET,
                SIM_STATE_FIELD_DAMAGE_EVENT_READ_AMOUNT, out_diff)) return true;
    }

    uint32_t expected_write = expected->damage_events.write_index;
    uint32_t actual_write = actual->damage_events.write_index;
    DIFF_SCALAR(expected->damage_events.counts[expected_write],
                actual->damage_events.counts[actual_write],
                SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_COUNT);
    for (uint32_t ordinal = 0u;
         ordinal < expected->damage_events.counts[expected_write]; ++ordinal) {
        if (diff_damage_event_record(
                expected->damage_events.buffers[expected_write][ordinal],
                actual->damage_events.buffers[actual_write][ordinal], ordinal,
                SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_SOURCE,
                SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_TARGET,
                SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_AMOUNT, out_diff)) return true;
    }
    return false;
}

static bool diff_transform(const SimWorld* expected, const SimWorld* actual,
                           SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->transforms.membership.count, actual->transforms.membership.count,
                SIM_STATE_FIELD_TRANSFORM_COUNT);
    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        EntityId expected_entity = entity_at(&expected->entities, index);
        EntityId actual_entity = entity_at(&actual->entities, index);
        bool expected_present = transform_pool_has(&expected->transforms, expected_entity);
        bool actual_present = transform_pool_has(&actual->transforms, actual_entity);
        if (expected_present != actual_present) {
            set_diff(out_diff, SIM_STATE_FIELD_TRANSFORM_ENTITY, index,
                     expected_present ? expected_entity.h : 0u,
                     actual_present ? actual_entity.h : 0u);
            return true;
        }
        if (!expected_present) continue;
        if (expected_entity.h != actual_entity.h) {
            set_diff(out_diff, SIM_STATE_FIELD_TRANSFORM_ENTITY, index,
                     expected_entity.h, actual_entity.h);
            return true;
        }
        uint32_t ed = component_pool_dense_index(&expected->transforms.membership, expected_entity);
        uint32_t ad = component_pool_dense_index(&actual->transforms.membership, actual_entity);
        if (expected->transforms.position_x[ed] != actual->transforms.position_x[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_POSITION_X, index,
                     i32_bits(expected->transforms.position_x[ed]),
                     i32_bits(actual->transforms.position_x[ad]));
            return true;
        }
        if (expected->transforms.position_y[ed] != actual->transforms.position_y[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_POSITION_Y, index,
                     i32_bits(expected->transforms.position_y[ed]),
                     i32_bits(actual->transforms.position_y[ad]));
            return true;
        }
        if (expected->transforms.facing[ed] != actual->transforms.facing[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_FACING, index,
                     i32_bits(expected->transforms.facing[ed]),
                     i32_bits(actual->transforms.facing[ad]));
            return true;
        }
    }
    return false;
}

static bool diff_velocity(const SimWorld* expected, const SimWorld* actual,
                          SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->velocities.membership.count, actual->velocities.membership.count,
                SIM_STATE_FIELD_VELOCITY_COUNT);
    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        EntityId expected_entity = entity_at(&expected->entities, index);
        EntityId actual_entity = entity_at(&actual->entities, index);
        bool expected_present = velocity_pool_has(&expected->velocities, expected_entity);
        bool actual_present = velocity_pool_has(&actual->velocities, actual_entity);
        if (expected_present != actual_present) {
            set_diff(out_diff, SIM_STATE_FIELD_VELOCITY_ENTITY, index,
                     expected_present ? expected_entity.h : 0u,
                     actual_present ? actual_entity.h : 0u);
            return true;
        }
        if (!expected_present) continue;
        if (expected_entity.h != actual_entity.h) {
            set_diff(out_diff, SIM_STATE_FIELD_VELOCITY_ENTITY, index,
                     expected_entity.h, actual_entity.h);
            return true;
        }
        uint32_t ed = component_pool_dense_index(&expected->velocities.membership, expected_entity);
        uint32_t ad = component_pool_dense_index(&actual->velocities.membership, actual_entity);
        if (expected->velocities.velocity_x[ed] != actual->velocities.velocity_x[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_VELOCITY_X, index,
                     i32_bits(expected->velocities.velocity_x[ed]),
                     i32_bits(actual->velocities.velocity_x[ad]));
            return true;
        }
        if (expected->velocities.velocity_y[ed] != actual->velocities.velocity_y[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_VELOCITY_Y, index,
                     i32_bits(expected->velocities.velocity_y[ed]),
                     i32_bits(actual->velocities.velocity_y[ad]));
            return true;
        }
    }
    return false;
}

static bool diff_health(const SimWorld* expected, const SimWorld* actual,
                        SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->health.membership.count, actual->health.membership.count,
                SIM_STATE_FIELD_HEALTH_COUNT);
    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        EntityId expected_entity = entity_at(&expected->entities, index);
        EntityId actual_entity = entity_at(&actual->entities, index);
        bool expected_present = health_pool_has(&expected->health, expected_entity);
        bool actual_present = health_pool_has(&actual->health, actual_entity);
        if (expected_present != actual_present) {
            set_diff(out_diff, SIM_STATE_FIELD_HEALTH_ENTITY, index,
                     expected_present ? expected_entity.h : 0u,
                     actual_present ? actual_entity.h : 0u);
            return true;
        }
        if (!expected_present) continue;
        if (expected_entity.h != actual_entity.h) {
            set_diff(out_diff, SIM_STATE_FIELD_HEALTH_ENTITY, index,
                     expected_entity.h, actual_entity.h);
            return true;
        }
        uint32_t ed = component_pool_dense_index(&expected->health.membership, expected_entity);
        uint32_t ad = component_pool_dense_index(&actual->health.membership, actual_entity);
        if (expected->health.current[ed] != actual->health.current[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_HEALTH_CURRENT, index,
                     i32_bits(expected->health.current[ed]), i32_bits(actual->health.current[ad]));
            return true;
        }
        if (expected->health.maximum[ed] != actual->health.maximum[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_HEALTH_MAXIMUM, index,
                     i32_bits(expected->health.maximum[ed]), i32_bits(actual->health.maximum[ad]));
            return true;
        }
        if (expected->health.damage_cooldown[ed] != actual->health.damage_cooldown[ad]) {
            set_diff(out_diff, SIM_STATE_FIELD_DAMAGE_COOLDOWN, index,
                     expected->health.damage_cooldown[ed], actual->health.damage_cooldown[ad]);
            return true;
        }
    }
    return false;
}

static bool diff_map(const SimWorld* expected, const SimWorld* actual, SimStateDiff* out_diff) {
    const MapGrid* e = &expected->map;
    const MapGrid* a = &actual->map;
    DIFF_SCALAR(e->map_id, a->map_id, SIM_STATE_FIELD_MAP_ID);
    DIFF_SCALAR(e->width_cells, a->width_cells, SIM_STATE_FIELD_MAP_WIDTH);
    DIFF_SCALAR(e->height_cells, a->height_cells, SIM_STATE_FIELD_MAP_HEIGHT);
    if (e->cell_size_q16 != a->cell_size_q16) {
        set_diff(out_diff, SIM_STATE_FIELD_MAP_CELL_SIZE, SIM_STATE_DIFF_NO_INDEX,
                 i32_bits(e->cell_size_q16), i32_bits(a->cell_size_q16));
        return true;
    }
    for (uint32_t cell = 0u; cell < e->cell_count; ++cell) {
        if (e->flags[cell] != a->flags[cell]) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_CELL_FLAGS, cell, e->flags[cell], a->flags[cell]);
            return true;
        }
        if (e->movement_cost[cell] != a->movement_cost[cell]) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_CELL_COST, cell,
                     e->movement_cost[cell], a->movement_cost[cell]);
            return true;
        }
        if (e->height_cell[cell] != a->height_cell[cell]) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_CELL_HEIGHT, cell,
                     static_cast<uint16_t>(e->height_cell[cell]),
                     static_cast<uint16_t>(a->height_cell[cell]));
            return true;
        }
    }
    DIFF_SCALAR(e->lane_count, a->lane_count, SIM_STATE_FIELD_MAP_LANE_COUNT);
    for (uint32_t lane = 0u; lane < e->lane_count; ++lane) {
        const MapLane& el = e->lanes[lane];
        const MapLane& al = a->lanes[lane];
        if (el.lane_id != al.lane_id) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_LANE_ID, lane, el.lane_id, al.lane_id);
            return true;
        }
        if (el.waypoint_count != al.waypoint_count) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_LANE_WAYPOINT_COUNT, lane,
                     el.waypoint_count, al.waypoint_count);
            return true;
        }
        if (el.wave_interval_ticks != al.wave_interval_ticks) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_LANE_WAVE_INTERVAL, lane,
                     el.wave_interval_ticks, al.wave_interval_ticks);
            return true;
        }
        if (el.first_wave_tick != al.first_wave_tick) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_LANE_FIRST_WAVE, lane,
                     el.first_wave_tick, al.first_wave_tick);
            return true;
        }
        if (el.creep_archetype_id != al.creep_archetype_id) {
            set_diff(out_diff, SIM_STATE_FIELD_MAP_LANE_ARCHETYPE, lane,
                     el.creep_archetype_id, al.creep_archetype_id);
            return true;
        }
        const uint16_t* ew = map_lane_waypoints(e, static_cast<uint16_t>(lane));
        const uint16_t* aw = map_lane_waypoints(a, static_cast<uint16_t>(lane));
        for (uint32_t i = 0u; i < el.waypoint_count; ++i) {
            if (ew[i] != aw[i]) {
                set_diff(out_diff, SIM_STATE_FIELD_MAP_LANE_WAYPOINT,
                         lane * SIM_MAP_MAX_WAYPOINTS + i, ew[i], aw[i]);
                return true;
            }
        }
    }
    return false;
}

// The hero block walks the identical order sim_hash_state does. The def table is
// installed whole and immutable while ticking, so it diffs per record.
static bool diff_hero_defs(const SimWorld* expected, const SimWorld* actual,
                           SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->hero_defs.count, actual->hero_defs.count,
                SIM_STATE_FIELD_HERO_DEF_COUNT);
    for (uint16_t i = 0u; i < expected->hero_defs.count; ++i) {
        uint64_t e = hash_hero_def(FNV1A64_OFFSET, &expected->hero_defs.defs[i]);
        uint64_t a = hash_hero_def(FNV1A64_OFFSET, &actual->hero_defs.defs[i]);
        if (e != a) {
            set_diff(out_diff, SIM_STATE_FIELD_HERO_DEF_RECORD, i, e, a);
            return true;
        }
    }
    return false;
}

#define DIFF_POOL_FIELD(expected_value, actual_value, field_name) do {                   \
    if ((expected_value) != (actual_value)) {                                            \
        set_diff(out_diff, field_name, index, static_cast<uint64_t>(expected_value),      \
                 static_cast<uint64_t>(actual_value));                                    \
        return true;                                                                      \
    }                                                                                     \
} while (0)

static bool diff_heroes(const SimWorld* expected, const SimWorld* actual,
                        SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->heroes.membership.count, actual->heroes.membership.count,
                SIM_STATE_FIELD_HERO_COUNT);
    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        EntityId ee = entity_at(&expected->entities, index);
        EntityId ae = entity_at(&actual->entities, index);
        bool ep = hero_pool_has(&expected->heroes, ee);
        bool ap = hero_pool_has(&actual->heroes, ae);
        if (ep != ap || (ep && ee.h != ae.h)) {
            set_diff(out_diff, SIM_STATE_FIELD_HERO_ENTITY, index, ep ? ee.h : 0u,
                     ap ? ae.h : 0u);
            return true;
        }
        if (!ep) continue;
        uint32_t ed = component_pool_dense_index(&expected->heroes.membership, ee);
        uint32_t ad = component_pool_dense_index(&actual->heroes.membership, ae);
        DIFF_POOL_FIELD(expected->heroes.def_index[ed], actual->heroes.def_index[ad],
                        SIM_STATE_FIELD_HERO_DEF_INDEX);
        DIFF_POOL_FIELD(i32_bits(expected->heroes.resource[ed]),
                        i32_bits(actual->heroes.resource[ad]), SIM_STATE_FIELD_HERO_RESOURCE);
        DIFF_POOL_FIELD(expected->heroes.basic_attack_cooldown[ed],
                        actual->heroes.basic_attack_cooldown[ad],
                        SIM_STATE_FIELD_HERO_BASIC_ATTACK_COOLDOWN);
        for (uint32_t slot = 0u; slot < SIM_MAX_ACTION_SLOTS; ++slot) {
            DIFF_POOL_FIELD(
                expected->heroes.action_cooldown[ed * SIM_MAX_ACTION_SLOTS + slot],
                actual->heroes.action_cooldown[ad * SIM_MAX_ACTION_SLOTS + slot],
                SIM_STATE_FIELD_HERO_ACTION_COOLDOWN);
        }
        DIFF_POOL_FIELD(expected->heroes.pending_kind[ed], actual->heroes.pending_kind[ad],
                        SIM_STATE_FIELD_HERO_PENDING_KIND);
        DIFF_POOL_FIELD(expected->heroes.pending_slot[ed], actual->heroes.pending_slot[ad],
                        SIM_STATE_FIELD_HERO_PENDING_SLOT);
        DIFF_POOL_FIELD(expected->heroes.pending_target[ed].h,
                        actual->heroes.pending_target[ad].h,
                        SIM_STATE_FIELD_HERO_PENDING_TARGET);
        DIFF_POOL_FIELD(i32_bits(expected->heroes.pending_point_x[ed]),
                        i32_bits(actual->heroes.pending_point_x[ad]),
                        SIM_STATE_FIELD_HERO_PENDING_POINT_X);
        DIFF_POOL_FIELD(i32_bits(expected->heroes.pending_point_y[ed]),
                        i32_bits(actual->heroes.pending_point_y[ad]),
                        SIM_STATE_FIELD_HERO_PENDING_POINT_Y);
    }
    return false;
}

static bool diff_projectiles(const SimWorld* expected, const SimWorld* actual,
                             SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->projectiles.membership.count, actual->projectiles.membership.count,
                SIM_STATE_FIELD_PROJECTILE_COUNT);
    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        EntityId ee = entity_at(&expected->entities, index);
        EntityId ae = entity_at(&actual->entities, index);
        bool ep = projectile_pool_has(&expected->projectiles, ee);
        bool ap = projectile_pool_has(&actual->projectiles, ae);
        if (ep != ap || (ep && ee.h != ae.h)) {
            set_diff(out_diff, SIM_STATE_FIELD_PROJECTILE_ENTITY, index, ep ? ee.h : 0u,
                     ap ? ae.h : 0u);
            return true;
        }
        if (!ep) continue;
        uint32_t ed = component_pool_dense_index(&expected->projectiles.membership, ee);
        uint32_t ad = component_pool_dense_index(&actual->projectiles.membership, ae);
        DIFF_POOL_FIELD(expected->projectiles.source[ed].h, actual->projectiles.source[ad].h,
                        SIM_STATE_FIELD_PROJECTILE_SOURCE);
        DIFF_POOL_FIELD(expected->projectiles.target[ed].h, actual->projectiles.target[ad].h,
                        SIM_STATE_FIELD_PROJECTILE_TARGET);
        DIFF_POOL_FIELD(expected->projectiles.def_index[ed], actual->projectiles.def_index[ad],
                        SIM_STATE_FIELD_PROJECTILE_DEF_INDEX);
        DIFF_POOL_FIELD(expected->projectiles.action_slot[ed],
                        actual->projectiles.action_slot[ad],
                        SIM_STATE_FIELD_PROJECTILE_ACTION_SLOT);
        DIFF_POOL_FIELD(expected->projectiles.effect_index[ed],
                        actual->projectiles.effect_index[ad],
                        SIM_STATE_FIELD_PROJECTILE_EFFECT_INDEX);
        DIFF_POOL_FIELD(i32_bits(expected->projectiles.position_x[ed]),
                        i32_bits(actual->projectiles.position_x[ad]),
                        SIM_STATE_FIELD_PROJECTILE_POSITION_X);
        DIFF_POOL_FIELD(i32_bits(expected->projectiles.position_y[ed]),
                        i32_bits(actual->projectiles.position_y[ad]),
                        SIM_STATE_FIELD_PROJECTILE_POSITION_Y);
        DIFF_POOL_FIELD(i32_bits(expected->projectiles.speed[ed]),
                        i32_bits(actual->projectiles.speed[ad]),
                        SIM_STATE_FIELD_PROJECTILE_SPEED);
        DIFF_POOL_FIELD(expected->projectiles.remaining_ticks[ed],
                        actual->projectiles.remaining_ticks[ad],
                        SIM_STATE_FIELD_PROJECTILE_REMAINING_TICKS);
    }
    return false;
}

static bool diff_statuses(const SimWorld* expected, const SimWorld* actual,
                          SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->statuses.membership.count, actual->statuses.membership.count,
                SIM_STATE_FIELD_STATUS_COUNT);
    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        EntityId ee = entity_at(&expected->entities, index);
        EntityId ae = entity_at(&actual->entities, index);
        bool ep = status_pool_has(&expected->statuses, ee);
        bool ap = status_pool_has(&actual->statuses, ae);
        if (ep != ap || (ep && ee.h != ae.h)) {
            set_diff(out_diff, SIM_STATE_FIELD_STATUS_ENTITY, index, ep ? ee.h : 0u,
                     ap ? ae.h : 0u);
            return true;
        }
        if (!ep) continue;
        uint32_t ed = component_pool_dense_index(&expected->statuses.membership, ee);
        uint32_t ad = component_pool_dense_index(&actual->statuses.membership, ae);
        DIFF_POOL_FIELD(expected->statuses.effect_type[ed], actual->statuses.effect_type[ad],
                        SIM_STATE_FIELD_STATUS_EFFECT_TYPE);
        DIFF_POOL_FIELD(expected->statuses.stack_count[ed], actual->statuses.stack_count[ad],
                        SIM_STATE_FIELD_STATUS_STACK_COUNT);
        DIFF_POOL_FIELD(expected->statuses.remaining_ticks[ed],
                        actual->statuses.remaining_ticks[ad],
                        SIM_STATE_FIELD_STATUS_REMAINING_TICKS);
        DIFF_POOL_FIELD(i32_bits(expected->statuses.magnitude[ed]),
                        i32_bits(actual->statuses.magnitude[ad]),
                        SIM_STATE_FIELD_STATUS_MAGNITUDE);
        DIFF_POOL_FIELD(i32_bits(expected->statuses.scalar[ed]),
                        i32_bits(actual->statuses.scalar[ad]),
                        SIM_STATE_FIELD_STATUS_SCALAR);
    }
    return false;
}

static bool diff_sim_event_phase(const SimEventQueue* expected, const SimEventQueue* actual,
                                 uint32_t expected_buffer, uint32_t actual_buffer,
                                 SimStateField count_field, SimStateField tick_field,
                                 SimStateField kind_field, SimStateField payload_field,
                                 SimStateDiff* out_diff) {
    DIFF_SCALAR(expected->counts[expected_buffer], actual->counts[actual_buffer], count_field);
    for (uint32_t index = 0u; index < expected->counts[expected_buffer]; ++index) {
        const SimEvent& e = expected->buffers[expected_buffer][index];
        const SimEvent& a = actual->buffers[actual_buffer][index];
        DIFF_POOL_FIELD(e.tick, a.tick, tick_field);
        DIFF_POOL_FIELD(e.event_kind, a.event_kind, kind_field);
        DIFF_POOL_FIELD(e.payload_size, a.payload_size, payload_field);
        for (uint16_t byte = 0u; byte < e.payload_size; ++byte) {
            DIFF_POOL_FIELD(e.payload[byte], a.payload[byte], payload_field);
        }
    }
    return false;
}

static bool diff_sim_events(const SimWorld* expected, const SimWorld* actual,
                            SimStateDiff* out_diff) {
    if (expected->config.sim_event_capacity == 0u ||
        actual->config.sim_event_capacity == 0u) return false;
    if (diff_sim_event_phase(&expected->sim_events, &actual->sim_events,
                             expected->sim_events.read_index, actual->sim_events.read_index,
                             SIM_STATE_FIELD_SIM_EVENT_READ_COUNT,
                             SIM_STATE_FIELD_SIM_EVENT_READ_TICK,
                             SIM_STATE_FIELD_SIM_EVENT_READ_KIND,
                             SIM_STATE_FIELD_SIM_EVENT_READ_PAYLOAD, out_diff)) return true;
    return diff_sim_event_phase(&expected->sim_events, &actual->sim_events,
                                expected->sim_events.write_index,
                                actual->sim_events.write_index,
                                SIM_STATE_FIELD_SIM_EVENT_WRITE_COUNT,
                                SIM_STATE_FIELD_SIM_EVENT_WRITE_TICK,
                                SIM_STATE_FIELD_SIM_EVENT_WRITE_KIND,
                                SIM_STATE_FIELD_SIM_EVENT_WRITE_PAYLOAD, out_diff);
}

bool sim_diff_state(const SimWorld* expected, const SimWorld* actual, SimStateDiff* out_diff) {
    if (!out_diff) return false;
    set_diff(out_diff, SIM_STATE_FIELD_NONE, SIM_STATE_DIFF_NO_INDEX, 0u, 0u);
    if (!canonical_world_valid(expected) || !canonical_world_valid(actual)) {
        set_diff(out_diff, SIM_STATE_FIELD_INVALID, SIM_STATE_DIFF_NO_INDEX, 0u, 0u);
        return true;
    }

    DIFF_SCALAR(expected->tick, actual->tick, SIM_STATE_FIELD_TICK);
    DIFF_SCALAR(expected->config.max_entities, actual->config.max_entities,
                SIM_STATE_FIELD_CONFIG_MAX_ENTITIES);
    DIFF_SCALAR(expected->config.initial_unit_count, actual->config.initial_unit_count,
                SIM_STATE_FIELD_CONFIG_INITIAL_UNIT_COUNT);
    DIFF_SCALAR(expected->config.damage_event_capacity, actual->config.damage_event_capacity,
                SIM_STATE_FIELD_CONFIG_DAMAGE_EVENT_CAPACITY);
    DIFF_SCALAR(expected->rng.state, actual->rng.state, SIM_STATE_FIELD_RNG_STATE);
    DIFF_SCALAR(expected->rng.inc, actual->rng.inc, SIM_STATE_FIELD_RNG_INC);
    DIFF_SCALAR(expected->entities.capacity, actual->entities.capacity,
                SIM_STATE_FIELD_ENTITY_CAPACITY);
    DIFF_SCALAR(expected->entities.live_count, actual->entities.live_count,
                SIM_STATE_FIELD_ENTITY_LIVE_COUNT);
    DIFF_SCALAR(expected->entities.next_fresh, actual->entities.next_fresh,
                SIM_STATE_FIELD_ENTITY_NEXT_FRESH);
    DIFF_SCALAR(expected->entities.free_count, actual->entities.free_count,
                SIM_STATE_FIELD_ENTITY_FREE_COUNT);

    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        if (expected->entities.generations[index] != actual->entities.generations[index]) {
            set_diff(out_diff, SIM_STATE_FIELD_ENTITY_GENERATION, index,
                     expected->entities.generations[index], actual->entities.generations[index]);
            return true;
        }
    }
    for (uint32_t index = 0u; index < expected->entities.next_fresh; ++index) {
        if (expected->entities.liveness[index] != actual->entities.liveness[index]) {
            set_diff(out_diff, SIM_STATE_FIELD_ENTITY_LIVENESS, index,
                     expected->entities.liveness[index], actual->entities.liveness[index]);
            return true;
        }
    }
    for (uint32_t ordinal = 0u; ordinal < expected->entities.free_count; ++ordinal) {
        if (expected->entities.free_stack[ordinal] != actual->entities.free_stack[ordinal]) {
            set_diff(out_diff, SIM_STATE_FIELD_ENTITY_FREE_STACK, ordinal,
                     expected->entities.free_stack[ordinal], actual->entities.free_stack[ordinal]);
            return true;
        }
    }
    for (uint32_t unit = 0u; unit < SIM_MAX_UNITS; ++unit) {
        if (expected->unit_entities[unit].h != actual->unit_entities[unit].h) {
            set_diff(out_diff, SIM_STATE_FIELD_UNIT_ENTITY, unit,
                     expected->unit_entities[unit].h, actual->unit_entities[unit].h);
            return true;
        }
    }
    DIFF_SCALAR(expected->pending_destroy_count, actual->pending_destroy_count,
                SIM_STATE_FIELD_PENDING_DESTROY_COUNT);
    for (uint32_t ordinal = 0u; ordinal < expected->pending_destroy_count; ++ordinal) {
        if (expected->pending_destroy[ordinal].h != actual->pending_destroy[ordinal].h) {
            set_diff(out_diff, SIM_STATE_FIELD_PENDING_DESTROY_ENTITY, ordinal,
                     expected->pending_destroy[ordinal].h, actual->pending_destroy[ordinal].h);
            return true;
        }
    }

    if (diff_damage_events(expected, actual, out_diff)) return true;

    if (diff_transform(expected, actual, out_diff) ||
        diff_velocity(expected, actual, out_diff) ||
        diff_health(expected, actual, out_diff) ||
        diff_map(expected, actual, out_diff) ||
        diff_hero_defs(expected, actual, out_diff) ||
        diff_heroes(expected, actual, out_diff) ||
        diff_projectiles(expected, actual, out_diff) ||
        diff_statuses(expected, actual, out_diff) ||
        diff_sim_events(expected, actual, out_diff)) return true;
    return false;
}

#undef DIFF_POOL_FIELD
#undef DIFF_SCALAR

const char* sim_state_field_name(SimStateField field) {
    switch (field) {
        case SIM_STATE_FIELD_NONE: return "none";
        case SIM_STATE_FIELD_INVALID: return "invalid";
        case SIM_STATE_FIELD_TICK: return "tick";
        case SIM_STATE_FIELD_CONFIG_MAX_ENTITIES: return "config_max_entities";
        case SIM_STATE_FIELD_CONFIG_INITIAL_UNIT_COUNT: return "config_initial_unit_count";
        case SIM_STATE_FIELD_CONFIG_DAMAGE_EVENT_CAPACITY: return "config_damage_event_capacity";
        case SIM_STATE_FIELD_RNG_STATE: return "rng_state";
        case SIM_STATE_FIELD_RNG_INC: return "rng_inc";
        case SIM_STATE_FIELD_ENTITY_CAPACITY: return "entity_capacity";
        case SIM_STATE_FIELD_ENTITY_LIVE_COUNT: return "entity_live_count";
        case SIM_STATE_FIELD_ENTITY_NEXT_FRESH: return "entity_next_fresh";
        case SIM_STATE_FIELD_ENTITY_FREE_COUNT: return "entity_free_count";
        case SIM_STATE_FIELD_ENTITY_GENERATION: return "entity_generation";
        case SIM_STATE_FIELD_ENTITY_LIVENESS: return "entity_liveness";
        case SIM_STATE_FIELD_ENTITY_FREE_STACK: return "entity_free_stack";
        case SIM_STATE_FIELD_UNIT_ENTITY: return "unit_entity";
        case SIM_STATE_FIELD_PENDING_DESTROY_COUNT: return "pending_destroy_count";
        case SIM_STATE_FIELD_PENDING_DESTROY_ENTITY: return "pending_destroy_entity";
        case SIM_STATE_FIELD_DAMAGE_EVENT_READ_COUNT: return "damage_event_read_count";
        case SIM_STATE_FIELD_DAMAGE_EVENT_READ_SOURCE: return "damage_event_read_source";
        case SIM_STATE_FIELD_DAMAGE_EVENT_READ_TARGET: return "damage_event_read_target";
        case SIM_STATE_FIELD_DAMAGE_EVENT_READ_AMOUNT: return "damage_event_read_amount";
        case SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_COUNT: return "damage_event_write_count";
        case SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_SOURCE: return "damage_event_write_source";
        case SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_TARGET: return "damage_event_write_target";
        case SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_AMOUNT: return "damage_event_write_amount";
        case SIM_STATE_FIELD_TRANSFORM_COUNT: return "transform_count";
        case SIM_STATE_FIELD_TRANSFORM_ENTITY: return "transform_entity";
        case SIM_STATE_FIELD_POSITION_X: return "position_x";
        case SIM_STATE_FIELD_POSITION_Y: return "position_y";
        case SIM_STATE_FIELD_FACING: return "facing";
        case SIM_STATE_FIELD_VELOCITY_COUNT: return "velocity_count";
        case SIM_STATE_FIELD_VELOCITY_ENTITY: return "velocity_entity";
        case SIM_STATE_FIELD_VELOCITY_X: return "velocity_x";
        case SIM_STATE_FIELD_VELOCITY_Y: return "velocity_y";
        case SIM_STATE_FIELD_HEALTH_COUNT: return "health_count";
        case SIM_STATE_FIELD_HEALTH_ENTITY: return "health_entity";
        case SIM_STATE_FIELD_HEALTH_CURRENT: return "health_current";
        case SIM_STATE_FIELD_HEALTH_MAXIMUM: return "health_maximum";
        case SIM_STATE_FIELD_DAMAGE_COOLDOWN: return "damage_cooldown";
        case SIM_STATE_FIELD_MAP_ID: return "map_id";
        case SIM_STATE_FIELD_MAP_WIDTH: return "map_width";
        case SIM_STATE_FIELD_MAP_HEIGHT: return "map_height";
        case SIM_STATE_FIELD_MAP_CELL_SIZE: return "map_cell_size";
        case SIM_STATE_FIELD_MAP_CELL_FLAGS: return "map_cell_flags";
        case SIM_STATE_FIELD_MAP_CELL_COST: return "map_cell_cost";
        case SIM_STATE_FIELD_MAP_CELL_HEIGHT: return "map_cell_height";
        case SIM_STATE_FIELD_MAP_LANE_COUNT: return "map_lane_count";
        case SIM_STATE_FIELD_MAP_LANE_ID: return "map_lane_id";
        case SIM_STATE_FIELD_MAP_LANE_WAYPOINT_COUNT: return "map_lane_waypoint_count";
        case SIM_STATE_FIELD_MAP_LANE_WAVE_INTERVAL: return "map_lane_wave_interval";
        case SIM_STATE_FIELD_MAP_LANE_FIRST_WAVE: return "map_lane_first_wave";
        case SIM_STATE_FIELD_MAP_LANE_ARCHETYPE: return "map_lane_archetype";
        case SIM_STATE_FIELD_MAP_LANE_WAYPOINT: return "map_lane_waypoint";
        case SIM_STATE_FIELD_HERO_DEF_COUNT: return "hero_def_count";
        case SIM_STATE_FIELD_HERO_DEF_RECORD: return "hero_def_record";
        case SIM_STATE_FIELD_HERO_COUNT: return "hero_count";
        case SIM_STATE_FIELD_HERO_ENTITY: return "hero_entity";
        case SIM_STATE_FIELD_HERO_DEF_INDEX: return "hero_def_index";
        case SIM_STATE_FIELD_HERO_RESOURCE: return "hero_resource";
        case SIM_STATE_FIELD_HERO_BASIC_ATTACK_COOLDOWN: return "hero_basic_attack_cooldown";
        case SIM_STATE_FIELD_HERO_ACTION_COOLDOWN: return "hero_action_cooldown";
        case SIM_STATE_FIELD_HERO_PENDING_KIND: return "hero_pending_kind";
        case SIM_STATE_FIELD_HERO_PENDING_SLOT: return "hero_pending_slot";
        case SIM_STATE_FIELD_HERO_PENDING_TARGET: return "hero_pending_target";
        case SIM_STATE_FIELD_HERO_PENDING_POINT_X: return "hero_pending_point_x";
        case SIM_STATE_FIELD_HERO_PENDING_POINT_Y: return "hero_pending_point_y";
        case SIM_STATE_FIELD_PROJECTILE_COUNT: return "projectile_count";
        case SIM_STATE_FIELD_PROJECTILE_ENTITY: return "projectile_entity";
        case SIM_STATE_FIELD_PROJECTILE_SOURCE: return "projectile_source";
        case SIM_STATE_FIELD_PROJECTILE_TARGET: return "projectile_target";
        case SIM_STATE_FIELD_PROJECTILE_DEF_INDEX: return "projectile_def_index";
        case SIM_STATE_FIELD_PROJECTILE_ACTION_SLOT: return "projectile_action_slot";
        case SIM_STATE_FIELD_PROJECTILE_EFFECT_INDEX: return "projectile_effect_index";
        case SIM_STATE_FIELD_PROJECTILE_POSITION_X: return "projectile_position_x";
        case SIM_STATE_FIELD_PROJECTILE_POSITION_Y: return "projectile_position_y";
        case SIM_STATE_FIELD_PROJECTILE_SPEED: return "projectile_speed";
        case SIM_STATE_FIELD_PROJECTILE_REMAINING_TICKS: return "projectile_remaining_ticks";
        case SIM_STATE_FIELD_STATUS_COUNT: return "status_count";
        case SIM_STATE_FIELD_STATUS_ENTITY: return "status_entity";
        case SIM_STATE_FIELD_STATUS_EFFECT_TYPE: return "status_effect_type";
        case SIM_STATE_FIELD_STATUS_STACK_COUNT: return "status_stack_count";
        case SIM_STATE_FIELD_STATUS_REMAINING_TICKS: return "status_remaining_ticks";
        case SIM_STATE_FIELD_STATUS_MAGNITUDE: return "status_magnitude";
        case SIM_STATE_FIELD_STATUS_SCALAR: return "status_scalar";
        case SIM_STATE_FIELD_SIM_EVENT_READ_COUNT: return "sim_event_read_count";
        case SIM_STATE_FIELD_SIM_EVENT_READ_TICK: return "sim_event_read_tick";
        case SIM_STATE_FIELD_SIM_EVENT_READ_KIND: return "sim_event_read_kind";
        case SIM_STATE_FIELD_SIM_EVENT_READ_PAYLOAD: return "sim_event_read_payload";
        case SIM_STATE_FIELD_SIM_EVENT_WRITE_COUNT: return "sim_event_write_count";
        case SIM_STATE_FIELD_SIM_EVENT_WRITE_TICK: return "sim_event_write_tick";
        case SIM_STATE_FIELD_SIM_EVENT_WRITE_KIND: return "sim_event_write_kind";
        case SIM_STATE_FIELD_SIM_EVENT_WRITE_PAYLOAD: return "sim_event_write_payload";
        default: return "invalid";
    }
}
