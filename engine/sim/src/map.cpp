#include "sim/map.h"

#include <cstring>

// ---------------------------------------------------------------- config / init

bool map_config_is_empty(MapConfig config) {
    return config.max_width == 0u && config.max_height == 0u &&
           config.max_lanes == 0u && config.max_waypoints == 0u;
}

bool map_config_valid(MapConfig config) {
    if (map_config_is_empty(config)) return true;
    if (config.max_width == 0u || config.max_height == 0u) return false;
    if (config.max_width > SIM_MAP_MAX_WIDTH || config.max_height > SIM_MAP_MAX_HEIGHT)
        return false;
    if (config.max_lanes > SIM_MAP_MAX_LANES || config.max_waypoints > SIM_MAP_MAX_WAYPOINTS)
        return false;
    // A lane without waypoint capacity, or waypoint capacity without lanes, is a
    // configuration mistake rather than a valid empty state.
    if ((config.max_lanes == 0u) != (config.max_waypoints == 0u)) return false;
    return true;
}

static uint32_t config_cell_capacity(MapConfig config) {
    return static_cast<uint32_t>(config.max_width) * static_cast<uint32_t>(config.max_height);
}

static uint32_t config_waypoint_capacity(MapConfig config) {
    return static_cast<uint32_t>(config.max_lanes) * static_cast<uint32_t>(config.max_waypoints);
}

static bool add_required(size_t* total, size_t bytes, size_t align) {
    if (bytes == 0u) return true;
    size_t slack = align - 1u;
    if (bytes > SIZE_MAX - slack || bytes + slack > SIZE_MAX - *total) return false;
    *total += bytes + slack;
    return true;
}

size_t map_memory_required(MapConfig config) {
    if (!map_config_valid(config) || map_config_is_empty(config)) return 0u;
    size_t cells = config_cell_capacity(config);
    size_t waypoints = config_waypoint_capacity(config);
    size_t total = 0u;
    if (!add_required(&total, cells * sizeof(uint8_t), alignof(uint8_t)) ||
        !add_required(&total, cells * sizeof(uint8_t), alignof(uint8_t)) ||
        !add_required(&total, cells * sizeof(int16_t), alignof(int16_t)) ||
        !add_required(&total, static_cast<size_t>(config.max_lanes) * sizeof(MapLane),
                      alignof(MapLane)) ||
        !add_required(&total, waypoints * sizeof(uint16_t), alignof(uint16_t))) return 0u;
    return total;
}

bool map_init(MapGrid* grid, Arena* arena, MapConfig config) {
    if (!grid || !map_config_valid(config)) return false;
    MapGrid staged{};
    staged.config = config;
    staged.schema_version = SIM_MAP_SCHEMA_VERSION;
    if (map_config_is_empty(config)) {
        *grid = staged;
        return true;
    }
    size_t required = map_memory_required(config);
    if (!arena || !arena->base || arena->offset > arena->reserved || required == 0u ||
        required > arena->reserved - arena->offset) return false;

    TempMemory temp = temp_begin(arena);
    uint32_t cells = config_cell_capacity(config);
    uint32_t waypoints = config_waypoint_capacity(config);
    staged.flags = ARENA_PUSH_ARRAY(arena, uint8_t, cells);
    staged.movement_cost = ARENA_PUSH_ARRAY(arena, uint8_t, cells);
    staged.height_cell = ARENA_PUSH_ARRAY(arena, int16_t, cells);
    staged.lanes = config.max_lanes ? ARENA_PUSH_ARRAY(arena, MapLane, config.max_lanes) : nullptr;
    staged.lane_waypoints = waypoints ? ARENA_PUSH_ARRAY(arena, uint16_t, waypoints) : nullptr;
    if (!staged.flags || !staged.movement_cost || !staged.height_cell ||
        (config.max_lanes && !staged.lanes) || (waypoints && !staged.lane_waypoints)) {
        temp_end(temp);
        return false;
    }
    *grid = staged;
    return true;
}

// ---------------------------------------------------------------- validity

static bool cell_flags_valid(uint8_t flags) {
    if (flags & static_cast<uint8_t>(~MAP_CELL_FLAG_MASK)) return false;
    if ((flags & MAP_CELL_WALKABLE) && (flags & MAP_CELL_BLOCKED)) return false;
    return true;
}

static bool lane_valid_for_grid(const MapGrid* grid, const MapLane* lane,
                                const uint16_t* waypoints) {
    if (!lane || !waypoints) return false;
    if (lane->waypoint_count < 2u || lane->waypoint_count > grid->config.max_waypoints)
        return false;
    for (uint32_t i = 0u; i < lane->waypoint_count; ++i) {
        uint32_t cell = waypoints[i];
        if (cell >= grid->cell_count) return false;
        uint8_t flags = grid->flags[cell];
        if (!(flags & MAP_CELL_WALKABLE) || !(flags & MAP_CELL_LANE)) return false;
    }
    return true;
}

bool map_valid(const MapGrid* grid) {
    if (!grid || !map_config_valid(grid->config)) return false;
    if (grid->schema_version != SIM_MAP_SCHEMA_VERSION) return false;
    if (map_config_is_empty(grid->config)) {
        return grid->width_cells == 0u && grid->height_cells == 0u && grid->cell_count == 0u &&
               grid->lane_count == 0u && !grid->flags && !grid->movement_cost &&
               !grid->height_cell && !grid->lanes && !grid->lane_waypoints;
    }
    if (!grid->flags || !grid->movement_cost || !grid->height_cell) return false;
    if (grid->config.max_lanes && (!grid->lanes || !grid->lane_waypoints)) return false;
    if (grid->width_cells > grid->config.max_width || grid->height_cells > grid->config.max_height)
        return false;
    if ((grid->width_cells == 0u) != (grid->height_cells == 0u)) return false;
    uint32_t expected_cells =
        static_cast<uint32_t>(grid->width_cells) * static_cast<uint32_t>(grid->height_cells);
    if (grid->cell_count != expected_cells) return false;
    if (grid->cell_count && grid->cell_size_q16 <= 0) return false;
    if (grid->lane_count > grid->config.max_lanes) return false;
    for (uint32_t cell = 0u; cell < grid->cell_count; ++cell) {
        if (!cell_flags_valid(grid->flags[cell])) return false;
    }
    for (uint32_t lane = 0u; lane < grid->lane_count; ++lane) {
        const uint16_t* waypoints =
            grid->lane_waypoints + static_cast<size_t>(lane) * grid->config.max_waypoints;
        if (!lane_valid_for_grid(grid, &grid->lanes[lane], waypoints)) return false;
        for (uint32_t previous = 0u; previous < lane; ++previous) {
            if (grid->lanes[previous].lane_id == grid->lanes[lane].lane_id) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- authoring

static bool dimensions_fit(const MapGrid* grid, uint16_t width, uint16_t height,
                           mm::fix cell_size) {
    if (width == 0u || height == 0u || cell_size <= 0) return false;
    if (width > grid->config.max_width || height > grid->config.max_height) return false;
    // Every cell centre must stay representable in Q16.16.
    int64_t extent_x = static_cast<int64_t>(width) * cell_size;
    int64_t extent_y = static_cast<int64_t>(height) * cell_size;
    return extent_x <= INT32_MAX && extent_y <= INT32_MAX;
}

MapStatus map_set_dimensions(MapGrid* grid, uint64_t map_id, uint16_t width_cells,
                             uint16_t height_cells, mm::fix cell_size_q16) {
    if (!grid || !map_config_valid(grid->config) || map_config_is_empty(grid->config))
        return MAP_STATUS_INVALID_ARGUMENT;
    if (!dimensions_fit(grid, width_cells, height_cells, cell_size_q16))
        return MAP_STATUS_BAD_DIMENSIONS;
    uint32_t cells = static_cast<uint32_t>(width_cells) * static_cast<uint32_t>(height_cells);
    std::memset(grid->flags, 0, cells);
    std::memset(grid->movement_cost, 0, cells);
    std::memset(grid->height_cell, 0, static_cast<size_t>(cells) * sizeof(int16_t));
    grid->map_id = map_id;
    grid->width_cells = width_cells;
    grid->height_cells = height_cells;
    grid->cell_size_q16 = cell_size_q16;
    grid->cell_count = cells;
    grid->lane_count = 0u;
    return MAP_STATUS_OK;
}

MapStatus map_set_cell(MapGrid* grid, uint16_t x, uint16_t y, uint8_t flags,
                       uint8_t movement_cost, int16_t height_cell) {
    uint32_t cell;
    if (!map_cell_index(grid, x, y, &cell)) return MAP_STATUS_OUT_OF_BOUNDS;
    if (!cell_flags_valid(flags)) return MAP_STATUS_BAD_CELL;
    grid->flags[cell] = flags;
    grid->movement_cost[cell] = movement_cost;
    grid->height_cell[cell] = height_cell;
    return MAP_STATUS_OK;
}

// ---------------------------------------------------------------- coordinates

bool map_cell_index(const MapGrid* grid, uint16_t x, uint16_t y, uint32_t* out_cell) {
    if (!grid || !out_cell || !grid->flags) return false;
    if (x >= grid->width_cells || y >= grid->height_cells) return false;
    *out_cell = static_cast<uint32_t>(y) * grid->width_cells + x;
    return true;
}

bool map_cell_coords(const MapGrid* grid, uint32_t cell, uint16_t* out_x, uint16_t* out_y) {
    if (!grid || !out_x || !out_y || grid->width_cells == 0u || cell >= grid->cell_count)
        return false;
    *out_x = static_cast<uint16_t>(cell % grid->width_cells);
    *out_y = static_cast<uint16_t>(cell / grid->width_cells);
    return true;
}

MapStatus map_cell_to_world(const MapGrid* grid, uint32_t cell,
                            mm::fix* out_world_x, mm::fix* out_world_y) {
    uint16_t x, y;
    if (!out_world_x || !out_world_y) return MAP_STATUS_INVALID_ARGUMENT;
    if (!map_cell_coords(grid, cell, &x, &y)) return MAP_STATUS_OUT_OF_BOUNDS;
    int64_t size = grid->cell_size_q16;
    int64_t half = size / 2;
    int64_t wx = static_cast<int64_t>(x) * size + half;
    int64_t wy = static_cast<int64_t>(y) * size + half;
    if (wx > INT32_MAX || wy > INT32_MAX) return MAP_STATUS_OVERFLOW;
    *out_world_x = static_cast<mm::fix>(wx);
    *out_world_y = static_cast<mm::fix>(wy);
    return MAP_STATUS_OK;
}

// fix_div truncates toward zero; grid lookup needs the floor so that a coordinate
// just below zero lands outside the grid rather than in cell zero.
static int64_t floor_div(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator;
    int64_t remainder = numerator % denominator;
    if (remainder != 0 && ((remainder < 0) != (denominator < 0))) --quotient;
    return quotient;
}

MapStatus map_world_to_cell(const MapGrid* grid, mm::fix world_x, mm::fix world_y,
                            uint32_t* out_cell) {
    if (!grid || !out_cell) return MAP_STATUS_INVALID_ARGUMENT;
    if (!grid->flags || grid->cell_count == 0u || grid->cell_size_q16 <= 0)
        return MAP_STATUS_OUT_OF_BOUNDS;
    int64_t cx = floor_div(world_x, grid->cell_size_q16);
    int64_t cy = floor_div(world_y, grid->cell_size_q16);
    if (cx < 0 || cy < 0 || cx >= grid->width_cells || cy >= grid->height_cells)
        return MAP_STATUS_OUT_OF_BOUNDS;
    *out_cell = static_cast<uint32_t>(cy) * grid->width_cells + static_cast<uint32_t>(cx);
    return MAP_STATUS_OK;
}

// ---------------------------------------------------------------- queries

bool map_is_walkable(const MapGrid* grid, uint32_t cell) {
    if (!grid || !grid->flags || cell >= grid->cell_count) return false;
    return (grid->flags[cell] & MAP_CELL_WALKABLE) != 0u;
}

uint8_t map_cost(const MapGrid* grid, uint32_t cell) {
    if (!grid || !grid->movement_cost || cell >= grid->cell_count) return 0u;
    return grid->movement_cost[cell];
}

uint8_t map_flags(const MapGrid* grid, uint32_t cell) {
    if (!grid || !grid->flags || cell >= grid->cell_count) return 0u;
    return grid->flags[cell];
}

static bool walkable_at(const MapGrid* grid, int32_t x, int32_t y, uint32_t* out_cell) {
    if (x < 0 || y < 0 || x >= grid->width_cells || y >= grid->height_cells) return false;
    uint32_t cell = static_cast<uint32_t>(y) * grid->width_cells + static_cast<uint32_t>(x);
    if (!(grid->flags[cell] & MAP_CELL_WALKABLE)) return false;
    *out_cell = cell;
    return true;
}

uint32_t map_neighbors(const MapGrid* grid, uint32_t cell, uint32_t* out_cells) {
    uint16_t ux, uy;
    if (!out_cells || !map_cell_coords(grid, cell, &ux, &uy)) return 0u;
    int32_t x = ux;
    int32_t y = uy;
    uint32_t count = 0u;
    uint32_t n = 0u, e = 0u, s = 0u, w = 0u, d = 0u;
    bool has_n = walkable_at(grid, x, y - 1, &n);
    bool has_e = walkable_at(grid, x + 1, y, &e);
    bool has_s = walkable_at(grid, x, y + 1, &s);
    bool has_w = walkable_at(grid, x - 1, y, &w);
    if (has_n) out_cells[count++] = n;
    if (has_e) out_cells[count++] = e;
    if (has_s) out_cells[count++] = s;
    if (has_w) out_cells[count++] = w;
    if (has_n && has_e && walkable_at(grid, x + 1, y - 1, &d)) out_cells[count++] = d;
    if (has_s && has_e && walkable_at(grid, x + 1, y + 1, &d)) out_cells[count++] = d;
    if (has_s && has_w && walkable_at(grid, x - 1, y + 1, &d)) out_cells[count++] = d;
    if (has_n && has_w && walkable_at(grid, x - 1, y - 1, &d)) out_cells[count++] = d;
    return count;
}

// ---------------------------------------------------------------- lanes / spawns

MapStatus map_add_lane(MapGrid* grid, const MapLane* lane, const uint16_t* waypoints) {
    if (!grid || !lane || !waypoints) return MAP_STATUS_INVALID_ARGUMENT;
    if (!grid->lanes || !grid->lane_waypoints || grid->cell_count == 0u)
        return MAP_STATUS_INVALID_ARGUMENT;
    if (grid->lane_count >= grid->config.max_lanes) return MAP_STATUS_CAPACITY;
    if (!lane_valid_for_grid(grid, lane, waypoints)) return MAP_STATUS_BAD_LANE;
    for (uint32_t previous = 0u; previous < grid->lane_count; ++previous) {
        if (grid->lanes[previous].lane_id == lane->lane_id) return MAP_STATUS_BAD_LANE;
    }
    uint16_t* slot = grid->lane_waypoints +
                     static_cast<size_t>(grid->lane_count) * grid->config.max_waypoints;
    std::memset(slot, 0, static_cast<size_t>(grid->config.max_waypoints) * sizeof(uint16_t));
    std::memcpy(slot, waypoints, static_cast<size_t>(lane->waypoint_count) * sizeof(uint16_t));
    grid->lanes[grid->lane_count] = *lane;
    ++grid->lane_count;
    return MAP_STATUS_OK;
}

const uint16_t* map_lane_waypoints(const MapGrid* grid, uint16_t lane_index) {
    if (!grid || !grid->lane_waypoints || lane_index >= grid->lane_count) return nullptr;
    return grid->lane_waypoints + static_cast<size_t>(lane_index) * grid->config.max_waypoints;
}

MapStatus map_find_spawn(const MapGrid* grid, uint32_t ordinal, uint32_t* out_cell) {
    if (!grid || !out_cell) return MAP_STATUS_INVALID_ARGUMENT;
    if (!grid->flags) return MAP_STATUS_OUT_OF_BOUNDS;
    uint32_t seen = 0u;
    for (uint32_t cell = 0u; cell < grid->cell_count; ++cell) {
        if (!(grid->flags[cell] & MAP_CELL_SPAWN)) continue;
        if (seen == ordinal) {
            *out_cell = cell;
            return MAP_STATUS_OK;
        }
        ++seen;
    }
    return MAP_STATUS_OUT_OF_BOUNDS;
}

// ---------------------------------------------------------------- codec

static bool writer_has(ByteWriter* writer, size_t size) {
    if (!writer || !writer->ok || writer->offset > writer->capacity ||
        size > writer->capacity - writer->offset) {
        if (writer) writer->ok = false;
        return false;
    }
    return true;
}

static MapStatus reader_fail(ByteReader* reader, MapStatus status) {
    if (reader) reader->ok = false;
    return status;
}

MapStatus map_encode(const MapGrid* grid, ByteWriter* writer) {
    if (!map_valid(grid) || map_config_is_empty(grid->config) || grid->cell_count == 0u)
        return MAP_STATUS_INVALID_ARGUMENT;
    size_t lane_bytes = 0u;
    for (uint32_t lane = 0u; lane < grid->lane_count; ++lane) {
        lane_bytes += MAPDESC_LANE_BASE_ENCODED_SIZE +
                      static_cast<size_t>(grid->lanes[lane].waypoint_count) *
                          MAPDESC_WAYPOINT_ENCODED_SIZE;
    }
    size_t total = MAPDESC_HEADER_ENCODED_SIZE +
                   static_cast<size_t>(grid->cell_count) * MAPDESC_CELL_ENCODED_SIZE +
                   sizeof(uint16_t) + lane_bytes;
    if (!writer_has(writer, total)) return MAP_STATUS_OVERFLOW;

    byte_writer_write_bytes(writer, MAPDESC_MAGIC, sizeof(MAPDESC_MAGIC));
    byte_writer_write_u32(writer, grid->schema_version);
    byte_writer_write_u64(writer, grid->map_id);
    byte_writer_write_u16(writer, grid->width_cells);
    byte_writer_write_u16(writer, grid->height_cells);
    byte_writer_write_i32(writer, grid->cell_size_q16);
    byte_writer_write_u32(writer, grid->cell_count);
    for (uint32_t cell = 0u; cell < grid->cell_count; ++cell) {
        byte_writer_write_u8(writer, grid->flags[cell]);
        byte_writer_write_u8(writer, grid->movement_cost[cell]);
        byte_writer_write_i16(writer, grid->height_cell[cell]);
    }
    byte_writer_write_u16(writer, grid->lane_count);
    for (uint32_t lane = 0u; lane < grid->lane_count; ++lane) {
        const MapLane& def = grid->lanes[lane];
        const uint16_t* waypoints = map_lane_waypoints(grid, static_cast<uint16_t>(lane));
        byte_writer_write_u8(writer, def.lane_id);
        byte_writer_write_u8(writer, def.waypoint_count);
        byte_writer_write_u16(writer, def.wave_interval_ticks);
        byte_writer_write_u32(writer, def.first_wave_tick);
        byte_writer_write_u64(writer, def.creep_archetype_id);
        for (uint32_t i = 0u; i < def.waypoint_count; ++i)
            byte_writer_write_u16(writer, waypoints[i]);
    }
    return byte_writer_ok(writer) ? MAP_STATUS_OK : MAP_STATUS_OVERFLOW;
}

typedef struct MapDescHeader {
    uint32_t schema_version;
    uint64_t map_id;
    uint16_t width_cells;
    uint16_t height_cells;
    mm::fix cell_size_q16;
    uint32_t cell_count;
} MapDescHeader;

static MapStatus read_header(ByteReader* cursor, MapDescHeader* out) {
    uint8_t magic[4];
    MapDescHeader header{};
    if (!byte_reader_read_bytes(cursor, magic, sizeof(magic)) ||
        !byte_reader_read_u32(cursor, &header.schema_version) ||
        !byte_reader_read_u64(cursor, &header.map_id) ||
        !byte_reader_read_u16(cursor, &header.width_cells) ||
        !byte_reader_read_u16(cursor, &header.height_cells) ||
        !byte_reader_read_i32(cursor, &header.cell_size_q16) ||
        !byte_reader_read_u32(cursor, &header.cell_count)) return MAP_STATUS_TRUNCATED;
    if (std::memcmp(magic, MAPDESC_MAGIC, sizeof(magic)) != 0) return MAP_STATUS_BAD_MAGIC;
    if (header.schema_version != SIM_MAP_SCHEMA_VERSION) return MAP_STATUS_BAD_VERSION;
    *out = header;
    return MAP_STATUS_OK;
}

// One walk validates every boundary of the stream against the grid's capacity and
// against the streamed cells themselves (lane waypoints are checked by peeking at
// the cell block already read), so nothing is written unless the entire stream is
// accepted. The apply walk then repeats the same reads and stores; both walks
// share this function so a stream cannot validate one way and apply another.
static MapStatus walk_stream(MapGrid* grid, ByteReader* cursor, bool apply, MapDescHeader* header) {
    MapStatus status = read_header(cursor, header);
    if (status != MAP_STATUS_OK) return status;
    if (!dimensions_fit(grid, header->width_cells, header->height_cells, header->cell_size_q16))
        return MAP_STATUS_BAD_DIMENSIONS;
    uint32_t expected_cells = static_cast<uint32_t>(header->width_cells) *
                              static_cast<uint32_t>(header->height_cells);
    if (header->cell_count != expected_cells) return MAP_STATUS_BAD_COUNT;
    uint64_t remaining = static_cast<uint64_t>(byte_reader_remaining(cursor));
    if (header->cell_count > remaining / MAPDESC_CELL_ENCODED_SIZE) return MAP_STATUS_TRUNCATED;

    if (apply) {
        status = map_set_dimensions(grid, header->map_id, header->width_cells,
                                    header->height_cells, header->cell_size_q16);
        if (status != MAP_STATUS_OK) return status;
    }
    const uint8_t* cell_block = cursor->data + cursor->offset;
    for (uint32_t cell = 0u; cell < header->cell_count; ++cell) {
        uint8_t flags, cost;
        int16_t height;
        if (!byte_reader_read_u8(cursor, &flags) || !byte_reader_read_u8(cursor, &cost) ||
            !byte_reader_read_i16(cursor, &height)) return MAP_STATUS_TRUNCATED;
        if (!cell_flags_valid(flags)) return MAP_STATUS_BAD_CELL;
        if (apply) {
            grid->flags[cell] = flags;
            grid->movement_cost[cell] = cost;
            grid->height_cell[cell] = height;
        }
    }

    uint16_t lane_count;
    if (!byte_reader_read_u16(cursor, &lane_count)) return MAP_STATUS_TRUNCATED;
    if (lane_count > grid->config.max_lanes) return MAP_STATUS_CAPACITY;
    remaining = static_cast<uint64_t>(byte_reader_remaining(cursor));
    if (lane_count > remaining / MAPDESC_LANE_BASE_ENCODED_SIZE) return MAP_STATUS_TRUNCATED;

    uint8_t seen_ids[SIM_MAP_MAX_LANES] = {};
    for (uint32_t lane = 0u; lane < lane_count; ++lane) {
        MapLane def{};
        uint16_t waypoints[SIM_MAP_MAX_WAYPOINTS];
        if (!byte_reader_read_u8(cursor, &def.lane_id) ||
            !byte_reader_read_u8(cursor, &def.waypoint_count) ||
            !byte_reader_read_u16(cursor, &def.wave_interval_ticks) ||
            !byte_reader_read_u32(cursor, &def.first_wave_tick) ||
            !byte_reader_read_u64(cursor, &def.creep_archetype_id)) return MAP_STATUS_TRUNCATED;
        if (def.waypoint_count < 2u || def.waypoint_count > grid->config.max_waypoints)
            return MAP_STATUS_BAD_LANE;
        for (uint32_t i = 0u; i < def.waypoint_count; ++i) {
            if (!byte_reader_read_u16(cursor, &waypoints[i])) return MAP_STATUS_TRUNCATED;
            if (waypoints[i] >= header->cell_count) return MAP_STATUS_BAD_LANE;
            uint8_t streamed_flags = cell_block[static_cast<size_t>(waypoints[i]) * MAPDESC_CELL_ENCODED_SIZE];
            if (!(streamed_flags & MAP_CELL_WALKABLE) || !(streamed_flags & MAP_CELL_LANE))
                return MAP_STATUS_BAD_LANE;
        }
        for (uint32_t previous = 0u; previous < lane; ++previous) {
            if (seen_ids[previous] == def.lane_id) return MAP_STATUS_BAD_LANE;
        }
        seen_ids[lane] = def.lane_id;
        if (apply) {
            status = map_add_lane(grid, &def, waypoints);
            if (status != MAP_STATUS_OK) return status;
        }
    }
    return MAP_STATUS_OK;
}

MapStatus map_decode(MapGrid* grid, ByteReader* reader) {
    if (!grid || !reader || !reader->ok) return reader_fail(reader, MAP_STATUS_TRUNCATED);
    if (!map_config_valid(grid->config) || map_config_is_empty(grid->config) || !grid->flags)
        return reader_fail(reader, MAP_STATUS_INVALID_ARGUMENT);

    ByteReader probe = *reader;
    MapDescHeader header{};
    MapStatus status = walk_stream(grid, &probe, false, &header);
    if (status != MAP_STATUS_OK) return reader_fail(reader, status);

    ByteReader cursor = *reader;
    status = walk_stream(grid, &cursor, true, &header);
    if (status != MAP_STATUS_OK) return reader_fail(reader, status); // unreachable after probe
    *reader = cursor;
    return MAP_STATUS_OK;
}

MapStatus map_require_end(const ByteReader* reader) {
    if (!reader || !reader->ok) return MAP_STATUS_TRUNCATED;
    return byte_reader_remaining(reader) == 0u ? MAP_STATUS_OK : MAP_STATUS_TRAILING_BYTES;
}

const char* map_status_string(MapStatus status) {
    switch (status) {
        case MAP_STATUS_OK: return "ok";
        case MAP_STATUS_INVALID_ARGUMENT: return "invalid_argument";
        case MAP_STATUS_OUT_OF_BOUNDS: return "out_of_bounds";
        case MAP_STATUS_BAD_DIMENSIONS: return "bad_dimensions";
        case MAP_STATUS_BAD_CELL: return "bad_cell";
        case MAP_STATUS_BAD_LANE: return "bad_lane";
        case MAP_STATUS_CAPACITY: return "capacity";
        case MAP_STATUS_TRUNCATED: return "truncated";
        case MAP_STATUS_BAD_MAGIC: return "bad_magic";
        case MAP_STATUS_BAD_VERSION: return "bad_version";
        case MAP_STATUS_BAD_COUNT: return "bad_count";
        case MAP_STATUS_OVERFLOW: return "overflow";
        case MAP_STATUS_TRAILING_BYTES: return "trailing_bytes";
        default: return "unknown";
    }
}
