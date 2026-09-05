#include "sim/objectives.h"

#include "core/assert.h"
#include "sim/combat.h"
#include "sim/systems.h"

// ---------------------------------------------------------------- match state

SimMatchState sim_match_state_initial(void) {
    SimMatchState state{};
    state.over = 0u;
    state.winner = SIM_TEAM_NONE;
    state.reserved = 0u;
    state.end_tick = 0u;
    return state;
}

// ---------------------------------------------------------------- spawning

static bool pool_has_room(const ComponentPool* pool) {
    return pool && pool->capacity > 0u && pool->count < pool->capacity;
}

static bool cell_to_world(const MapGrid* map, uint32_t cell, mm::fix* out_x, mm::fix* out_y) {
    return map_cell_to_world(map, cell, out_x, out_y) == MAP_STATUS_OK;
}

// Undoes a partially built entity so a rejected spawn is invisible. Removal order
// mirrors sys_flush_destroy, so the pools see the same order either way.
static void unwind_spawn(SimWorld* world, EntityId entity) {
    if (minion_pool_has(&world->minions, entity)) (void)minion_pool_remove(&world->minions, entity);
    if (objective_pool_has(&world->objectives, entity))
        (void)objective_pool_remove(&world->objectives, entity);
    if (team_pool_has(&world->teams, entity)) (void)team_pool_remove(&world->teams, entity);
    if (health_pool_has(&world->health, entity)) (void)health_pool_remove(&world->health, entity);
    if (velocity_pool_has(&world->velocities, entity))
        (void)velocity_pool_remove(&world->velocities, entity);
    if (transform_pool_has(&world->transforms, entity))
        (void)transform_pool_remove(&world->transforms, entity);
    (void)entity_manager_release(&world->entities, entity);
}

bool sim_spawn_objective(SimWorld* world, uint16_t def_index, uint32_t cell,
                         EntityId* out_entity) {
    if (!world || world->config.objective_capacity == 0u ||
        world->config.team_capacity == 0u) return false;
    if (def_index >= world->match.objective_count) return false;
    const SimObjectiveDef& def = world->match.objectives[def_index];
    mm::fix world_x = 0;
    mm::fix world_y = 0;
    if (!cell_to_world(&world->map, cell, &world_x, &world_y)) return false;
    if (!pool_has_room(&world->objectives.membership) ||
        !pool_has_room(&world->teams.membership) ||
        !pool_has_room(&world->transforms.membership) ||
        !pool_has_room(&world->health.membership)) return false;

    EntityId entity = entity_manager_create(&world->entities);
    if (entity.h == HANDLE_NULL) return false;
    if (!transform_pool_add(&world->transforms, entity, world_x, world_y, 0) ||
        !health_pool_add(&world->health, entity, def.max_health, def.max_health, 0u) ||
        !team_pool_add(&world->teams, entity, def.owner_team) ||
        !objective_pool_add(&world->objectives, entity, def_index, def.owner_team, def.kind)) {
        unwind_spawn(world, entity);
        return false;
    }
    if (out_entity) *out_entity = entity;
    return true;
}

bool sim_spawn_match_objectives(SimWorld* world) {
    if (!world) return false;
    if (world->match.team_count == 0u) return true;
    for (uint16_t team = 0u; team < world->match.team_count; ++team) {
        const SimTeamDef& def = world->match.teams[team];
        if (!sim_spawn_objective(world, def.tower_objective_index, def.tower_cell, nullptr))
            return false;
        if (!sim_spawn_objective(world, def.core_objective_index, def.core_cell, nullptr))
            return false;
    }
    return true;
}

bool sim_spawn_minion(SimWorld* world, uint8_t team, uint8_t lane_id, uint8_t waypoint_index,
                      uint32_t cell, EntityId* out_entity) {
    if (!world || world->config.minion_capacity == 0u ||
        world->config.team_capacity == 0u) return false;
    mm::fix world_x = 0;
    mm::fix world_y = 0;
    if (!cell_to_world(&world->map, cell, &world_x, &world_y)) return false;
    if (!pool_has_room(&world->minions.membership) ||
        !pool_has_room(&world->teams.membership) ||
        !pool_has_room(&world->transforms.membership) ||
        !pool_has_room(&world->velocities.membership) ||
        !pool_has_room(&world->health.membership)) return false;

    EntityId entity = entity_manager_create(&world->entities);
    if (entity.h == HANDLE_NULL) return false;
    if (!transform_pool_add(&world->transforms, entity, world_x, world_y, 0) ||
        !velocity_pool_add(&world->velocities, entity, 0, 0) ||
        !health_pool_add(&world->health, entity, world->match.creep.max_health,
                         world->match.creep.max_health, 0u) ||
        !team_pool_add(&world->teams, entity, team) ||
        !minion_pool_add(&world->minions, entity, lane_id, waypoint_index)) {
        unwind_spawn(world, entity);
        return false;
    }
    if (out_entity) *out_entity = entity;
    return true;
}

bool sim_wave_tick_due(const SimWorld* world) {
    if (!world || world->match.team_count == 0u) return false;
    uint16_t lane_index = 0u;
    if (!sim_match_lane_index(&world->match, &world->map, 0u, &lane_index)) return false;
    const MapLane& lane = world->map.lanes[lane_index];
    if (lane.wave_interval_ticks == 0u) return false;
    if (world->tick < static_cast<uint64_t>(lane.first_wave_tick)) return false;
    uint64_t since = world->tick - static_cast<uint64_t>(lane.first_wave_tick);
    return since % static_cast<uint64_t>(lane.wave_interval_ticks) == 0u;
}
