#include "sim/match.h"

#include "sim/sim.h"

// Section 5 invalid-schema: every record is accepted whole or rejected whole. There
// is exactly one byte pattern per intent, so every reserved field, every pad byte,
// and every unused trailing slot must be zero.

static bool bytes_all_zero(const void* base, size_t bytes) {
    const uint8_t* cursor = static_cast<const uint8_t*>(base);
    for (size_t i = 0u; i < bytes; ++i) {
        if (cursor[i] != 0u) return false;
    }
    return true;
}

int32_t sim_ledger_add_saturating(int32_t total, uint32_t amount) {
    if (total < 0) return total;
    uint32_t headroom = static_cast<uint32_t>(INT32_MAX) - static_cast<uint32_t>(total);
    if (amount >= headroom) return INT32_MAX;
    return total + static_cast<int32_t>(amount);
}

bool sim_objective_def_valid(const SimObjectiveDef* def, uint16_t team_count) {
    if (!def) return false;
    if (def->kind != SIM_OBJECTIVE_TOWER && def->kind != SIM_OBJECTIVE_CORE) return false;
    if (def->owner_team >= team_count) return false;
    if (def->max_health <= 0 || def->armor < 0) return false;
    if (def->target_policy != SIM_TARGET_POLICY_NONE &&
        def->target_policy != SIM_TARGET_POLICY_TOWER) return false;
    if (def->reserved != 0u) return false;
    return bytes_all_zero(def->pad, sizeof(def->pad));
}

bool sim_economy_rule_valid(const SimEconomyRule* rule) {
    if (!rule) return false;
    if (rule->award_kind != SIM_AWARD_GOLD && rule->award_kind != SIM_AWARD_XP) return false;
    if (rule->source_kind != SIM_AWARD_SOURCE_HERO_KILL &&
        rule->source_kind != SIM_AWARD_SOURCE_MINION_KILL &&
        rule->source_kind != SIM_AWARD_SOURCE_OBJECTIVE_DAMAGE &&
        rule->source_kind != SIM_AWARD_SOURCE_OBJECTIVE_DESTROYED) return false;
    return rule->reserved == 0u;
}

bool sim_minion_def_valid(const SimMinionDef* def) {
    return def && def->max_health > 0 && def->move_speed > 0 && def->attack_range > 0 &&
           def->attack_magnitude > 0 && def->attack_cooldown_ticks > 0u &&
           def->reserved == 0u && def->reserved2 == 0u;
}

bool sim_match_def_is_absent(const SimMatchDef* def) {
    return def && bytes_all_zero(def, sizeof(*def));
}

static bool team_defs_valid(const SimMatchDef* def) {
    for (uint16_t i = 0u; i < def->team_count; ++i) {
        const SimTeamDef& team = def->teams[i];
        if (team.team_id != static_cast<uint8_t>(i)) return false;
        if (team.waypoint_reverse > 1u) return false;
        if (!bytes_all_zero(team.pad, sizeof(team.pad))) return false;
        if (team.tower_objective_index >= def->objective_count ||
            team.core_objective_index >= def->objective_count) return false;
        const SimObjectiveDef& tower = def->objectives[team.tower_objective_index];
        const SimObjectiveDef& core = def->objectives[team.core_objective_index];
        if (tower.owner_team != team.team_id || tower.kind != SIM_OBJECTIVE_TOWER) return false;
        if (core.owner_team != team.team_id || core.kind != SIM_OBJECTIVE_CORE) return false;
    }
    // One lane in the fixture, two distinct spawns, and the two sides walk it in
    // opposite directions - otherwise both waves would start at the same end.
    if (def->teams[0].lane_id != def->teams[1].lane_id) return false;
    if (def->teams[0].spawn_ordinal == def->teams[1].spawn_ordinal) return false;
    if (def->teams[0].waypoint_reverse == def->teams[1].waypoint_reverse) return false;

    uint16_t referenced[4] = {def->teams[0].tower_objective_index,
                              def->teams[0].core_objective_index,
                              def->teams[1].tower_objective_index,
                              def->teams[1].core_objective_index};
    for (uint32_t i = 0u; i < 4u; ++i) {
        for (uint32_t j = i + 1u; j < 4u; ++j) {
            if (referenced[i] == referenced[j]) return false;
        }
    }
    return true;
}

bool sim_match_def_valid(const SimMatchDef* def) {
    if (!def) return false;
    if (def->team_count == 0u) return sim_match_def_is_absent(def);
    if (def->team_count != SIM_MAX_TEAMS) return false;
    if (def->objective_count > SIM_MAX_OBJECTIVES ||
        def->economy_count > SIM_MAX_ECONOMY_RULES ||
        def->minions_per_wave > SIM_MAX_MINIONS_PER_WAVE) return false;
    if (static_cast<uint32_t>(def->objective_count) <
        2u * static_cast<uint32_t>(def->team_count)) return false;
    if (!sim_minion_def_valid(&def->creep)) return false;

    for (uint16_t i = 0u; i < def->objective_count; ++i) {
        if (!sim_objective_def_valid(&def->objectives[i], def->team_count)) return false;
    }
    if (!team_defs_valid(def)) return false;

    for (uint16_t i = 0u; i < def->economy_count; ++i) {
        if (!sim_economy_rule_valid(&def->economy[i])) return false;
        // A duplicate (award_kind, source_kind) pair would make rule order the only
        // thing separating two awards, so a re-order would be a silent behaviour
        // change. Rejected outright.
        for (uint16_t j = 0u; j < i; ++j) {
            if (def->economy[j].award_kind == def->economy[i].award_kind &&
                def->economy[j].source_kind == def->economy[i].source_kind) return false;
        }
    }

    for (uint16_t i = def->team_count; i < SIM_MAX_TEAMS; ++i) {
        if (!bytes_all_zero(&def->teams[i], sizeof(SimTeamDef))) return false;
    }
    for (uint16_t i = def->objective_count; i < SIM_MAX_OBJECTIVES; ++i) {
        if (!bytes_all_zero(&def->objectives[i], sizeof(SimObjectiveDef))) return false;
    }
    for (uint16_t i = def->economy_count; i < SIM_MAX_ECONOMY_RULES; ++i) {
        if (!bytes_all_zero(&def->economy[i], sizeof(SimEconomyRule))) return false;
    }
    return true;
}

bool sim_match_lane_index(const SimMatchDef* def, const MapGrid* map, uint8_t team_id,
                          uint16_t* out_lane_index) {
    if (!def || !map || !out_lane_index || team_id >= def->team_count || !map->lanes) return false;
    for (uint16_t lane = 0u; lane < map->lane_count; ++lane) {
        if (map->lanes[lane].lane_id == def->teams[team_id].lane_id) {
            *out_lane_index = lane;
            return true;
        }
    }
    return false;
}

static bool objective_cell_ok(const MapGrid* map, uint32_t cell) {
    return cell < map->cell_count && (map_flags(map, cell) & MAP_CELL_OBJECTIVE) != 0u;
}

bool sim_match_def_valid_against_map(const SimMatchDef* def, const MapGrid* map) {
    if (!sim_match_def_valid(def) || !map) return false;
    if (def->team_count == 0u) return true;

    uint32_t spawn_cells[SIM_MAX_TEAMS] = {0u, 0u};
    for (uint16_t team = 0u; team < def->team_count; ++team) {
        uint16_t lane_index = 0u;
        if (!sim_match_lane_index(def, map, static_cast<uint8_t>(team), &lane_index)) return false;
        const MapLane& lane = map->lanes[lane_index];
        if (lane.creep_archetype_id != def->creep.archetype_id) return false;
        if (lane.waypoint_count < 2u) return false;
        if (map_find_spawn(map, def->teams[team].spawn_ordinal, &spawn_cells[team]) !=
            MAP_STATUS_OK) return false;
        if (!objective_cell_ok(map, def->teams[team].tower_cell) ||
            !objective_cell_ok(map, def->teams[team].core_cell)) return false;
    }
    return spawn_cells[0] != spawn_cells[1];
}

bool sim_install_match_def(SimWorld* world, const SimMatchDef* def) {
    if (!world || !def) return false;
    if (!sim_match_def_valid_against_map(def, &world->map)) return false;
    world->match = *def;
    return true;
}
