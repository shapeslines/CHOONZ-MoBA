#include "test.h"
#include "sim/combat.h"
#include "sim/command.h"
#include "sim/sim.h"
#include "sim/sim_hash.h"
#include "sim/replay.h"
#include "sim/systems.h"

#include <cstring>

// M5.2 hero combat (m5-hero-combat). The sim owns a POD mirror of proto-design
// section 3.3, so this suite cross-checks the mirror against the section 3.3 text
// field by field, then exercises the bounded pools, the SimEvent envelope, the
// unified resolve_effect pipeline, and the three data-defined effects.

static const size_t HERO_WORLD_BYTES = 512u * 1024u;
alignas(16) static uint8_t g_hero_storage[HERO_WORLD_BYTES];
alignas(16) static uint8_t g_hero_storage_b[HERO_WORLD_BYTES];
alignas(16) static uint8_t g_hero_storage_c[HERO_WORLD_BYTES];

// MSVC /WX flags a compile-time constant inside an if (C4127), and CHECK is an if.
// Routing layout constants through a function keeps the assertion a runtime one.
static size_t sz(size_t value) { return value; }
static uint64_t u64(uint64_t value) { return value; }

static SimWorldConfig hero_config(uint32_t max_entities = 64u) {
    SimWorldConfig config{};
    config.max_entities = max_entities;
    config.initial_unit_count = 0u;
    config.damage_event_capacity = 16u;
    config.hero_def_capacity = 2u;
    config.hero_capacity = 8u;
    config.projectile_capacity = 8u;
    config.status_capacity = 8u;
    config.sim_event_capacity = 16u;
    return config;
}

static SimEffectDef effect_projectile_damage(int32_t magnitude) {
    SimEffectDef effect{};
    effect.effect_type = SIM_EFFECT_PROJECTILE_DAMAGE;
    effect.magnitude = magnitude;
    return effect;
}

static SimEffectDef effect_self_heal(int32_t magnitude) {
    SimEffectDef effect{};
    effect.effect_type = SIM_EFFECT_SELF_HEAL;
    effect.magnitude = magnitude;
    return effect;
}

static SimEffectDef effect_area_slow(mm::fix radius, mm::fix scalar, uint16_t duration) {
    SimEffectDef effect{};
    effect.effect_type = SIM_EFFECT_AREA_SLOW;
    effect.duration_ticks = duration;
    effect.magnitude = 1;
    effect.radius = radius;
    effect.scalar = scalar;
    return effect;
}

// Slot 0: single-target projectile damage. Slot 1: self heal. Slot 2: area slow.
static SimHeroDef fixture_hero_def(uint64_t id = 0x1122334455667788ULL) {
    SimHeroDef def{};
    def.hero_def_id = id;
    def.max_health = 100;
    def.move_speed = mm::fix_from_int(2);
    def.attack_range = mm::fix_from_int(4);
    def.action_count = 3u;

    SimActionDef& bolt = def.actions[0];
    bolt.action_id = 0xA1ULL;
    bolt.slot = 0u;
    bolt.target_mode = SIM_TARGET_ENTITY;
    bolt.effect_count = 1u;
    bolt.cooldown_ticks = 5u;
    bolt.resource_cost = 10u;
    bolt.range = mm::fix_from_int(10);
    bolt.projectile_speed = mm::fix_from_int(30);
    bolt.effects[0] = effect_projectile_damage(20);

    SimActionDef& mend = def.actions[1];
    mend.action_id = 0xA2ULL;
    mend.slot = 1u;
    mend.target_mode = SIM_TARGET_SELF;
    mend.effect_count = 1u;
    mend.cooldown_ticks = 7u;
    mend.resource_cost = 5u;
    mend.effects[0] = effect_self_heal(15);

    SimActionDef& mire = def.actions[2];
    mire.action_id = 0xA3ULL;
    mire.slot = 2u;
    mire.target_mode = SIM_TARGET_AREA;
    mire.effect_count = 1u;
    mire.cooldown_ticks = 9u;
    mire.resource_cost = 20u;
    mire.range = mm::fix_from_int(8);
    mire.effects[0] = effect_area_slow(mm::fix_from_int(3), FIX_ONE / 2, 4u);
    return def;
}

TEST(sim_hero_combat, mirror_matches_proto_design_section_3_3_field_for_field) {
    // EffectDef: effect_type, damage_type, duration_ticks, magnitude, radius_q16,
    // scalar_q16 -- eight bytes of scalars then two Q16.16 words.
    CHECK(sz(sizeof(SimEffectDef)) == 16u);
    CHECK(sz(offsetof(SimEffectDef, effect_type)) == 0u);
    CHECK(sz(offsetof(SimEffectDef, damage_type)) == 1u);
    CHECK(sz(offsetof(SimEffectDef, duration_ticks)) == 2u);
    CHECK(sz(offsetof(SimEffectDef, magnitude)) == 4u);
    CHECK(sz(offsetof(SimEffectDef, radius)) == 8u);
    CHECK(sz(offsetof(SimEffectDef, scalar)) == 12u);

    // ActionDef: action_id, slot, target_mode, effect_count, cooldown_ticks,
    // cast_time_ticks, resource_cost, range_q16, projectile_speed_q16, effects[].
    CHECK(sz(offsetof(SimActionDef, action_id)) == 0u);
    CHECK(sz(offsetof(SimActionDef, slot)) == 8u);
    CHECK(sz(offsetof(SimActionDef, target_mode)) == 9u);
    CHECK(sz(offsetof(SimActionDef, effect_count)) == 10u);
    CHECK(sz(offsetof(SimActionDef, cooldown_ticks)) == 12u);
    CHECK(sz(offsetof(SimActionDef, cast_time_ticks)) == 16u);
    CHECK(sz(offsetof(SimActionDef, resource_cost)) == 20u);
    CHECK(sz(offsetof(SimActionDef, range)) == 24u);
    CHECK(sz(offsetof(SimActionDef, projectile_speed)) == 28u);
    CHECK(sz(offsetof(SimActionDef, effects)) == 32u);
    CHECK(sz(sizeof(SimActionDef)) == 32u + 16u * SIM_MAX_EFFECTS_PER_ACTION);

    // HeroDef: hero_def_id, max_health, move_speed_q16, attack_range_q16,
    // action_count, actions[]. schema_version stays in the asset layer.
    CHECK(sz(offsetof(SimHeroDef, hero_def_id)) == 0u);
    CHECK(sz(offsetof(SimHeroDef, max_health)) == 8u);
    CHECK(sz(offsetof(SimHeroDef, move_speed)) == 12u);
    CHECK(sz(offsetof(SimHeroDef, attack_range)) == 16u);
    CHECK(sz(offsetof(SimHeroDef, action_count)) == 20u);
    CHECK(sz(sizeof(SimHeroDef)) == 24u + sizeof(SimActionDef) * SIM_MAX_ACTION_SLOTS);
    CHECK(sz(SIM_MAX_ACTION_SLOTS) == 8u);
    CHECK(sz(SIM_MAX_EFFECTS_PER_ACTION) == 4u);
}

TEST(sim_hero_combat, invalid_schema_is_rejected_whole) {
    SimHeroDef def = fixture_hero_def();
    CHECK(sim_hero_def_valid(&def));

    SimHeroDef bad = def;
    bad.actions[0].effects[0].effect_type = 9u;      // unknown effect enum
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.actions[0].effect_count = SIM_MAX_EFFECTS_PER_ACTION + 1u;
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.action_count = SIM_MAX_ACTION_SLOTS + 1u;
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.max_health = -1;
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.actions[1].slot = 0u;                        // duplicate slot
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.actions[0].target_mode = 4u;                 // unknown target mode
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.actions[3].action_id = 1u;                   // trailing action must be zero
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.actions[0].effects[1].magnitude = 3;         // trailing effect must be zero
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.actions[2].effects[0].scalar = FIX_ONE;      // a slow that slows nothing
    CHECK(!sim_hero_def_valid(&bad));

    bad = def;
    bad.hero_def_id = 0u;
    CHECK(!sim_hero_def_valid(&bad));
}

TEST(sim_hero_combat, zero_capacity_world_allocates_nothing_and_reports_absent) {
    SimWorldConfig defaults = sim_world_config_default();
    CHECK(defaults.hero_def_capacity == 0u);
    CHECK(defaults.hero_capacity == 0u);
    CHECK(defaults.projectile_capacity == 0u);
    CHECK(defaults.status_capacity == 0u);
    CHECK(defaults.sim_event_capacity == 0u);

    SimWorldConfig bare{};
    bare.max_entities = 64u;
    bare.initial_unit_count = 4u;
    bare.damage_event_capacity = 8u;
    size_t bare_bytes = sim_world_memory_required(bare);
    CHECK(bare_bytes > 0u);

    Arena arena;
    arena_init_fixed(&arena, g_hero_storage, sizeof(g_hero_storage));
    SimWorld world{};
    CHECK(sim_init(&world, &arena, 7u, bare));
    CHECK(world.hero_defs.capacity == 0u);
    CHECK(world.hero_defs.defs == nullptr);
    CHECK(world.heroes.membership.capacity == 0u);
    CHECK(world.projectiles.membership.capacity == 0u);
    CHECK(world.statuses.membership.capacity == 0u);
    CHECK(world.sim_events.capacity == 0u);
    CHECK(world.sim_events.buffers[0] == nullptr);

    EntityId absent = world.unit_entities[0];
    CHECK(!hero_pool_has(&world.heroes, absent));
    CHECK(!projectile_pool_has(&world.projectiles, absent));
    CHECK(!status_pool_has(&world.statuses, absent));
    SimHeroDef def = fixture_hero_def();
    CHECK(!sim_install_hero_def(&world, &def, nullptr));
    CHECK(sim_hero_def_table_valid(&world.hero_defs));

    // The arena consumed exactly the reported budget, and the world still ticks.
    CHECK(arena.offset <= bare_bytes);
    CHECK(sim_tick(&world, nullptr));
    CHECK(world.tick == 1u);
}

TEST(sim_hero_combat, non_zero_capacity_world_initializes_every_pool) {
    SimWorldConfig config = hero_config();
    size_t required = sim_world_memory_required(config);
    CHECK(required > 0u);
    CHECK(required > sim_world_memory_required(SimWorldConfig{64u, 0u, 16u}));

    Arena arena;
    arena_init_fixed(&arena, g_hero_storage, sizeof(g_hero_storage));
    SimWorld world{};
    CHECK(sim_init(&world, &arena, 11u, config));
    CHECK(world.hero_defs.capacity == config.hero_def_capacity);
    CHECK(world.hero_defs.count == 0u);
    CHECK(world.heroes.membership.capacity == config.hero_capacity);
    CHECK(world.projectiles.membership.capacity == config.projectile_capacity);
    CHECK(world.statuses.membership.capacity == config.status_capacity);
    CHECK(sim_event_queue_is_valid(&world.sim_events));
    CHECK(world.sim_events.capacity == config.sim_event_capacity);
    CHECK(arena.offset <= required);

    uint16_t index = 0xFFFFu;
    SimHeroDef def = fixture_hero_def();
    CHECK(sim_install_hero_def(&world, &def, &index));
    CHECK(index == 0u);
    CHECK(world.hero_defs.count == 1u);
    const SimHeroDef* stored = sim_hero_def_table_get(&world.hero_defs, 0u);
    CHECK(stored != nullptr);
    CHECK(std::memcmp(stored, &def, sizeof(def)) == 0);

    CHECK(!sim_install_hero_def(&world, &def, nullptr));       // duplicate id
    SimHeroDef second = fixture_hero_def(0x99ULL);
    CHECK(sim_install_hero_def(&world, &second, &index));
    CHECK(index == 1u);
    SimHeroDef third = fixture_hero_def(0xABULL);
    CHECK(!sim_install_hero_def(&world, &third, nullptr));      // table is full
    CHECK(world.hero_defs.count == 2u);
    CHECK(sim_tick(&world, nullptr));
}

TEST(sim_hero_combat, initialization_failure_leaves_the_arena_untouched) {
    SimWorldConfig config = hero_config();
    size_t required = sim_world_memory_required(config);
    CHECK(required > 0u);

    Arena arena;
    arena_init_fixed(&arena, g_hero_storage, required - 1u);
    SimWorld world{};
    world.tick = 1234u;
    SimWorld before = world;
    CHECK(!sim_init(&world, &arena, 3u, config));
    CHECK(std::memcmp(&world, &before, sizeof(before)) == 0);
    CHECK(arena.offset == 0u);

    // A hero pool without a def table or an event queue is invalid configuration.
    SimWorldConfig broken = config;
    broken.hero_def_capacity = 0u;
    CHECK(sim_world_memory_required(broken) == 0u);
    broken = config;
    broken.sim_event_capacity = 0u;
    CHECK(sim_world_memory_required(broken) == 0u);
    broken = config;
    broken.hero_capacity = 0u;
    CHECK(sim_world_memory_required(broken) == 0u);
}

TEST(sim_hero_combat, pools_round_trip_and_swap_remove_keeps_rows_intact) {
    Arena arena;
    arena_init_fixed(&arena, g_hero_storage, sizeof(g_hero_storage));
    SimWorld world{};
    CHECK(sim_init(&world, &arena, 5u, hero_config()));

    EntityId a = entity_manager_create(&world.entities);
    EntityId b = entity_manager_create(&world.entities);
    CHECK(a.h != HANDLE_NULL && b.h != HANDLE_NULL);

    CHECK(hero_pool_add(&world.heroes, a, 0u, 40));
    CHECK(hero_pool_add(&world.heroes, b, 1u, 60));
    CHECK(!hero_pool_add(&world.heroes, a, 0u, 1));        // duplicate membership
    CHECK(!hero_pool_add(&world.heroes, b, 0u, -1));       // negative resource

    HeroView hero{};
    CHECK(hero_pool_get(&world.heroes, a, &hero));
    CHECK(*hero.def_index == 0u);
    CHECK(*hero.resource == 40);
    CHECK(*hero.basic_attack_cooldown == 0u);
    for (uint16_t slot = 0u; slot < SIM_MAX_ACTION_SLOTS; ++slot)
        CHECK(hero.action_cooldown[slot] == 0u);
    hero.action_cooldown[3] = 9u;

    CHECK(hero_pool_remove(&world.heroes, a));             // swap-removes b into slot 0
    CHECK(!hero_pool_has(&world.heroes, a));
    CHECK(hero_pool_get(&world.heroes, b, &hero));
    CHECK(*hero.resource == 60);
    CHECK(*hero.def_index == 1u);
    for (uint16_t slot = 0u; slot < SIM_MAX_ACTION_SLOTS; ++slot)
        CHECK(hero.action_cooldown[slot] == 0u);

    CHECK(status_pool_add(&world.statuses, b, SIM_EFFECT_AREA_SLOW, 4u, 1, FIX_ONE / 2));
    StatusView status{};
    CHECK(status_pool_get(&world.statuses, b, &status));
    CHECK(*status.remaining_ticks == 4u);
    CHECK(*status.stack_count == 1u);
    // Refresh-and-max: longer duration wins, larger magnitude carries its scalar.
    CHECK(status_pool_add(&world.statuses, b, SIM_EFFECT_AREA_SLOW, 2u, 5, FIX_ONE / 4));
    CHECK(*status.remaining_ticks == 4u);
    CHECK(*status.magnitude == 5);
    CHECK(*status.scalar == FIX_ONE / 4);
    CHECK(*status.stack_count == 2u);
    CHECK(world.statuses.membership.count == 1u);
    CHECK(status_pool_remove(&world.statuses, b));
    CHECK(!status_pool_has(&world.statuses, b));

    EntityId shot = entity_manager_create(&world.entities);
    CHECK(projectile_pool_add(&world.projectiles, shot, b, a, 0u, 0u, 0u,
                              mm::fix_from_int(1), mm::fix_from_int(2),
                              mm::fix_from_int(3), 20u));
    CHECK(!projectile_pool_add(&world.projectiles, shot, b, a, 0u, 0u, 0u, 0, 0, 0, 20u));
    ProjectileView shot_view{};
    CHECK(projectile_pool_get(&world.projectiles, shot, &shot_view));
    CHECK(*shot_view.speed == mm::fix_from_int(3));
    CHECK(*shot_view.remaining_ticks == 20u);
    CHECK(projectile_pool_remove(&world.projectiles, shot));
    CHECK(world.projectiles.membership.count == 0u);
}

TEST(sim_hero_combat, sim_event_envelope_is_canonical_and_phase_buffered) {
    CHECK(sz(sizeof(SimEvent)) == 32u);
    CHECK(sim_event_payload_size(SIM_EVENT_NONE) == 0u);
    CHECK(sim_event_payload_size(SIM_EVENT_HEAL) == 8u);
    CHECK(sim_event_payload_size(SIM_EVENT_STATUS_APPLIED) == 16u);
    CHECK(sim_event_payload_size(SIM_EVENT_STATUS_EXPIRED) == 8u);
    CHECK(sim_event_payload_size(SIM_EVENT_DEATH) == 8u);
    CHECK(sim_event_payload_size(77u) == 0u);

    alignas(16) uint8_t storage[4096]{};
    size_t required = sim_event_queue_memory_required(3u);
    CHECK(required > 0u);
    Arena arena;
    arena_init_fixed(&arena, storage, required);
    SimEventQueue queue{};
    CHECK(sim_event_queue_init(&queue, &arena, 3u));
    CHECK(sim_event_queue_is_valid(&queue));
    CHECK(sim_event_queue_write_room(&queue) == 3u);

    EntityId target{handle_make(4u, 1u)};
    EntityId source{handle_make(5u, 1u)};
    SimEvent heal = sim_event_make_heal(9u, target, 12);
    CHECK(sim_event_is_canonical(&heal));
    CHECK(heal.payload_size == 8u);
    CHECK(sim_event_payload_u32(&heal, 0u) == target.h);
    CHECK(sim_event_payload_i32(&heal, 4u) == 12);

    SimEvent slow = sim_event_make_status_applied(9u, target, SIM_EFFECT_AREA_SLOW, 6u, 3,
                                                  FIX_ONE / 2);
    CHECK(sim_event_is_canonical(&slow));
    CHECK(sim_event_payload_u8(&slow, 4u) == SIM_EFFECT_AREA_SLOW);
    CHECK(sim_event_payload_u16(&slow, 6u) == 6u);
    CHECK(sim_event_payload_i32(&slow, 8u) == 3);
    CHECK(sim_event_payload_i32(&slow, 12u) == FIX_ONE / 2);

    SimEvent death = sim_event_make_death(9u, target, source);
    CHECK(sim_event_is_canonical(&death));
    CHECK(sim_event_payload_u32(&death, 4u) == source.h);

    SimEvent malformed = heal;
    malformed.payload_size = 12u;
    CHECK(!sim_event_is_canonical(&malformed));
    malformed = heal;
    malformed.payload[9] = 1u;                       // a non-zero byte past the payload
    CHECK(!sim_event_is_canonical(&malformed));
    malformed = heal;
    malformed.event_kind = SIM_EVENT_NONE;
    CHECK(!sim_event_is_canonical(&malformed));
    CHECK(!sim_event_queue_append(&queue, &malformed));

    CHECK(sim_event_queue_append(&queue, &heal));
    CHECK(sim_event_queue_append(&queue, &slow));
    CHECK(sim_event_queue_append(&queue, &death));
    CHECK(sim_event_queue_write_room(&queue) == 0u);
    CHECK(!sim_event_queue_append(&queue, &heal));    // reject at source, never drop

    SimEventView view{};
    CHECK(sim_event_queue_read(&queue, &view));
    CHECK(view.count == 0u);                          // still the write phase
    CHECK(sim_event_queue_publish(&queue));
    CHECK(sim_event_queue_read(&queue, &view));
    CHECK(view.count == 3u);
    CHECK(view.events[0].event_kind == SIM_EVENT_HEAL);
    CHECK(view.events[0].append_ordinal == 0u);
    CHECK(view.events[1].event_kind == SIM_EVENT_STATUS_APPLIED);
    CHECK(view.events[1].append_ordinal == 1u);
    CHECK(view.events[2].event_kind == SIM_EVENT_DEATH);
    CHECK(view.events[2].append_ordinal == 2u);
    CHECK(!sim_event_queue_publish(&queue));          // read phase is not drained
    CHECK(sim_event_queue_consume(&queue));
    CHECK(sim_event_queue_read(&queue, &view));
    CHECK(view.count == 0u);
    CHECK(sim_event_queue_publish(&queue));

    alignas(16) uint8_t short_storage[4096]{};
    Arena short_arena;
    arena_init_fixed(&short_arena, short_storage, required - 1u);
    SimEventQueue untouched{};
    untouched.capacity = 42u;
    SimEventQueue before = untouched;
    CHECK(!sim_event_queue_init(&untouched, &short_arena, 3u));
    CHECK(std::memcmp(&untouched, &before, sizeof(before)) == 0);
    CHECK(short_arena.offset == 0u);
    CHECK(sim_event_queue_memory_required(0u) == 0u);
}

TEST(sim_hero_combat, hero_capacity_does_not_move_an_otherwise_identical_world) {
    // The M5.2 pools are additive canonical state: two worlds that differ only in
    // whether the hero feature is configured must still tick, and the configured
    // world must stay internally consistent.
    Arena arena_a;
    arena_init_fixed(&arena_a, g_hero_storage, sizeof(g_hero_storage));
    Arena arena_b;
    arena_init_fixed(&arena_b, g_hero_storage_b, sizeof(g_hero_storage_b));

    SimWorldConfig bare{};
    bare.max_entities = 64u;
    bare.initial_unit_count = 8u;
    bare.damage_event_capacity = 8u;
    SimWorldConfig rich = bare;
    rich.hero_def_capacity = 2u;
    rich.hero_capacity = 4u;
    rich.projectile_capacity = 4u;
    rich.status_capacity = 4u;
    rich.sim_event_capacity = 8u;

    SimWorld plain{};
    SimWorld fancy{};
    CHECK(sim_init(&plain, &arena_a, 21u, bare));
    CHECK(sim_init(&fancy, &arena_b, 21u, rich));
    for (uint32_t tick = 0u; tick < 32u; ++tick) {
        CHECK(sim_tick(&plain, nullptr));
        CHECK(sim_tick(&fancy, nullptr));
    }
    CHECK(plain.tick == 32u);
    CHECK(fancy.tick == 32u);
    CHECK(plain.rng.state == fancy.rng.state);
    CHECK(sim_hash_state(&plain) != 0u);
    CHECK(sim_hash_state(&fancy) != 0u);
}

// ------------------------------------------------------------------ S2 pipeline

// Builds a two-unit world with a hero in slot 0 and a plain target in slot 1.
struct HeroFixture {
    SimWorld world;
    EntityId hero;
    EntityId target;
    uint16_t def_index;
    size_t arena_offset;                 // arena bytes consumed by sim_init
};

static bool build_fixture_with_def(HeroFixture* fixture, uint8_t* storage,
                                   size_t storage_bytes, SimWorldConfig config,
                                   mm::fix target_x, const SimHeroDef* def) {
    Arena arena;
    arena_init_fixed(&arena, storage, storage_bytes);
    config.initial_unit_count = 2u;
    if (!sim_init(&fixture->world, &arena, 17u, config)) return false;
    fixture->arena_offset = arena.offset;
    if (!sim_install_hero_def(&fixture->world, def, &fixture->def_index)) return false;

    fixture->hero = fixture->world.unit_entities[0];
    fixture->target = fixture->world.unit_entities[1];
    TransformView hero_transform{};
    TransformView target_transform{};
    if (!transform_pool_get(&fixture->world.transforms, fixture->hero, &hero_transform) ||
        !transform_pool_get(&fixture->world.transforms, fixture->target, &target_transform))
        return false;
    *hero_transform.position_x = 0;
    *hero_transform.position_y = 0;
    *target_transform.position_x = target_x;
    *target_transform.position_y = 0;
    return hero_pool_add(&fixture->world.heroes, fixture->hero, fixture->def_index, 50);
}

static bool build_fixture(HeroFixture* fixture, uint8_t* storage, size_t storage_bytes,
                          SimWorldConfig config, mm::fix target_x) {
    SimHeroDef def = fixture_hero_def();
    return build_fixture_with_def(fixture, storage, storage_bytes, config, target_x, &def);
}

static SimCommand attack_command(uint16_t actor_slot, int32_t target_slot) {
    SimCommand command{};
    command.kind = SIM_COMMAND_ATTACK;
    command.unit_index = actor_slot;
    command.value_x = mm::fix_from_int(target_slot);
    return command;
}

static int32_t health_of(SimWorld* world, EntityId entity) {
    HealthView view{};
    if (!health_pool_get(&world->health, entity, &view)) return -1;
    return *view.current;
}

TEST(sim_hero_combat, attack_command_canonicality_is_self_contained) {
    SimCommand attack = attack_command(0u, 3);
    CHECK(sim_command_is_canonical(&attack, SIM_MAX_PLAYERS, SIM_MAX_UNITS));

    SimCommand bad = attack;
    bad.value_y = mm::fix_from_int(1);
    CHECK(!sim_command_is_canonical(&bad, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    bad = attack;
    bad.amount = 1;
    CHECK(!sim_command_is_canonical(&bad, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    bad = attack;
    bad.value_x = mm::fix_from_int(1) + 1;                 // not an exact unit slot
    CHECK(!sim_command_is_canonical(&bad, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    bad = attack;
    bad.value_x = mm::fix_from_int(-1);
    CHECK(!sim_command_is_canonical(&bad, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    bad = attack;
    bad.value_x = mm::fix_from_int(static_cast<int32_t>(SIM_MAX_UNITS));
    CHECK(!sim_command_is_canonical(&bad, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    CHECK(sz(sizeof(SimCommand)) == 16u);                  // the record stays frozen
}

TEST(sim_hero_combat, basic_attack_runs_through_the_pipeline_with_cooldown_and_range) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;
    CHECK(health_of(&world, fixture.target) == 100);

    SimCommand attack = attack_command(0u, 1);
    SimCommandBuffer buffer{&attack, 1u};
    CHECK(sim_validate_commands(&world, &buffer));
    CHECK(sim_tick(&world, &buffer));
    CHECK(health_of(&world, fixture.target) == 100 - SIM_BASIC_ATTACK_MAGNITUDE);

    HeroView hero{};
    CHECK(hero_pool_get(&world.heroes, fixture.hero, &hero));
    // sys_cooldown_tick runs after the action in the same tick, so one tick of the
    // cadence is already spent.
    CHECK(*hero.basic_attack_cooldown == SIM_BASIC_ATTACK_COOLDOWN_TICKS - 1u);
    CHECK(*hero.pending_kind == SIM_HERO_PENDING_NONE);
    CHECK(hero.pending_target->h == HANDLE_NULL);

    // On cooldown the command is still accepted and the action is a no-op, never a
    // rejected tick.
    CHECK(sim_tick(&world, &buffer));
    CHECK(health_of(&world, fixture.target) == 100 - SIM_BASIC_ATTACK_MAGNITUDE);
    for (uint32_t i = 0u; i < SIM_BASIC_ATTACK_COOLDOWN_TICKS - 2u; ++i)
        CHECK(sim_tick(&world, nullptr));
    CHECK(*hero.basic_attack_cooldown == 0u);
    CHECK(sim_tick(&world, &buffer));
    CHECK(health_of(&world, fixture.target) == 100 - 2 * SIM_BASIC_ATTACK_MAGNITUDE);
}

TEST(sim_hero_combat, out_of_range_attack_is_a_no_op_not_a_failed_tick) {
    HeroFixture fixture{};
    // attack_range is 4; put the target at 9 so the squared test misses by a mile.
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(9)));
    SimWorld& world = fixture.world;
    SimCommand attack = attack_command(0u, 1);
    SimCommandBuffer buffer{&attack, 1u};
    CHECK(sim_tick(&world, &buffer));
    CHECK(health_of(&world, fixture.target) == 100);
    HeroView hero{};
    CHECK(hero_pool_get(&world.heroes, fixture.hero, &hero));
    CHECK(*hero.basic_attack_cooldown == 0u);
    CHECK(*hero.pending_kind == SIM_HERO_PENDING_NONE);

    // The range test is a squared comparison in 64 bits, so a Q16.16 square that
    // overflows 32 bits still compares correctly.
    CHECK(sim_range_squared(mm::fix_from_int(4)) ==
          static_cast<int64_t>(mm::fix_from_int(4)) * mm::fix_from_int(4));
    int64_t distance_squared = 0;
    CHECK(sim_distance_squared(&world, fixture.hero, fixture.target, &distance_squared));
    CHECK(distance_squared > sim_range_squared(mm::fix_from_int(4)));
    CHECK(distance_squared == sim_range_squared(mm::fix_from_int(9)));
}

TEST(sim_hero_combat, resolve_effect_is_the_only_committer_and_rejects_stale_handles) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    SimEffectDef basic = sim_basic_attack_effect();
    CHECK(basic.effect_type == SIM_EFFECT_PROJECTILE_DAMAGE);
    CHECK(basic.magnitude == SIM_BASIC_ATTACK_MAGNITUDE);
    CHECK(sim_effect_def_valid(&basic));

    uint32_t damage_events = 0u;
    uint32_t sim_events = 0u;
    CHECK(resolve_effect_measure(&world, fixture.hero, fixture.target, &basic, 0, 0,
                                 &damage_events, &sim_events));
    CHECK(damage_events == 1u);
    CHECK(sim_events == 0u);
    CHECK(world.damage_events.counts[world.damage_events.write_index] == 0u);

    SimEffectDef heal = effect_self_heal(15);
    damage_events = 0u;
    sim_events = 0u;
    CHECK(resolve_effect_measure(&world, fixture.hero, fixture.hero, &heal, 0, 0,
                                 &damage_events, &sim_events));
    CHECK(damage_events == 0u);
    CHECK(sim_events == 1u);

    // A stale handle fails the pipeline instead of committing a partial effect.
    EntityId stale{handle_make(handle_index(fixture.target.h),
                               static_cast<uint16_t>(handle_gen(fixture.target.h) + 1u))};
    CHECK(!resolve_effect(&world, fixture.hero, stale, &basic, 0, 0));
    CHECK(!resolve_effect(&world, stale, fixture.target, &basic, 0, 0));
    SimEffectDef malformed = basic;
    malformed.effect_type = 12u;
    CHECK(!resolve_effect(&world, fixture.hero, fixture.target, &malformed, 0, 0));
    CHECK(world.damage_events.counts[world.damage_events.write_index] == 0u);

    CHECK(resolve_effect(&world, fixture.hero, fixture.target, &basic, 0, 0));
    CHECK(world.damage_events.counts[world.damage_events.write_index] == 1u);
}

TEST(sim_hero_combat, basic_attack_translation_only_fires_for_a_hero_actor) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    Command command{};
    command.command_kind = COMMAND_KIND_BASIC_ATTACK;
    command.actor = fixture.hero;
    command.target = fixture.target;
    SimCommand translated{};
    CHECK(command_to_sim(&world, &command, &translated));
    CHECK(translated.kind == SIM_COMMAND_ATTACK);
    CHECK(translated.unit_index == 0u);
    CHECK(translated.value_x == mm::fix_from_int(1));
    CHECK(translated.amount == 0);

    // A unit with no hero row keeps the M5.1 DAMAGE placeholder untouched.
    command.actor = fixture.target;
    command.target = fixture.hero;
    CHECK(command_to_sim(&world, &command, &translated));
    CHECK(translated.kind == SIM_COMMAND_DAMAGE);
    CHECK(translated.unit_index == 0u);
    CHECK(translated.amount == 1);
}

// ------------------------------------------------------------------ S3 effects

static SimCommand cast_command(uint16_t actor_slot, int32_t action_slot,
                               mm::fix value_x = 0, mm::fix value_y = 0) {
    SimCommand command{};
    command.kind = SIM_COMMAND_CAST;
    command.unit_index = actor_slot;
    command.amount = action_slot;
    command.value_x = value_x;
    command.value_y = value_y;
    return command;
}

TEST(sim_hero_combat, cast_command_canonicality_bounds_only_the_action_slot) {
    SimCommand cast = cast_command(0u, 2, mm::fix_from_int(3), mm::fix_from_int(-4));
    CHECK(sim_command_is_canonical(&cast, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    SimCommand bad = cast;
    bad.amount = static_cast<int32_t>(SIM_MAX_ACTION_SLOTS);
    CHECK(!sim_command_is_canonical(&bad, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    bad = cast;
    bad.amount = -1;
    CHECK(!sim_command_is_canonical(&bad, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
}

TEST(sim_hero_combat, projectile_damage_flies_then_re_enters_the_pipeline) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(6)));
    SimWorld& world = fixture.world;

    // Slot 0: entity-targeted projectile damage, range 10, speed 30, magnitude 20.
    SimCommand cast = cast_command(0u, 0, mm::fix_from_int(1));
    SimCommandBuffer buffer{&cast, 1u};
    CHECK(sim_validate_commands(&world, &buffer));
    CHECK(sim_tick(&world, &buffer));

    // The projectile exists and no damage has landed yet.
    CHECK(world.projectiles.membership.count == 1u);
    CHECK(health_of(&world, fixture.target) == 100);
    HeroView hero{};
    CHECK(hero_pool_get(&world.heroes, fixture.hero, &hero));
    CHECK(*hero.resource == 40);                       // 50 - resource_cost 10
    CHECK(hero.action_cooldown[0] == 4u);              // 5, minus this tick

    uint32_t flight = 0u;
    while (world.projectiles.membership.count > 0u && flight < 64u) {
        CHECK(sim_tick(&world, nullptr));
        ++flight;
    }
    CHECK(flight > 0u);
    CHECK(world.projectiles.membership.count == 0u);
    CHECK(health_of(&world, fixture.target) == 80);
    CHECK(world.entities.free_count == 1u);            // the projectile entity recycled
}

TEST(sim_hero_combat, self_heal_rides_the_envelope_and_clamps_at_maximum) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    HealthView hero_health{};
    CHECK(health_pool_get(&world.health, fixture.hero, &hero_health));
    *hero_health.current = 50;

    SimCommand cast = cast_command(0u, 1);             // slot 1: self heal, magnitude 15
    SimCommandBuffer buffer{&cast, 1u};
    CHECK(sim_tick(&world, &buffer));
    CHECK(health_of(&world, fixture.hero) == 65);
    HeroView hero{};
    CHECK(hero_pool_get(&world.heroes, fixture.hero, &hero));
    CHECK(*hero.resource == 45);                       // 50 - resource_cost 5

    // Not enough resource is a no-op, not a rejected tick.
    *hero.resource = 2;
    for (uint32_t i = 0u; i < 8u; ++i) CHECK(sim_tick(&world, nullptr));
    CHECK(sim_tick(&world, &buffer));
    CHECK(health_of(&world, fixture.hero) == 65);
    CHECK(*hero.resource == 2);

    // A heal never exceeds maximum.
    *hero.resource = 50;
    *hero_health.current = 95;
    CHECK(sim_tick(&world, &buffer));
    CHECK(health_of(&world, fixture.hero) == 100);
}

TEST(sim_hero_combat, area_slow_applies_a_status_that_reaches_movement_next_tick) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    VelocityView velocity{};
    CHECK(velocity_pool_get(&world.velocities, fixture.target, &velocity));
    *velocity.velocity_x = mm::fix_from_int(4);
    TransformView target_transform{};
    CHECK(transform_pool_get(&world.transforms, fixture.target, &target_transform));
    mm::fix full_step = mm::fix_mul(mm::fix_from_int(4), SIM_DT_FIXED);
    mm::fix start_x = *target_transform.position_x;

    // Slot 2: area slow, radius 3, scalar 0.5, duration 4, cast at the target point.
    SimCommand cast = cast_command(0u, 2, mm::fix_from_int(2), 0);
    SimCommandBuffer buffer{&cast, 1u};
    CHECK(sim_tick(&world, &buffer));

    // sys_status runs after sys_movement, so this tick still integrated at full
    // speed and the slow first bites on the next tick. Intended, per section 4.
    CHECK(*target_transform.position_x == start_x + full_step);
    CHECK(status_pool_has(&world.statuses, fixture.target));
    StatusView status{};
    CHECK(status_pool_get(&world.statuses, fixture.target, &status));
    CHECK(*status.effect_type == SIM_EFFECT_AREA_SLOW);
    // sys_effects_resolve commits the row after sys_status has already run, so the
    // full duration is intact on the tick it was applied.
    CHECK(*status.remaining_ticks == 4u);
    CHECK(*status.scalar == FIX_ONE / 2);

    mm::fix before = *target_transform.position_x;
    CHECK(sim_tick(&world, nullptr));
    CHECK(*target_transform.position_x == before + mm::fix_mul(full_step, FIX_ONE / 2));
    CHECK(*status.remaining_ticks == 3u);

    // Four slowed ticks, then the row expires on schedule and is gone.
    CHECK(sim_tick(&world, nullptr));
    CHECK(sim_tick(&world, nullptr));
    CHECK(status_pool_has(&world.statuses, fixture.target));
    CHECK(sim_tick(&world, nullptr));
    CHECK(!status_pool_has(&world.statuses, fixture.target));
    before = *target_transform.position_x;
    CHECK(sim_tick(&world, nullptr));
    CHECK(*target_transform.position_x == before + full_step);
}

TEST(sim_hero_combat, use_action_translates_to_cast_through_the_def_table) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    Command command{};
    command.command_kind = COMMAND_KIND_USE_ACTION;
    command.actor = fixture.hero;
    command.action_id = 0xA1ULL;                       // slot 0, entity-targeted
    command.target = fixture.target;
    SimCommand translated{};
    CHECK(command_to_sim(&world, &command, &translated));
    CHECK(translated.kind == SIM_COMMAND_CAST);
    CHECK(translated.unit_index == 0u);
    CHECK(translated.amount == 0);
    CHECK(translated.value_x == mm::fix_from_int(1));

    command.action_id = 0xA3ULL;                       // slot 2, area-targeted
    command.target = EntityId{HANDLE_NULL};
    command.point_x_q16 = mm::fix_from_int(3);
    command.point_y_q16 = mm::fix_from_int(-2);
    CHECK(command_to_sim(&world, &command, &translated));
    CHECK(translated.amount == 2);
    CHECK(translated.value_x == mm::fix_from_int(3));
    CHECK(translated.value_y == mm::fix_from_int(-2));

    command.action_id = 0xDEADULL;                     // no such action in the def
    CHECK(!command_to_sim(&world, &command, &translated));

    // A unit with no hero row still has no USE_ACTION seam.
    command.actor = fixture.target;
    command.action_id = 0xA1ULL;
    CHECK(!command_to_sim(&world, &command, &translated));
}

TEST(sim_hero_combat, unknown_cast_slot_is_an_atomic_packet_reject) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    SimCommand cast = cast_command(0u, 5);             // canonical, but not in the def
    SimCommandBuffer buffer{&cast, 1u};
    CHECK(sim_command_is_canonical(&cast, SIM_MAX_PLAYERS, SIM_MAX_UNITS));
    CHECK(!sim_validate_commands(&world, &buffer));
    uint64_t before = sim_hash_state(&world);
    CHECK(!sim_tick(&world, &buffer));
    CHECK(sim_hash_state(&world) == before);
    CHECK(world.tick == 0u);

    // The same command from a unit with no hero row is rejected too.
    SimCommand from_non_hero = cast_command(1u, 0);
    SimCommandBuffer non_hero{&from_non_hero, 1u};
    CHECK(!sim_validate_commands(&world, &non_hero));
}

// ------------------------------------------------------------------ S4 hash + diff

TEST(sim_hero_combat, the_hero_block_is_canonical_state_and_diffs_in_hash_order) {
    HeroFixture a{};
    HeroFixture b{};
    CHECK(build_fixture(&a, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    CHECK(build_fixture(&b, g_hero_storage_b, sizeof(g_hero_storage_b), hero_config(),
                        mm::fix_from_int(2)));

    uint64_t base = sim_hash_state(&a.world);
    CHECK(base != 0u);
    CHECK(sim_hash_state(&b.world) == base);
    SimStateDiff diff{};
    CHECK(!sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_NONE);

    HeroView hero{};
    CHECK(hero_pool_get(&b.world.heroes, b.hero, &hero));
    *hero.resource += 1;
    CHECK(sim_hash_state(&b.world) != base);
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_HERO_RESOURCE);
    CHECK(diff.expected_value == 50u);
    CHECK(diff.actual_value == 51u);
    *hero.resource -= 1;
    CHECK(sim_hash_state(&b.world) == base);

    hero.action_cooldown[2] = 4u;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_HERO_ACTION_COOLDOWN);
    hero.action_cooldown[2] = 0u;

    *hero.pending_kind = SIM_HERO_PENDING_ATTACK;
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_HERO_PENDING_KIND);
    *hero.pending_kind = SIM_HERO_PENDING_NONE;

    // A second def in one world only: the def table is hashed and diffs per record.
    SimHeroDef extra = fixture_hero_def(0x5150ULL);
    CHECK(sim_install_hero_def(&b.world, &extra, nullptr));
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_HERO_DEF_COUNT);
    CHECK(sim_hash_state(&b.world) != base);

    // Status and projectile rows are canonical state too.
    CHECK(status_pool_add(&a.world.statuses, a.target, SIM_EFFECT_AREA_SLOW, 3u, 1,
                          FIX_ONE / 2));
    CHECK(sim_hash_state(&a.world) != base);
    CHECK(sim_diff_state(&a.world, &b.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_HERO_DEF_COUNT);   // still the earliest block
    CHECK(status_pool_remove(&a.world.statuses, a.target));
    CHECK(sim_hash_state(&a.world) == base);
}

TEST(sim_hero_combat, every_new_state_field_has_a_name_and_the_logic_key_moved_once) {
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_HERO_DEF_COUNT),
                      "hero_def_count") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_HERO_DEF_RECORD),
                      "hero_def_record") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_HERO_RESOURCE),
                      "hero_resource") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_PROJECTILE_POSITION_X),
                      "projectile_position_x") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_STATUS_REMAINING_TICKS),
                      "status_remaining_ticks") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_SIM_EVENT_WRITE_PAYLOAD),
                      "sim_event_write_payload") == 0);
    // The M5.0 block still reports the same names, so old divergence reports read
    // exactly as they did before the append.
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_MAP_LANE_WAYPOINT),
                      "map_lane_waypoint") == 0);
    CHECK(std::strcmp(sim_state_field_name(SIM_STATE_FIELD_POSITION_X), "position_x") == 0);
    CHECK(u64(SIM_LOGIC_HASH) == 0x46e9e287878ba88cULL);
}

TEST(sim_hero_combat, a_hero_row_naming_an_absent_def_is_not_a_canonical_world) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    CHECK(sim_hash_state(&fixture.world) != 0u);

    HeroView hero{};
    CHECK(hero_pool_get(&fixture.world.heroes, fixture.hero, &hero));
    *hero.def_index = 9u;
    CHECK(sim_hash_state(&fixture.world) == 0u);
    SimStateDiff diff{};
    CHECK(sim_diff_state(&fixture.world, &fixture.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_INVALID);
    *hero.def_index = fixture.def_index;
    CHECK(sim_hash_state(&fixture.world) != 0u);
}

// ------------------------------------------------------------------ S5 acceptance
// proto-design section 7.2 items 3, 4, and 5, plus the command-intake seam for
// USE_ACTION. Everything above proves a mechanism; this block proves the three
// properties the milestone is accepted on.

// Item 3. ADR-0014 is reject-at-source: a write phase that cannot hold the tick's
// events fails the operation that produced them, and never drops or overwrites an
// event that is already queued.
TEST(sim_hero_combat, a_full_event_queue_fails_the_source_operation_and_drops_nothing) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;
    HealthView hero_health{};
    CHECK(health_pool_get(&world.health, fixture.hero, &hero_health));
    *hero_health.current = 50;

    // Fill the write phase to capacity with canonical events of our own, so the
    // rejection below is provably backpressure and not a malformed payload.
    const uint32_t capacity = world.config.sim_event_capacity;
    CHECK(sim_event_queue_write_room(&world.sim_events) == capacity);
    for (uint32_t i = 0u; i < capacity; ++i) {
        SimEvent filler = sim_event_make_heal(world.tick, fixture.hero,
                                              static_cast<int32_t>(i) + 1);
        CHECK(sim_event_queue_append(&world.sim_events, &filler));
    }
    CHECK(sim_event_queue_write_room(&world.sim_events) == 0u);
    CHECK(world.sim_events.counts[world.sim_events.write_index] == capacity);
    // One more append is refused at the queue, not silently wrapped.
    SimEvent overflow = sim_event_make_heal(world.tick, fixture.hero, 99);
    CHECK(!sim_event_queue_append(&world.sim_events, &overflow));

    HeroView hero{};
    CHECK(hero_pool_get(&world.heroes, fixture.hero, &hero));
    const int32_t resource_before = *hero.resource;
    const int32_t health_before = *hero_health.current;
    const uint32_t cooldown_before = hero.action_cooldown[1];
    const uint64_t tick_before = world.tick;

    // Slot 1 is the self heal: exactly one SimEvent, and there is no room for it.
    // The shape is fine, so intake and validation both pass; only the source
    // operation fails.
    SimCommand cast = cast_command(0u, 1);
    SimCommandBuffer buffer{&cast, 1u};
    CHECK(sim_validate_commands(&world, &buffer));
    CHECK(!sim_tick(&world, &buffer));
    CHECK(world.tick == tick_before);

    // Nothing queued was dropped, reordered, or overwritten.
    CHECK(world.sim_events.counts[world.sim_events.write_index] == capacity);
    const SimEvent* written = world.sim_events.buffers[world.sim_events.write_index];
    for (uint32_t i = 0u; i < capacity; ++i) {
        CHECK(written[i].event_kind == SIM_EVENT_HEAL);
        CHECK(written[i].append_ordinal == i);
        CHECK(sim_event_payload_u32(&written[i], 0u) == fixture.hero.h);
        CHECK(sim_event_payload_i32(&written[i], 4u) == static_cast<int32_t>(i) + 1);
        CHECK(sim_event_is_canonical(&written[i]));
    }
    // The rejected source operation paid nothing: no health, no resource, no cooldown.
    CHECK(*hero_health.current == health_before);
    CHECK(*hero.resource == resource_before);
    CHECK(hero.action_cooldown[1] == cooldown_before);

    // Draining the queue makes the identical command succeed, so the world was
    // refused, not corrupted, and sim_tick advances again.
    CHECK(sim_event_queue_publish(&world.sim_events));
    CHECK(sim_event_queue_consume(&world.sim_events));
    CHECK(sim_event_queue_write_room(&world.sim_events) == capacity);
    CHECK(sim_tick(&world, &buffer));
    CHECK(world.tick == tick_before + 1u);
    CHECK(*hero_health.current == health_before + 15);
    CHECK(*hero.resource == resource_before - 5);
    CHECK(hero.action_cooldown[1] == 7u - 1u);
}

// The same property on the damage wire: a full DamageEventQueue fails the attack
// that would have appended to it, and DamageEvent stays untouched.
TEST(sim_hero_combat, a_full_damage_queue_fails_the_attack_that_would_append) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    const uint32_t capacity = world.config.damage_event_capacity;
    CHECK(damage_event_queue_write_room(&world.damage_events) == capacity);
    for (uint32_t i = 0u; i < capacity; ++i) {
        DamageEvent filler{};
        filler.source = fixture.hero;
        filler.target = fixture.target;
        filler.amount = static_cast<int32_t>(i) + 1;
        CHECK(damage_event_is_canonical(&filler));
        CHECK(damage_event_queue_append(&world.damage_events, &filler));
    }
    CHECK(damage_event_queue_write_room(&world.damage_events) == 0u);

    HeroView hero{};
    CHECK(hero_pool_get(&world.heroes, fixture.hero, &hero));
    const int32_t target_health_before = health_of(&world, fixture.target);
    const uint32_t attack_cooldown_before = *hero.basic_attack_cooldown;

    SimCommand attack = attack_command(0u, 1);
    SimCommandBuffer buffer{&attack, 1u};
    CHECK(!sim_tick(&world, &buffer));
    CHECK(world.tick == 0u);
    CHECK(world.damage_events.counts[world.damage_events.write_index] == capacity);
    CHECK(health_of(&world, fixture.target) == target_health_before);
    CHECK(*hero.basic_attack_cooldown == attack_cooldown_before);
}

// Item 4. ADR-0013: sys_rng_advance draws exactly once per tick, unconditionally.
// Two worlds that differ only in how much hero data they carry must therefore agree
// on the stream AND on the number of draws taken to reach it.
TEST(sim_hero_combat, an_extra_action_def_changes_neither_rng_stream_nor_draw_count) {
    SimHeroDef lean = fixture_hero_def();
    SimHeroDef rich = fixture_hero_def();
    SimActionDef& extra = rich.actions[3];
    extra.action_id = 0xA4ULL;
    extra.slot = 3u;
    extra.target_mode = SIM_TARGET_SELF;
    extra.effect_count = 1u;
    extra.cooldown_ticks = 3u;
    extra.resource_cost = 1u;
    extra.effects[0] = effect_self_heal(2);
    rich.action_count = 4u;
    CHECK(sim_hero_def_valid(&lean));
    CHECK(sim_hero_def_valid(&rich));

    HeroFixture lean_world{};
    HeroFixture rich_world{};
    CHECK(build_fixture_with_def(&lean_world, g_hero_storage, sizeof(g_hero_storage),
                                 hero_config(), mm::fix_from_int(2), &lean));
    CHECK(build_fixture_with_def(&rich_world, g_hero_storage_b, sizeof(g_hero_storage_b),
                                 hero_config(), mm::fix_from_int(2), &rich));

    // A local generator seeded from the world's own initial state is the draw
    // counter: after N ticks the world must be exactly N draws along, so equality
    // proves the count as well as the stream, with no instrumentation in the sim.
    mm::pcg32 lean_expected = lean_world.world.rng;
    mm::pcg32 rich_expected = rich_world.world.rng;
    CHECK(lean_expected.state == rich_expected.state);
    CHECK(lean_expected.inc == rich_expected.inc);

    HeroView lean_hero{};
    HeroView rich_hero{};
    CHECK(hero_pool_get(&lean_world.world.heroes, lean_world.hero, &lean_hero));
    CHECK(hero_pool_get(&rich_world.world.heroes, rich_world.hero, &rich_hero));
    HealthView lean_health{};
    HealthView rich_health{};
    CHECK(health_pool_get(&lean_world.world.health, lean_world.target, &lean_health));
    CHECK(health_pool_get(&rich_world.world.health, rich_world.target, &rich_health));

    const uint32_t TICKS = 1000u;
    uint32_t drift = 0u;
    for (uint32_t tick = 0u; tick < TICKS; ++tick) {
        // One scripted stream drives both worlds. The top-up at the end of each
        // 25-tick cycle is applied identically to both, so neither world ever runs
        // out of resource or loses its target, and the comparison stays live for
        // the whole run.
        if (tick % 25u == 24u) {
            *lean_hero.resource = 50;
            *rich_hero.resource = 50;
            *lean_health.current = 100;
            *rich_health.current = 100;
        }
        SimCommand command{};
        SimCommandBuffer buffer{&command, 1u};
        const SimCommandBuffer* drive = nullptr;
        switch (tick % 25u) {
            case 0u:  command = cast_command(0u, 0, mm::fix_from_int(1)); drive = &buffer; break;
            case 7u:  command = cast_command(0u, 1);                      drive = &buffer; break;
            case 14u: command = cast_command(0u, 2, mm::fix_from_int(2), 0); drive = &buffer; break;
            case 20u: command = attack_command(0u, 1);                    drive = &buffer; break;
            default: break;
        }
        if (!sim_tick(&lean_world.world, drive)) ++drift;
        if (!sim_tick(&rich_world.world, drive)) ++drift;
        (void)mm::pcg32_next(&lean_expected);
        (void)mm::pcg32_next(&rich_expected);
        if (lean_world.world.rng.state != lean_expected.state) ++drift;
        if (rich_world.world.rng.state != rich_expected.state) ++drift;
        if (lean_world.world.rng.state != rich_world.world.rng.state) ++drift;
    }
    CHECK(drift == 0u);
    CHECK(lean_world.world.tick == TICKS);
    CHECK(rich_world.world.tick == TICKS);
    CHECK(lean_world.world.rng.state == rich_world.world.rng.state);
    CHECK(lean_world.world.rng.inc == rich_world.world.rng.inc);
    // The extra action really is in the rich world, so the agreement above is not
    // the agreement of two identical worlds.
    CHECK(lean_world.world.hero_defs.defs[0].action_count == 3u);
    CHECK(rich_world.world.hero_defs.defs[0].action_count == 4u);
    CHECK(sim_hash_state(&lean_world.world) != sim_hash_state(&rich_world.world));
}

// Item 5. A scripted duel that drives all three data-defined effects through
// resolve_effect for 3,000 ticks. Two identical runs must agree at every checkpoint
// and at the end.
static const uint32_t DUEL_TICKS = 3000u;
static const uint32_t DUEL_CHECKPOINT = 500u;
static const uint32_t DUEL_CHECKPOINTS = DUEL_TICKS / DUEL_CHECKPOINT;

struct DuelTrace {
    uint64_t checkpoints[DUEL_CHECKPOINTS];
    uint64_t final_hash;
    uint32_t projectile_spawns;
    uint32_t projectile_landings;
    uint32_t heals;
    uint32_t slow_applications;
    uint32_t failed_ticks;
    uint32_t resource_payments;
    uint32_t cooldown_arms;
};

// Pure function of (seed world, script): no wall clock, no RNG of its own, no
// branch on anything but the tick index.
static bool run_duel(HeroFixture* fixture, DuelTrace* trace) {
    SimWorld& world = fixture->world;
    HeroView hero{};
    HealthView hero_health{};
    HealthView target_health{};
    if (!hero_pool_get(&world.heroes, fixture->hero, &hero)) return false;
    if (!health_pool_get(&world.health, fixture->hero, &hero_health)) return false;
    if (!health_pool_get(&world.health, fixture->target, &target_health)) return false;

    uint32_t previous_projectiles = 0u;
    int32_t previous_hero_health = *hero_health.current;
    int32_t previous_target_health = *target_health.current;
    bool previously_slowed = status_pool_has(&world.statuses, fixture->target);

    for (uint32_t tick = 0u; tick < DUEL_TICKS; ++tick) {
        if (tick % 25u == 24u) {
            // Scripted top-up, identical in both runs: resource never regenerates
            // in v1 and the hero must stay wounded for the heal to be observable.
            *hero.resource = 50;
            *hero_health.current = 60;
            *target_health.current = 100;
            previous_hero_health = 60;
            previous_target_health = 100;
        }
        SimCommand command{};
        SimCommandBuffer buffer{&command, 1u};
        const SimCommandBuffer* drive = nullptr;
        switch (tick % 25u) {
            case 0u:  command = cast_command(0u, 0, mm::fix_from_int(1)); drive = &buffer; break;
            case 6u:  command = cast_command(0u, 1);                      drive = &buffer; break;
            // The area is centred on the target itself (radius 3, and the target
            // stands at x = 6), so the slow lands rather than falling short.
            case 12u: command = cast_command(0u, 2, mm::fix_from_int(6), 0); drive = &buffer; break;
            case 18u: command = attack_command(0u, 1);                    drive = &buffer; break;
            default: break;
        }
        const int32_t resource_before = *hero.resource;
        const uint32_t slot_cooldowns_before =
            hero.action_cooldown[0] + hero.action_cooldown[1] + hero.action_cooldown[2];

        if (!sim_tick(&world, drive)) ++trace->failed_ticks;

        if (*hero.resource < resource_before) ++trace->resource_payments;
        const uint32_t slot_cooldowns_after =
            hero.action_cooldown[0] + hero.action_cooldown[1] + hero.action_cooldown[2];
        if (slot_cooldowns_after > slot_cooldowns_before) ++trace->cooldown_arms;

        const uint32_t projectiles = world.projectiles.membership.count;
        if (projectiles > previous_projectiles) ++trace->projectile_spawns;
        previous_projectiles = projectiles;
        if (*target_health.current < previous_target_health - SIM_BASIC_ATTACK_MAGNITUDE)
            ++trace->projectile_landings;
        previous_target_health = *target_health.current;
        if (*hero_health.current > previous_hero_health) ++trace->heals;
        previous_hero_health = *hero_health.current;
        const bool slowed = status_pool_has(&world.statuses, fixture->target);
        if (slowed && !previously_slowed) ++trace->slow_applications;
        previously_slowed = slowed;

        if ((tick + 1u) % DUEL_CHECKPOINT == 0u)
            trace->checkpoints[(tick + 1u) / DUEL_CHECKPOINT - 1u] = sim_hash_state(&world);
    }
    trace->final_hash = sim_hash_state(&world);
    return true;
}

TEST(sim_hero_combat, two_identical_scripted_duels_hash_equal_at_every_checkpoint) {
    HeroFixture first{};
    HeroFixture second{};
    CHECK(build_fixture(&first, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(6)));
    CHECK(build_fixture(&second, g_hero_storage_b, sizeof(g_hero_storage_b), hero_config(),
                        mm::fix_from_int(6)));
    CHECK(sim_hash_state(&first.world) == sim_hash_state(&second.world));

    DuelTrace a{};
    DuelTrace b{};
    CHECK(run_duel(&first, &a));
    CHECK(run_duel(&second, &b));

    CHECK(a.failed_ticks == 0u);
    CHECK(b.failed_ticks == 0u);
    CHECK(first.world.tick == DUEL_TICKS);
    CHECK(second.world.tick == DUEL_TICKS);

    // All three data-defined effects actually ran, so the equality below is over a
    // duel and not over two idle worlds.
    CHECK(a.projectile_spawns > 0u);
    CHECK(a.projectile_landings > 0u);
    CHECK(a.heals > 0u);
    CHECK(a.slow_applications > 0u);
    // Cooldown and resource are integer ticks paid through the pipeline.
    CHECK(a.resource_payments > 0u);
    CHECK(a.cooldown_arms > 0u);
    CHECK(a.resource_payments == a.cooldown_arms);

    CHECK(a.projectile_spawns == b.projectile_spawns);
    CHECK(a.projectile_landings == b.projectile_landings);
    CHECK(a.heals == b.heals);
    CHECK(a.slow_applications == b.slow_applications);
    CHECK(a.resource_payments == b.resource_payments);
    CHECK(a.cooldown_arms == b.cooldown_arms);

    for (uint32_t i = 0u; i < DUEL_CHECKPOINTS; ++i) {
        CHECK(a.checkpoints[i] != 0u);
        CHECK(a.checkpoints[i] == b.checkpoints[i]);
    }
    CHECK(a.final_hash != 0u);
    CHECK(a.final_hash == b.final_hash);
    CHECK(a.final_hash == a.checkpoints[DUEL_CHECKPOINTS - 1u]);
    CHECK(sim_hash_state(&first.world) == sim_hash_state(&second.world));
    SimStateDiff diff{};
    CHECK(!sim_diff_state(&first.world, &second.world, &diff));
    CHECK(diff.field == SIM_STATE_FIELD_NONE);

    // Cooldown and resource are whole ticks and whole points: every action_cooldown
    // is a countdown of integers, never a fixed-point remainder.
    HeroView hero{};
    CHECK(hero_pool_get(&first.world.heroes, first.hero, &hero));
    for (uint16_t slot = 0u; slot < SIM_MAX_ACTION_SLOTS; ++slot)
        CHECK(hero.action_cooldown[slot] <= 9u);          // the largest def cooldown
}

// The intake seam for USE_ACTION. command_intake_run is the front door, so the
// verdicts it produces for an action command are part of acceptance.
TEST(sim_hero_combat, use_action_intake_verdicts_and_translation) {
    HeroFixture fixture{};
    CHECK(build_fixture(&fixture, g_hero_storage, sizeof(g_hero_storage), hero_config(),
                        mm::fix_from_int(2)));
    SimWorld& world = fixture.world;

    CommandIntakeConfig config{};
    config.tick = 0u;
    config.player_count = 1u;
    config.damage_capacity_remaining = world.config.damage_event_capacity;

    Command input{};
    input.tick = 0u;
    input.command_kind = COMMAND_KIND_USE_ACTION;
    input.actor = fixture.hero;

    Command accepted[4]{};
    CommandReject rejects[4]{};
    uint32_t accepted_count = 0u;
    uint32_t reject_count = 0u;

    // Unknown action_id: the def table is the only authority, so an action the def
    // does not carry is a shape failure, not a cooldown or range verdict.
    input.action_id = 0xDEADULL;
    input.target = fixture.target;
    CHECK(command_intake_run(&world, config, &input, 1u, 4u, accepted, &accepted_count,
                            rejects, &reject_count));
    CHECK(accepted_count == 0u);
    CHECK(reject_count == 1u);
    CHECK(rejects[0].reason == COMMAND_REJECT_MALFORMED);

    // A known action whose target_mode is ENTITY, aimed at a handle that is not
    // alive at that exact generation: stale-entity.
    EntityId stale{handle_make(handle_index(fixture.target.h),
                               static_cast<uint16_t>(handle_gen(fixture.target.h) + 1u))};
    input.action_id = 0xA1ULL;                        // slot 0, SIM_TARGET_ENTITY
    input.target = stale;
    CHECK(command_intake_run(&world, config, &input, 1u, 4u, accepted, &accepted_count,
                            rejects, &reject_count));
    CHECK(accepted_count == 0u);
    CHECK(reject_count == 1u);
    CHECK(rejects[0].reason == COMMAND_REJECT_STALE_ENTITY);

    // The same action at a live target is accepted and translates to CAST.
    input.target = fixture.target;
    CHECK(command_intake_run(&world, config, &input, 1u, 4u, accepted, &accepted_count,
                            rejects, &reject_count));
    CHECK(accepted_count == 1u);
    CHECK(reject_count == 0u);
    SimCommand translated{};
    CHECK(command_to_sim(&world, &accepted[0], &translated));
    CHECK(translated.kind == SIM_COMMAND_CAST);
    CHECK(translated.unit_index == 0u);
    CHECK(translated.amount == 0);                    // the def's slot, not the wire's
    CHECK(translated.value_x == mm::fix_from_int(1));
    CHECK(translated.value_y == 0);
    CHECK(sim_command_is_canonical(&translated, SIM_MAX_PLAYERS, SIM_MAX_UNITS));

    // A unit with no hero row has no action intent at all.
    input.actor = fixture.target;
    input.target = fixture.hero;
    CHECK(command_intake_run(&world, config, &input, 1u, 4u, accepted, &accepted_count,
                            rejects, &reject_count));
    CHECK(accepted_count == 0u);
    CHECK(reject_count == 1u);
    CHECK(rejects[0].reason == COMMAND_REJECT_MALFORMED);

    // And the whole batch translates as one, so the accepted buffer the tick sees
    // is the batch the intake produced.
    input.actor = fixture.hero;
    input.target = fixture.target;
    input.action_id = 0xA2ULL;                        // slot 1, SIM_TARGET_SELF
    input.point_x_q16 = mm::fix_from_int(7);
    input.point_y_q16 = mm::fix_from_int(-3);
    CHECK(command_intake_run(&world, config, &input, 1u, 4u, accepted, &accepted_count,
                            rejects, &reject_count));
    CHECK(accepted_count == 1u);
    SimCommand batch[4]{};
    CHECK(command_batch_to_sim(&world, accepted, accepted_count, batch) == accepted_count);
    CHECK(batch[0].kind == SIM_COMMAND_CAST);
    CHECK(batch[0].amount == 1);
    CHECK(batch[0].value_x == mm::fix_from_int(7));    // SELF reads the point through
    CHECK(batch[0].value_y == mm::fix_from_int(-3));
}
