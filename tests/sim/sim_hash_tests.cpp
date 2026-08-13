#include "test.h"
#include "sim/sim_hash.h"

#include <cstring>

static const size_t HASH_WORLD_BYTES = 16384u;

typedef struct HashWorldFixture {
    alignas(16) uint8_t storage[HASH_WORLD_BYTES];
    Arena arena;
    SimWorld world;
} HashWorldFixture;

static bool hash_world_init(HashWorldFixture* fixture, uint64_t seed = 1234u,
                            SimWorldConfig config = SimWorldConfig{64u, 4u, 8u}) {
    if (!fixture) return false;
    std::memset(fixture, 0, sizeof(*fixture));
    arena_init_fixed(&fixture->arena, fixture->storage, sizeof(fixture->storage));
    return sim_init(&fixture->world, &fixture->arena, seed, config);
}

static bool unit_views(SimWorld* world, uint32_t unit,
                       TransformView* transform, VelocityView* velocity, HealthView* health) {
    if (!world || unit >= SIM_MAX_UNITS) return false;
    EntityId entity = world->unit_entities[unit];
    return transform_pool_get(&world->transforms, entity, transform) &&
           velocity_pool_get(&world->velocities, entity, velocity) &&
           health_pool_get(&world->health, entity, health);
}

static EntityId create_componentless(HashWorldFixture* fixture) {
    return entity_manager_create(&fixture->world.entities);
}

TEST(sim, canonical_hash_covers_scalars_lifecycle_order_and_component_values) {
    HashWorldFixture base{};
    CHECK(hash_world_init(&base));
    uint64_t baseline = sim_hash_state(&base.world);
    CHECK(baseline != 0u);

    HashWorldFixture changed{};
    CHECK(hash_world_init(&changed));
    ++changed.world.tick;
    CHECK(sim_hash_state(&changed.world) != baseline);
    CHECK(hash_world_init(&changed));
    changed.world.rng.state ^= 1u;
    CHECK(sim_hash_state(&changed.world) != baseline);
    CHECK(hash_world_init(&changed));
    changed.world.rng.inc ^= 2u;
    CHECK(sim_hash_state(&changed.world) != baseline);

    HashWorldFixture other_config{};
    CHECK(hash_world_init(&other_config, 1234u, SimWorldConfig{65u, 4u, 8u}));
    CHECK(sim_hash_state(&other_config.world) != baseline);
    CHECK(hash_world_init(&other_config, 1234u, SimWorldConfig{64u, 3u, 8u}));
    CHECK(sim_hash_state(&other_config.world) != baseline);
    CHECK(hash_world_init(&other_config, 1234u, SimWorldConfig{64u, 4u, 9u}));
    CHECK(sim_hash_state(&other_config.world) != baseline);

    CHECK(hash_world_init(&changed));
    EntityId mapped = changed.world.unit_entities[0];
    changed.world.unit_entities[0] = changed.world.unit_entities[1];
    changed.world.unit_entities[1] = mapped;
    CHECK(sim_hash_state(&changed.world) != baseline);

    CHECK(hash_world_init(&changed));
    EntityId extra = create_componentless(&changed);
    CHECK(extra.h != HANDLE_NULL);
    CHECK(sim_hash_state(&changed.world) != baseline);
    CHECK(entity_manager_release(&changed.world.entities, extra));
    CHECK(sim_hash_state(&changed.world) != baseline);

    const SimStateField value_fields[] = {
        SIM_STATE_FIELD_POSITION_X, SIM_STATE_FIELD_POSITION_Y, SIM_STATE_FIELD_FACING,
        SIM_STATE_FIELD_VELOCITY_X, SIM_STATE_FIELD_VELOCITY_Y,
        SIM_STATE_FIELD_HEALTH_CURRENT, SIM_STATE_FIELD_HEALTH_MAXIMUM,
        SIM_STATE_FIELD_DAMAGE_COOLDOWN,
    };
    for (SimStateField field : value_fields) {
        CHECK(hash_world_init(&changed));
        TransformView transform{};
        VelocityView velocity{};
        HealthView health{};
        CHECK(unit_views(&changed.world, 2u, &transform, &velocity, &health));
        switch (field) {
            case SIM_STATE_FIELD_POSITION_X: *transform.position_x ^= 1; break;
            case SIM_STATE_FIELD_POSITION_Y: *transform.position_y ^= 1; break;
            case SIM_STATE_FIELD_FACING: *transform.facing ^= 1; break;
            case SIM_STATE_FIELD_VELOCITY_X: *velocity.velocity_x ^= 1; break;
            case SIM_STATE_FIELD_VELOCITY_Y: *velocity.velocity_y ^= 1; break;
            case SIM_STATE_FIELD_HEALTH_CURRENT: *health.current = 99; break;
            case SIM_STATE_FIELD_HEALTH_MAXIMUM: *health.maximum = 101; break;
            case SIM_STATE_FIELD_DAMAGE_COOLDOWN: *health.damage_cooldown = 1u; break;
            default: break;
        }
        CHECK(sim_hash_state(&changed.world) != baseline);
    }
}

TEST(sim, canonical_hash_covers_pool_membership_free_stack_and_queue_order) {
    HashWorldFixture componentless{};
    CHECK(hash_world_init(&componentless));
    EntityId extra = create_componentless(&componentless);
    CHECK(extra.h != HANDLE_NULL);
    uint64_t componentless_hash = sim_hash_state(&componentless.world);

    HashWorldFixture with_transform{};
    CHECK(hash_world_init(&with_transform));
    EntityId transform_entity = create_componentless(&with_transform);
    CHECK(transform_pool_add(&with_transform.world.transforms, transform_entity, 1, 2, 3));
    CHECK(sim_hash_state(&with_transform.world) != componentless_hash);

    HashWorldFixture with_velocity{};
    CHECK(hash_world_init(&with_velocity));
    EntityId velocity_entity = create_componentless(&with_velocity);
    CHECK(velocity_pool_add(&with_velocity.world.velocities, velocity_entity, 4, 5));
    CHECK(sim_hash_state(&with_velocity.world) != componentless_hash);

    HashWorldFixture with_health{};
    CHECK(hash_world_init(&with_health));
    EntityId health_entity = create_componentless(&with_health);
    CHECK(health_pool_add(&with_health.world.health, health_entity, 6, 7, 8u));
    CHECK(sim_hash_state(&with_health.world) != componentless_hash);

    HashWorldFixture free_ab{}, free_ba{};
    CHECK(hash_world_init(&free_ab, 1u, SimWorldConfig{8u, 0u, 8u}));
    CHECK(hash_world_init(&free_ba, 1u, SimWorldConfig{8u, 0u, 8u}));
    EntityId ab0 = create_componentless(&free_ab);
    EntityId ab1 = create_componentless(&free_ab);
    EntityId ba0 = create_componentless(&free_ba);
    EntityId ba1 = create_componentless(&free_ba);
    CHECK(entity_manager_release(&free_ab.world.entities, ab0));
    CHECK(entity_manager_release(&free_ab.world.entities, ab1));
    CHECK(entity_manager_release(&free_ba.world.entities, ba1));
    CHECK(entity_manager_release(&free_ba.world.entities, ba0));
    CHECK(sim_hash_state(&free_ab.world) != sim_hash_state(&free_ba.world));

    HashWorldFixture queue_ab{}, queue_ba{};
    CHECK(hash_world_init(&queue_ab));
    CHECK(hash_world_init(&queue_ba));
    CHECK(sim_destroy_deferred(&queue_ab.world, queue_ab.world.unit_entities[0]));
    CHECK(sim_destroy_deferred(&queue_ab.world, queue_ab.world.unit_entities[1]));
    CHECK(sim_destroy_deferred(&queue_ba.world, queue_ba.world.unit_entities[1]));
    CHECK(sim_destroy_deferred(&queue_ba.world, queue_ba.world.unit_entities[0]));
    CHECK(sim_hash_state(&queue_ab.world) != sim_hash_state(&queue_ba.world));

    uint64_t queued_hash = sim_hash_state(&queue_ab.world);
    CHECK(sim_tick(&queue_ab.world, nullptr));
    CHECK(sim_hash_state(&queue_ab.world) != 0u);
    CHECK(sim_hash_state(&queue_ab.world) != queued_hash);

    HashWorldFixture damage_ab{}, damage_ba{};
    CHECK(hash_world_init(&damage_ab));
    CHECK(hash_world_init(&damage_ba));
    DamageEvent damage_first{
        damage_ab.world.unit_entities[2], damage_ab.world.unit_entities[0], 11};
    DamageEvent damage_second{
        EntityId{HANDLE_NULL}, damage_ab.world.unit_entities[1], 22};
    DamageEvent damage_first_b{
        damage_ba.world.unit_entities[2], damage_ba.world.unit_entities[0], 11};
    DamageEvent damage_second_b{
        EntityId{HANDLE_NULL}, damage_ba.world.unit_entities[1], 22};
    CHECK(damage_event_queue_append(&damage_ab.world.damage_events, &damage_first));
    CHECK(damage_event_queue_append(&damage_ab.world.damage_events, &damage_second));
    CHECK(damage_event_queue_append(&damage_ba.world.damage_events, &damage_second_b));
    CHECK(damage_event_queue_append(&damage_ba.world.damage_events, &damage_first_b));
    CHECK(sim_hash_state(&damage_ab.world) != sim_hash_state(&damage_ba.world));

    HashWorldFixture damage_write{}, damage_read{};
    CHECK(hash_world_init(&damage_write));
    CHECK(hash_world_init(&damage_read));
    DamageEvent pending{
        damage_write.world.unit_entities[2], damage_write.world.unit_entities[0], 11};
    DamageEvent published{
        damage_read.world.unit_entities[2], damage_read.world.unit_entities[0], 11};
    CHECK(damage_event_queue_append(&damage_write.world.damage_events, &pending));
    CHECK(damage_event_queue_append(&damage_read.world.damage_events, &published));
    CHECK(damage_event_queue_publish(&damage_read.world.damage_events));
    CHECK(sim_hash_state(&damage_read.world) != sim_hash_state(&damage_write.world));

    HashWorldFixture damage_field{};
    CHECK(hash_world_init(&damage_field));
    DamageEvent changed_source{
        damage_field.world.unit_entities[3], damage_field.world.unit_entities[0], 11};
    CHECK(damage_event_queue_append(&damage_field.world.damage_events, &changed_source));
    CHECK(sim_hash_state(&damage_field.world) != sim_hash_state(&damage_write.world));
    CHECK(hash_world_init(&damage_field));
    DamageEvent changed_target{
        damage_field.world.unit_entities[2], damage_field.world.unit_entities[1], 11};
    CHECK(damage_event_queue_append(&damage_field.world.damage_events, &changed_target));
    CHECK(sim_hash_state(&damage_field.world) != sim_hash_state(&damage_write.world));
    CHECK(hash_world_init(&damage_field));
    DamageEvent changed_amount{
        damage_field.world.unit_entities[2], damage_field.world.unit_entities[0], 12};
    CHECK(damage_event_queue_append(&damage_field.world.damage_events, &changed_amount));
    CHECK(sim_hash_state(&damage_field.world) != sim_hash_state(&damage_write.world));
}

static void swap_u32(uint32_t* values, uint32_t a, uint32_t b) {
    uint32_t value = values[a];
    values[a] = values[b];
    values[b] = value;
}

static void swap_i32(int32_t* values, uint32_t a, uint32_t b) {
    int32_t value = values[a];
    values[a] = values[b];
    values[b] = value;
}

static void swap_members(ComponentPool* pool, uint32_t a, uint32_t b) {
    EntityId first = pool->dense_entities[a];
    EntityId second = pool->dense_entities[b];
    pool->dense_entities[a] = second;
    pool->dense_entities[b] = first;
    pool->sparse[handle_index(first.h)] = b + 1u;
    pool->sparse[handle_index(second.h)] = a + 1u;
}

TEST(sim, canonical_hash_excludes_pointers_unused_capacity_and_dense_order) {
    HashWorldFixture expected{}, reordered{};
    CHECK(hash_world_init(&expected));
    CHECK(hash_world_init(&reordered));
    uint64_t baseline = sim_hash_state(&expected.world);
    CHECK(baseline == sim_hash_state(&reordered.world));
    CHECK(std::memcmp(&expected.world, &reordered.world, sizeof(expected.world)) != 0);

    ComponentPoolOrderedView derived{};
    CHECK(component_pool_ordered_view(&reordered.world.transforms.membership, &derived));
    CHECK(derived.count > 0u);
    reordered.world.transforms.membership.ordered_entities[0] = EntityId{HANDLE_NULL};
    reordered.world.transforms.membership.ordered_count = 1u;
    reordered.world.transforms.membership.ordered_dirty = 0u;

    reordered.world.entities.generations[50] = 123u;
    reordered.world.entities.liveness[50] = 1u;
    reordered.world.entities.free_stack[50] = 50u;
    reordered.world.pending_destroy[50] = EntityId{handle_make(50u, 1u)};
    reordered.world.transforms.membership.sparse[50] = 44u;
    reordered.world.velocities.membership.sparse[50] = 45u;
    reordered.world.health.membership.sparse[50] = 46u;
    reordered.world.transforms.position_x[50] = 101;
    reordered.world.transforms.position_y[50] = 102;
    reordered.world.transforms.facing[50] = 103;
    reordered.world.velocities.velocity_x[50] = 104;
    reordered.world.velocities.velocity_y[50] = 105;
    reordered.world.health.current[50] = 106;
    reordered.world.health.maximum[50] = 107;
    reordered.world.health.damage_cooldown[50] = 108u;
    reordered.world.damage_events.buffers[0][7] =
        DamageEvent{EntityId{HANDLE_NULL}, reordered.world.unit_entities[0], 999};
    reordered.world.damage_events.buffers[1][7] =
        DamageEvent{reordered.world.unit_entities[1], reordered.world.unit_entities[2], 888};
    uint8_t read_index = reordered.world.damage_events.read_index;
    reordered.world.damage_events.read_index = reordered.world.damage_events.write_index;
    reordered.world.damage_events.write_index = read_index;

    CHECK(sim_hash_state(&reordered.world) == baseline);

    swap_members(&reordered.world.transforms.membership, 0u, 3u);
    swap_i32(reordered.world.transforms.position_x, 0u, 3u);
    swap_i32(reordered.world.transforms.position_y, 0u, 3u);
    swap_i32(reordered.world.transforms.facing, 0u, 3u);
    swap_members(&reordered.world.velocities.membership, 0u, 3u);
    swap_i32(reordered.world.velocities.velocity_x, 0u, 3u);
    swap_i32(reordered.world.velocities.velocity_y, 0u, 3u);
    swap_members(&reordered.world.health.membership, 0u, 3u);
    swap_i32(reordered.world.health.current, 0u, 3u);
    swap_i32(reordered.world.health.maximum, 0u, 3u);
    swap_u32(reordered.world.health.damage_cooldown, 0u, 3u);
    CHECK(sim_hash_state(&reordered.world) == baseline);
}

TEST(sim, canonical_diff_reports_matching_field_and_index_domain) {
    HashWorldFixture expected{}, actual{};
    CHECK(hash_world_init(&expected, 77u, SimWorldConfig{64u, 8u, 8u}));
    CHECK(hash_world_init(&actual, 77u, SimWorldConfig{64u, 8u, 8u}));
    TransformView transform{};
    VelocityView velocity{};
    HealthView health{};
    CHECK(unit_views(&actual.world, 7u, &transform, &velocity, &health));
    *transform.position_x ^= 1;

    SimStateDiff diff{};
    CHECK(sim_diff_state(&expected.world, &actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_POSITION_X);
    CHECK(diff.index == 7u);
    CHECK(std::strcmp(sim_state_field_name(diff.field), "position_x") == 0);
    CHECK(diff.expected_value != diff.actual_value);

    ++actual.world.tick;
    CHECK(sim_diff_state(&expected.world, &actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_TICK);
    CHECK(diff.index == SIM_STATE_DIFF_NO_INDEX);

    CHECK(hash_world_init(&actual, 77u, SimWorldConfig{64u, 8u, 8u}));
    EntityId first = actual.world.unit_entities[0];
    actual.world.unit_entities[0] = actual.world.unit_entities[1];
    actual.world.unit_entities[1] = first;
    CHECK(sim_diff_state(&expected.world, &actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_UNIT_ENTITY);
    CHECK(diff.index == 0u);

    HashWorldFixture queue_ab{}, queue_ba{};
    CHECK(hash_world_init(&queue_ab));
    CHECK(hash_world_init(&queue_ba));
    CHECK(sim_destroy_deferred(&queue_ab.world, queue_ab.world.unit_entities[0]));
    CHECK(sim_destroy_deferred(&queue_ab.world, queue_ab.world.unit_entities[1]));
    CHECK(sim_destroy_deferred(&queue_ba.world, queue_ba.world.unit_entities[1]));
    CHECK(sim_destroy_deferred(&queue_ba.world, queue_ba.world.unit_entities[0]));
    CHECK(sim_diff_state(&queue_ab.world, &queue_ba.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_PENDING_DESTROY_ENTITY);
    CHECK(diff.index == 0u);

    HashWorldFixture free_ab{}, free_ba{};
    CHECK(hash_world_init(&free_ab, 1u, SimWorldConfig{8u, 0u, 8u}));
    CHECK(hash_world_init(&free_ba, 1u, SimWorldConfig{8u, 0u, 8u}));
    EntityId ab0 = create_componentless(&free_ab);
    EntityId ab1 = create_componentless(&free_ab);
    EntityId ba0 = create_componentless(&free_ba);
    EntityId ba1 = create_componentless(&free_ba);
    CHECK(entity_manager_release(&free_ab.world.entities, ab0));
    CHECK(entity_manager_release(&free_ab.world.entities, ab1));
    CHECK(entity_manager_release(&free_ba.world.entities, ba1));
    CHECK(entity_manager_release(&free_ba.world.entities, ba0));
    CHECK(sim_diff_state(&free_ab.world, &free_ba.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_ENTITY_FREE_STACK);
    CHECK(diff.index == 0u);

    HashWorldFixture event_expected{}, event_actual{};
    CHECK(hash_world_init(&event_expected));
    CHECK(hash_world_init(&event_actual));
    DamageEvent expected_event{
        event_expected.world.unit_entities[2], event_expected.world.unit_entities[0], 5};
    DamageEvent actual_event{
        event_actual.world.unit_entities[2], event_actual.world.unit_entities[0], 6};
    CHECK(damage_event_queue_append(&event_expected.world.damage_events, &expected_event));
    CHECK(damage_event_queue_append(&event_actual.world.damage_events, &actual_event));
    CHECK(sim_diff_state(&event_expected.world, &event_actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_DAMAGE_EVENT_WRITE_AMOUNT);
    CHECK(diff.index == 0u);
    CHECK(std::strcmp(sim_state_field_name(diff.field), "damage_event_write_amount") == 0);

    CHECK(hash_world_init(&actual, 77u, SimWorldConfig{64u, 8u, 8u}));
    CHECK(!sim_diff_state(&expected.world, &actual.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_NONE);
}

TEST(sim, invalid_canonical_hash_and_diff_fail_closed) {
    HashWorldFixture invalid{};
    CHECK(hash_world_init(&invalid));
    invalid.world.entities.live_count = invalid.world.entities.next_fresh + 1u;
    CHECK(sim_hash_state(nullptr) == 0u);
    CHECK(sim_hash_state(&invalid.world) == 0u);
    SimStateDiff diff{};
    CHECK(sim_diff_state(&invalid.world, &invalid.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_INVALID);
    CHECK(diff.index == SIM_STATE_DIFF_NO_INDEX);

    HashWorldFixture stale_event{};
    CHECK(hash_world_init(&stale_event));
    DamageEvent event{
        EntityId{HANDLE_NULL}, EntityId{handle_make(7u, 1u)}, 1};
    CHECK(damage_event_queue_append(&stale_event.world.damage_events, &event));
    CHECK(sim_hash_state(&stale_event.world) == 0u);
}
