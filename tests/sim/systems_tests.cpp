#include "test.h"
#include "sim/systems.h"

#include <cstring>

static const size_t SYSTEM_WORLD_BYTES = 8192u;

typedef struct SystemFixture {
    alignas(16) uint8_t storage[SYSTEM_WORLD_BYTES];
    Arena arena;
    SimWorld world;
} SystemFixture;

static bool system_fixture_init(SystemFixture* fixture, uint32_t initial_units = 4u) {
    if (!fixture) return false;
    std::memset(fixture, 0, sizeof(*fixture));
    arena_init_fixed(&fixture->arena, fixture->storage, sizeof(fixture->storage));
    return sim_init(&fixture->world, &fixture->arena, 42u,
                    SimWorldConfig{8u, initial_units, 8u});
}

static SimCommand system_velocity(uint16_t unit, mm::fix x) {
    SimCommand command{};
    command.kind = SIM_COMMAND_SET_VELOCITY;
    command.unit_index = unit;
    command.value_x = x;
    return command;
}

static SimCommand system_damage(uint16_t unit, int32_t amount) {
    SimCommand command{};
    command.kind = SIM_COMMAND_DAMAGE;
    command.unit_index = unit;
    command.amount = amount;
    return command;
}

TEST(sim_systems, command_system_updates_velocity_and_emits_damage_in_recorded_order) {
    SystemFixture fixture{};
    CHECK(system_fixture_init(&fixture));
    SimCommand records[] = {
        system_velocity(2u, mm::fix_from_int(5)),
        system_damage(2u, 11),
        system_velocity(2u, mm::fix_from_int(-3)),
        system_damage(1u, 22),
    };
    SimCommandBuffer commands{records, 4u};
    CHECK(sys_apply_commands(&fixture.world, &commands));

    VelocityView velocity{};
    CHECK(velocity_pool_get(&fixture.world.velocities,
                            fixture.world.unit_entities[2], &velocity));
    CHECK(*velocity.velocity_x == mm::fix_from_int(-3));
    CHECK(fixture.world.damage_events.counts[fixture.world.damage_events.write_index] == 2u);
    CHECK(damage_event_queue_publish(&fixture.world.damage_events));
    DamageEventView events{};
    CHECK(damage_event_queue_read(&fixture.world.damage_events, &events));
    CHECK(events.count == 2u);
    CHECK(events.events[0].target.h == fixture.world.unit_entities[2].h);
    CHECK(events.events[0].amount == 11);
    CHECK(events.events[1].target.h == fixture.world.unit_entities[1].h);
    CHECK(events.events[1].amount == 22);
}

static bool add_motion_entity(SimWorld* world, EntityId entity, int32_t position, int32_t velocity) {
    return transform_pool_add(&world->transforms, entity, mm::fix_from_int(position), 0, 0) &&
           velocity_pool_add(&world->velocities, entity, mm::fix_from_int(velocity), 0);
}

TEST(sim_systems, movement_uses_ascending_entities_not_dense_storage_order) {
    SystemFixture a{}, b{};
    CHECK(system_fixture_init(&a, 0u));
    CHECK(system_fixture_init(&b, 0u));
    EntityId a_entities[4]{};
    EntityId b_entities[4]{};
    for (uint32_t i = 0u; i < 4u; ++i) {
        a_entities[i] = entity_manager_create(&a.world.entities);
        b_entities[i] = entity_manager_create(&b.world.entities);
        CHECK(a_entities[i].h == b_entities[i].h);
    }
    for (uint32_t i = 0u; i < 4u; ++i)
        CHECK(add_motion_entity(&a.world, a_entities[i], static_cast<int32_t>(i),
                                static_cast<int32_t>(i + 1u)));
    for (uint32_t reverse = 4u; reverse > 0u; --reverse) {
        uint32_t i = reverse - 1u;
        CHECK(add_motion_entity(&b.world, b_entities[i], static_cast<int32_t>(i),
                                static_cast<int32_t>(i + 1u)));
    }
    CHECK(a.world.transforms.membership.dense_entities[0].h !=
          b.world.transforms.membership.dense_entities[0].h);
    CHECK(sys_movement(&a.world));
    CHECK(sys_movement(&b.world));
    for (uint32_t i = 0u; i < 4u; ++i) {
        TransformView ta{}, tb{};
        CHECK(transform_pool_get(&a.world.transforms, a_entities[i], &ta));
        CHECK(transform_pool_get(&b.world.transforms, b_entities[i], &tb));
        CHECK(*ta.position_x == *tb.position_x);
    }
}

TEST(sim_systems, combat_prevalidates_the_whole_read_phase) {
    SystemFixture fixture{};
    CHECK(system_fixture_init(&fixture));
    EntityId target = fixture.world.unit_entities[0];
    DamageEvent valid{EntityId{HANDLE_NULL}, target, 30};
    DamageEvent unresolved{EntityId{HANDLE_NULL}, EntityId{handle_make(7u, 1u)}, 10};
    CHECK(damage_event_queue_append(&fixture.world.damage_events, &valid));
    CHECK(damage_event_queue_append(&fixture.world.damage_events, &unresolved));
    CHECK(damage_event_queue_publish(&fixture.world.damage_events));

    HealthView health{};
    CHECK(health_pool_get(&fixture.world.health, target, &health));
    int32_t before = *health.current;
    CHECK(!sys_combat_resolve(&fixture.world));
    CHECK(*health.current == before);

    CHECK(damage_event_queue_consume(&fixture.world.damage_events));
    CHECK(damage_event_queue_append(&fixture.world.damage_events, &valid));
    CHECK(damage_event_queue_publish(&fixture.world.damage_events));
    CHECK(sys_combat_resolve(&fixture.world));
    CHECK(*health.current == 70);
    CHECK(*health.damage_cooldown == 3u);
}
