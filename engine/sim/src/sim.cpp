#include "sim/sim.h"

#include "core/assert.h"
#include "sim/objectives.h"
#include "sim/systems.h"

static bool sim_config_valid(SimWorldConfig config) {
    return config.max_entities > 0u && config.max_entities <= HANDLE_INDEX_MASK + 1u &&
           config.initial_unit_count <= SIM_MAX_UNITS &&
           config.initial_unit_count <= config.max_entities &&
           config.damage_event_capacity > 0u && map_config_valid(config.map) &&
           config.hero_def_capacity <= config.max_entities &&
           config.hero_capacity <= config.max_entities &&
           config.projectile_capacity <= config.max_entities &&
           config.status_capacity <= config.max_entities &&
           config.sim_event_capacity <= config.max_entities &&
           // A hero pool without a def table, or projectiles/status/heroes without an
           // event queue, is a configuration mistake rather than a valid empty state.
           (config.hero_capacity == 0u || config.hero_def_capacity > 0u) &&
           (config.hero_capacity == 0u || config.sim_event_capacity > 0u) &&
           (config.projectile_capacity == 0u || config.hero_capacity > 0u) &&
           (config.status_capacity == 0u || config.hero_capacity > 0u) &&
           config.team_capacity <= config.max_entities &&
           config.minion_capacity <= config.max_entities &&
           config.objective_capacity <= config.max_entities &&
           // M5.3: a minion or an objective without a team row has no side, and
           // neither can report a death without the event envelope.
           (config.minion_capacity == 0u || config.team_capacity > 0u) &&
           (config.objective_capacity == 0u || config.team_capacity > 0u) &&
           (config.minion_capacity == 0u || config.sim_event_capacity > 0u) &&
           (config.objective_capacity == 0u || config.sim_event_capacity > 0u);
}

static bool add_required(size_t* total, size_t required) {
    if (!total || required == 0u || required > SIZE_MAX - *total) return false;
    *total += required;
    return true;
}

SimWorldConfig sim_world_config_default(void) {
    // Every M5.2 hero-combat and M5.3 lane-objective capacity is zero here on
    // purpose: the placeholder world, every existing fixture, and the recorded
    // oracle keep their exact arena layout and canonical state until a caller
    // opts in.
    return SimWorldConfig{
        SIM_DEFAULT_MAX_ENTITIES, SIM_MAX_UNITS, SIM_DEFAULT_DAMAGE_EVENT_CAPACITY,
        MapConfig{}, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
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
    if (!map_config_is_empty(config.map) &&
        !add_required(&total, map_memory_required(config.map))) return 0u;

    // Zero capacity is skipped outright: add_required rejects a zero-size request,
    // so a disabled pool must never reach it.
    if (config.hero_def_capacity > 0u &&
        !add_required(&total, sim_hero_def_table_memory_required(config.hero_def_capacity)))
        return 0u;
    if (config.hero_capacity > 0u &&
        !add_required(&total, hero_pool_memory_required(config.max_entities,
                                                        config.hero_capacity))) return 0u;
    if (config.projectile_capacity > 0u &&
        !add_required(&total, projectile_pool_memory_required(
                                  config.max_entities, config.projectile_capacity))) return 0u;
    if (config.status_capacity > 0u &&
        !add_required(&total, status_pool_memory_required(config.max_entities,
                                                          config.status_capacity))) return 0u;
    if (config.sim_event_capacity > 0u &&
        !add_required(&total, sim_event_queue_memory_required(config.sim_event_capacity)))
        return 0u;
    if (config.team_capacity > 0u &&
        !add_required(&total, team_pool_memory_required(config.max_entities,
                                                        config.team_capacity))) return 0u;
    if (config.minion_capacity > 0u &&
        !add_required(&total, minion_pool_memory_required(config.max_entities,
                                                          config.minion_capacity))) return 0u;
    if (config.objective_capacity > 0u &&
        !add_required(&total, objective_pool_memory_required(
                                  config.max_entities, config.objective_capacity))) return 0u;

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
                                 config.damage_event_capacity) ||
        !map_init(&staged.map, arena, config.map)) {
        temp_end(temp);
        return false;
    }
    if ((config.hero_def_capacity > 0u &&
         !sim_hero_def_table_init(&staged.hero_defs, arena, config.hero_def_capacity)) ||
        (config.hero_capacity > 0u &&
         !hero_pool_init(&staged.heroes, arena, config.max_entities, config.hero_capacity)) ||
        (config.projectile_capacity > 0u &&
         !projectile_pool_init(&staged.projectiles, arena, config.max_entities,
                               config.projectile_capacity)) ||
        (config.status_capacity > 0u &&
         !status_pool_init(&staged.statuses, arena, config.max_entities,
                           config.status_capacity)) ||
        (config.sim_event_capacity > 0u &&
         !sim_event_queue_init(&staged.sim_events, arena, config.sim_event_capacity)) ||
        (config.team_capacity > 0u &&
         !team_pool_init(&staged.teams, arena, config.max_entities, config.team_capacity)) ||
        (config.minion_capacity > 0u &&
         !minion_pool_init(&staged.minions, arena, config.max_entities,
                           config.minion_capacity)) ||
        (config.objective_capacity > 0u &&
         !objective_pool_init(&staged.objectives, arena, config.max_entities,
                              config.objective_capacity))) {
        temp_end(temp);
        return false;
    }
    // winner is SIM_TEAM_NONE while the match is running; objectives.cpp owns the
    // three match-state fields, so the initial value comes from there.
    staged.match_state = sim_match_state_initial();
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
        case SIM_COMMAND_ATTACK: {
            // Self-contained by construction: this predicate never sees the world,
            // so anything needing the def table or a live handle belongs in
            // sim_validate_commands instead.
            if (command->value_y != 0 || command->amount != 0) return false;
            int32_t slot = mm::fix_to_int(command->value_x);
            return slot >= 0 && static_cast<uint32_t>(slot) < unit_count &&
                   command->value_x == mm::fix_from_int(slot);
        }
        case SIM_COMMAND_CAST:
            // value_x/value_y are read according to the action's target_mode, which
            // lives in the def table; this predicate can only bound the action slot.
            return command->amount >= 0 &&
                   command->amount < static_cast<int32_t>(SIM_MAX_ACTION_SLOTS);
        default:
            return false;
    }
}

bool sim_install_hero_def(SimWorld* world, const SimHeroDef* def, uint16_t* out_index) {
    if (!world || world->config.hero_def_capacity == 0u) return false;
    return sim_hero_def_table_install(&world->hero_defs, def, out_index);
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
        !world->pending_destroy || world->pending_destroy_count > world->config.max_entities ||
        !sim_hero_def_table_valid(&world->hero_defs) ||
        world->hero_defs.capacity != world->config.hero_def_capacity ||
        world->heroes.membership.capacity != world->config.hero_capacity ||
        world->projectiles.membership.capacity != world->config.projectile_capacity ||
        world->statuses.membership.capacity != world->config.status_capacity ||
        world->teams.membership.capacity != world->config.team_capacity ||
        world->minions.membership.capacity != world->config.minion_capacity ||
        world->objectives.membership.capacity != world->config.objective_capacity)
        return false;
    // Only the read phase must be drained between ticks. sys_effects_resolve runs
    // after publish, so a death it reports rides the write phase into the next
    // tick's read phase - explicit phases are exactly what makes that a schedule
    // decision rather than a storage change.
    if (world->config.sim_event_capacity > 0u &&
        (!sim_event_queue_is_valid(&world->sim_events) ||
         world->sim_events.capacity != world->config.sim_event_capacity ||
         world->sim_events.counts[world->sim_events.read_index] != 0u)) return false;

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
        if (command.kind == SIM_COMMAND_ATTACK) {
            EntityId actor = world->unit_entities[command.unit_index];
            uint32_t target_slot = static_cast<uint32_t>(mm::fix_to_int(command.value_x));
            if (!hero_pool_has(&world->heroes, actor) || target_slot >= SIM_MAX_UNITS ||
                world->unit_entities[target_slot].h == HANDLE_NULL) return false;
            ++damage_count;
        }
        if (command.kind == SIM_COMMAND_CAST) {
            EntityId actor = world->unit_entities[command.unit_index];
            uint16_t def_index = 0u;
            if (!hero_pool_def_index(&world->heroes, actor, &def_index)) return false;
            const SimHeroDef* def = sim_hero_def_table_get(&world->hero_defs, def_index);
            if (!def) return false;
            bool slot_exists = false;
            for (uint16_t action_index = 0u; action_index < def->action_count; ++action_index) {
                if (def->actions[action_index].slot == static_cast<uint8_t>(command.amount))
                    slot_exists = true;
            }
            if (!slot_exists) return false;
        }
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
    bool acted = sys_hero_actions(world);
    if (!acted) return false;
    bool flew = sys_projectiles(world);
    if (!flew) return false;
    // Section 4 puts sys_status after sys_movement, so a slow applied on tick N
    // first reduces movement on tick N+1. That is the schedule, not a bug.
    bool statuses = sys_status(world);
    if (!statuses) return false;
    bool published = damage_event_queue_publish(&world->damage_events);
    ENSURE(published);
    if (world->config.sim_event_capacity > 0u) {
        bool published_events = sim_event_queue_publish(&world->sim_events);
        ENSURE(published_events);
    }
    bool resolved = sys_combat_resolve(world);
    ENSURE(resolved);
    bool effects = sys_effects_resolve(world);
    if (!effects) return false;
    bool consumed = damage_event_queue_consume(&world->damage_events);
    ENSURE(consumed);
    if (world->config.sim_event_capacity > 0u) {
        bool consumed_events = sim_event_queue_consume(&world->sim_events);
        ENSURE(consumed_events);
    }
    bool cooled_down = sys_cooldown_tick(world);
    ENSURE(cooled_down);
    sys_rng_advance(world);
    bool destroyed = sys_flush_destroy(world);
    ENSURE(destroyed);
    ++world->tick;
    return true;
}
