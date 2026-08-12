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

static bool membership_valid(const ComponentPool* pool, const EntityManager* manager,
                             uint32_t capacity) {
    if (!pool || !pool->sparse || !pool->dense_entities ||
        pool->entity_capacity != capacity || pool->capacity != capacity ||
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
        !damage_event_queue_is_valid(&world->damage_events) ||
        !world->pending_destroy || world->pending_destroy_count > world->config.max_entities)
        return false;

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

uint64_t sim_hash_state(const SimWorld* world) {
    if (!canonical_world_valid(world)) return 0u;

    uint64_t hash = FNV1A64_OFFSET;
    hash = hash_u64_le(hash, world->tick);
    hash = hash_u32_le(hash, world->config.max_entities);
    hash = hash_u32_le(hash, world->config.initial_unit_count);
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

    hash = hash_transform(hash, world);
    hash = hash_velocity(hash, world);
    return hash_health(hash, world);
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

    if (diff_transform(expected, actual, out_diff) ||
        diff_velocity(expected, actual, out_diff) ||
        diff_health(expected, actual, out_diff)) return true;
    return false;
}

#undef DIFF_SCALAR

const char* sim_state_field_name(SimStateField field) {
    switch (field) {
        case SIM_STATE_FIELD_NONE: return "none";
        case SIM_STATE_FIELD_INVALID: return "invalid";
        case SIM_STATE_FIELD_TICK: return "tick";
        case SIM_STATE_FIELD_CONFIG_MAX_ENTITIES: return "config_max_entities";
        case SIM_STATE_FIELD_CONFIG_INITIAL_UNIT_COUNT: return "config_initial_unit_count";
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
        default: return "invalid";
    }
}
