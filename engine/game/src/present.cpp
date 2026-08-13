#include "game/present.h"

static bool present_state_valid(const PresentState* state) {
    return state && state->previous.units && state->current.units &&
           state->previous.units != state->current.units &&
           state->previous.live_count <= SIM_MAX_UNITS &&
           state->current.live_count <= SIM_MAX_UNITS;
}

size_t present_memory_required(void) {
    const size_t bytes = sizeof(RenderSnapshotUnit) * static_cast<size_t>(SIM_MAX_UNITS);
    const size_t padding = alignof(RenderSnapshotUnit) - 1u;
    return (bytes + padding) * 2u;
}

bool snapshot_extract(const SimWorld* world, RenderSnapshot* out) {
    if (!world || !out || !out->units) return false;

    EntityId entities[SIM_MAX_UNITS]{};
    ConstTransformView transforms[SIM_MAX_UNITS]{};
    uint32_t live_count = 0u;

    // Validate and collect every source pointer before writing output, so malformed
    // world mappings cannot leave a partially updated snapshot.
    for (uint32_t slot = 0u; slot < SIM_MAX_UNITS; ++slot) {
        EntityId entity = world->unit_entities[slot];
        entities[slot] = entity;
        if (entity.h == HANDLE_NULL) continue;
        if (!entity_manager_is_alive(&world->entities, entity) ||
            !transform_pool_get_const(&world->transforms, entity, &transforms[slot])) {
            return false;
        }
        ++live_count;
    }

    for (uint32_t slot = 0u; slot < SIM_MAX_UNITS; ++slot) {
        if (entities[slot].h == HANDLE_NULL) {
            out->units[slot] = RenderSnapshotUnit{};
            continue;
        }
        const ConstTransformView& transform = transforms[slot];
        out->units[slot] = RenderSnapshotUnit{
            entities[slot], *transform.position_x, *transform.position_y, *transform.facing};
    }
    out->tick = world->tick;
    out->live_count = live_count;
    return true;
}

bool present_init(PresentState* state, Arena* arena, const SimWorld* initial_world) {
    const size_t required = present_memory_required();
    if (!state || !initial_world || !arena || !arena->base ||
        arena->offset > arena->reserved || required > arena->reserved - arena->offset) {
        return false;
    }

    TempMemory temporary = temp_begin(arena);
    PresentState staged{};
    staged.previous.units = ARENA_PUSH_ARRAY(arena, RenderSnapshotUnit, SIM_MAX_UNITS);
    staged.current.units = ARENA_PUSH_ARRAY(arena, RenderSnapshotUnit, SIM_MAX_UNITS);
    if (!snapshot_extract(initial_world, &staged.current)) {
        temp_end(temporary);
        return false;
    }

    staged.previous.tick = staged.current.tick;
    staged.previous.live_count = staged.current.live_count;
    for (uint32_t slot = 0u; slot < SIM_MAX_UNITS; ++slot) {
        staged.previous.units[slot] = staged.current.units[slot];
    }
    *state = staged;
    return true;
}

bool present_capture(PresentState* state, const SimWorld* world) {
    if (!present_state_valid(state) || !world) return false;
    RenderSnapshot next = state->previous;
    if (!snapshot_extract(world, &next)) return false;
    state->previous = state->current;
    state->current = next;
    return true;
}

uint32_t present_build_draw_items(const PresentState* state, double alpha,
                                  MeshHandle mesh, MaterialHandle material,
                                  DrawItem* out_items, uint32_t out_capacity) {
    if (!present_state_valid(state) || !out_items || out_capacity == 0u) return 0u;
    if (!(alpha >= 0.0)) alpha = 0.0; // negative and NaN
    if (alpha > 1.0) alpha = 1.0;

    const float fixed_scale = 1.0f / static_cast<float>(FIX_ONE);
    const float blend = static_cast<float>(alpha);
    uint32_t written = 0u;
    for (uint32_t slot = 0u; slot < SIM_MAX_UNITS && written < out_capacity; ++slot) {
        const RenderSnapshotUnit& current = state->current.units[slot];
        if (current.entity.h == HANDLE_NULL) continue;

        float x = static_cast<float>(current.x) * fixed_scale;
        float z = static_cast<float>(current.y) * fixed_scale;
        float facing = static_cast<float>(current.facing) * fixed_scale;
        const RenderSnapshotUnit& previous = state->previous.units[slot];
        if (previous.entity.h == current.entity.h) {
            const float previous_x = static_cast<float>(previous.x) * fixed_scale;
            const float previous_z = static_cast<float>(previous.y) * fixed_scale;
            const float previous_facing = static_cast<float>(previous.facing) * fixed_scale;
            x = previous_x + (x - previous_x) * blend;
            z = previous_z + (z - previous_z) * blend;
            facing = previous_facing + (facing - previous_facing) * blend;
        }

        DrawItem& item = out_items[written];
        item.mesh = mesh;
        item.material = material;
        item.model = mm::mat4_trs(mm::vec3_make(x, 0.5f, z),
                                  mm::quat_from_axis_angle(mm::vec3_make(0, 1, 0), facing),
                                  mm::vec3_splat(0.8f));
        ++written;
    }
    return written;
}
