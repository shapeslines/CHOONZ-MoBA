#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/mem.h"
#include "math/fix.h"
#include "serialize/byte_io.h"

// M5.0 map grid: the single fixed-extent integer tile grid that is the simulation
// authority for passability, lanes, and spawns (ARCHITECTURE.md section 12.2,
// slate-moba-proto-design.md section 3.2). Storage is SoA in a caller-owned arena,
// coordinates are Q16.16 (ADR-0002), and nothing here reads platform, renderer,
// or RNG state (ADR-0013: navigation never draws).
//
// Serialized field order for .mapdesc is exactly the order of the MapGrid and
// MapLane fields below (little-endian, explicit counts, cells row-major y then x).

static const uint16_t SIM_MAP_MAX_WIDTH = 512;
static const uint16_t SIM_MAP_MAX_HEIGHT = 512;
static const uint16_t SIM_MAP_MAX_LANES = 8;
static const uint8_t SIM_MAP_MAX_WAYPOINTS = 32;
static const uint32_t SIM_MAP_SCHEMA_VERSION = 1;
static const uint32_t SIM_MAP_NEIGHBOR_MAX = 8;

static const uint8_t MAPDESC_MAGIC[4] = {'M', 'A', 'P', 'D'};
// magic(4) schema_version(4) map_id(8) width(2) height(2) cell_size(4) cell_count(4)
static const size_t MAPDESC_HEADER_ENCODED_SIZE = 28;
static const size_t MAPDESC_CELL_ENCODED_SIZE = 4;    // flags(1) movement_cost(1) height_cell(2)
static const size_t MAPDESC_LANE_BASE_ENCODED_SIZE = 16; // lane_id(1) waypoint_count(1) interval(2) first(4) archetype(8)
static const size_t MAPDESC_WAYPOINT_ENCODED_SIZE = 2;

typedef enum MapCellFlag : uint8_t {
    MAP_CELL_WALKABLE = 0x01,
    MAP_CELL_BLOCKED = 0x02,
    MAP_CELL_LANE = 0x04,
    MAP_CELL_SPAWN = 0x08,
    MAP_CELL_OBJECTIVE = 0x10,
    MAP_CELL_BLOCK_VISION = 0x20,
    MAP_CELL_RAMP = 0x40,
} MapCellFlag;

static const uint8_t MAP_CELL_FLAG_MASK = 0x7F;

typedef enum MapStatus : uint8_t {
    MAP_STATUS_OK = 0,
    MAP_STATUS_INVALID_ARGUMENT,
    MAP_STATUS_OUT_OF_BOUNDS,
    MAP_STATUS_BAD_DIMENSIONS,
    MAP_STATUS_BAD_CELL,
    MAP_STATUS_BAD_LANE,
    MAP_STATUS_CAPACITY,
    MAP_STATUS_TRUNCATED,
    MAP_STATUS_BAD_MAGIC,
    MAP_STATUS_BAD_VERSION,
    MAP_STATUS_BAD_COUNT,
    MAP_STATUS_OVERFLOW,
    MAP_STATUS_TRAILING_BYTES,
} MapStatus;

// Capacity the arena reserves. An all-zero config is the empty map: valid, no
// cells, no lanes, no storage; every query reports OUT_OF_BOUNDS.
typedef struct MapConfig {
    uint16_t max_width;
    uint16_t max_height;
    uint16_t max_lanes;
    uint8_t max_waypoints;
} MapConfig;

typedef struct MapLane {
    uint8_t lane_id;
    uint8_t waypoint_count;
    uint16_t wave_interval_ticks;
    uint32_t first_wave_tick;
    uint64_t creep_archetype_id;
} MapLane;

typedef struct MapGrid {
    MapConfig config;
    uint32_t schema_version;
    uint64_t map_id;
    uint16_t width_cells;
    uint16_t height_cells;
    mm::fix cell_size_q16;
    uint32_t cell_count;
    uint8_t* flags;          // MAP_CELL_* bits, row-major y then x
    uint8_t* movement_cost;
    int16_t* height_cell;
    uint16_t lane_count;
    MapLane* lanes;
    uint16_t* lane_waypoints; // lane i owns [i * max_waypoints, +waypoint_count)
} MapGrid;

bool map_config_valid(MapConfig config);
bool map_config_is_empty(MapConfig config);
size_t map_memory_required(MapConfig config);

// Transactional: on failure the grid and the arena offset are untouched.
bool map_init(MapGrid* grid, Arena* arena, MapConfig config);

// Structural validity used by the canonical hash and the codec: pointers present
// when capacity is nonzero, dimensions within capacity, cell flags in range and
// not both walkable and blocked, lanes well-formed against the cells.
bool map_valid(const MapGrid* grid);

// Sets identity and dimensions, clears every cell to zero flags/cost/height and
// drops all lanes. Rejects without mutation when the dimensions exceed capacity.
MapStatus map_set_dimensions(MapGrid* grid, uint64_t map_id, uint16_t width_cells,
                             uint16_t height_cells, mm::fix cell_size_q16);
MapStatus map_set_cell(MapGrid* grid, uint16_t x, uint16_t y, uint8_t flags,
                       uint8_t movement_cost, int16_t height_cell);

bool map_cell_index(const MapGrid* grid, uint16_t x, uint16_t y, uint32_t* out_cell);
bool map_cell_coords(const MapGrid* grid, uint32_t cell, uint16_t* out_x, uint16_t* out_y);
// Cell centre in world units: cell * size + size / 2.
MapStatus map_cell_to_world(const MapGrid* grid, uint32_t cell,
                            mm::fix* out_world_x, mm::fix* out_world_y);
// Floor semantics (negative world coordinates floor toward the lower cell), then
// bounds-checked against the grid.
MapStatus map_world_to_cell(const MapGrid* grid, mm::fix world_x, mm::fix world_y,
                            uint32_t* out_cell);

bool map_is_walkable(const MapGrid* grid, uint32_t cell);
uint8_t map_cost(const MapGrid* grid, uint32_t cell);
uint8_t map_flags(const MapGrid* grid, uint32_t cell);
// Walkable neighbours in the fixed order N, E, S, W, NE, SE, SW, NW (y grows
// south). A diagonal is offered only when both of its orthogonal neighbours are
// walkable. Returns the count written to out_cells[SIM_MAP_NEIGHBOR_MAX].
uint32_t map_neighbors(const MapGrid* grid, uint32_t cell, uint32_t* out_cells);

// Lane waypoints are cell indices; each must be in bounds, walkable, and flagged
// MAP_CELL_LANE; at least two are required. Rejects without mutation.
MapStatus map_add_lane(MapGrid* grid, const MapLane* lane, const uint16_t* waypoints);
const uint16_t* map_lane_waypoints(const MapGrid* grid, uint16_t lane_index);
// ordinal-th spawn-flagged cell in ascending cell order.
MapStatus map_find_spawn(const MapGrid* grid, uint32_t ordinal, uint32_t* out_cell);

// .mapdesc codec. Decode validates the entire stream against the grid's capacity
// before writing anything, so a rejected stream leaves the grid untouched; the
// reported status names the first invalid boundary.
MapStatus map_encode(const MapGrid* grid, ByteWriter* writer);
MapStatus map_decode(MapGrid* grid, ByteReader* reader);
MapStatus map_require_end(const ByteReader* reader);
const char* map_status_string(MapStatus status);
