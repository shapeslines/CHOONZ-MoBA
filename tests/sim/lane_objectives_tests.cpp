#include "test.h"
#include "sim/combat.h"
#include "sim/command.h"
#include "sim/match.h"
#include "sim/objectives.h"
#include "sim/replay.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"
#include "sim/systems.h"
#include "sim/team.h"

#include <cstring>

// M5.3 lane objectives (m5-lane-objectives). The sim owns a POD mirror of
// proto-design sections 3.2 and 3.3, so this suite cross-checks the mirror against
// that text field by field, then exercises the bounded pools, the match validators,
// creep waves, the minion FSM, tower fire, the generalized death verdict, the gold
// and XP ledgers, and the core-destruction win.

static const size_t LANE_WORLD_BYTES = 512u * 1024u;
alignas(16) static uint8_t g_lane_storage[LANE_WORLD_BYTES];
alignas(16) static uint8_t g_lane_storage_b[LANE_WORLD_BYTES];

// MSVC /WX flags a compile-time constant inside an if (C4127), and CHECK is an if.
// Routing layout constants through a function keeps the assertion a runtime one.
static size_t sz(size_t value) { return value; }
static uint64_t u64(uint64_t value) { return value; }
static uint32_t u32(uint32_t value) { return value; }

static const uint64_t LANE_SLICE_MAP_ID = 0x00000000005ACE01ULL;
static const uint64_t LANE_SLICE_ARCHETYPE = 0x00BADC0DE0000001ULL;
static const uint8_t LANE_SLICE_LANE_ID = 1u;
static const uint16_t LANE_SLICE_WAVE_INTERVAL = 20u;
static const uint32_t LANE_SLICE_FIRST_WAVE = 5u;

// One straight lane along row y = 3 with a spawn at each end, and four objective
// cells on row y = 2: each team's core at the outer column and its tower two cells
// in. Cell size is 1.0 so a waypoint step is exactly one world unit.
static const char LANE_SLICE_ROWS[8][9] = {
    "........",
    "........",
    "O.O..O.O",
    "SLLLLLLS",
    "........",
    "........",
    "........",
    "........",
};

static const uint32_t LANE_SLICE_TOWER_CELL[2] = {18u, 21u};
static const uint32_t LANE_SLICE_CORE_CELL[2] = {16u, 23u};
static const uint16_t LANE_SLICE_WAYPOINTS[8] = {24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u};

static uint8_t lane_slice_flags(char c) {
    switch (c) {
        case '.': return MAP_CELL_WALKABLE;
        case 'L': return MAP_CELL_WALKABLE | MAP_CELL_LANE;
        case 'S': return MAP_CELL_WALKABLE | MAP_CELL_LANE | MAP_CELL_SPAWN;
        case 'O': return MAP_CELL_WALKABLE | MAP_CELL_OBJECTIVE;
        default: return 0u;
    }
}

static bool author_lane_slice(MapGrid* grid) {
    if (map_set_dimensions(grid, LANE_SLICE_MAP_ID, 8u, 8u, FIX_ONE) != MAP_STATUS_OK)
        return false;
    for (uint16_t y = 0u; y < 8u; ++y) {
        for (uint16_t x = 0u; x < 8u; ++x) {
            if (map_set_cell(grid, x, y, lane_slice_flags(LANE_SLICE_ROWS[y][x]), 1u, 0) !=
                MAP_STATUS_OK) return false;
        }
    }
    MapLane lane{};
    lane.lane_id = LANE_SLICE_LANE_ID;
    lane.waypoint_count = 8u;
    lane.wave_interval_ticks = LANE_SLICE_WAVE_INTERVAL;
    lane.first_wave_tick = LANE_SLICE_FIRST_WAVE;
    lane.creep_archetype_id = LANE_SLICE_ARCHETYPE;
    return map_add_lane(grid, &lane, LANE_SLICE_WAYPOINTS) == MAP_STATUS_OK;
}

static SimObjectiveDef objective_def(uint64_t id, uint8_t owner_team, uint8_t kind,
                                     int32_t max_health, uint8_t policy) {
    SimObjectiveDef def{};
    def.objective_id = id;
    def.owner_team = owner_team;
    def.kind = kind;
    def.max_health = max_health;
    def.target_policy = policy;
    return def;
}

static SimMatchDef fixture_match_def(void) {
    SimMatchDef def{};
    def.team_count = 2u;
    def.objective_count = 4u;
    def.economy_count = 4u;
    def.minions_per_wave = 2u;

    def.creep.archetype_id = LANE_SLICE_ARCHETYPE;
    def.creep.max_health = 10;
    def.creep.move_speed = FIX_ONE / 2;
    def.creep.attack_range = FIX_ONE;
    def.creep.attack_magnitude = 2;
    def.creep.attack_cooldown_ticks = 4u;

    def.objectives[0] = objective_def(0xB0ULL, 0u, SIM_OBJECTIVE_TOWER, 30,
                                      SIM_TARGET_POLICY_TOWER);
    def.objectives[1] = objective_def(0xB1ULL, 0u, SIM_OBJECTIVE_CORE, 60,
                                      SIM_TARGET_POLICY_NONE);
    def.objectives[2] = objective_def(0xB2ULL, 1u, SIM_OBJECTIVE_TOWER, 30,
                                      SIM_TARGET_POLICY_TOWER);
    def.objectives[3] = objective_def(0xB3ULL, 1u, SIM_OBJECTIVE_CORE, 60,
                                      SIM_TARGET_POLICY_NONE);

    def.teams[0].team_id = 0u;
    def.teams[0].lane_id = LANE_SLICE_LANE_ID;
    def.teams[0].spawn_ordinal = 0u;
    def.teams[0].tower_objective_index = 0u;
    def.teams[0].core_objective_index = 1u;
    def.teams[0].tower_cell = LANE_SLICE_TOWER_CELL[0];
    def.teams[0].core_cell = LANE_SLICE_CORE_CELL[0];
    def.teams[0].waypoint_reverse = 0u;

    def.teams[1].team_id = 1u;
    def.teams[1].lane_id = LANE_SLICE_LANE_ID;
    def.teams[1].spawn_ordinal = 1u;
    def.teams[1].tower_objective_index = 2u;
    def.teams[1].core_objective_index = 3u;
    def.teams[1].tower_cell = LANE_SLICE_TOWER_CELL[1];
    def.teams[1].core_cell = LANE_SLICE_CORE_CELL[1];
    def.teams[1].waypoint_reverse = 1u;

    def.economy[0].award_kind = SIM_AWARD_GOLD;
    def.economy[0].source_kind = SIM_AWARD_SOURCE_MINION_KILL;
    def.economy[0].award_amount = 20u;
    def.economy[1].award_kind = SIM_AWARD_XP;
    def.economy[1].source_kind = SIM_AWARD_SOURCE_MINION_KILL;
    def.economy[1].award_amount = 15u;
    def.economy[2].award_kind = SIM_AWARD_GOLD;
    def.economy[2].source_kind = SIM_AWARD_SOURCE_OBJECTIVE_DESTROYED;
    def.economy[2].award_amount = 100u;
    def.economy[3].award_kind = SIM_AWARD_GOLD;
    def.economy[3].source_kind = SIM_AWARD_SOURCE_HERO_KILL;
    def.economy[3].award_amount = 300u;
    return def;
}

static SimWorldConfig lane_config(void) {
    SimWorldConfig config{};
    config.max_entities = 64u;
    config.initial_unit_count = 0u;
    config.damage_event_capacity = 32u;
    config.map = MapConfig{8u, 8u, 2u, 8u};
    config.sim_event_capacity = 64u;
    config.team_capacity = 40u;
    config.minion_capacity = 24u;
    config.objective_capacity = 8u;
    return config;
}

struct LaneFixture {
    SimWorld world;
    size_t arena_offset;
};

static bool build_lane_world(LaneFixture* fixture, uint8_t* storage, size_t storage_bytes,
                             SimWorldConfig config) {
    Arena arena;
    arena_init_fixed(&arena, storage, storage_bytes);
    if (!sim_init(&fixture->world, &arena, 23u, config)) return false;
    fixture->arena_offset = arena.offset;
    if (map_config_is_empty(config.map)) return true;
    return author_lane_slice(&fixture->world.map);
}

static bool build_match_world(LaneFixture* fixture, uint8_t* storage, size_t storage_bytes) {
    if (!build_lane_world(fixture, storage, storage_bytes, lane_config())) return false;
    SimMatchDef def = fixture_match_def();
    return sim_install_match_def(&fixture->world, &def) &&
           sim_spawn_match_objectives(&fixture->world);
}

// ------------------------------------------------------------------ S1 records

TEST(sim_lane_objectives, mirror_matches_proto_design_sections_3_2_and_3_3) {
    // ObjectiveDef: objective_id, owner_team, kind, reserved, max_health, armor,
    // target_policy, pad -- every reserved byte explicit, no implicit tail padding.
    CHECK(sz(sizeof(SimObjectiveDef)) == 24u);
    CHECK(sz(offsetof(SimObjectiveDef, objective_id)) == 0u);
    CHECK(sz(offsetof(SimObjectiveDef, owner_team)) == 8u);
    CHECK(sz(offsetof(SimObjectiveDef, kind)) == 9u);
    CHECK(sz(offsetof(SimObjectiveDef, reserved)) == 10u);
    CHECK(sz(offsetof(SimObjectiveDef, max_health)) == 12u);
    CHECK(sz(offsetof(SimObjectiveDef, armor)) == 16u);
    CHECK(sz(offsetof(SimObjectiveDef, target_policy)) == 20u);
    CHECK(sz(offsetof(SimObjectiveDef, pad)) == 21u);

    // EconomyRule: award_kind, source_kind, reserved, award_amount.
    CHECK(sz(sizeof(SimEconomyRule)) == 8u);
    CHECK(sz(offsetof(SimEconomyRule, award_kind)) == 0u);
    CHECK(sz(offsetof(SimEconomyRule, source_kind)) == 1u);
    CHECK(sz(offsetof(SimEconomyRule, reserved)) == 2u);
    CHECK(sz(offsetof(SimEconomyRule, award_amount)) == 4u);

    CHECK(sz(sizeof(SimMinionDef)) == 32u);
    CHECK(sz(offsetof(SimMinionDef, archetype_id)) == 0u);
    CHECK(sz(offsetof(SimMinionDef, max_health)) == 8u);
    CHECK(sz(offsetof(SimMinionDef, move_speed)) == 12u);
    CHECK(sz(offsetof(SimMinionDef, attack_range)) == 16u);
    CHECK(sz(offsetof(SimMinionDef, attack_magnitude)) == 20u);
    CHECK(sz(offsetof(SimMinionDef, attack_cooldown_ticks)) == 24u);
    CHECK(sz(offsetof(SimMinionDef, reserved)) == 26u);
    CHECK(sz(offsetof(SimMinionDef, reserved2)) == 28u);

    CHECK(sz(sizeof(SimTeamDef)) == 20u);
    CHECK(sz(offsetof(SimTeamDef, team_id)) == 0u);
    CHECK(sz(offsetof(SimTeamDef, lane_id)) == 1u);
    CHECK(sz(offsetof(SimTeamDef, spawn_ordinal)) == 2u);
    CHECK(sz(offsetof(SimTeamDef, tower_objective_index)) == 4u);
    CHECK(sz(offsetof(SimTeamDef, core_objective_index)) == 6u);
    CHECK(sz(offsetof(SimTeamDef, tower_cell)) == 8u);
    CHECK(sz(offsetof(SimTeamDef, core_cell)) == 12u);
    CHECK(sz(offsetof(SimTeamDef, waypoint_reverse)) == 16u);
    CHECK(sz(offsetof(SimTeamDef, pad)) == 17u);

    CHECK(sz(sizeof(SimMatchDef)) == 336u);
    CHECK(sz(offsetof(SimMatchDef, team_count)) == 0u);
    CHECK(sz(offsetof(SimMatchDef, objective_count)) == 2u);
    CHECK(sz(offsetof(SimMatchDef, economy_count)) == 4u);
    CHECK(sz(offsetof(SimMatchDef, minions_per_wave)) == 6u);
    CHECK(sz(offsetof(SimMatchDef, creep)) == 8u);
    CHECK(sz(offsetof(SimMatchDef, teams)) == 40u);
    CHECK(sz(offsetof(SimMatchDef, objectives)) == 80u);
    CHECK(sz(offsetof(SimMatchDef, economy)) == 272u);

    CHECK(sz(sizeof(SimLedger)) == 16u);
    CHECK(sz(sizeof(SimMatchState)) == 8u);
    CHECK(sz(offsetof(SimMatchState, over)) == 0u);
    CHECK(sz(offsetof(SimMatchState, winner)) == 1u);
    CHECK(sz(offsetof(SimMatchState, reserved)) == 2u);
    CHECK(sz(offsetof(SimMatchState, end_tick)) == 4u);
}

TEST(sim_lane_objectives, the_absent_match_has_exactly_one_byte_pattern) {
    SimMatchDef absent{};
    CHECK(sim_match_def_is_absent(&absent));
    CHECK(sim_match_def_valid(&absent));

    // team_count 0 with any other byte set is not "absent" and not valid either.
    SimMatchDef littered = absent;
    littered.minions_per_wave = 1u;
    CHECK(!sim_match_def_is_absent(&littered));
    CHECK(!sim_match_def_valid(&littered));

    littered = absent;
    littered.objectives[7].max_health = 1;
    CHECK(!sim_match_def_valid(&littered));
}

TEST(sim_lane_objectives, every_validity_rule_rejects_its_own_violation) {
    SimMatchDef base = fixture_match_def();
    CHECK(sim_match_def_valid(&base));

    SimMatchDef bad = base;
    bad.team_count = 1u;
    CHECK(!sim_match_def_valid(&bad));                       // team_count is 0 or exactly 2

    bad = base;
    bad.objective_count = 3u;
    CHECK(!sim_match_def_valid(&bad));                       // objective_count >= 2 * team_count

    bad = base;
    bad.economy_count = static_cast<uint16_t>(SIM_MAX_ECONOMY_RULES + 1u);
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.minions_per_wave = static_cast<uint16_t>(SIM_MAX_MINIONS_PER_WAVE + 1u);
    CHECK(!sim_match_def_valid(&bad));

    bad = base;
    bad.teams[1].team_id = 0u;
    CHECK(!sim_match_def_valid(&bad));                       // team_id must equal its index
    bad = base;
    bad.teams[1].lane_id = 2u;
    CHECK(!sim_match_def_valid(&bad));                       // one lane in the fixture
    bad = base;
    bad.teams[1].spawn_ordinal = 0u;
    CHECK(!sim_match_def_valid(&bad));                       // the two spawns must differ
    bad = base;
    bad.teams[1].waypoint_reverse = 0u;
    CHECK(!sim_match_def_valid(&bad));                       // the two directions must differ
    bad = base;
    bad.teams[0].waypoint_reverse = 2u;
    CHECK(!sim_match_def_valid(&bad));                       // 0 or 1 only
    bad = base;
    bad.teams[0].pad[2] = 1u;
    CHECK(!sim_match_def_valid(&bad));

    bad = base;
    bad.teams[1].tower_objective_index = 4u;
    CHECK(!sim_match_def_valid(&bad));                       // < objective_count
    bad = base;
    bad.teams[1].core_objective_index = 2u;                  // same slot as its tower
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.objectives[2].owner_team = 0u;                       // team 1's tower owned by team 0
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.objectives[3].kind = SIM_OBJECTIVE_TOWER;            // core slot naming a tower
    CHECK(!sim_match_def_valid(&bad));

    bad = base;
    bad.objectives[0].kind = SIM_OBJECTIVE_NONE;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.objectives[0].max_health = 0;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.objectives[0].armor = -1;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.objectives[0].target_policy = 9u;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.objectives[0].reserved = 1u;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.objectives[0].pad[0] = 1u;
    CHECK(!sim_match_def_valid(&bad));

    bad = base;
    bad.economy[1].award_kind = SIM_AWARD_NONE;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.economy[1].source_kind = SIM_AWARD_SOURCE_NONE;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.economy[1].reserved = 1u;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.economy[1] = bad.economy[0];                         // duplicate (kind, source) pair
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.economy[1].award_amount = 0u;                        // an award of zero is legal
    CHECK(sim_match_def_valid(&bad));

    bad = base;
    bad.creep.max_health = 0;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.creep.move_speed = 0;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.creep.attack_range = 0;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.creep.attack_magnitude = 0;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.creep.attack_cooldown_ticks = 0u;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.creep.reserved2 = 1u;
    CHECK(!sim_match_def_valid(&bad));

    // Trailing unused slots must be all zero bytes.
    bad = base;
    bad.objectives[5].max_health = 1;
    CHECK(!sim_match_def_valid(&bad));
    bad = base;
    bad.economy[6].award_amount = 1u;
    CHECK(!sim_match_def_valid(&bad));
}

TEST(sim_lane_objectives, install_checks_the_def_against_the_map_and_rejects_whole) {
    LaneFixture fixture{};
    CHECK(build_lane_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimMatchDef def = fixture_match_def();
    CHECK(sim_match_def_valid_against_map(&def, &fixture.world.map));

    SimMatchDef bad = def;
    bad.creep.archetype_id = 0x1ULL;                         // lane archetype disagreement
    CHECK(!sim_match_def_valid_against_map(&bad, &fixture.world.map));
    CHECK(!sim_install_match_def(&fixture.world, &bad));
    CHECK(u32(fixture.world.match.team_count) == 0u);        // rejection mutates nothing

    bad = def;
    bad.teams[0].tower_cell = 0u;                            // not MAP_CELL_OBJECTIVE
    CHECK(!sim_install_match_def(&fixture.world, &bad));
    bad = def;
    bad.teams[0].core_cell = 999u;                           // out of bounds
    CHECK(!sim_install_match_def(&fixture.world, &bad));
    bad = def;
    bad.teams[1].spawn_ordinal = 7u;                         // no such spawn
    CHECK(!sim_install_match_def(&fixture.world, &bad));

    CHECK(sim_install_match_def(&fixture.world, &def));
    CHECK(u32(fixture.world.match.team_count) == 2u);
    CHECK(fixture.world.match.creep.archetype_id == LANE_SLICE_ARCHETYPE);
    uint16_t lane_index = 99u;
    CHECK(sim_match_lane_index(&fixture.world.match, &fixture.world.map, 1u, &lane_index));
    CHECK(u32(lane_index) == 0u);
}

TEST(sim_lane_objectives, zero_capacity_leaves_every_new_pool_absent) {
    SimWorldConfig defaults = sim_world_config_default();
    CHECK(u32(defaults.team_capacity) == 0u);
    CHECK(u32(defaults.minion_capacity) == 0u);
    CHECK(u32(defaults.objective_capacity) == 0u);

    LaneFixture fixture{};
    SimWorldConfig bare{};
    bare.max_entities = 32u;
    bare.initial_unit_count = 4u;
    bare.damage_event_capacity = 8u;
    CHECK(build_lane_world(&fixture, g_lane_storage, sizeof(g_lane_storage), bare));
    CHECK(u32(fixture.world.teams.membership.capacity) == 0u);
    CHECK(u32(fixture.world.minions.membership.capacity) == 0u);
    CHECK(u32(fixture.world.objectives.membership.capacity) == 0u);
    CHECK(!fixture.world.teams.team);
    CHECK(!fixture.world.minions.lane);
    CHECK(!fixture.world.objectives.def_index);
    CHECK(sim_match_def_is_absent(&fixture.world.match));
    CHECK(u32(fixture.world.match_state.over) == 0u);
    CHECK(u32(fixture.world.match_state.winner) == SIM_TEAM_NONE);
    CHECK(fixture.world.ledger.gold[0] == 0 && fixture.world.ledger.xp[1] == 0);

    // A world with no match ticks exactly as it did before this slice, and no
    // objective can be spawned into it.
    uint64_t before = sim_hash_state(&fixture.world);
    CHECK(before != 0u);
    CHECK(sim_spawn_match_objectives(&fixture.world));       // nothing to create
    CHECK(!sim_spawn_objective(&fixture.world, 0u, 16u, nullptr));
    CHECK(sim_hash_state(&fixture.world) == before);
    CHECK(sim_tick(&fixture.world, nullptr));

    // Requesting minions or objectives without a team pool is a configuration
    // mistake, not a valid empty state.
    SimWorldConfig broken = bare;
    broken.minion_capacity = 4u;
    CHECK(sim_world_memory_required(broken) == 0u);
    broken = bare;
    broken.objective_capacity = 4u;
    CHECK(sim_world_memory_required(broken) == 0u);
    broken = bare;
    broken.team_capacity = 4u;
    broken.minion_capacity = 4u;
    CHECK(sim_world_memory_required(broken) == 0u);          // no SimEvent queue
}

TEST(sim_lane_objectives, team_pool_holds_one_side_byte_per_entity) {
    LaneFixture fixture{};
    CHECK(build_lane_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimWorld& world = fixture.world;

    EntityId a = entity_manager_create(&world.entities);
    EntityId b = entity_manager_create(&world.entities);
    CHECK(team_pool_add(&world.teams, a, 0u));
    CHECK(team_pool_add(&world.teams, b, 1u));
    CHECK(!team_pool_add(&world.teams, a, 0u));              // duplicate
    CHECK(!team_pool_add(&world.teams, entity_manager_create(&world.entities), 5u));

    uint8_t team = 9u;
    CHECK(team_pool_team(&world.teams, a, &team));
    CHECK(u32(team) == 0u);
    CHECK(team_is_enemy(&world.teams, a, b));
    CHECK(!team_is_enemy(&world.teams, a, a));

    TeamView view{};
    CHECK(team_pool_get(&world.teams, b, &view));
    *view.team = SIM_TEAM_NONE;
    CHECK(!team_is_enemy(&world.teams, a, b));               // NONE fights nobody
    CHECK(team_pool_remove(&world.teams, b));
    CHECK(!team_pool_has(&world.teams, b));
    CHECK(!team_pool_team(&world.teams, b, &team));
}

TEST(sim_lane_objectives, minion_and_objective_pools_swap_remove_without_leaking_rows) {
    LaneFixture fixture{};
    CHECK(build_lane_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimWorld& world = fixture.world;

    EntityId a = entity_manager_create(&world.entities);
    EntityId b = entity_manager_create(&world.entities);
    CHECK(minion_pool_add(&world.minions, a, 1u, 0u));
    CHECK(minion_pool_add(&world.minions, b, 1u, 7u));
    MinionView minion{};
    CHECK(minion_pool_get(&world.minions, b, &minion));
    CHECK(u32(*minion.waypoint_index) == 7u);
    CHECK(u32(*minion.state) == SIM_MINION_PUSH);
    CHECK(minion.target->h == HANDLE_NULL);
    *minion.state = SIM_MINION_ATTACK;
    *minion.target = a;
    CHECK(minion_pool_remove(&world.minions, a));            // swap-remove moves b down
    CHECK(minion_pool_get(&world.minions, b, &minion));
    CHECK(u32(*minion.waypoint_index) == 7u);
    CHECK(u32(*minion.state) == SIM_MINION_ATTACK);
    CHECK(minion.target->h == a.h);

    CHECK(objective_pool_add(&world.objectives, a, 0u, 0u, SIM_OBJECTIVE_TOWER));
    CHECK(!objective_pool_add(&world.objectives, b, 0u, 0u, SIM_OBJECTIVE_NONE));
    CHECK(!objective_pool_add(&world.objectives, b, 0u, SIM_MAX_TEAMS, SIM_OBJECTIVE_CORE));
    ObjectiveView objective{};
    CHECK(objective_pool_get(&world.objectives, a, &objective));
    CHECK(u32(*objective.state) == SIM_OBJECTIVE_ALIVE);
    CHECK(u32(*objective.kind) == SIM_OBJECTIVE_TOWER);
    CHECK(u32(*objective.attack_cooldown) == 0u);
    CHECK(objective_pool_remove(&world.objectives, a));
    CHECK(!objective_pool_has(&world.objectives, a));
}

TEST(sim_lane_objectives, health_rows_carry_a_cleared_kill_credit_slot) {
    LaneFixture fixture{};
    CHECK(build_lane_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimWorld& world = fixture.world;

    EntityId a = entity_manager_create(&world.entities);
    EntityId b = entity_manager_create(&world.entities);
    CHECK(health_pool_add(&world.health, a, 10, 10, 0u));
    CHECK(health_pool_add(&world.health, b, 10, 10, 0u));
    HealthView health{};
    CHECK(health_pool_get(&world.health, a, &health));
    CHECK(health.last_damage_source->h == HANDLE_NULL);
    *health.last_damage_source = b;

    // A recycled row must never inherit a stale killer.
    CHECK(health_pool_remove(&world.health, a));
    CHECK(health_pool_add(&world.health, a, 10, 10, 0u));
    CHECK(health_pool_get(&world.health, a, &health));
    CHECK(health.last_damage_source->h == HANDLE_NULL);
}

TEST(sim_lane_objectives, installed_match_instantiates_one_tower_and_core_per_team) {
    LaneFixture fixture{};
    CHECK(build_match_world(&fixture, g_lane_storage, sizeof(g_lane_storage)));
    SimWorld& world = fixture.world;
    CHECK(u32(world.objectives.membership.count) == 4u);
    CHECK(u32(world.teams.membership.count) == 4u);

    ComponentPoolOrderedView ordered{};
    CHECK(component_pool_ordered_view(&world.objectives.membership, &ordered));
    CHECK(u32(ordered.count) == 4u);
    static const uint16_t expected_def[4] = {0u, 1u, 2u, 3u};
    static const uint32_t expected_cell[4] = {18u, 16u, 21u, 23u};
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        EntityId entity = ordered.entities[i];
        ObjectiveView objective{};
        HealthView health{};
        ConstTransformView transform{};
        uint8_t team = SIM_TEAM_NONE;
        CHECK(objective_pool_get(&world.objectives, entity, &objective));
        CHECK(health_pool_get(&world.health, entity, &health));
        CHECK(transform_pool_get_const(&world.transforms, entity, &transform));
        CHECK(team_pool_team(&world.teams, entity, &team));
        CHECK(u32(*objective.def_index) == expected_def[i]);
        CHECK(u32(*objective.owner_team) == u32(world.match.objectives[expected_def[i]].owner_team));
        CHECK(u32(team) == u32(*objective.owner_team));
        CHECK(u32(*objective.kind) == u32(world.match.objectives[expected_def[i]].kind));
        CHECK(*health.current == world.match.objectives[expected_def[i]].max_health);
        mm::fix cell_x = 0;
        mm::fix cell_y = 0;
        CHECK(map_cell_to_world(&world.map, expected_cell[i], &cell_x, &cell_y) == MAP_STATUS_OK);
        CHECK(*transform.position_x == cell_x);
        CHECK(*transform.position_y == cell_y);
        // A tower never moves, so it never carries a Velocity row.
        CHECK(!velocity_pool_has(&world.velocities, entity));
    }
}

TEST(sim_lane_objectives, ledger_awards_saturate_instead_of_wrapping) {
    CHECK(sim_ledger_add_saturating(0, 5u) == 5);
    CHECK(sim_ledger_add_saturating(INT32_MAX, 0u) == INT32_MAX);
    CHECK(sim_ledger_add_saturating(INT32_MAX - 4, 4u) == INT32_MAX);
    CHECK(sim_ledger_add_saturating(INT32_MAX - 4, 5u) == INT32_MAX);
    CHECK(sim_ledger_add_saturating(INT32_MAX - 4, 4000000000u) == INT32_MAX);
}

TEST(sim_lane_objectives, the_wave_clock_is_the_authored_lane_clock) {
    LaneFixture fixture{};
    CHECK(build_match_world(&fixture, g_lane_storage_b, sizeof(g_lane_storage_b)));
    SimWorld& world = fixture.world;
    for (uint64_t tick = 0u; tick < 3u * LANE_SLICE_WAVE_INTERVAL; ++tick) {
        world.tick = tick;
        bool due = tick >= LANE_SLICE_FIRST_WAVE &&
                   (tick - LANE_SLICE_FIRST_WAVE) % LANE_SLICE_WAVE_INTERVAL == 0u;
        CHECK(sim_wave_tick_due(&world) == due);
    }
    world.tick = 0u;
    CHECK(u64(SIM_LOGIC_HASH) == 0x46e9e287878ba88cULL);
}
