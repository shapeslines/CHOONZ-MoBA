#include "sim/team.h"

static bool valid_pool_shape(uint32_t entity_capacity, uint32_t capacity) {
    return entity_capacity > 0u && entity_capacity <= HANDLE_INDEX_MASK + 1u &&
           capacity > 0u && capacity <= entity_capacity;
}

static bool arena_has_budget(const Arena* arena, size_t required) {
    return arena && arena->base && arena->offset <= arena->reserved &&
           required <= arena->reserved - arena->offset;
}

static bool team_byte_valid(uint8_t team) {
    return team < SIM_MAX_TEAMS || team == SIM_TEAM_NONE;
}

size_t team_pool_memory_required(uint32_t entity_capacity, uint32_t capacity) {
    if (!valid_pool_shape(entity_capacity, capacity)) return 0u;
    size_t total = component_pool_memory_required(entity_capacity, capacity);
    if (total == 0u) return 0u;
    size_t bytes = sizeof(uint8_t) * static_cast<size_t>(capacity);
    if (bytes > SIZE_MAX - total) return 0u;
    return total + bytes;
}

bool team_pool_init(TeamPool* pool, Arena* arena, uint32_t entity_capacity, uint32_t capacity) {
    size_t required = team_pool_memory_required(entity_capacity, capacity);
    if (!pool || required == 0u || !arena_has_budget(arena, required)) return false;

    TempMemory temp = temp_begin(arena);
    TeamPool staged{};
    if (!component_pool_init(&staged.membership, arena, entity_capacity, capacity)) {
        temp_end(temp);
        return false;
    }
    staged.team = static_cast<uint8_t*>(
        arena_push_zero(arena, sizeof(uint8_t) * static_cast<size_t>(capacity), alignof(uint8_t)));
    *pool = staged;
    return true;
}

bool team_pool_add(TeamPool* pool, EntityId entity, uint8_t team) {
    if (!pool || !pool->team || !team_byte_valid(team)) return false;
    uint32_t dense = 0u;
    if (!component_pool_add(&pool->membership, entity, &dense)) return false;
    pool->team[dense] = team;
    return true;
}

bool team_pool_remove(TeamPool* pool, EntityId entity) {
    if (!pool || !pool->team) return false;
    ComponentPoolRemoveResult result{};
    if (!component_pool_remove(&pool->membership, entity, &result)) return false;
    if (result.removed_dense != result.moved_from_dense)
        pool->team[result.removed_dense] = pool->team[result.moved_from_dense];
    pool->team[result.moved_from_dense] = 0u;
    return true;
}

bool team_pool_has(const TeamPool* pool, EntityId entity) {
    return pool && component_pool_has(&pool->membership, entity);
}

bool team_pool_get(TeamPool* pool, EntityId entity, TeamView* view) {
    if (!pool || !view || !pool->team) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE) return false;
    TeamView staged{&pool->team[dense]};
    *view = staged;
    return true;
}

bool team_pool_team(const TeamPool* pool, EntityId entity, uint8_t* out_team) {
    if (!pool || !pool->team || !out_team) return false;
    uint32_t dense = component_pool_dense_index(&pool->membership, entity);
    if (dense == COMPONENT_POOL_INVALID_DENSE) return false;
    *out_team = pool->team[dense];
    return true;
}

bool team_is_enemy(const TeamPool* pool, EntityId a, EntityId b) {
    uint8_t team_a = SIM_TEAM_NONE;
    uint8_t team_b = SIM_TEAM_NONE;
    if (!team_pool_team(pool, a, &team_a) || !team_pool_team(pool, b, &team_b)) return false;
    if (team_a == SIM_TEAM_NONE || team_b == SIM_TEAM_NONE) return false;
    return team_a != team_b;
}
