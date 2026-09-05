#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/mem.h"
#include "sim/component_pool.h"

// M5.3 team identity (m5-lane-objectives). One byte per entity saying which side it
// fights for. Everything that targets, credits, or awards reads this pool, so it is
// the single authority on "friend or foe" and nothing else may carry a second copy.
//
// Storage, failure policy, and iteration follow TransformPool exactly: sparse-set
// membership plus one typed parallel array, atomic failure, and every walk through
// component_pool_ordered_view (ascending entity index) so swap-remove history can
// never define system order.

typedef uint8_t SimTeamId;

static const SimTeamId SIM_TEAM_NONE = 0xFFu;
static const uint8_t SIM_MAX_TEAMS = 2;          // section 3.2: team_count must equal 2

typedef struct TeamView {
    uint8_t* team;
} TeamView;

typedef struct TeamPool {
    ComponentPool membership;
    uint8_t* team;                               // 0, 1, or SIM_TEAM_NONE
} TeamPool;

size_t team_pool_memory_required(uint32_t entity_capacity, uint32_t capacity);
bool team_pool_init(TeamPool* pool, Arena* arena, uint32_t entity_capacity, uint32_t capacity);

// A stored team is 0, 1, or SIM_TEAM_NONE; any other byte is rejected here and makes
// a world non-canonical if it is forced in behind this seam.
bool team_pool_add(TeamPool* pool, EntityId entity, uint8_t team);
bool team_pool_remove(TeamPool* pool, EntityId entity);
bool team_pool_has(const TeamPool* pool, EntityId entity);
bool team_pool_get(TeamPool* pool, EntityId entity, TeamView* view);

// Read-only accessor for const seams such as sim_validate_commands and the intake's
// ownership check, mirroring hero_pool_def_index.
bool team_pool_team(const TeamPool* pool, EntityId entity, uint8_t* out_team);

// True when both entities carry a team row, neither is SIM_TEAM_NONE, and the two
// differ. This is the one definition of "enemy" every AI walk uses.
bool team_is_enemy(const TeamPool* pool, EntityId a, EntityId b);
