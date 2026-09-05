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

// ---------------------------------------------------------------- shared queries

static const MapLane* lane_by_id(const MapGrid* map, uint8_t lane_id, uint16_t* out_index) {
    if (!map || !map->lanes) return nullptr;
    for (uint16_t lane = 0u; lane < map->lane_count; ++lane) {
        if (map->lanes[lane].lane_id != lane_id) continue;
        if (out_index) *out_index = lane;
        return &map->lanes[lane];
    }
    return nullptr;
}

// Alive, damageable, and not already at zero: the one definition of "a live target"
// every acquisition tier uses.
static bool target_is_live(SimWorld* world, EntityId entity) {
    HealthView health{};
    if (!entity_manager_is_alive(&world->entities, entity)) return false;
    if (!health_pool_get(&world->health, entity, &health)) return false;
    return *health.current > 0;
}

static bool entity_position(const SimWorld* world, EntityId entity, mm::fix* out_x,
                            mm::fix* out_y) {
    ConstTransformView view{};
    if (!transform_pool_get_const(&world->transforms, entity, &view)) return false;
    *out_x = *view.position_x;
    *out_y = *view.position_y;
    return true;
}

// Nearest live enemy of `actor` within range_squared, tie-broken by ascending
// EntityId so the choice is a total order over already-hashed state and never
// depends on traversal luck. `filter` narrows the tier; nullptr accepts every enemy.
typedef bool (*TargetFilter)(SimWorld* world, EntityId candidate);

static bool acquire_nearest_enemy(SimWorld* world, EntityId actor, int64_t range_squared,
                                  TargetFilter filter, EntityId* out_target) {
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->teams.membership, &ordered)) return false;
    bool found = false;
    EntityId best{HANDLE_NULL};
    int64_t best_distance = 0;
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        EntityId candidate = ordered.entities[i];
        if (candidate.h == actor.h) continue;
        if (!team_is_enemy(&world->teams, actor, candidate)) continue;
        if (!target_is_live(world, candidate)) continue;
        if (filter && !filter(world, candidate)) continue;
        int64_t distance = 0;
        if (!sim_distance_squared(world, actor, candidate, &distance)) continue;
        if (distance > range_squared) continue;
        if (!found || distance < best_distance ||
            (distance == best_distance && candidate.h < best.h)) {
            found = true;
            best = candidate;
            best_distance = distance;
        }
    }
    if (found && out_target) *out_target = best;
    return found;
}

// ---------------------------------------------------------------- sys_waves

static uint32_t entity_headroom(const EntityManager* manager) {
    uint32_t fresh = manager->capacity - manager->next_fresh;
    return manager->free_count + fresh;
}

static uint32_t pool_room(const ComponentPool* pool) {
    return pool->capacity - pool->count;
}

bool sys_waves(SimWorld* world) {
    if (!world) return false;
    if (world->match.team_count == 0u || world->config.minion_capacity == 0u) return true;
    if (!sim_wave_tick_due(world)) return true;

    uint32_t per_team = world->match.minions_per_wave;
    uint32_t wanted = per_team * world->match.team_count;
    if (wanted == 0u) return true;

    // A partial wave would be worse than no wave, so the whole wave is pre-flighted
    // before the first entity exists: an overflow fails the source operation and
    // therefore the tick, and nothing is dropped (ADR-0014).
    if (entity_headroom(&world->entities) < wanted ||
        pool_room(&world->minions.membership) < wanted ||
        pool_room(&world->teams.membership) < wanted ||
        pool_room(&world->transforms.membership) < wanted ||
        pool_room(&world->velocities.membership) < wanted ||
        pool_room(&world->health.membership) < wanted) return false;

    for (uint16_t team = 0u; team < world->match.team_count; ++team) {
        const SimTeamDef& def = world->match.teams[team];
        uint16_t lane_index = 0u;
        const MapLane* lane = lane_by_id(&world->map, def.lane_id, &lane_index);
        if (!lane || lane->waypoint_count < 2u) return false;
        uint32_t cell = 0u;
        if (map_find_spawn(&world->map, def.spawn_ordinal, &cell) != MAP_STATUS_OK) return false;
        uint8_t waypoint = def.waypoint_reverse
                               ? static_cast<uint8_t>(lane->waypoint_count - 1u)
                               : 0u;
        for (uint32_t ordinal = 0u; ordinal < per_team; ++ordinal) {
            if (!sim_spawn_minion(world, static_cast<uint8_t>(team), def.lane_id, waypoint,
                                  cell, nullptr)) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- sys_minion_ai

// Velocity toward (target_x, target_y) at `speed`, normalized in fixed point through
// fix_isqrt64 on the 64-bit squared distance. No sqrt, no floating point.
static void steer_toward(mm::fix from_x, mm::fix from_y, mm::fix to_x, mm::fix to_y,
                         mm::fix speed, mm::fix* out_x, mm::fix* out_y) {
    int64_t dx = static_cast<int64_t>(to_x) - static_cast<int64_t>(from_x);
    int64_t dy = static_cast<int64_t>(to_y) - static_cast<int64_t>(from_y);
    uint64_t length_squared = static_cast<uint64_t>(dx * dx + dy * dy);
    uint64_t length = mm::fix_isqrt64(length_squared);
    if (length == 0u) {
        *out_x = 0;
        *out_y = 0;
        return;
    }
    int64_t signed_length = static_cast<int64_t>(length);
    *out_x = static_cast<mm::fix>(dx * static_cast<int64_t>(speed) / signed_length);
    *out_y = static_cast<mm::fix>(dy * static_cast<int64_t>(speed) / signed_length);
}

static bool waypoint_position(const MapGrid* map, const MapLane* lane, uint16_t lane_index,
                              uint8_t waypoint_index, mm::fix* out_x, mm::fix* out_y) {
    const uint16_t* waypoints = map_lane_waypoints(map, lane_index);
    if (!waypoints || waypoint_index >= lane->waypoint_count) return false;
    return map_cell_to_world(map, waypoints[waypoint_index], out_x, out_y) == MAP_STATUS_OK;
}

// The per-tick displacement sys_movement will produce for this speed. Arrival is a
// squared-distance comparison against exactly one such step.
static int64_t step_squared(mm::fix speed) {
    int64_t step = mm::fix_mul(speed, SIM_DT_FIXED);
    return step * step;
}

static bool minion_push(SimWorld* world, EntityId entity, MinionView* minion,
                        VelocityView* velocity, uint8_t reverse) {
    uint16_t lane_index = 0u;
    const MapLane* lane = lane_by_id(&world->map, *minion->lane, &lane_index);
    if (!lane) return false;
    mm::fix from_x = 0;
    mm::fix from_y = 0;
    mm::fix to_x = 0;
    mm::fix to_y = 0;
    if (!entity_position(world, entity, &from_x, &from_y) ||
        !waypoint_position(&world->map, lane, lane_index, *minion->waypoint_index,
                           &to_x, &to_y)) return false;

    int64_t distance = 0;
    if (!sim_distance_squared_point(world, entity, to_x, to_y, &distance)) return false;
    if (distance <= step_squared(world->match.creep.move_speed)) {
        bool terminal = reverse ? (*minion->waypoint_index == 0u)
                                : (static_cast<uint32_t>(*minion->waypoint_index) + 1u >=
                                   static_cast<uint32_t>(lane->waypoint_count));
        if (terminal) {
            *velocity->velocity_x = 0;
            *velocity->velocity_y = 0;
            return true;
        }
        *minion->waypoint_index = static_cast<uint8_t>(
            reverse ? *minion->waypoint_index - 1u : *minion->waypoint_index + 1u);
        if (!waypoint_position(&world->map, lane, lane_index, *minion->waypoint_index,
                               &to_x, &to_y)) return false;
    }
    steer_toward(from_x, from_y, to_x, to_y, world->match.creep.move_speed,
                 velocity->velocity_x, velocity->velocity_y);
    return true;
}

// One basic attack through the one pipeline: resolve_effect appends, the committer
// applies. Arming the cooldown is the only state this writes.
static bool minion_fire(SimWorld* world, EntityId entity, MinionView* minion,
                        const SimMinionDef& creep) {
    if (*minion->attack_cooldown != 0u) return true;
    SimEffectDef effect{};
    effect.effect_type = SIM_EFFECT_PROJECTILE_DAMAGE;
    effect.magnitude = creep.attack_magnitude;
    if (!resolve_effect(world, entity, *minion->target, &effect, 0, 0)) return false;
    *minion->attack_cooldown = creep.attack_cooldown_ticks;
    return true;
}

bool sys_minion_ai(SimWorld* world) {
    if (!world) return false;
    if (world->match.team_count == 0u || world->config.minion_capacity == 0u) return true;
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->minions.membership, &ordered)) return false;

    const SimMinionDef& creep = world->match.creep;
    int64_t attack_range_squared = sim_range_squared(creep.attack_range);
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        EntityId entity = ordered.entities[i];
        MinionView minion{};
        VelocityView velocity{};
        uint8_t team = SIM_TEAM_NONE;
        if (!minion_pool_get(&world->minions, entity, &minion) ||
            !velocity_pool_get(&world->velocities, entity, &velocity) ||
            !team_pool_team(&world->teams, entity, &team)) return false;
        if (!target_is_live(world, entity)) continue;      // dead this tick; sys_death owns it
        if (team >= world->match.team_count) return false;

        if (*minion.state == SIM_MINION_RETURN) {
            // RETURN is a distinct hashed state that lasts exactly one tick, so
            // "lost its target" is observable rather than a silent re-entry.
            *minion.state = SIM_MINION_PUSH;
            *minion.target = EntityId{HANDLE_NULL};
            *velocity.velocity_x = 0;
            *velocity.velocity_y = 0;
            continue;
        }

        if (*minion.state == SIM_MINION_ATTACK) {
            int64_t distance = 0;
            bool holds = target_is_live(world, *minion.target) &&
                         team_is_enemy(&world->teams, entity, *minion.target) &&
                         sim_distance_squared(world, entity, *minion.target, &distance) &&
                         distance <= attack_range_squared;
            if (!holds) {
                *minion.state = SIM_MINION_RETURN;
                *minion.target = EntityId{HANDLE_NULL};
                *velocity.velocity_x = 0;
                *velocity.velocity_y = 0;
                continue;
            }
            *velocity.velocity_x = 0;
            *velocity.velocity_y = 0;
            if (!minion_fire(world, entity, &minion, creep)) return false;
            continue;
        }

        EntityId acquired{HANDLE_NULL};
        if (acquire_nearest_enemy(world, entity, attack_range_squared, nullptr, &acquired)) {
            // Acquisition and the first shot are one step: a creep that has just
            // reached its enemy does not stand idle for a tick before swinging.
            *minion.state = SIM_MINION_ATTACK;
            *minion.target = acquired;
            *velocity.velocity_x = 0;
            *velocity.velocity_y = 0;
            if (!minion_fire(world, entity, &minion, creep)) return false;
            continue;
        }
        if (!minion_push(world, entity, &minion, &velocity,
                         world->match.teams[team].waypoint_reverse)) return false;
    }
    return true;
}
