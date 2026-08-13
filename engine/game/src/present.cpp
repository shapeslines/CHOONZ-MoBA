#include "game/present.h"

#include "core/sim_config.h"

void present_init(PresentState* p) {
    p->accumulator = 0.0;
    p->alpha = 0.0;
    p->prev = RenderSnapshot{};
    p->curr = RenderSnapshot{};
}

uint32_t snapshot_extract(const SimWorld* world, RenderSnapshot* out) {
    if (!world || !out) return 0u;
    out->tick = (uint32_t)world->tick;   // 30 Hz for 2^32 ticks is ~4.5k years; u32 is ample
    out->count = 0u;
    for (uint32_t slot = 0u; slot < SIM_MAX_UNITS; ++slot) {
        EntityId entity = world->unit_entities[slot];
        TransformView t{};
        // pool_get is a read (sparse/dense lookup); const_cast documents that the
        // extract is contractually non-mutating (the slate's "cannot mutate SimWorld").
        bool present = entity.h != HANDLE_NULL &&
                       transform_pool_get(const_cast<TransformPool*>(&world->transforms),
                                          entity, &t);
        if (!present) {
            out->units[slot] = RenderSnapshotUnit{};   // dead slot: HANDLE_NULL identity
            continue;
        }
        out->units[slot].entity = entity;
        out->units[slot].x = *t.position_x;
        out->units[slot].y = *t.position_y;
        out->units[slot].facing = *t.facing;
        out->count = slot + 1u;
    }
    return out->count;
}

uint32_t present_advance(PresentState* p, double frame_delta, SimWorld* world,
                         const SimCommandBuffer* commands) {
    if (!p || !world) return 0u;
    const double sim_dt = 1.0 / (double)SIM_HZ;
    p->accumulator += frame_delta;
    if (p->accumulator > SIM_MAX_CATCHUP_S) p->accumulator = SIM_MAX_CATCHUP_S;

    uint32_t ticks = 0u;
    while (p->accumulator >= sim_dt) {
        if (!sim_tick(world, commands)) break;   // atomic reject (ADR-0014): nothing applied
        p->prev = p->curr;
        snapshot_extract(world, &p->curr);
        p->accumulator -= sim_dt;
        ++ticks;
    }
    if (p->curr.tick == 0u && ticks > 0u) p->prev = p->curr;   // first frame has no prev

    p->alpha = p->accumulator / sim_dt;
    if (p->alpha >= 1.0) p->alpha = 0.0;                        // defensive; while-loop keeps < 1
    return ticks;
}

uint32_t present_build_draw_items(const PresentState* p, const MeshHandle mesh,
                                  const MaterialHandle material, Arena* frame_arena,
                                  DrawItem* out_items, uint32_t out_capacity) {
    (void)frame_arena;   // items are POD; kept for the frame-arena discipline later
    if (!p || !out_items) return 0u;
    uint32_t written = 0u;
    for (uint32_t i = 0u; i < p->curr.count && written < out_capacity; ++i) {
        if (p->curr.units[i].entity.h == HANDLE_NULL) continue;
        const RenderSnapshotUnit& c = p->curr.units[i];
        const RenderSnapshotUnit& pr = p->prev.units[i];
        const bool has_prev = pr.entity.h != HANDLE_NULL;

        // THE single fixed->float conversion (ARCHITECTURE §2.1): Q16.16 -> float,
        // then lerp in float. Sim y maps to world z (top-down arena).
        const float k = 1.0f / (float)FIX_ONE;
        const float a = (float)p->alpha;
        const float wx = (float)c.x * k * (1.0f - a) + (has_prev ? (float)pr.x * k * a : 0.0f);
        const float wz = (float)c.y * k * (1.0f - a) + (has_prev ? (float)pr.y * k * a : 0.0f);
        const float f  = (float)c.facing * k * (1.0f - a) + (has_prev ? (float)pr.facing * k * a : 0.0f);

        DrawItem& item = out_items[written];
        item.mesh = mesh;
        item.material = material;
        item.model = mm::mat4_trs(mm::vec3_make(wx, 0.5f, wz),
                                  mm::quat_from_axis_angle(mm::vec3_make(0, 1, 0), f),
                                  mm::vec3_splat(0.8f));
        ++written;
    }
    return written;
}
