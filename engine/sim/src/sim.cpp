#include "sim/sim.h"

#include "core/assert.h"
#include "sim/systems.h"

static bool sim_config_valid(SimWorldConfig config) {
    return config.max_entities > 0u && config.max_entities <= HANDLE_INDEX_MASK + 1u &&
           config.initial_unit_count <= SIM_MAX_UNITS &&
           config.initial_unit_count <= config.max_entities &&
           config.damage_event_capacity > 0u;
}

static bool add_required(size_t* total, size_t required) {
    if (!total || required == 0u || required > SIZE_MAX - *total) return false;
    *total += required;
    return true;
}

SimWorldConfig sim_world_config_default(void) {
    return SimWorldConfig{
        SIM_DEFAULT_MAX_ENTITIES, SIM_MAX_UNITS, SIM_DEFAULT_DAMAGE_EVENT_CAPACITY};
}

size_t sim_world_memory_required(SimWorldConfig config) {
    if (!sim_config_valid(config)) return 0u;
    size_t total = 0u;
    if (!add_required(&total, entity_manager_memory_required(config.max_entities)) ||
        !add_required(&total, transform_pool_memory_required(
                                  config.max_entities, config.max_entities)) ||
        !add_required(&total, velocity_pool_memory_required(
                                  config.max_entities, config.max_entities)) ||
        !add_required(&total, health_pool_memory_required(
                                  config.max_entities, config.max_entities)) ||
        !add_required(&total, damage_event_queue_memory_required(
                                  config.damage_event_capacity))) return 0u;

    size_t destroy_bytes = sizeof(EntityId) * static_cast<size_t>(config.max_entities);
    if (destroy_bytes > SIZE_MAX - (alignof(EntityId) - 1u) ||
        !add_required(&total, destroy_bytes + alignof(EntityId) - 1u)) return 0u;
    return total;
}

bool sim_init(SimWorld* world, Arena* arena, uint64_t seed, SimWorldConfig config) {
    size_t required = sim_world_memory_required(config);
    if (!world || !arena || !arena->base || arena->offset > arena->reserved ||
        required == 0u || required > arena->reserved - arena->offset) return false;

    TempMemory temp = temp_begin(arena);
    SimWorld staged{};
    staged.config = config;
    if (!entity_manager_init(&staged.entities, arena, config.max_entities) ||
        !transform_pool_init(&staged.transforms, arena,
                             config.max_entities, config.max_entities) ||
        !velocity_pool_init(&staged.velocities, arena,
                            config.max_entities, config.max_entities) ||
        !health_pool_init(&staged.health, arena,
                          config.max_entities, config.max_entities) ||
        !damage_event_queue_init(&staged.damage_events, arena,
                                 config.damage_event_capacity)) {
        temp_end(temp);
        return false;
    }
    staged.pending_destroy = static_cast<EntityId*>(arena_push_zero(
        arena, sizeof(EntityId) * static_cast<size_t>(config.max_entities), alignof(EntityId)));
    mm::pcg32_seed(&staged.rng, seed, 1u);

    for (uint32_t i = 0; i < config.initial_unit_count; ++i) {
        EntityId entity = entity_manager_create(&staged.entities);
        int32_t grid_x = static_cast<int32_t>(i % 8u) - 4;
        int32_t grid_y = static_cast<int32_t>(i / 8u) - 4;
        if (entity.h == HANDLE_NULL ||
            !transform_pool_add(&staged.transforms, entity,
                                mm::fix_from_int(grid_x), mm::fix_from_int(grid_y), 0) ||
            !velocity_pool_add(&staged.velocities, entity, 0, 0) ||
            !health_pool_add(&staged.health, entity, 100, 100, 0u)) {
            temp_end(temp);
            return false;
        }
        staged.unit_entities[i] = entity;
    }
    *world = staged;
    return true;
}

bool sim_command_is_canonical(const SimCommand* command, uint32_t player_count, uint32_t unit_count) {
    if (!command || player_count == 0 || player_count > SIM_MAX_PLAYERS ||
        unit_count > SIM_MAX_UNITS || command->player_id >= player_count ||
        command->unit_index >= unit_count) return false;
    switch (command->kind) {
        case SIM_COMMAND_SET_VELOCITY:
            return command->amount == 0;
        case SIM_COMMAND_DAMAGE:
            return command->value_x == 0 && command->value_y == 0 && command->amount >= 0;
        default:
            return false;
    }
}

bool sim_destroy_deferred(SimWorld* world, EntityId entity) {
    if (!world || !sim_config_valid(world->config) || !world->pending_destroy ||
        !entity_manager_is_alive(&world->entities, entity) ||
        world->pending_destroy_count >= world->config.max_entities) return false;
    for (uint32_t i = 0u; i < world->pending_destroy_count; ++i) {
        if (world->pending_destroy[i].h == entity.h) return false;
    }
    world->pending_destroy[world->pending_destroy_count] = entity;
    ++world->pending_destroy_count;
    return true;
}

bool sim_validate_commands(const SimWorld* world, const SimCommandBuffer* commands) {
    if (!world || !sim_config_valid(world->config) ||
        world->entities.capacity != world->config.max_entities ||
        world->transforms.membership.capacity != world->config.max_entities ||
        world->velocities.membership.capacity != world->config.max_entities ||
        world->health.membership.capacity != world->config.max_entities ||
        !damage_event_queue_is_valid(&world->damage_events) ||
        world->damage_events.capacity != world->config.damage_event_capacity ||
        world->damage_events.counts[world->damage_events.read_index] != 0u ||
        world->damage_events.counts[world->damage_events.write_index] != 0u ||
        !world->pending_destroy || world->pending_destroy_count > world->config.max_entities)
        return false;

    // Validate the complete unit-slot schedule before any command or integration
    // mutation. Cleared slots are legal; mapped slots must resolve every required
    // component through the exact live generation.
    for (uint32_t i = 0; i < SIM_MAX_UNITS; ++i) {
        EntityId entity = world->unit_entities[i];
        if (entity.h == HANDLE_NULL) continue;
        if (i >= world->config.initial_unit_count ||
            !entity_manager_is_alive(&world->entities, entity) ||
            !transform_pool_has(&world->transforms, entity) ||
            !velocity_pool_has(&world->velocities, entity) ||
            !health_pool_has(&world->health, entity)) return false;
    }
    for (uint32_t i = 0u; i < world->pending_destroy_count; ++i) {
        EntityId entity = world->pending_destroy[i];
        if (!entity_manager_is_alive(&world->entities, entity)) return false;
        for (uint32_t previous = 0u; previous < i; ++previous) {
            if (world->pending_destroy[previous].h == entity.h) return false;
        }
    }
    if (!commands) return true;
    if (commands->count > SIM_MAX_COMMANDS_PER_TICK) return false;
    if (commands->count > 0 && !commands->commands) return false;

    uint32_t damage_count = 0u;
    for (uint32_t i = 0; i < commands->count; ++i) {
        const SimCommand& command = commands->commands[i];
        if (!sim_command_is_canonical(&command, SIM_MAX_PLAYERS, SIM_MAX_UNITS) ||
            world->unit_entities[command.unit_index].h == HANDLE_NULL) return false;
        if (command.kind == SIM_COMMAND_DAMAGE) ++damage_count;
    }
    return damage_count <= world->damage_events.capacity;
}

bool sim_tick(SimWorld* world, const SimCommandBuffer* commands) {
    // Validate the complete input first. No command or tick mutation happens when
    // any record is malformed, so callers can reject a packet atomically.
    if (!sim_validate_commands(world, commands)) return false;

    // Literal M3.2 schedule. Damage is intentionally published and resolved in
    // the same tick; moving publish to the next tick boundary is the retained
    // next-tick experiment seam.
    bool applied = sys_apply_commands(world, commands);
    ENSURE(applied);
    bool moved = sys_movement(world);
    ENSURE(moved);
    bool published = damage_event_queue_publish(&world->damage_events);
    ENSURE(published);
    bool resolved = sys_combat_resolve(world);
    ENSURE(resolved);
    bool consumed = damage_event_queue_consume(&world->damage_events);
    ENSURE(consumed);
    bool cooled_down = sys_cooldown_tick(world);
    ENSURE(cooled_down);
    sys_rng_advance(world);
    bool destroyed = sys_flush_destroy(world);
    ENSURE(destroyed);
    ++world->tick;
    return true;
}
