#include "test.h"
#include "sim/map.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"

#include <cstring>

// M5.0 map grid: storage, conversions, neighbours, lanes, .mapdesc codec.
// The authored 8x8 golden below is the same map as assets/maps/lane_test.mapdesc;
// tests/sim/map_golden.py regenerates both from one description.

static const size_t MAP_TEST_ARENA_BYTES = 65536u;
static const MapConfig SMALL_CONFIG = MapConfig{8u, 8u, 2u, 8u};
static const mm::fix HALF_UNIT = FIX_HALF;

typedef struct MapFixture {
    alignas(16) uint8_t storage[MAP_TEST_ARENA_BYTES];
    Arena arena;
    MapGrid grid;
} MapFixture;

static bool fixture_init(MapFixture* fixture, MapConfig config = SMALL_CONFIG) {
    std::memset(fixture, 0, sizeof(*fixture));
    arena_init_fixed(&fixture->arena, fixture->storage, sizeof(fixture->storage));
    return map_init(&fixture->grid, &fixture->arena, config);
}

// Row-major 8x8 authored map (y grows south). '.' walkable, '#' blocked,
// 'L' lane (walkable + lane), 'S' spawn (walkable + spawn + lane), 'O' objective
// (walkable + objective), 'R' ramp (walkable + ramp, height 1), 'V' vision block.
static const char AUTHORED_ROWS[8][9] = {
    "S.L.....",
    ".#L.#...",
    "..L.....",
    "..LLLL..",
    ".....L..",
    "#V...L.R",
    ".....L..",
    ".....LOS",
};

static uint8_t authored_flags(char c) {
    switch (c) {
        case '.': return MAP_CELL_WALKABLE;
        case '#': return MAP_CELL_BLOCKED;
        case 'L': return MAP_CELL_WALKABLE | MAP_CELL_LANE;
        case 'S': return MAP_CELL_WALKABLE | MAP_CELL_LANE | MAP_CELL_SPAWN;
        case 'O': return MAP_CELL_WALKABLE | MAP_CELL_OBJECTIVE;
        case 'R': return MAP_CELL_WALKABLE | MAP_CELL_RAMP;
        case 'V': return MAP_CELL_BLOCKED | MAP_CELL_BLOCK_VISION;
        default: return 0u;
    }
}

static bool author_map(MapGrid* grid) {
    if (map_set_dimensions(grid, 0x0123456789abcdefULL, 8u, 8u, HALF_UNIT) != MAP_STATUS_OK)
        return false;
    for (uint16_t y = 0u; y < 8u; ++y) {
        for (uint16_t x = 0u; x < 8u; ++x) {
            char c = AUTHORED_ROWS[y][x];
            uint8_t cost = static_cast<uint8_t>(c == 'R' ? 3u : 1u);
            int16_t height = static_cast<int16_t>(c == 'R' ? 1 : 0);
            if (map_set_cell(grid, x, y, authored_flags(c), cost, height) != MAP_STATUS_OK)
                return false;
        }
    }
    // Lane 1 runs from the north spawn (0,0) down the L cells to the south spawn (7,7).
    static const uint16_t waypoints[] = {0u, 2u, 10u, 18u, 26u, 27u, 28u, 29u};
    MapLane lane{};
    lane.lane_id = 1u;
    lane.waypoint_count = 8u;
    lane.wave_interval_ticks = 900u;
    lane.first_wave_tick = 300u;
    lane.creep_archetype_id = 0x00c0ffee00c0ffeeULL;
    return map_add_lane(grid, &lane, waypoints) == MAP_STATUS_OK;
}

// ---------------------------------------------------------------- S1 storage + conversions

TEST(sim_map, empty_config_is_valid_and_costs_no_memory) {
    MapFixture fixture{};
    CHECK(map_config_valid(MapConfig{}));
    CHECK(map_memory_required(MapConfig{}) == 0u);
    CHECK(fixture_init(&fixture, MapConfig{}));
    CHECK(map_valid(&fixture.grid));
    CHECK(fixture.arena.offset == 0u);
    uint32_t cell = 99u;
    CHECK(map_world_to_cell(&fixture.grid, 0, 0, &cell) == MAP_STATUS_OUT_OF_BOUNDS);
    CHECK(!map_is_walkable(&fixture.grid, 0u));
    CHECK(map_set_dimensions(&fixture.grid, 1u, 1u, 1u, FIX_ONE) == MAP_STATUS_INVALID_ARGUMENT);
}

TEST(sim_map, invalid_configs_are_rejected_without_arena_mutation) {
    MapFixture fixture{};
    CHECK(!map_config_valid(MapConfig{SIM_MAP_MAX_WIDTH + 1u, 8u, 1u, 4u}));
    CHECK(!map_config_valid(MapConfig{8u, 0u, 1u, 4u}));
    CHECK(!map_config_valid(MapConfig{8u, 8u, 1u, 0u}));
    CHECK(!map_config_valid(MapConfig{8u, 8u, 0u, 4u}));
    CHECK(!map_config_valid(MapConfig{8u, 8u, SIM_MAP_MAX_LANES + 1u, 4u}));
    std::memset(&fixture, 0, sizeof(fixture));
    arena_init_fixed(&fixture.arena, fixture.storage, 64u);
    CHECK(!map_init(&fixture.grid, &fixture.arena, SMALL_CONFIG)); // budget too small
    CHECK(fixture.arena.offset == 0u);
    CHECK(!fixture.grid.flags);
}

TEST(sim_map, dimensions_and_cell_index_round_trip_every_boundary) {
    MapFixture fixture{};
    CHECK(fixture_init(&fixture));
    CHECK(map_set_dimensions(&fixture.grid, 7u, 8u, 8u, HALF_UNIT) == MAP_STATUS_OK);
    CHECK(map_valid(&fixture.grid));
    CHECK(fixture.grid.cell_count == 64u);
    static const uint16_t corners[4][2] = {{0u, 0u}, {7u, 0u}, {0u, 7u}, {7u, 7u}};
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint32_t cell = 0u;
        uint16_t x = 0u, y = 0u;
        CHECK(map_cell_index(&fixture.grid, corners[i][0], corners[i][1], &cell));
        CHECK(cell == static_cast<uint32_t>(corners[i][1]) * 8u + corners[i][0]);
        CHECK(map_cell_coords(&fixture.grid, cell, &x, &y));
        CHECK(x == corners[i][0] && y == corners[i][1]);
    }
    uint32_t cell = 0u;
    CHECK(!map_cell_index(&fixture.grid, 8u, 0u, &cell));
    CHECK(!map_cell_index(&fixture.grid, 0u, 8u, &cell));
    CHECK(map_set_dimensions(&fixture.grid, 7u, 9u, 8u, HALF_UNIT) == MAP_STATUS_BAD_DIMENSIONS);
    CHECK(map_set_dimensions(&fixture.grid, 7u, 8u, 8u, 0) == MAP_STATUS_BAD_DIMENSIONS);
    CHECK(fixture.grid.width_cells == 8u && fixture.grid.cell_size_q16 == HALF_UNIT); // untouched
}

TEST(sim_map, world_conversions_use_cell_centres_and_floor_semantics) {
    MapFixture fixture{};
    CHECK(fixture_init(&fixture));
    CHECK(map_set_dimensions(&fixture.grid, 7u, 8u, 8u, HALF_UNIT) == MAP_STATUS_OK);
    mm::fix wx = 0, wy = 0;
    CHECK(map_cell_to_world(&fixture.grid, 0u, &wx, &wy) == MAP_STATUS_OK);
    CHECK(wx == FIX_HALF / 2 && wy == FIX_HALF / 2);
    CHECK(map_cell_to_world(&fixture.grid, 63u, &wx, &wy) == MAP_STATUS_OK);
    CHECK(wx == 7 * FIX_HALF + FIX_HALF / 2);
    CHECK(map_cell_to_world(&fixture.grid, 64u, &wx, &wy) == MAP_STATUS_OUT_OF_BOUNDS);

    uint32_t cell = 0u;
    CHECK(map_world_to_cell(&fixture.grid, 0, 0, &cell) == MAP_STATUS_OK && cell == 0u);
    CHECK(map_world_to_cell(&fixture.grid, FIX_HALF - 1, FIX_HALF - 1, &cell) == MAP_STATUS_OK);
    CHECK(cell == 0u);
    CHECK(map_world_to_cell(&fixture.grid, FIX_HALF, FIX_HALF, &cell) == MAP_STATUS_OK);
    CHECK(cell == 9u);
    // -1/65536 of a unit floors to cell -1, which is outside the grid (not cell 0).
    CHECK(map_world_to_cell(&fixture.grid, -1, 0, &cell) == MAP_STATUS_OUT_OF_BOUNDS);
    CHECK(map_world_to_cell(&fixture.grid, 0, -1, &cell) == MAP_STATUS_OUT_OF_BOUNDS);
    CHECK(map_world_to_cell(&fixture.grid, 8 * FIX_HALF, 0, &cell) == MAP_STATUS_OUT_OF_BOUNDS);
    CHECK(map_world_to_cell(&fixture.grid, 8 * FIX_HALF - 1, 8 * FIX_HALF - 1, &cell) == MAP_STATUS_OK);
    CHECK(cell == 63u);
    // Every centre maps back to its own cell.
    for (uint32_t c = 0u; c < 64u; ++c) {
        CHECK(map_cell_to_world(&fixture.grid, c, &wx, &wy) == MAP_STATUS_OK);
        CHECK(map_world_to_cell(&fixture.grid, wx, wy, &cell) == MAP_STATUS_OK && cell == c);
    }
}

// ---------------------------------------------------------------- S2 passability + neighbours

TEST(sim_map, cell_flags_are_validated_and_queryable) {
    MapFixture fixture{};
    CHECK(fixture_init(&fixture));
    CHECK(author_map(&fixture.grid));
    CHECK(map_valid(&fixture.grid));
    CHECK(map_is_walkable(&fixture.grid, 0u));
    CHECK(!map_is_walkable(&fixture.grid, 9u));           // '#'
    CHECK(map_cost(&fixture.grid, 47u) == 3u);            // 'R' at (7,5)
    CHECK(fixture.grid.height_cell[47u] == 1);
    CHECK(map_flags(&fixture.grid, 41u) == (MAP_CELL_BLOCKED | MAP_CELL_BLOCK_VISION));
    CHECK(map_set_cell(&fixture.grid, 0u, 0u, MAP_CELL_WALKABLE | MAP_CELL_BLOCKED, 1u, 0) ==
          MAP_STATUS_BAD_CELL);
    CHECK(map_set_cell(&fixture.grid, 0u, 0u, 0x80u, 1u, 0) == MAP_STATUS_BAD_CELL);
    CHECK(map_set_cell(&fixture.grid, 8u, 0u, MAP_CELL_WALKABLE, 1u, 0) == MAP_STATUS_OUT_OF_BOUNDS);
    CHECK(map_flags(&fixture.grid, 0u) == authored_flags('S'));   // rejects left it alone
}

TEST(sim_map, neighbours_follow_fixed_order_and_corner_rule) {
    MapFixture fixture{};
    CHECK(fixture_init(&fixture));
    CHECK(author_map(&fixture.grid));
    uint32_t out[SIM_MAP_NEIGHBOR_MAX];

    // (0,0): N/W off-grid; E (1,0) and S (0,1) walkable; SE (1,1) is '#'.
    uint32_t count = map_neighbors(&fixture.grid, 0u, out);
    CHECK(count == 2u && out[0] == 1u && out[1] == 8u);

    // (3,2): all four orthogonals walkable; NE (4,1) is '#', so only three diagonals.
    count = map_neighbors(&fixture.grid, 19u, out);
    CHECK(count == 7u);
    CHECK(out[0] == 11u && out[1] == 20u && out[2] == 27u && out[3] == 18u);
    CHECK(out[4] == 28u && out[5] == 26u && out[6] == 10u);

    // (0,1) has '#' to the east: E missing, so NE and SE are not offered even though
    // (1,0) and (1,2) are walkable.
    count = map_neighbors(&fixture.grid, 8u, out);
    CHECK(count == 2u && out[0] == 0u && out[1] == 16u);

    // A blocked cell still reports its walkable neighbours (queries never mutate).
    count = map_neighbors(&fixture.grid, 9u, out);
    CHECK(count == 8u);
    CHECK(map_neighbors(&fixture.grid, 64u, out) == 0u);
}

// ---------------------------------------------------------------- S3 lanes + spawns

TEST(sim_map, lanes_validate_waypoints_and_reject_without_mutation) {
    MapFixture fixture{};
    CHECK(fixture_init(&fixture));
    CHECK(author_map(&fixture.grid));
    CHECK(fixture.grid.lane_count == 1u);
    const uint16_t* waypoints = map_lane_waypoints(&fixture.grid, 0u);
    CHECK(waypoints && waypoints[0] == 0u && waypoints[7] == 29u);
    CHECK(!map_lane_waypoints(&fixture.grid, 1u));

    MapLane lane{};
    lane.lane_id = 2u;
    lane.waypoint_count = 2u;
    static const uint16_t off_grid[] = {0u, 64u};
    static const uint16_t blocked[] = {0u, 9u};
    static const uint16_t not_lane[] = {0u, 1u};
    static const uint16_t duplicate_id[] = {0u, 2u};
    static const uint16_t ok[] = {0u, 2u};
    CHECK(map_add_lane(&fixture.grid, &lane, off_grid) == MAP_STATUS_BAD_LANE);
    CHECK(map_add_lane(&fixture.grid, &lane, blocked) == MAP_STATUS_BAD_LANE);
    CHECK(map_add_lane(&fixture.grid, &lane, not_lane) == MAP_STATUS_BAD_LANE);
    lane.waypoint_count = 1u;
    CHECK(map_add_lane(&fixture.grid, &lane, ok) == MAP_STATUS_BAD_LANE);
    lane.waypoint_count = 9u;
    CHECK(map_add_lane(&fixture.grid, &lane, ok) == MAP_STATUS_BAD_LANE);
    lane.waypoint_count = 2u;
    lane.lane_id = 1u;
    CHECK(map_add_lane(&fixture.grid, &lane, duplicate_id) == MAP_STATUS_BAD_LANE);
    CHECK(fixture.grid.lane_count == 1u);
    lane.lane_id = 2u;
    CHECK(map_add_lane(&fixture.grid, &lane, ok) == MAP_STATUS_OK);
    CHECK(fixture.grid.lane_count == 2u);
    lane.lane_id = 3u;
    CHECK(map_add_lane(&fixture.grid, &lane, ok) == MAP_STATUS_CAPACITY);
    CHECK(map_valid(&fixture.grid));
}

TEST(sim_map, spawns_are_found_in_ascending_cell_order) {
    MapFixture fixture{};
    CHECK(fixture_init(&fixture));
    CHECK(author_map(&fixture.grid));
    uint32_t cell = 0u;
    CHECK(map_find_spawn(&fixture.grid, 0u, &cell) == MAP_STATUS_OK && cell == 0u);
    CHECK(map_find_spawn(&fixture.grid, 1u, &cell) == MAP_STATUS_OK && cell == 63u);
    CHECK(map_find_spawn(&fixture.grid, 2u, &cell) == MAP_STATUS_OUT_OF_BOUNDS);
}

// ---------------------------------------------------------------- S4 .mapdesc codec

// Byte-identical to assets/maps/lane_test.mapdesc (generated by tests/sim/map_golden.py).
#include "map_golden.inc"

TEST(sim_map, encode_matches_golden_and_decode_round_trips) {
    MapFixture authored{};
    CHECK(fixture_init(&authored));
    CHECK(author_map(&authored.grid));

    uint8_t buffer[1024];
    ByteWriter writer;
    byte_writer_init(&writer, buffer, sizeof(buffer));
    CHECK(map_encode(&authored.grid, &writer) == MAP_STATUS_OK);
    CHECK(byte_writer_size(&writer) == sizeof(MAP_GOLDEN_MAPDESC));
    CHECK(std::memcmp(buffer, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC)) == 0);

    MapFixture decoded{};
    CHECK(fixture_init(&decoded));
    ByteReader reader;
    byte_reader_init(&reader, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    CHECK(map_decode(&decoded.grid, &reader) == MAP_STATUS_OK);
    CHECK(map_require_end(&reader) == MAP_STATUS_OK);
    CHECK(map_valid(&decoded.grid));
    CHECK(decoded.grid.map_id == authored.grid.map_id);
    CHECK(decoded.grid.width_cells == 8u && decoded.grid.height_cells == 8u);
    CHECK(decoded.grid.cell_size_q16 == HALF_UNIT);
    CHECK(std::memcmp(decoded.grid.flags, authored.grid.flags, 64u) == 0);
    CHECK(std::memcmp(decoded.grid.movement_cost, authored.grid.movement_cost, 64u) == 0);
    CHECK(std::memcmp(decoded.grid.height_cell, authored.grid.height_cell, 64u * sizeof(int16_t)) == 0);
    CHECK(decoded.grid.lane_count == 1u);
    CHECK(std::memcmp(map_lane_waypoints(&decoded.grid, 0u), map_lane_waypoints(&authored.grid, 0u),
                      8u * sizeof(uint16_t)) == 0);
    CHECK(decoded.grid.lanes[0].creep_archetype_id == 0x00c0ffee00c0ffeeULL);

    // Re-encoding the decoded grid is byte-identical (deterministic cook).
    uint8_t again[1024];
    ByteWriter writer2;
    byte_writer_init(&writer2, again, sizeof(again));
    CHECK(map_encode(&decoded.grid, &writer2) == MAP_STATUS_OK);
    CHECK(byte_writer_size(&writer2) == sizeof(MAP_GOLDEN_MAPDESC));
    CHECK(std::memcmp(again, buffer, sizeof(MAP_GOLDEN_MAPDESC)) == 0);

    // Encode refuses a buffer that is one byte short and poisons the writer.
    ByteWriter tight;
    byte_writer_init(&tight, buffer, sizeof(MAP_GOLDEN_MAPDESC) - 1u);
    CHECK(map_encode(&authored.grid, &tight) == MAP_STATUS_OVERFLOW);
    CHECK(!byte_writer_ok(&tight));
}

static MapStatus decode_bytes(MapFixture* fixture, const uint8_t* bytes, size_t size,
                              bool* out_untouched) {
    CHECK(fixture_init(fixture));
    CHECK(map_set_dimensions(&fixture->grid, 0xfeedULL, 2u, 2u, FIX_ONE) == MAP_STATUS_OK);
    CHECK(map_set_cell(&fixture->grid, 1u, 1u, MAP_CELL_WALKABLE, 5u, 2) == MAP_STATUS_OK);
    ByteReader reader;
    byte_reader_init(&reader, bytes, size);
    MapStatus status = map_decode(&fixture->grid, &reader);
    if (status != MAP_STATUS_OK) CHECK(!byte_reader_ok(&reader));
    *out_untouched = fixture->grid.map_id == 0xfeedULL && fixture->grid.width_cells == 2u &&
                     fixture->grid.cell_count == 4u && fixture->grid.lane_count == 0u &&
                     fixture->grid.flags[3] == MAP_CELL_WALKABLE &&
                     fixture->grid.movement_cost[3] == 5u && fixture->grid.height_cell[3] == 2;
    return status;
}

TEST(sim_map, decode_rejects_each_malformed_stream_at_first_invalid_boundary) {
    MapFixture fixture{};
    uint8_t bytes[sizeof(MAP_GOLDEN_MAPDESC) + 4u];
    bool untouched = false;

    // truncated header
    CHECK(decode_bytes(&fixture, MAP_GOLDEN_MAPDESC, 10u, &untouched) == MAP_STATUS_TRUNCATED);
    CHECK(untouched);
    // truncated cell block
    CHECK(decode_bytes(&fixture, MAP_GOLDEN_MAPDESC, MAPDESC_HEADER_ENCODED_SIZE + 40u, &untouched) ==
          MAP_STATUS_TRUNCATED);
    CHECK(untouched);
    // truncated lane block
    CHECK(decode_bytes(&fixture, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC) - 1u, &untouched) ==
          MAP_STATUS_TRUNCATED);
    CHECK(untouched);
    // bad magic
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[0] = 'X';
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC), &untouched) == MAP_STATUS_BAD_MAGIC);
    CHECK(untouched);
    // bad version
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[4] = 2u;
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC), &untouched) == MAP_STATUS_BAD_VERSION);
    CHECK(untouched);
    // dimensions exceed capacity (width 9 on an 8-wide fixture)
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[16] = 9u;
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC), &untouched) == MAP_STATUS_BAD_DIMENSIONS);
    CHECK(untouched);
    // cell_count disagrees with width*height
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[24] = 63u;
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC), &untouched) == MAP_STATUS_BAD_COUNT);
    CHECK(untouched);
    // cell with walkable + blocked
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[MAPDESC_HEADER_ENCODED_SIZE + 4u * 5u] = MAP_CELL_WALKABLE | MAP_CELL_BLOCKED;
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC), &untouched) == MAP_STATUS_BAD_CELL);
    CHECK(untouched);
    // lane waypoint on a non-lane cell
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[MAPDESC_HEADER_ENCODED_SIZE + 64u * 4u + 2u + 16u] = 1u; // first waypoint -> cell 1 ('.')
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC), &untouched) == MAP_STATUS_BAD_LANE);
    CHECK(untouched);
    // lane count beyond capacity
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[MAPDESC_HEADER_ENCODED_SIZE + 64u * 4u] = 3u;
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC), &untouched) == MAP_STATUS_CAPACITY);
    CHECK(untouched);
    // trailing bytes
    std::memcpy(bytes, MAP_GOLDEN_MAPDESC, sizeof(MAP_GOLDEN_MAPDESC));
    bytes[sizeof(MAP_GOLDEN_MAPDESC)] = 0u;
    CHECK(decode_bytes(&fixture, bytes, sizeof(MAP_GOLDEN_MAPDESC) + 1u, &untouched) == MAP_STATUS_OK);
    ByteReader reader;
    byte_reader_init(&reader, bytes, sizeof(MAP_GOLDEN_MAPDESC) + 1u);
    MapFixture again{};
    CHECK(fixture_init(&again));
    CHECK(map_decode(&again.grid, &reader) == MAP_STATUS_OK);
    CHECK(map_require_end(&reader) == MAP_STATUS_TRAILING_BYTES);
}

// ---------------------------------------------------------------- SimWorld integration

TEST(sim_map, sim_world_carries_the_map_and_stays_transactional) {
    alignas(16) static uint8_t storage[262144u];
    Arena arena;
    arena_init_fixed(&arena, storage, sizeof(storage));
    SimWorldConfig config = SimWorldConfig{64u, 4u, 8u, SMALL_CONFIG};
    SimWorld world{};
    CHECK(sim_world_memory_required(config) > sim_world_memory_required(SimWorldConfig{64u, 4u, 8u}));
    CHECK(sim_init(&world, &arena, 7u, config));
    CHECK(map_valid(&world.map));
    CHECK(world.map.config.max_width == 8u && world.map.flags);
    CHECK(author_map(&world.map));
    CHECK(sim_hash_state(&world) != 0u);

    // An invalid map config invalidates the world config as a whole.
    SimWorldConfig bad = config;
    bad.map.max_lanes = SIM_MAP_MAX_LANES + 1u;
    CHECK(sim_world_memory_required(bad) == 0u);
    size_t before = arena.offset;
    SimWorld untouched{};
    CHECK(!sim_init(&untouched, &arena, 7u, bad));
    CHECK(arena.offset == before);
}
