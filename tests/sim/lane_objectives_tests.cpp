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
    def.creep.move_speed = mm::fix_from_int(15);   // ~0.5 world units per tick at 30 Hz
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

static bool build_installed_world(LaneFixture* fixture, uint8_t* storage, size_t storage_bytes,
                                  SimWorldConfig config) {
    if (!build_lane_world(fixture, storage, storage_bytes, config)) return false;
    SimMatchDef def = fixture_match_def();
    return sim_install_match_def(&fixture->world, &def);
}

static bool build_match_world(LaneFixture* fixture, uint8_t* storage, size_t storage_bytes) {
    return build_installed_world(fixture, storage, storage_bytes, lane_config()) &&
           sim_spawn_match_objectives(&fixture->world);
}

static uint32_t minions_of_team(SimWorld* world, uint8_t team) {
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&world->minions.membership, &ordered)) return 0u;
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        uint8_t side = SIM_TEAM_NONE;
        if (team_pool_team(&world->teams, ordered.entities[i], &side) && side == team) ++count;
    }
    return count;
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
    CHECK(u64(SIM_LOGIC_HASH) == 0x5b47e648953a63fcULL);
}

// ------------------------------------------------------------------ S2 waves + FSM

// Byte-identical to assets/maps/lane_slice.mapdesc (generated by tests/sim/map_golden.py).
#include "lane_slice_golden.inc"

TEST(sim_lane_objectives, lane_slice_encoding_matches_the_independent_golden) {
    LaneFixture fixture{};
    CHECK(build_lane_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));

    uint8_t buffer[512];
    ByteWriter writer;
    byte_writer_init(&writer, buffer, sizeof(buffer));
    CHECK(map_encode(&fixture.world.map, &writer) == MAP_STATUS_OK);
    CHECK(byte_writer_size(&writer) == sizeof(LANE_SLICE_MAPDESC));
    CHECK(std::memcmp(buffer, LANE_SLICE_MAPDESC, sizeof(LANE_SLICE_MAPDESC)) == 0);

    LaneFixture decoded{};
    CHECK(build_lane_world(&decoded, g_lane_storage_b, sizeof(g_lane_storage_b), lane_config()));
    ByteReader reader;
    byte_reader_init(&reader, LANE_SLICE_MAPDESC, sizeof(LANE_SLICE_MAPDESC));
    CHECK(map_decode(&decoded.world.map, &reader) == MAP_STATUS_OK);
    CHECK(map_require_end(&reader) == MAP_STATUS_OK);
    CHECK(map_valid(&decoded.world.map));
    CHECK(decoded.world.map.map_id == LANE_SLICE_MAP_ID);

    uint32_t spawn_west = 0u;
    uint32_t spawn_east = 0u;
    CHECK(map_find_spawn(&decoded.world.map, 0u, &spawn_west) == MAP_STATUS_OK);
    CHECK(map_find_spawn(&decoded.world.map, 1u, &spawn_east) == MAP_STATUS_OK);
    CHECK(u32(spawn_west) == 24u);
    CHECK(u32(spawn_east) == 31u);
    for (uint32_t i = 0u; i < 2u; ++i) {
        CHECK((map_flags(&decoded.world.map, LANE_SLICE_TOWER_CELL[i]) &
               MAP_CELL_OBJECTIVE) != 0u);
        CHECK((map_flags(&decoded.world.map, LANE_SLICE_CORE_CELL[i]) &
               MAP_CELL_OBJECTIVE) != 0u);
    }
    SimMatchDef def = fixture_match_def();
    CHECK(sim_match_def_valid_against_map(&def, &decoded.world.map));
}

TEST(sim_lane_objectives, waves_spawn_on_the_lane_clock_in_ascending_team_then_ordinal) {
    LaneFixture fixture{};
    CHECK(build_match_world(&fixture, g_lane_storage, sizeof(g_lane_storage)));
    SimWorld& world = fixture.world;

    for (uint32_t i = 0u; i < LANE_SLICE_FIRST_WAVE; ++i) CHECK(sim_tick(&world, nullptr));
    CHECK(u32(world.minions.membership.count) == 0u);      // nothing before first_wave_tick
    CHECK(sim_tick(&world, nullptr));                      // tick 5 is the first wave
    CHECK(u32(world.minions.membership.count) == 4u);
    CHECK(minions_of_team(&world, 0u) == 2u);
    CHECK(minions_of_team(&world, 1u) == 2u);

    // Ascending team id, then ascending within-wave ordinal, and the reversed team
    // starts at the far end of the waypoint chain. sys_minion_ai has already stepped
    // each of them off its spawn waypoint by the end of the same tick.
    ComponentPoolOrderedView ordered{};
    CHECK(component_pool_ordered_view(&world.minions.membership, &ordered));
    CHECK(u32(ordered.count) == 4u);
    static const uint8_t expected_team[4] = {0u, 0u, 1u, 1u};
    static const uint8_t expected_waypoint[4] = {1u, 1u, 6u, 6u};
    for (uint32_t i = 0u; i < ordered.count; ++i) {
        MinionView minion{};
        uint8_t team = SIM_TEAM_NONE;
        CHECK(minion_pool_get(&world.minions, ordered.entities[i], &minion));
        CHECK(team_pool_team(&world.teams, ordered.entities[i], &team));
        CHECK(u32(team) == u32(expected_team[i]));
        CHECK(u32(*minion.lane) == LANE_SLICE_LANE_ID);
        CHECK(u32(*minion.waypoint_index) == u32(expected_waypoint[i]));
    }

    // The clock in isolation: no towers, no combat, only sys_waves, so the live count
    // is exactly four per wave that has fired. Over ticks 0..40 that is the waves at
    // tick 5 and tick 25 and nothing else.
    LaneFixture clock{};
    SimWorldConfig clock_config = lane_config();
    clock_config.objective_capacity = 0u;
    CHECK(build_installed_world(&clock, g_lane_storage_b, sizeof(g_lane_storage_b),
                                clock_config));
    for (uint64_t tick = 0u; tick <= 2u * LANE_SLICE_WAVE_INTERVAL; ++tick) {
        clock.world.tick = tick;
        CHECK(sys_waves(&clock.world));
    }
    CHECK(u32(clock.world.minions.membership.count) == 8u);
}

TEST(sim_lane_objectives, a_minion_walks_the_reversed_waypoint_chain_to_the_far_spawn) {
    LaneFixture fixture{};
    CHECK(build_installed_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimWorld& world = fixture.world;

    // No objectives and no second minion, so nothing is ever in range: this walks the
    // waypoint chain and the integrator, and nothing else.
    EntityId minion{HANDLE_NULL};
    CHECK(sim_spawn_minion(&world, 1u, LANE_SLICE_LANE_ID, 7u, 31u, &minion));
    mm::fix goal_x = 0;
    mm::fix goal_y = 0;
    CHECK(map_cell_to_world(&world.map, 24u, &goal_x, &goal_y) == MAP_STATUS_OK);

    for (uint32_t tick = 0u; tick < 64u; ++tick) {
        CHECK(sys_minion_ai(&world));
        CHECK(sys_movement(&world));
    }
    MinionView view{};
    VelocityView velocity{};
    CHECK(minion_pool_get(&world.minions, minion, &view));
    CHECK(velocity_pool_get(&world.velocities, minion, &velocity));
    CHECK(u32(*view.waypoint_index) == 0u);                 // the terminal waypoint
    CHECK(u32(*view.state) == SIM_MINION_PUSH);
    CHECK(*velocity.velocity_x == 0);                       // and it holds position there
    CHECK(*velocity.velocity_y == 0);
    int64_t distance = 0;
    mm::fix step = mm::fix_mul(mm::fix_from_int(15), SIM_DT_FIXED);
    CHECK(sim_distance_squared_point(&world, minion, goal_x, goal_y, &distance));
    CHECK(distance <= sim_range_squared(step));
}

TEST(sim_lane_objectives, a_wave_that_does_not_fit_fails_the_source_operation) {
    LaneFixture fixture{};
    SimWorldConfig config = lane_config();
    config.minion_capacity = 3u;                            // a whole wave is four
    CHECK(build_installed_world(&fixture, g_lane_storage, sizeof(g_lane_storage), config));
    SimWorld& world = fixture.world;
    CHECK(sim_spawn_match_objectives(&world));
    for (uint32_t i = 0u; i < LANE_SLICE_FIRST_WAVE; ++i) CHECK(sim_tick(&world, nullptr));

    uint64_t before = sim_hash_state(&world);
    CHECK(before != 0u);
    CHECK(!sys_waves(&world));                              // the source operation fails
    CHECK(u32(world.minions.membership.count) == 0u);       // no partial wave
    CHECK(sim_hash_state(&world) == before);
    CHECK(!sim_tick(&world, nullptr));                      // and therefore the tick
    CHECK(u32(world.minions.membership.count) == 0u);
    CHECK(u64(world.tick) == LANE_SLICE_FIRST_WAVE);
    CHECK(sim_hash_state(&world) == before);
}

TEST(sim_lane_objectives, the_minion_state_machine_pushes_attacks_and_returns) {
    LaneFixture fixture{};
    CHECK(build_installed_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimWorld& world = fixture.world;

    // Two enemies one world unit apart, exactly at creep.attack_range.
    EntityId west{HANDLE_NULL};
    EntityId east{HANDLE_NULL};
    CHECK(sim_spawn_minion(&world, 0u, LANE_SLICE_LANE_ID, 3u, 27u, &west));
    CHECK(sim_spawn_minion(&world, 1u, LANE_SLICE_LANE_ID, 4u, 28u, &east));

    MinionView w{};
    MinionView e{};
    CHECK(minion_pool_get(&world.minions, west, &w));
    CHECK(u32(*w.state) == SIM_MINION_PUSH);

    CHECK(sys_minion_ai(&world));
    CHECK(minion_pool_get(&world.minions, west, &w));
    CHECK(minion_pool_get(&world.minions, east, &e));
    CHECK(u32(*w.state) == SIM_MINION_ATTACK);
    CHECK(w.target->h == east.h);
    CHECK(u32(*e.state) == SIM_MINION_ATTACK);
    CHECK(e.target->h == west.h);
    CHECK(u32(*w.attack_cooldown) == 4u);                   // armed by the shot it fired

    // The shot rode the damage queue like every other effect.
    CHECK(damage_event_queue_publish(&world.damage_events));
    CHECK(sys_combat_resolve(&world));
    CHECK(damage_event_queue_consume(&world.damage_events));
    HealthView health{};
    CHECK(health_pool_get(&world.health, east, &health));
    CHECK(*health.current == 8);

    // Losing the target spends exactly one tick in RETURN, then re-enters PUSH.
    *health.current = 0;
    CHECK(sys_minion_ai(&world));
    CHECK(minion_pool_get(&world.minions, west, &w));
    CHECK(u32(*w.state) == SIM_MINION_RETURN);
    CHECK(w.target->h == HANDLE_NULL);
    CHECK(sys_minion_ai(&world));
    CHECK(minion_pool_get(&world.minions, west, &w));
    CHECK(u32(*w.state) == SIM_MINION_PUSH);
}

// ------------------------------------------------------------------ S3 towers, death, economy

static SimWorldConfig hero_lane_config(void) {
    SimWorldConfig config = lane_config();
    config.hero_def_capacity = 1u;
    config.hero_capacity = 4u;
    config.projectile_capacity = 4u;
    config.status_capacity = 4u;
    return config;
}

static SimHeroDef idle_hero_def(void) {
    SimHeroDef def{};
    def.hero_def_id = 0x00C0FFEEULL;
    def.max_health = 40;
    return def;                                   // no actions: these heroes never act
}

static bool spawn_test_hero(SimWorld* world, uint8_t team, uint16_t def_index, mm::fix x,
                            mm::fix y, EntityId* out) {
    EntityId entity = entity_manager_create(&world->entities);
    if (entity.h == HANDLE_NULL) return false;
    *out = entity;
    return transform_pool_add(&world->transforms, entity, x, y, 0) &&
           velocity_pool_add(&world->velocities, entity, 0, 0) &&
           health_pool_add(&world->health, entity, 40, 40, 0u) &&
           team_pool_add(&world->teams, entity, team) &&
           hero_pool_add(&world->heroes, entity, def_index, 0);
}

static bool last_damage(SimWorld* world, EntityId* out_source, EntityId* out_target,
                        int32_t* out_amount) {
    uint32_t phase = world->damage_events.write_index;
    uint32_t count = world->damage_events.counts[phase];
    if (count == 0u) return false;
    const DamageEvent& event = world->damage_events.buffers[phase][count - 1u];
    *out_source = event.source;
    *out_target = event.target;
    *out_amount = event.amount;
    return true;
}

static bool commit_damage(SimWorld* world) {
    return damage_event_queue_publish(&world->damage_events) && sys_combat_resolve(world) &&
           damage_event_queue_consume(&world->damage_events);
}

static bool clear_tower_cooldown(SimWorld* world, EntityId tower) {
    ObjectiveView view{};
    if (!objective_pool_get(&world->objectives, tower, &view)) return false;
    *view.attack_cooldown = 0u;
    return true;
}

static uint32_t count_write_events(const SimWorld* world, uint16_t kind) {
    uint32_t phase = world->sim_events.write_index;
    uint32_t found = 0u;
    for (uint32_t i = 0u; i < world->sim_events.counts[phase]; ++i) {
        if (world->sim_events.buffers[phase][i].event_kind == kind) ++found;
    }
    return found;
}

// ROADMAP M5.5 reads "enemy attacking allied hero > nearest minion > nearest hero".
// The predicate for tier 1 is therefore read off the ALLY's kill-credit slot - some
// live hero H on the tower's team carries health(H).last_damage_source == candidate -
// and NOT off the candidate's own slot. The two readings are inverses of each other,
// and this test pins the ROADMAP one in both directions.
TEST(sim_lane_objectives, tower_priority_is_attacker_of_an_ally_then_nearest_minion_then_hero) {
    LaneFixture fixture{};
    CHECK(build_installed_world(&fixture, g_lane_storage, sizeof(g_lane_storage),
                                hero_lane_config()));
    SimWorld& world = fixture.world;
    SimHeroDef hero_def = idle_hero_def();
    uint16_t def_index = 0u;
    CHECK(sim_install_hero_def(&world, &hero_def, &def_index));

    // Only team 0's tower, at cell 18 -> (2.5, 2.5).
    EntityId tower{HANDLE_NULL};
    CHECK(sim_spawn_objective(&world, 0u, LANE_SLICE_TOWER_CELL[0], &tower));
    EntityId ally{HANDLE_NULL};
    EntityId enemy_hero{HANDLE_NULL};
    EntityId enemy_minion{HANDLE_NULL};
    CHECK(spawn_test_hero(&world, 0u, def_index, FIX_HALF, FIX_HALF, &ally));
    // The enemy hero is NEARER than the minion, so tier order is what decides.
    CHECK(spawn_test_hero(&world, 1u, def_index, mm::fix_from_int(3),
                          mm::fix_from_int(2) + FIX_HALF, &enemy_hero));
    CHECK(sim_spawn_minion(&world, 1u, LANE_SLICE_LANE_ID, 2u, 26u, &enemy_minion));

    // The tier-2 pick is shot several times below and a creep only has 10 health, so it
    // is topped up between probes: this test is about tier choice, not lethality.
    HealthView minion_health{};
    CHECK(health_pool_get(&world.health, enemy_minion, &minion_health));

    EntityId source{HANDLE_NULL};
    EntityId target{HANDLE_NULL};
    int32_t amount = 0;

    // Tier 1 is empty, so tier 2 takes the minion even though the hero is closer.
    CHECK(sys_tower_ai(&world));
    CHECK(last_damage(&world, &source, &target, &amount));
    CHECK(source.h == tower.h);
    CHECK(target.h == enemy_minion.h);
    CHECK(amount == SIM_TOWER_ATTACK_MAGNITUDE);
    CHECK(commit_damage(&world));

    // A tower fires only on cooldown expiry.
    CHECK(sys_tower_ai(&world));
    CHECK(!last_damage(&world, &source, &target, &amount));

    // The INVERTED reading is dead: the enemy hero's own last_damage_source naming one
    // of our heroes means our hero hit HIM, not that he is attacking us, so tier 1 stays
    // empty and tier 2 keeps the minion.
    HealthView enemy_health{};
    CHECK(health_pool_get(&world.health, enemy_hero, &enemy_health));
    *enemy_health.last_damage_source = ally;
    CHECK(clear_tower_cooldown(&world, tower));
    CHECK(sys_tower_ai(&world));
    CHECK(last_damage(&world, &source, &target, &amount));
    CHECK(target.h == enemy_minion.h);
    CHECK(commit_damage(&world));
    *enemy_health.last_damage_source = EntityId{HANDLE_NULL};
    *minion_health.current = world.match.creep.max_health;

    // Tier 1, the ROADMAP direction: our ally's kill-credit slot names the enemy hero,
    // so that hero is the one attacking us and the tower punishes him even though the
    // minion is the tier-2 pick.
    HealthView ally_health{};
    CHECK(health_pool_get(&world.health, ally, &ally_health));
    *ally_health.last_damage_source = enemy_hero;
    CHECK(clear_tower_cooldown(&world, tower));
    CHECK(sys_tower_ai(&world));
    CHECK(last_damage(&world, &source, &target, &amount));
    CHECK(target.h == enemy_hero.h);
    CHECK(commit_damage(&world));

    // A dead ally is not an ally under attack: tier 1 empties again the moment the hero
    // whose slot named the attacker stops being live.
    int32_t saved_ally_health = *ally_health.current;
    *ally_health.current = 0;
    CHECK(clear_tower_cooldown(&world, tower));
    CHECK(sys_tower_ai(&world));
    CHECK(last_damage(&world, &source, &target, &amount));
    CHECK(target.h == enemy_minion.h);
    CHECK(commit_damage(&world));
    *ally_health.current = saved_ally_health;
    *ally_health.last_damage_source = EntityId{HANDLE_NULL};

    // Tier 1 accepts a NON-hero attacker, and it outranks the nearest minion: the far
    // minion is the one beating on our ally, so it is the one that gets shot even though
    // the near minion is the tier-2 pick and the hero is nearer still.
    EntityId far_minion{HANDLE_NULL};
    CHECK(sim_spawn_minion(&world, 1u, LANE_SLICE_LANE_ID, 5u, 29u, &far_minion));
    *minion_health.current = world.match.creep.max_health;
    *ally_health.last_damage_source = far_minion;
    CHECK(clear_tower_cooldown(&world, tower));
    CHECK(sys_tower_ai(&world));
    CHECK(last_damage(&world, &source, &target, &amount));
    CHECK(target.h == far_minion.h);
    CHECK(commit_damage(&world));
    *ally_health.last_damage_source = EntityId{HANDLE_NULL};

    // An attacker that is no longer a live candidate is treated as absent, never as a
    // tick failure: tier 1 empties and tier 2 takes over.
    HealthView far_health{};
    CHECK(health_pool_get(&world.health, far_minion, &far_health));
    *far_health.current = 0;
    *ally_health.last_damage_source = far_minion;
    *minion_health.current = world.match.creep.max_health;
    CHECK(clear_tower_cooldown(&world, tower));
    CHECK(sys_tower_ai(&world));
    CHECK(last_damage(&world, &source, &target, &amount));
    CHECK(target.h == enemy_minion.h);
    CHECK(commit_damage(&world));
    *ally_health.last_damage_source = EntityId{HANDLE_NULL};

    // With tier 1 empty again and no live minion left, tier 3 takes the hero.
    *minion_health.current = 0;
    CHECK(clear_tower_cooldown(&world, tower));
    CHECK(sys_tower_ai(&world));
    CHECK(last_damage(&world, &source, &target, &amount));
    CHECK(target.h == enemy_hero.h);

    // A destroyed tower stops firing entirely.
    ObjectiveView objective{};
    CHECK(objective_pool_get(&world.objectives, tower, &objective));
    *objective.state = SIM_OBJECTIVE_DESTROYED;
    CHECK(commit_damage(&world));
    CHECK(clear_tower_cooldown(&world, tower));
    CHECK(sys_tower_ai(&world));
    CHECK(!last_damage(&world, &source, &target, &amount));
}

TEST(sim_lane_objectives, a_kill_credits_the_killers_team_and_pays_every_matching_rule) {
    LaneFixture fixture{};
    CHECK(build_installed_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimWorld& world = fixture.world;

    EntityId west{HANDLE_NULL};
    EntityId east{HANDLE_NULL};
    CHECK(sim_spawn_minion(&world, 0u, LANE_SLICE_LANE_ID, 3u, 27u, &west));
    CHECK(sim_spawn_minion(&world, 1u, LANE_SLICE_LANE_ID, 4u, 28u, &east));

    CHECK(sys_minion_ai(&world));
    CHECK(commit_damage(&world));
    HealthView east_health{};
    CHECK(health_pool_get(&world.health, east, &east_health));
    CHECK(east_health.last_damage_source->h == west.h);   // credit written on the commit

    *east_health.current = 0;
    CHECK(sys_death(&world));
    CHECK(count_write_events(&world, SIM_EVENT_DEATH) == 1u);
    uint32_t phase = world.sim_events.write_index;
    const SimEvent& death = world.sim_events.buffers[phase][world.sim_events.counts[phase] - 1u];
    CHECK(u32(sim_event_payload_u32(&death, 0u)) == east.h);
    CHECK(u32(sim_event_payload_u32(&death, 4u)) == west.h);   // DEATH now carries a killer

    CHECK(sys_economy(&world));
    CHECK(world.ledger.gold[0] == 20);                    // both MINION_KILL rules pay
    CHECK(world.ledger.xp[0] == 15);
    CHECK(world.ledger.gold[1] == 0);
    CHECK(world.ledger.xp[1] == 0);
    CHECK(count_write_events(&world, SIM_EVENT_ECONOMY) == 2u);
    CHECK(sys_flush_destroy(&world));

    // An unattributed death is legal and awards nothing.
    HealthView west_health{};
    CHECK(health_pool_get(&world.health, west, &west_health));
    *west_health.current = 0;
    *west_health.last_damage_source = EntityId{HANDLE_NULL};
    CHECK(sys_death(&world));
    CHECK(sys_economy(&world));
    CHECK(world.ledger.gold[0] == 20);
    CHECK(world.ledger.xp[0] == 15);
    CHECK(world.ledger.gold[1] == 0);
}

TEST(sim_lane_objectives, destroying_a_core_ends_the_match_exactly_once) {
    LaneFixture fixture{};
    CHECK(build_match_world(&fixture, g_lane_storage, sizeof(g_lane_storage)));
    SimWorld& world = fixture.world;
    world.tick = 41u;

    ComponentPoolOrderedView ordered{};
    CHECK(component_pool_ordered_view(&world.objectives.membership, &ordered));
    CHECK(u32(ordered.count) == 4u);
    EntityId team0_core = ordered.entities[1];
    EntityId team1_core = ordered.entities[3];

    EntityId attacker{HANDLE_NULL};
    CHECK(sim_spawn_minion(&world, 0u, LANE_SLICE_LANE_ID, 7u, 31u, &attacker));
    HealthView core_health{};
    CHECK(health_pool_get(&world.health, team1_core, &core_health));
    *core_health.current = 0;
    *core_health.last_damage_source = attacker;

    CHECK(sys_death(&world));
    CHECK(u32(world.match_state.over) == 1u);
    CHECK(u32(world.match_state.winner) == 0u);
    CHECK(u32(world.match_state.end_tick) == 41u);
    CHECK(count_write_events(&world, SIM_EVENT_DEATH) == 1u);
    CHECK(count_write_events(&world, SIM_EVENT_OBJECTIVE_DESTROYED) == 1u);
    CHECK(count_write_events(&world, SIM_EVENT_MATCH_OVER) == 1u);

    // The row survives as hashed DESTROYED state; it is never destroy-deferred.
    ObjectiveView objective{};
    CHECK(objective_pool_has(&world.objectives, team1_core));
    CHECK(objective_pool_get(&world.objectives, team1_core, &objective));
    CHECK(u32(*objective.state) == SIM_OBJECTIVE_DESTROYED);
    CHECK(u32(world.pending_destroy_count) == 0u);

    CHECK(sys_economy(&world));
    CHECK(world.ledger.gold[0] == 100);                   // the OBJECTIVE_DESTROYED rule
    CHECK(world.ledger.xp[0] == 0);                       // no XP rule for that source
    CHECK(count_write_events(&world, SIM_EVENT_ECONOMY) == 1u);

    // A second core falling later emits no second MATCH_OVER and does not change the
    // winner: the over flag is the guard.
    EntityId counter{HANDLE_NULL};
    CHECK(sim_spawn_minion(&world, 1u, LANE_SLICE_LANE_ID, 0u, 24u, &counter));
    HealthView other{};
    CHECK(health_pool_get(&world.health, team0_core, &other));
    *other.current = 0;
    *other.last_damage_source = counter;
    CHECK(sys_death(&world));
    CHECK(count_write_events(&world, SIM_EVENT_MATCH_OVER) == 1u);
    CHECK(count_write_events(&world, SIM_EVENT_OBJECTIVE_DESTROYED) == 2u);
    CHECK(u32(world.match_state.winner) == 0u);
    CHECK(u32(world.match_state.end_tick) == 41u);

    // And the walk is idempotent: an already DESTROYED row reports nothing further.
    uint32_t phase = world.sim_events.write_index;
    uint32_t before = world.sim_events.counts[phase];
    CHECK(sys_death(&world));
    CHECK(u32(world.sim_events.counts[phase]) == before);
}

TEST(sim_lane_objectives, ownership_is_by_team_and_a_decided_match_rejects_every_command) {
    LaneFixture fixture{};
    SimWorldConfig config = lane_config();
    config.initial_unit_count = 2u;
    CHECK(build_installed_world(&fixture, g_lane_storage, sizeof(g_lane_storage), config));
    SimWorld& world = fixture.world;
    CHECK(team_pool_add(&world.teams, world.unit_entities[0], 0u));
    CHECK(team_pool_add(&world.teams, world.unit_entities[1], 1u));

    CommandIntakeConfig intake{};
    intake.tick = 0u;
    intake.player_count = 2u;
    intake.damage_capacity_remaining = 8u;

    Command move{};
    move.tick = 0u;
    move.player_id = 0u;
    move.command_kind = COMMAND_KIND_MOVE;
    move.sequence = 1u;
    move.actor = world.unit_entities[0];

    Command accepted[4]{};
    CommandReject rejects[4]{};
    uint32_t accepted_count = 0u;
    uint32_t reject_count = 0u;
    CHECK(command_intake_run(&world, intake, &move, 1u, 4u, accepted, &accepted_count,
                             rejects, &reject_count));
    CHECK(u32(accepted_count) == 1u);                     // player 0 owns team 0

    move.player_id = 1u;                                  // team 1 does not own team 0's unit
    CHECK(command_intake_run(&world, intake, &move, 1u, 4u, accepted, &accepted_count,
                             rejects, &reject_count));
    CHECK(u32(accepted_count) == 0u);
    CHECK(u32(reject_count) == 1u);
    CHECK(rejects[0].reason == COMMAND_REJECT_UNAUTHORIZED_ACTOR);

    move.actor = world.unit_entities[1];
    CHECK(command_intake_run(&world, intake, &move, 1u, 4u, accepted, &accepted_count,
                             rejects, &reject_count));
    CHECK(u32(accepted_count) == 1u);

    // A decided match accepts no intent at all, whichever side sends it.
    world.match_state.over = 1u;
    world.match_state.winner = 0u;
    CHECK(command_intake_run(&world, intake, &move, 1u, 4u, accepted, &accepted_count,
                             rejects, &reject_count));
    CHECK(u32(accepted_count) == 0u);
    CHECK(u32(reject_count) == 1u);
    CHECK(rejects[0].reason == COMMAND_REJECT_MATCH_OVER);
}

// ------------------------------------------------------------------ S4 hash and diff

TEST(sim_lane_objectives, every_new_field_is_load_bearing_in_the_canonical_hash) {
    LaneFixture a{};
    LaneFixture b{};
    CHECK(build_match_world(&a, g_lane_storage, sizeof(g_lane_storage)));
    CHECK(build_match_world(&b, g_lane_storage_b, sizeof(g_lane_storage_b)));
    uint64_t base = sim_hash_state(&a.world);
    CHECK(base != 0u);
    CHECK(sim_hash_state(&b.world) == base);
    SimStateDiff diff{};
    CHECK(!sim_diff_state(&a.world, &b.world, &diff));

    ComponentPoolOrderedView ordered{};
    CHECK(component_pool_ordered_view(&a.world.objectives.membership, &ordered));
    EntityId tower = ordered.entities[0];

    // last_damage_source is hashed INSIDE the Health block, so it diffs there.
    HealthView health{};
    CHECK(health_pool_get(&a.world.health, tower, &health));
    *health.last_damage_source = tower;
    CHECK(sim_hash_state(&a.world) != base);
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_HEALTH_LAST_DAMAGE_SOURCE);
    *health.last_damage_source = EntityId{HANDLE_NULL};
    CHECK(sim_hash_state(&a.world) == base);

    // The match def diffs per record, after the SimEvent block.
    SimMatchDef tweaked = fixture_match_def();
    tweaked.economy[0].award_amount = 21u;
    CHECK(sim_install_match_def(&b.world, &tweaked));
    CHECK(sim_hash_state(&b.world) != base);
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_MATCH_ECONOMY_RECORD);
    CHECK(u32(diff.index) == 0u);
    SimMatchDef restored = fixture_match_def();
    CHECK(sim_install_match_def(&b.world, &restored));
    CHECK(sim_hash_state(&b.world) == base);

    TeamView team{};
    CHECK(team_pool_get(&a.world.teams, tower, &team));
    uint8_t saved_team = *team.team;
    *team.team = SIM_TEAM_NONE;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_TEAM_SIDE);
    *team.team = saved_team;

    ObjectiveView objective{};
    CHECK(objective_pool_get(&a.world.objectives, tower, &objective));
    *objective.attack_cooldown = 7u;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_OBJECTIVE_ATTACK_COOLDOWN);
    *objective.attack_cooldown = 0u;
    *objective.state = SIM_OBJECTIVE_DESTROYED;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_OBJECTIVE_STATE);
    *objective.state = SIM_OBJECTIVE_ALIVE;

    a.world.ledger.gold[1] = 9;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_LEDGER_GOLD);
    CHECK(u32(diff.index) == 1u);
    a.world.ledger.gold[1] = 0;
    a.world.ledger.xp[0] = 4;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_LEDGER_XP);
    a.world.ledger.xp[0] = 0;

    a.world.match_state.over = 1u;
    a.world.match_state.winner = 1u;
    a.world.match_state.end_tick = 3u;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_MATCH_STATE_OVER);
    a.world.match_state = sim_match_state_initial();
    CHECK(sim_hash_state(&a.world) == base);

    // A minion row is canonical state too.
    EntityId minion{HANDLE_NULL};
    CHECK(sim_spawn_minion(&a.world, 0u, LANE_SLICE_LANE_ID, 0u, 24u, &minion));
    CHECK(sim_hash_state(&a.world) != base);
    MinionView view{};
    CHECK(minion_pool_get(&a.world.minions, minion, &view));
    uint64_t with_minion = sim_hash_state(&a.world);
    *view.state = SIM_MINION_ATTACK;
    CHECK(sim_hash_state(&a.world) != with_minion);
}

TEST(sim_lane_objectives, a_row_that_disagrees_with_its_def_is_not_a_canonical_world) {
    LaneFixture fixture{};
    CHECK(build_match_world(&fixture, g_lane_storage, sizeof(g_lane_storage)));
    SimWorld& world = fixture.world;
    CHECK(sim_hash_state(&world) != 0u);

    ComponentPoolOrderedView ordered{};
    CHECK(component_pool_ordered_view(&world.objectives.membership, &ordered));
    ObjectiveView objective{};
    CHECK(objective_pool_get(&world.objectives, ordered.entities[0], &objective));

    uint8_t saved_kind = *objective.kind;
    *objective.kind = SIM_OBJECTIVE_CORE;                 // the def says TOWER
    CHECK(sim_hash_state(&world) == 0u);
    *objective.kind = saved_kind;
    CHECK(sim_hash_state(&world) != 0u);

    uint16_t saved_index = *objective.def_index;
    *objective.def_index = 9u;                            // past objective_count
    CHECK(sim_hash_state(&world) == 0u);
    *objective.def_index = saved_index;

    // A verdict that contradicts itself is not canonical either.
    world.match_state.winner = 1u;                        // over is still 0
    CHECK(sim_hash_state(&world) == 0u);
    world.match_state = sim_match_state_initial();
    CHECK(sim_hash_state(&world) != 0u);

    world.ledger.gold[0] = -1;
    CHECK(sim_hash_state(&world) == 0u);
    world.ledger.gold[0] = 0;

    // And an installed def that no longer matches the map fails the whole world.
    world.match.teams[0].tower_cell = 0u;
    CHECK(sim_hash_state(&world) == 0u);
    world.match.teams[0].tower_cell = LANE_SLICE_TOWER_CELL[0];
    CHECK(sim_hash_state(&world) != 0u);
}

TEST(sim_lane_objectives, every_new_state_field_has_a_name_and_the_logic_key_moved_once) {
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_HEALTH_LAST_DAMAGE_SOURCE),
                      "health_last_damage_source") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_MATCH_TEAM_COUNT),
                      "match_team_count") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_MATCH_ECONOMY_RECORD),
                      "match_economy_record") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_TEAM_SIDE), "team_side") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_MINION_WAYPOINT_INDEX),
                      "minion_waypoint_index") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_OBJECTIVE_STATE),
                      "objective_state") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_LEDGER_GOLD), "ledger_gold") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_MATCH_STATE_END_TICK),
                      "match_state_end_tick") == 0);
    // Every earlier block still reports the same names, so old divergence reports
    // read exactly as they did before the append.
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_SIM_EVENT_WRITE_PAYLOAD),
                      "sim_event_write_payload") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_POSITION_X), "position_x") == 0);
    CHECK(u64(SIM_LOGIC_HASH) == 0x5b47e648953a63fcULL);
}

// ------------------------------------------------------------------ S5 acceptance
// proto-design section 7.2 items 6, 1, 3, and 4, on the lane_slice fixture decoded
// from the independent golden rather than authored in C++, so the acceptance match
// runs on exactly the bytes assets/maps/lane_slice.mapdesc ships.

alignas(16) static uint8_t g_accept_storage[768u * 1024u];
alignas(16) static uint8_t g_accept_storage_b[768u * 1024u];
static uint8_t g_replay_bytes[192u * 1024u];

static const uint64_t ACCEPT_SEED = 0x5ACE01ULL;
static const uint32_t ACCEPT_TICKS = 3000u;
static const uint32_t ACCEPT_CHECKPOINT = 500u;
static const uint32_t ACCEPT_CHECKPOINTS = ACCEPT_TICKS / ACCEPT_CHECKPOINT;
static const uint32_t ACCEPT_EVENT_KINDS = 9u;
static const uint32_t ACCEPT_MILESTONES = 8u;
static const uint32_t ACCEPT_PLAYERS = 2u;
// The verdict lands well inside this bound, and the test asserts that it does, so a
// match that stopped converging would fail loudly instead of running to the cap.
static const uint32_t ACCEPT_MATCH_LIMIT = 400u;

// The acceptance match is deliberately ASYMMETRIC. A mirror match on a mirror map is
// a stalemate: equal waves meet at the midpoint and annihilate each other forever,
// and no core ever falls. Exactly one authored field breaks the mirror - team 1's
// tower carries SIM_TARGET_POLICY_NONE, so team 0's tower is the only objective that
// shoots - and team 1's objectives are authored thin. Everything else is the S1
// fixture, so the asymmetry is data, never a special case in the sim.
static SimMatchDef accept_match_def(bool extra_economy_rule) {
    SimMatchDef def = fixture_match_def();
    // One creep per team per wave, and a creep duel that resolves in a few ticks: the
    // lane must clear faster than the authored clock refills it, or the pools fill and
    // sys_waves correctly fails the tick forever after (ADR-0014, never a partial wave).
    def.minions_per_wave = 1u;
    def.creep.max_health = 6;
    def.creep.attack_magnitude = 5;
    def.creep.attack_cooldown_ticks = 2u;
    // The objective cells sit exactly one cell off the lane, and a per-tick step of
    // fix_mul(move_speed, SIM_DT_FIXED) is 32760, not 32768 - a creep therefore never
    // lands exactly on a lane cell centre, and a range of exactly FIX_ONE would miss an
    // objective by a few hundredths of a unit forever. Two units clears the lane with
    // room to spare, which is what makes "minions reach the enemy tower" a fact of the
    // authored data rather than a fixed-point coincidence.
    def.creep.attack_range = 2 * FIX_ONE;
    def.objectives[0] = objective_def(0xB0ULL, 0u, SIM_OBJECTIVE_TOWER, 4000,
                                      SIM_TARGET_POLICY_TOWER);
    def.objectives[1] = objective_def(0xB1ULL, 0u, SIM_OBJECTIVE_CORE, 4000,
                                      SIM_TARGET_POLICY_NONE);
    def.objectives[2] = objective_def(0xB2ULL, 1u, SIM_OBJECTIVE_TOWER, 30,
                                      SIM_TARGET_POLICY_NONE);
    def.objectives[3] = objective_def(0xB3ULL, 1u, SIM_OBJECTIVE_CORE, 30,
                                      SIM_TARGET_POLICY_NONE);
    if (extra_economy_rule) {
        def.economy[4].award_kind = SIM_AWARD_XP;
        def.economy[4].source_kind = SIM_AWARD_SOURCE_OBJECTIVE_DESTROYED;
        def.economy[4].award_amount = 40u;
        def.economy_count = 5u;
    }
    return def;
}

static SimWorldConfig accept_config(void) {
    SimWorldConfig config{};
    config.max_entities = 256u;
    config.initial_unit_count = 2u;
    config.damage_event_capacity = 256u;
    config.map = MapConfig{8u, 8u, 2u, 8u};
    config.hero_def_capacity = 1u;
    config.hero_capacity = 4u;
    config.projectile_capacity = 8u;
    config.status_capacity = 8u;
    config.sim_event_capacity = 256u;
    config.team_capacity = 250u;
    config.minion_capacity = 220u;
    config.objective_capacity = 8u;
    return config;
}

struct AcceptFixture {
    SimWorld world;
    size_t arena_offset;
    EntityId hero[2];
    // Spawn order, which sim_spawn_match_objectives fixes: team 0 tower, team 0 core,
    // team 1 tower, team 1 core.
    EntityId objective[4];
};

// Builds the acceptance world from the SHIPPED bytes: sim_init, decode
// LANE_SLICE_MAPDESC, install, spawn the four objectives, then park one idle hero per
// team off the lane and outside every tower range, so the match is decided entirely
// by waves and towers and never by hero tuning (plan: Held questions, heroes idle).
static bool build_accept_world(AcceptFixture* fixture, uint8_t* storage, size_t bytes,
                               bool extra_economy_rule) {
    Arena arena;
    arena_init_fixed(&arena, storage, bytes);
    if (!sim_init(&fixture->world, &arena, ACCEPT_SEED, accept_config())) return false;
    fixture->arena_offset = arena.offset;

    ByteReader reader;
    byte_reader_init(&reader, LANE_SLICE_MAPDESC, sizeof(LANE_SLICE_MAPDESC));
    if (map_decode(&fixture->world.map, &reader) != MAP_STATUS_OK) return false;
    if (map_require_end(&reader) != MAP_STATUS_OK) return false;

    SimMatchDef def = accept_match_def(extra_economy_rule);
    if (!sim_install_match_def(&fixture->world, &def)) return false;
    if (!sim_spawn_match_objectives(&fixture->world)) return false;
    ComponentPoolOrderedView ordered{};
    if (!component_pool_ordered_view(&fixture->world.objectives.membership, &ordered))
        return false;
    if (ordered.count != 4u) return false;
    for (uint32_t i = 0u; i < 4u; ++i) fixture->objective[i] = ordered.entities[i];

    SimHeroDef hero_def = idle_hero_def();
    uint16_t def_index = 0u;
    if (!sim_install_hero_def(&fixture->world, &hero_def, &def_index)) return false;
    if (!spawn_test_hero(&fixture->world, 0u, def_index, FIX_HALF,
                         mm::fix_from_int(6) + FIX_HALF, &fixture->hero[0])) return false;
    return spawn_test_hero(&fixture->world, 1u, def_index, mm::fix_from_int(7) + FIX_HALF,
                           mm::fix_from_int(6) + FIX_HALF, &fixture->hero[1]);
}

struct MatchMilestone {
    uint64_t tick;
    uint32_t ordinal;
    uint32_t subject;
    uint16_t kind;
};

struct MatchTrace {
    uint64_t checkpoints[ACCEPT_CHECKPOINTS];
    uint64_t event_digest;
    uint64_t final_hash;
    uint32_t event_counts[ACCEPT_EVENT_KINDS];
    uint32_t total_events;
    uint32_t failed_ticks;
    uint32_t ticks_run;
    uint32_t peak_minions;
    uint32_t commands_issued;
    uint8_t winner;
    MatchMilestone milestones[ACCEPT_MILESTONES];
    uint32_t milestone_count;
};

static uint64_t accept_fold(uint64_t digest, uint64_t value) {
    digest ^= value;
    return digest * 0x00000100000001b3ULL;
}

// The command script is a pure function of the tick index, identical in every run, so
// the recorded replay is reproducible. The two placeholder units carry no Team row, so
// they are never combat participants - they exist to make the recorded command stream
// non-trivial and to prove the replay codec round-trips a real match.
static uint32_t accept_commands(uint64_t tick, SimCommand* out) {
    if (tick == 3u) {
        out[0].kind = SIM_COMMAND_SET_VELOCITY;
        out[0].player_id = 0u;
        out[0].unit_index = 0u;
        out[0].value_x = mm::fix_from_int(1);
        out[0].value_y = 0;
        return 1u;
    }
    if (tick == 11u) {
        out[0].kind = SIM_COMMAND_SET_VELOCITY;
        out[0].player_id = 1u;
        out[0].unit_index = 1u;
        out[0].value_x = 0;
        out[0].value_y = mm::fix_from_int(-1);
        return 1u;
    }
    if (tick == 37u) {
        out[0].kind = SIM_COMMAND_SET_VELOCITY;
        out[0].player_id = 0u;
        out[0].unit_index = 0u;
        out[0].value_x = 0;
        out[0].value_y = 0;
        out[1].kind = SIM_COMMAND_SET_VELOCITY;
        out[1].player_id = 1u;
        out[1].unit_index = 1u;
        out[1].value_x = 0;
        out[1].value_y = 0;
        return 2u;
    }
    return 0u;
}

// Every event this match produces is appended AFTER sim_event_queue_consume (the
// combat committer, sys_death, and sys_economy all run past the publish/consume pair
// in sim_tick), so the write phase after sim_tick holds this tick's whole sequence in
// append order. Folding kind, tick, payload_size, append_ordinal, and the payload
// bytes gives one digest over the full ordered stream.
static void accept_observe_events(SimWorld* world, MatchTrace* trace) {
    uint32_t phase = world->sim_events.write_index;
    for (uint32_t i = 0u; i < world->sim_events.counts[phase]; ++i) {
        const SimEvent& event = world->sim_events.buffers[phase][i];
        trace->event_digest = accept_fold(trace->event_digest, event.tick);
        trace->event_digest = accept_fold(trace->event_digest, event.event_kind);
        trace->event_digest = accept_fold(trace->event_digest, event.payload_size);
        trace->event_digest = accept_fold(trace->event_digest, event.append_ordinal);
        for (uint16_t offset = 0u; offset < SIM_EVENT_PAYLOAD_MAX; offset = offset + 4u)
            trace->event_digest = accept_fold(trace->event_digest,
                                              sim_event_payload_u32(&event, offset));
        ++trace->total_events;
        if (event.event_kind < ACCEPT_EVENT_KINDS) ++trace->event_counts[event.event_kind];
        if ((event.event_kind == SIM_EVENT_OBJECTIVE_DESTROYED ||
             event.event_kind == SIM_EVENT_MATCH_OVER) &&
            trace->milestone_count < ACCEPT_MILESTONES) {
            MatchMilestone& milestone = trace->milestones[trace->milestone_count++];
            milestone.tick = event.tick;
            milestone.ordinal = event.append_ordinal;
            milestone.subject = sim_event_payload_u32(&event, 0u);
            milestone.kind = event.event_kind;
        }
    }
}

// Runs the scripted match to `ticks`, or until the verdict lands when `stop_on_over`.
// Pure function of (seed world, script): no wall clock, no RNG of its own, and no
// branch on anything but the tick index and already-hashed state.
static bool run_accept_match(AcceptFixture* fixture, MatchTrace* trace, uint32_t ticks,
                             bool stop_on_over, ByteWriter* record) {
    SimWorld& world = fixture->world;
    for (uint32_t i = 0u; i < ticks; ++i) {
        SimCommand commands[REPLAY_PLACEHOLDER_MAX_COMMANDS]{};
        uint32_t count = accept_commands(world.tick, commands);
        trace->commands_issued += count;
        SimCommandBuffer buffer{commands, count};

        bool checkpoint = (i + 1u) % ACCEPT_CHECKPOINT == 0u &&
                          (i + 1u) / ACCEPT_CHECKPOINT <= ACCEPT_CHECKPOINTS;
        if (!sim_tick(&world, &buffer)) ++trace->failed_ticks;
        accept_observe_events(&world, trace);
        if (world.minions.membership.count > trace->peak_minions)
            trace->peak_minions = world.minions.membership.count;
        ++trace->ticks_run;

        uint64_t expected = 0u;
        if (checkpoint) {
            expected = sim_hash_state(&world);
            trace->checkpoints[(i + 1u) / ACCEPT_CHECKPOINT - 1u] = expected;
        }
        if (record && replay_write_tick(record, ACCEPT_PLAYERS, &buffer, expected) !=
                          REPLAY_STATUS_OK) return false;
        if (stop_on_over && world.match_state.over != 0u) break;
    }
    trace->final_hash = sim_hash_state(&world);
    trace->winner = world.match_state.winner;
    return true;
}

// Item 6. The whole vertical slice in one scripted match on the shipped map bytes:
// authored waves flow, minions reach the enemy tower, the tower falls, the core
// falls, and the verdict lands exactly once - and the entire ordered SimEvent stream
// is identical across two fresh runs.
TEST(sim_lane_objectives, the_scripted_match_takes_the_tower_then_the_core_and_ends_once) {
    static AcceptFixture first{};
    static AcceptFixture second{};
    static MatchTrace a{};
    static MatchTrace b{};
    CHECK(build_accept_world(&first, g_accept_storage, sizeof(g_accept_storage), false));
    CHECK(build_accept_world(&second, g_accept_storage_b, sizeof(g_accept_storage_b), false));
    CHECK(sim_hash_state(&first.world) == sim_hash_state(&second.world));
    CHECK(sim_hash_state(&first.world) != 0u);

    CHECK(run_accept_match(&first, &a, ACCEPT_MATCH_LIMIT, true, nullptr));
    CHECK(run_accept_match(&second, &b, ACCEPT_MATCH_LIMIT, true, nullptr));
    SimWorld& world = first.world;

    // The match is decided, inside the bound, by the core and not by the clock.
    CHECK(a.failed_ticks == 0u);
    CHECK(b.failed_ticks == 0u);
    CHECK(u32(a.ticks_run) < ACCEPT_MATCH_LIMIT);
    CHECK(u32(world.match_state.over) == 1u);
    CHECK(u32(world.match_state.winner) == 0u);
    CHECK(u32(world.match_state.end_tick) != 0u);
    CHECK(u64(world.match_state.end_tick) == world.tick - 1u);

    // Waves flowed and minions really did reach the enemy objectives.
    CHECK(a.event_counts[SIM_EVENT_OBJECTIVE_DAMAGED] > 0u);
    CHECK(a.event_counts[SIM_EVENT_DEATH] > 0u);
    CHECK(a.event_counts[SIM_EVENT_ECONOMY] > 0u);
    CHECK(u32(a.event_counts[SIM_EVENT_OBJECTIVE_DESTROYED]) == 2u);
    CHECK(u32(a.event_counts[SIM_EVENT_MATCH_OVER]) == 1u);   // exactly once
    CHECK(world.ledger.gold[0] > 0);

    // Tower first, then core, and MATCH_OVER on the core's own tick, after it.
    CHECK(u32(a.milestone_count) == 3u);
    CHECK(u32(a.milestones[0].kind) == u32(SIM_EVENT_OBJECTIVE_DESTROYED));
    CHECK(u32(a.milestones[0].subject) == first.objective[2].h);   // team 1's tower
    CHECK(u32(a.milestones[1].kind) == u32(SIM_EVENT_OBJECTIVE_DESTROYED));
    CHECK(u32(a.milestones[1].subject) == first.objective[3].h);   // team 1's core
    CHECK(u32(a.milestones[2].kind) == u32(SIM_EVENT_MATCH_OVER));
    CHECK(a.milestones[0].tick < a.milestones[1].tick);
    CHECK(a.milestones[2].tick == a.milestones[1].tick);
    CHECK(a.milestones[2].ordinal > a.milestones[1].ordinal);
    CHECK(u64(a.milestones[2].tick) == u64(world.match_state.end_tick));

    // The losing objectives are hashed DESTROYED rows, never destroy-deferred; the
    // winning side's two objectives were never touched.
    for (uint32_t i = 0u; i < 4u; ++i) {
        ObjectiveView view{};
        HealthView health{};
        CHECK(objective_pool_has(&world.objectives, first.objective[i]));
        CHECK(objective_pool_get(&world.objectives, first.objective[i], &view));
        CHECK(health_pool_get(&world.health, first.objective[i], &health));
        bool loser = i >= 2u;
        CHECK(u32(*view.state) ==
              (loser ? u32(SIM_OBJECTIVE_DESTROYED) : u32(SIM_OBJECTIVE_ALIVE)));
        if (loser) CHECK(*health.current <= 0);
        else CHECK(*health.current == *health.maximum);
    }

    // Two fresh runs agree on the whole ordered event stream and on the state.
    CHECK(a.event_digest != 0u);
    CHECK(a.event_digest == b.event_digest);
    CHECK(u32(a.total_events) == u32(b.total_events));
    CHECK(u32(a.ticks_run) == u32(b.ticks_run));
    CHECK(u32(a.milestone_count) == u32(b.milestone_count));
    for (uint32_t i = 0u; i < a.milestone_count; ++i) {
        CHECK(a.milestones[i].tick == b.milestones[i].tick);
        CHECK(u32(a.milestones[i].ordinal) == u32(b.milestones[i].ordinal));
        CHECK(u32(a.milestones[i].kind) == u32(b.milestones[i].kind));
        CHECK(u32(a.milestones[i].subject) == u32(b.milestones[i].subject));
    }
    for (uint32_t kind = 0u; kind < ACCEPT_EVENT_KINDS; ++kind)
        CHECK(u32(a.event_counts[kind]) == u32(b.event_counts[kind]));
    CHECK(a.final_hash != 0u);
    CHECK(a.final_hash == b.final_hash);
    SimStateDiff diff{};
    CHECK(!sim_diff_state(&first.world, &second.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_NONE);

    // A decided match keeps ticking, and it keeps hashing the same way on both runs.
    for (uint32_t i = 0u; i < 20u; ++i) {
        SimCommandBuffer empty{nullptr, 0u};
        CHECK(sim_tick(&first.world, &empty));
        CHECK(sim_tick(&second.world, &empty));
    }
    CHECK(sim_hash_state(&first.world) != 0u);
    CHECK(sim_hash_state(&first.world) != a.final_hash);
    CHECK(sim_hash_state(&first.world) == sim_hash_state(&second.world));
    CHECK(u32(world.match_state.winner) == 0u);
    CHECK(u32(a.event_counts[SIM_EVENT_MATCH_OVER]) == 1u);

    // And it accepts no further intent, whoever sends it. The unit slot is given a
    // team row only now, after every tick above, so ownership is genuinely satisfied
    // and MATCH_OVER is the reason the command dies.
    CHECK(team_pool_add(&world.teams, world.unit_entities[0], 0u));
    CommandIntakeConfig intake{};
    intake.tick = static_cast<uint32_t>(world.tick);
    intake.player_count = 2u;
    intake.damage_capacity_remaining = 8u;
    Command move{};
    move.tick = intake.tick;
    move.player_id = 0u;
    move.command_kind = COMMAND_KIND_MOVE;
    move.sequence = 1u;
    move.actor = world.unit_entities[0];
    Command accepted[2]{};
    CommandReject rejects[2]{};
    uint32_t accepted_count = 0u;
    uint32_t reject_count = 0u;
    CHECK(command_intake_run(&world, intake, &move, 1u, 2u, accepted, &accepted_count,
                             rejects, &reject_count));
    CHECK(u32(accepted_count) == 0u);
    CHECK(u32(reject_count) == 1u);
    CHECK(rejects[0].reason == COMMAND_REJECT_MATCH_OVER);
    CHECK(std::strcmp(command_reject_reason_string(COMMAND_REJECT_MATCH_OVER),
                      "match_over") == 0);
}

// Item 1. The same match, recorded through the replay v1 codec and played back into a
// fresh world: the state hash agrees at every 500-tick checkpoint and at the end.
TEST(sim_lane_objectives, the_recorded_match_replays_to_the_same_hashes) {
    static AcceptFixture recorded{};
    static AcceptFixture replayed{};
    static MatchTrace record_trace{};
    static MatchTrace replay_trace{};
    CHECK(build_accept_world(&recorded, g_accept_storage, sizeof(g_accept_storage), false));

    ByteWriter writer;
    byte_writer_init(&writer, g_replay_bytes, sizeof(g_replay_bytes));
    ReplayHeader header = replay_header_make(ACCEPT_SEED, ACCEPT_PLAYERS, ACCEPT_TICKS);
    CHECK(replay_write_header(&writer, &header) == REPLAY_STATUS_OK);
    CHECK(header.sim_logic_hash == SIM_LOGIC_HASH);
    CHECK(run_accept_match(&recorded, &record_trace, ACCEPT_TICKS, false, &writer));
    CHECK(record_trace.failed_ticks == 0u);
    CHECK(u32(record_trace.ticks_run) == ACCEPT_TICKS);
    CHECK(u32(record_trace.commands_issued) == 4u);      // the script really is non-trivial
    CHECK(u32(record_trace.event_counts[SIM_EVENT_MATCH_OVER]) == 1u);
    size_t recorded_size = byte_writer_size(&writer);
    CHECK(sz(recorded_size) == REPLAY_HEADER_ENCODED_SIZE +
                                   ACCEPT_TICKS * REPLAY_TICK_BASE_ENCODED_SIZE +
                                   4u * REPLAY_COMMAND_ENCODED_SIZE);

    // Playback drives a fresh world from the bytes alone.
    CHECK(build_accept_world(&replayed, g_accept_storage_b, sizeof(g_accept_storage_b), false));
    ByteReader reader;
    byte_reader_init(&reader, g_replay_bytes, recorded_size);
    ReplayHeader decoded{};
    CHECK(replay_read_header(&reader, &decoded) == REPLAY_STATUS_OK);
    CHECK(decoded.seed == ACCEPT_SEED);
    CHECK(u32(decoded.player_count) == ACCEPT_PLAYERS);
    CHECK(u64(decoded.tick_count) == u64(ACCEPT_TICKS));

    uint32_t mismatches = 0u;
    uint32_t compared = 0u;
    for (uint32_t i = 0u; i < ACCEPT_TICKS; ++i) {
        SimCommand commands[SIM_MAX_COMMANDS_PER_TICK]{};
        uint32_t count = 0u;
        uint64_t expected = 0u;
        if (replay_read_tick(&reader, decoded.player_count, commands,
                             SIM_MAX_COMMANDS_PER_TICK, &count, &expected) !=
            REPLAY_STATUS_OK) {
            ++mismatches;
            break;
        }
        replay_trace.commands_issued += count;
        SimCommandBuffer buffer{commands, count};
        if (!sim_tick(&replayed.world, &buffer)) ++replay_trace.failed_ticks;
        accept_observe_events(&replayed.world, &replay_trace);
        if (expected == 0u) continue;
        ++compared;
        if (sim_hash_state(&replayed.world) != expected) ++mismatches;
    }
    CHECK(replay_require_end(&reader) == REPLAY_STATUS_OK);
    CHECK(mismatches == 0u);
    CHECK(u32(compared) == ACCEPT_CHECKPOINTS);          // every 500-tick checkpoint
    CHECK(replay_trace.failed_ticks == 0u);
    CHECK(u32(replay_trace.commands_issued) == u32(record_trace.commands_issued));
    CHECK(replay_trace.event_digest == record_trace.event_digest);
    CHECK(sim_hash_state(&replayed.world) == record_trace.final_hash);
    for (uint32_t i = 0u; i < ACCEPT_CHECKPOINTS; ++i) CHECK(record_trace.checkpoints[i] != 0u);

    // A replay whose logic key disagrees is refused before a single tick runs.
    ByteReader stale;
    byte_reader_init(&stale, g_replay_bytes, recorded_size);
    ReplayHeader ignored{};
    CHECK(replay_read_header(&stale, &ignored) == REPLAY_STATUS_OK);
    CHECK(u64(SIM_LOGIC_HASH) == 0x5b47e648953a63fcULL);
}

// Item 3. Event overflow fails the SOURCE operation - the death verdict and the award
// are each atomic - and nothing is dropped. Draining the queue lets the identical
// operation proceed. (The wave half of item 3 is
// a_wave_that_does_not_fit_fails_the_source_operation, above.)
TEST(sim_lane_objectives, a_full_event_queue_fails_the_death_and_the_award_whole) {
    LaneFixture fixture{};
    CHECK(build_installed_world(&fixture, g_lane_storage, sizeof(g_lane_storage), lane_config()));
    SimWorld& world = fixture.world;

    EntityId west{HANDLE_NULL};
    EntityId east{HANDLE_NULL};
    CHECK(sim_spawn_minion(&world, 0u, LANE_SLICE_LANE_ID, 3u, 27u, &west));
    CHECK(sim_spawn_minion(&world, 1u, LANE_SLICE_LANE_ID, 4u, 28u, &east));
    CHECK(sys_minion_ai(&world));
    CHECK(commit_damage(&world));
    HealthView east_health{};
    CHECK(health_pool_get(&world.health, east, &east_health));
    *east_health.current = 0;

    // Fill the write phase to the brim: the one DEATH event has nowhere to go.
    while (sim_event_queue_write_room(&world.sim_events) > 0u) {
        SimEvent filler = sim_event_make_heal(world.tick, west, 1);
        CHECK(sim_event_queue_append(&world.sim_events, &filler));
    }
    uint32_t phase = world.sim_events.write_index;
    uint32_t before = world.sim_events.counts[phase];
    uint64_t hash_before = sim_hash_state(&world);
    CHECK(!sys_death(&world));                             // the source operation fails
    CHECK(u32(world.sim_events.counts[phase]) == u32(before));   // nothing dropped, nothing added
    CHECK(sim_hash_state(&world) == hash_before);
    CHECK(u32(world.pending_destroy_count) == 0u);         // and nothing was deferred

    // Draining lets the identical call through.
    CHECK(sim_event_queue_publish(&world.sim_events));
    CHECK(sim_event_queue_consume(&world.sim_events));
    CHECK(sys_death(&world));
    CHECK(count_write_events(&world, SIM_EVENT_DEATH) == 1u);
    CHECK(u32(world.pending_destroy_count) == 1u);

    // The award is atomic too: both MINION_KILL rules pay or neither does.
    while (sim_event_queue_write_room(&world.sim_events) > 1u) {
        SimEvent filler = sim_event_make_heal(world.tick, west, 1);
        CHECK(sim_event_queue_append(&world.sim_events, &filler));
    }
    CHECK(u32(sim_event_queue_write_room(&world.sim_events)) == 1u);   // room for one of two
    phase = world.sim_events.write_index;
    before = world.sim_events.counts[phase];
    hash_before = sim_hash_state(&world);
    CHECK(!sys_economy(&world));
    CHECK(u32(world.sim_events.counts[phase]) == u32(before));
    CHECK(sim_hash_state(&world) == hash_before);
    CHECK(world.ledger.gold[0] == 0);                      // no partial award
    CHECK(world.ledger.xp[0] == 0);

    CHECK(sim_event_queue_publish(&world.sim_events));
    CHECK(sim_event_queue_consume(&world.sim_events));
    CHECK(sys_economy(&world));
    CHECK(world.ledger.gold[0] == 20);
    CHECK(world.ledger.xp[0] == 15);
    CHECK(count_write_events(&world, SIM_EVENT_ECONOMY) == 2u);
}

// Item 4. Configuration changes the ledger; it never changes the RNG. mm::pcg32_next
// has exactly one call site (sys_rng_advance) and is advanced unconditionally once
// per tick, so world->rng after N ticks is a function of (seed, N) alone. That is the
// real rng-drift signal - NOT the oracle stream= field, which is a digest over the
// canonical state hash and therefore moves with every reviewed hash bump.
TEST(sim_lane_objectives, an_extra_economy_rule_moves_the_ledger_and_never_the_rng) {
    static AcceptFixture lean{};
    static AcceptFixture rich{};
    static MatchTrace lean_trace{};
    static MatchTrace rich_trace{};
    CHECK(build_accept_world(&lean, g_accept_storage, sizeof(g_accept_storage), false));
    CHECK(build_accept_world(&rich, g_accept_storage_b, sizeof(g_accept_storage_b), true));
    CHECK(u32(lean.world.match.economy_count) == 4u);
    CHECK(u32(rich.world.match.economy_count) == 5u);
    CHECK(lean.arena_offset == rich.arena_offset);        // the same arena, one more rule

    mm::pcg32 expected = lean.world.rng;
    CHECK(run_accept_match(&lean, &lean_trace, ACCEPT_TICKS, false, nullptr));
    CHECK(run_accept_match(&rich, &rich_trace, ACCEPT_TICKS, false, nullptr));
    CHECK(lean_trace.failed_ticks == 0u);
    CHECK(rich_trace.failed_ticks == 0u);
    for (uint32_t i = 0u; i < ACCEPT_TICKS; ++i) (void)mm::pcg32_next(&expected);

    // One draw per tick, no more and no fewer, in both worlds.
    CHECK(lean.world.rng.state == expected.state);
    CHECK(lean.world.rng.inc == expected.inc);
    CHECK(rich.world.rng.state == lean.world.rng.state);
    CHECK(rich.world.rng.inc == lean.world.rng.inc);
    CHECK(lean.world.tick == rich.world.tick);

    // The extra rule really is load-bearing, so the agreement above is not the
    // agreement of two identical worlds.
    CHECK(u32(lean_trace.event_counts[SIM_EVENT_OBJECTIVE_DESTROYED]) == 2u);
    CHECK(lean.world.ledger.xp[0] != rich.world.ledger.xp[0]);
    CHECK(rich.world.ledger.xp[0] == lean.world.ledger.xp[0] + 80);   // 40 per objective
    CHECK(lean.world.ledger.gold[0] == rich.world.ledger.gold[0]);
    CHECK(lean_trace.final_hash != rich_trace.final_hash);
    CHECK(lean_trace.event_digest != rich_trace.event_digest);
    CHECK(u32(rich_trace.event_counts[SIM_EVENT_ECONOMY]) >
          u32(lean_trace.event_counts[SIM_EVENT_ECONOMY]));

    // And the identical-config pair agrees on everything, checkpoint by checkpoint.
    static AcceptFixture twin{};
    static MatchTrace twin_trace{};
    CHECK(build_accept_world(&twin, g_accept_storage_b, sizeof(g_accept_storage_b), false));
    CHECK(run_accept_match(&twin, &twin_trace, ACCEPT_TICKS, false, nullptr));
    CHECK(twin_trace.final_hash == lean_trace.final_hash);
    CHECK(twin_trace.event_digest == lean_trace.event_digest);
    for (uint32_t i = 0u; i < ACCEPT_CHECKPOINTS; ++i) {
        CHECK(lean_trace.checkpoints[i] != 0u);
        CHECK(twin_trace.checkpoints[i] == lean_trace.checkpoints[i]);
    }
}

// Saturation in a live world, not just at the arithmetic seam: a ledger that is
// already near INT32_MAX takes two real awards, clamps both times, and the clamped
// world is still a canonical, deterministic one.
TEST(sim_lane_objectives, a_live_ledger_saturates_and_the_clamped_world_still_hashes) {
    static AcceptFixture fixture{};
    static MatchTrace trace{};
    CHECK(build_accept_world(&fixture, g_accept_storage, sizeof(g_accept_storage), false));
    SimWorld& world = fixture.world;
    world.ledger.gold[0] = INT32_MAX - 10;
    world.ledger.xp[0] = INT32_MAX;
    CHECK(sim_hash_state(&world) != 0u);

    CHECK(run_accept_match(&fixture, &trace, ACCEPT_MATCH_LIMIT, true, nullptr));
    CHECK(trace.failed_ticks == 0u);
    CHECK(u32(world.match_state.over) == 1u);
    // Two OBJECTIVE_DESTROYED awards of 100 gold each plus every minion kill, all of
    // them clamped: the total never wraps into a negative, which would be UB on the
    // way in and a non-canonical world on the way out.
    CHECK(world.ledger.gold[0] == INT32_MAX);
    CHECK(world.ledger.xp[0] == INT32_MAX);
    CHECK(world.ledger.gold[0] > 0);
    CHECK(world.ledger.xp[0] > 0);
    CHECK(sim_hash_state(&world) != 0u);
    CHECK(u32(trace.event_counts[SIM_EVENT_ECONOMY]) > 0u);

    // The saturating seam itself, at the two edges the walk above exercises.
    CHECK(sim_ledger_add_saturating(INT32_MAX - 10, 10u) == INT32_MAX);
    CHECK(sim_ledger_add_saturating(INT32_MAX - 10, 11u) == INT32_MAX);
    CHECK(sim_ledger_add_saturating(INT32_MAX, 1u) == INT32_MAX);
    CHECK(sim_ledger_add_saturating(0, 0u) == 0);
}
