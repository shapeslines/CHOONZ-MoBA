#include "test.h"
#include "sim/command.h"
#include "sim/replay.h"
#include "sim/sim_hash.h"

#include <cstring>

// M5.1 command intake: shapes, ordering key, validation/reject reasons, dedup,
// backpressure at intake, purity, and live/replay parity through the legacy
// replay v1 seam. The oracle and replay bytes are untouched by this slice.

static const size_t COMMAND_WORLD_BYTES = 8192u;
static const uint32_t PLAYERS = 2u;
static const uint32_t UNITS = 4u;   // player 0 owns slots 0..31, player 1 owns 32..63 of 64;
                                    // with 4 initial units every live unit belongs to player 0.

typedef struct CommandFixture {
    alignas(16) uint8_t storage[COMMAND_WORLD_BYTES];
    Arena arena;
    SimWorld world;
} CommandFixture;

static bool fixture_init(CommandFixture* f, uint32_t event_capacity = 8u, uint32_t units = UNITS) {
    std::memset(f, 0, sizeof(*f));
    arena_init_fixed(&f->arena, f->storage, sizeof(f->storage));
    return sim_init(&f->world, &f->arena, 17u, SimWorldConfig{8u, units, event_capacity});
}

static CommandIntakeConfig config_for(uint32_t tick, uint32_t damage_remaining = 8u) {
    CommandIntakeConfig c{};
    c.tick = tick;
    c.player_count = static_cast<uint8_t>(PLAYERS);
    c.match_over = 0u;
    c.damage_capacity_remaining = damage_remaining;
    return c;
}

static Command move_cmd(uint32_t tick, uint8_t player, uint16_t sequence, EntityId actor,
                        mm::fix vx, mm::fix vy) {
    Command c{};
    c.tick = tick;
    c.player_id = player;
    c.command_kind = COMMAND_KIND_MOVE;
    c.sequence = sequence;
    c.actor = actor;
    c.point_x_q16 = vx;
    c.point_y_q16 = vy;
    return c;
}

static Command attack_cmd(uint32_t tick, uint8_t player, uint16_t sequence, EntityId actor,
                          EntityId target) {
    Command c{};
    c.tick = tick;
    c.player_id = player;
    c.command_kind = COMMAND_KIND_BASIC_ATTACK;
    c.sequence = sequence;
    c.actor = actor;
    c.target = target;
    return c;
}

typedef struct IntakeResult {
    Command accepted[SIM_MAX_COMMANDS_PER_TICK];
    uint32_t accepted_count;
    CommandReject rejects[SIM_MAX_COMMANDS_PER_TICK];
    uint32_t reject_count;
} IntakeResult;

static bool run_intake(const SimWorld* world, CommandIntakeConfig config, const Command* in,
                       uint32_t count, IntakeResult* out) {
    std::memset(out, 0, sizeof(*out));
    return command_intake_run(world, config, in, count, SIM_MAX_COMMANDS_PER_TICK,
                              out->accepted, &out->accepted_count, out->rejects, &out->reject_count);
}

// ---------------------------------------------------------------- S1 shapes + key

TEST(sim_command, shapes_and_key_are_a_strict_total_order) {
    static_assert(sizeof(Command) == 40u, "Command layout");
    static_assert(sizeof(CommandReject) == 12u, "CommandReject layout");
    Command a = move_cmd(5u, 0u, 1u, EntityId{handle_make(0u, 1u)}, 0, 0);
    Command b = a;
    CHECK(command_key_compare(&a, &b) == 0);
    b.tick = 6u;
    CHECK(command_key_compare(&a, &b) < 0 && command_key_compare(&b, &a) > 0);
    b = a; b.player_id = 1u;
    CHECK(command_key_compare(&a, &b) < 0);
    b = a; b.sequence = 2u;
    CHECK(command_key_compare(&a, &b) < 0);
    b = a; b.actor = EntityId{handle_make(3u, 1u)};
    CHECK(command_key_compare(&a, &b) < 0);
    b = a; b.command_kind = COMMAND_KIND_BASIC_ATTACK;
    CHECK(command_key_compare(&a, &b) < 0);
    // generation does not participate in the key (index does)
    b = a; b.actor = EntityId{handle_make(0u, 7u)};
    CHECK(command_key_compare(&a, &b) == 0);
    CHECK(std::strcmp(command_reject_reason_string(COMMAND_REJECT_DUPLICATE_SEQUENCE), "duplicate_sequence") == 0);
    CHECK(command_player_owns_unit(0u, 2u, 31u) && !command_player_owns_unit(0u, 2u, 32u));
    CHECK(command_player_owns_unit(1u, 2u, 63u) && !command_player_owns_unit(2u, 2u, 0u));
    CHECK(command_player_owns_unit(9u, 10u, 63u));   // last player takes the remainder
}

// ---------------------------------------------------------------- S2 validate + order + dedup

TEST(sim_command, every_reject_reason_is_produced_once_and_nothing_mutates) {
    CommandFixture f{};
    CHECK(fixture_init(&f));
    EntityId u0 = f.world.unit_entities[0];
    EntityId u1 = f.world.unit_entities[1];
    uint64_t before = sim_hash_state(&f.world);
    uint8_t snapshot[COMMAND_WORLD_BYTES];
    std::memcpy(snapshot, f.storage, sizeof(snapshot));

    Command in[8];
    uint32_t n = 0u;
    in[n++] = move_cmd(10u, 0u, 1u, u0, FIX_ONE, 0);                    // ok
    Command bad_kind = move_cmd(10u, 0u, 2u, u0, 0, 0); bad_kind.command_kind = 9u;
    in[n++] = bad_kind;                                                   // malformed
    in[n++] = move_cmd(11u, 0u, 3u, u0, 0, 0);                           // wrong tick
    in[n++] = move_cmd(10u, 0u, 4u, EntityId{handle_make(0u, 2u)}, 0, 0); // stale generation
    in[n++] = move_cmd(10u, 1u, 5u, u1, 0, 0);                           // player 1 does not own slot 1
    Command use = move_cmd(10u, 0u, 6u, u0, 0, 0); use.command_kind = COMMAND_KIND_USE_ACTION; use.action_id = 42u;
    in[n++] = use;                                                        // malformed (no action table yet)
    in[n++] = attack_cmd(10u, 0u, 7u, u0, EntityId{handle_make(5u, 1u)});// dead target -> stale
    in[n++] = move_cmd(10u, 0u, 1u, u1, 0, FIX_ONE);                     // duplicate (player 0, seq 1)

    IntakeResult r{};
    CHECK(run_intake(&f.world, config_for(10u), in, n, &r));
    CHECK(r.accepted_count == 1u && r.accepted[0].sequence == 1u && r.accepted[0].actor.h == u0.h);
    CHECK(r.reject_count == 7u);
    CHECK(r.rejects[0].reason == COMMAND_REJECT_MALFORMED && r.rejects[0].sequence == 2u);
    CHECK(r.rejects[1].reason == COMMAND_REJECT_WRONG_TICK);
    CHECK(r.rejects[2].reason == COMMAND_REJECT_STALE_ENTITY);
    CHECK(r.rejects[3].reason == COMMAND_REJECT_UNAUTHORIZED_ACTOR);
    CHECK(r.rejects[4].reason == COMMAND_REJECT_MALFORMED && r.rejects[4].sequence == 6u);
    CHECK(r.rejects[5].reason == COMMAND_REJECT_STALE_ENTITY && r.rejects[5].sequence == 7u);
    CHECK(r.rejects[6].reason == COMMAND_REJECT_DUPLICATE_SEQUENCE && r.rejects[6].actor.h == u1.h);

    CommandIntakeConfig over = config_for(10u);
    over.match_over = 1u;
    CHECK(run_intake(&f.world, over, in, 1u, &r));
    CHECK(r.accepted_count == 0u && r.reject_count == 1u && r.rejects[0].reason == COMMAND_REJECT_MATCH_OVER);

    CHECK(sim_hash_state(&f.world) == before);
    CHECK(std::memcmp(snapshot, f.storage, sizeof(snapshot)) == 0);
}

TEST(sim_command, accepted_commands_come_out_in_canonical_key_order_and_are_pure) {
    CommandFixture f{};
    CHECK(fixture_init(&f));
    EntityId u0 = f.world.unit_entities[0];
    EntityId u2 = f.world.unit_entities[2];
    // Shuffled input: sequences 4, 1, 3, 2 with mixed actors and kinds.
    Command in[4];
    in[0] = move_cmd(3u, 0u, 4u, u2, FIX_ONE, 0);
    in[1] = attack_cmd(3u, 0u, 1u, u0, u2);
    in[2] = move_cmd(3u, 0u, 3u, u0, 0, FIX_ONE);
    in[3] = move_cmd(3u, 0u, 2u, u2, -FIX_ONE, 0);
    IntakeResult a{}, b{};
    CHECK(run_intake(&f.world, config_for(3u), in, 4u, &a));
    CHECK(a.accepted_count == 4u && a.reject_count == 0u);
    CHECK(a.accepted[0].sequence == 1u && a.accepted[1].sequence == 2u &&
          a.accepted[2].sequence == 3u && a.accepted[3].sequence == 4u);
    // Same input again: byte-identical outputs (pure function).
    CHECK(run_intake(&f.world, config_for(3u), in, 4u, &b));
    CHECK(std::memcmp(a.accepted, b.accepted, sizeof(Command) * 4u) == 0);
    CHECK(a.reject_count == b.reject_count);
    // Reversed input gives the same accepted order.
    Command rev[4] = {in[3], in[2], in[1], in[0]};
    CHECK(run_intake(&f.world, config_for(3u), rev, 4u, &b));
    CHECK(std::memcmp(a.accepted, b.accepted, sizeof(Command) * 4u) == 0);
    // Translation into the legacy seam is canonical and validates.
    SimCommand sims[4];
    CHECK(command_batch_to_sim(&f.world, a.accepted, 4u, sims) == 4u);
    CHECK(sims[0].kind == SIM_COMMAND_DAMAGE && sims[0].unit_index == 2u && sims[0].amount == 1);
    CHECK(sims[1].kind == SIM_COMMAND_SET_VELOCITY && sims[1].unit_index == 2u && sims[1].value_x == -FIX_ONE);
    SimCommandBuffer buffer{sims, 4u};
    CHECK(sim_validate_commands(&f.world, &buffer));
    CHECK(sim_tick(&f.world, &buffer));
}

// ---------------------------------------------------------------- S3 backpressure

TEST(sim_command, damage_capacity_is_enforced_at_intake_and_at_the_seam) {
    CommandFixture f{};
    CHECK(fixture_init(&f, 1u));   // one damage event per tick
    EntityId u0 = f.world.unit_entities[0];
    EntityId u1 = f.world.unit_entities[1];
    EntityId u2 = f.world.unit_entities[2];
    Command in[3];
    in[0] = attack_cmd(0u, 0u, 2u, u1, u2);
    in[1] = attack_cmd(0u, 0u, 1u, u0, u2);
    in[2] = move_cmd(0u, 0u, 3u, u0, FIX_ONE, 0);
    IntakeResult r{};
    CHECK(run_intake(&f.world, config_for(0u, 1u), in, 3u, &r));
    // Key order keeps sequence 1's attack, rejects sequence 2's with CAPACITY, keeps the move.
    CHECK(r.accepted_count == 2u);
    CHECK(r.accepted[0].sequence == 1u && r.accepted[1].sequence == 3u);
    CHECK(r.reject_count == 1u && r.rejects[0].reason == COMMAND_REJECT_CAPACITY && r.rejects[0].sequence == 2u);

    SimCommand sims[2];
    CHECK(command_batch_to_sim(&f.world, r.accepted, 2u, sims) == 2u);
    SimCommandBuffer ok{sims, 2u};
    CHECK(sim_validate_commands(&f.world, &ok));

    // The seam itself still rejects an over-capacity buffer atomically (ADR-0014).
    SimCommand two_damage[2] = {sims[0], sims[0]};
    SimCommandBuffer over{two_damage, 2u};
    uint8_t snapshot[COMMAND_WORLD_BYTES];
    std::memcpy(snapshot, f.storage, sizeof(snapshot));
    SimWorld before = f.world;
    CHECK(!sim_validate_commands(&f.world, &over));
    CHECK(!sim_tick(&f.world, &over));
    CHECK(std::memcmp(&before, &f.world, sizeof(SimWorld)) == 0);
    CHECK(std::memcmp(snapshot, f.storage, sizeof(snapshot)) == 0);
    CHECK(sim_tick(&f.world, &ok));
}

// ---------------------------------------------------------------- S4 live/replay parity

static uint64_t mix64(uint64_t v) {
    v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 27; v *= 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}

// Deterministic hostile stream: moves and attacks with duplicates, stale actors,
// wrong owners, wrong ticks, and bursts that exceed a small damage budget.
static uint32_t generate_commands(uint64_t seed, uint32_t tick, const SimWorld* world, Command* out) {
    uint64_t bits = mix64(seed ^ (tick * 0x9e3779b97f4a7c15ULL + 1u));
    uint32_t n = 2u + static_cast<uint32_t>(bits % 5u);   // 2..6 per tick
    for (uint32_t i = 0u; i < n; ++i) {
        uint64_t b = mix64(bits + i);
        uint32_t slot = static_cast<uint32_t>(b % UNITS);
        EntityId actor = world->unit_entities[slot];
        uint8_t player = static_cast<uint8_t>((b >> 8) % 3u);            // 2 is malformed
        uint16_t seq = static_cast<uint16_t>((b >> 16) % 3u);             // duplicates likely
        uint32_t t = ((b >> 24) % 7u == 0u) ? tick + 1u : tick;           // some wrong-tick
        if ((b >> 32) % 5u == 0u) actor = EntityId{handle_make(slot, 3u)}; // stale generation
        if ((b >> 40) & 1u) {
            EntityId target = world->unit_entities[static_cast<uint32_t>((b >> 48) % UNITS)];
            out[i] = attack_cmd(t, player, seq, actor, target);
        } else {
            mm::fix vx = mm::fix_from_int(static_cast<int32_t>((b >> 48) % 5u) - 2);
            out[i] = move_cmd(t, player, seq, actor, vx, 0);
        }
    }
    return n;
}

static const uint32_t PARITY_TICKS = 2000u;
static const size_t PARITY_REPLAY_BYTES = REPLAY_HEADER_ENCODED_SIZE +
    static_cast<size_t>(PARITY_TICKS) * (REPLAY_TICK_BASE_ENCODED_SIZE + 6u * REPLAY_COMMAND_ENCODED_SIZE);
static uint8_t g_parity_replay[PARITY_REPLAY_BYTES];
static CommandReject g_parity_rejects_a[PARITY_TICKS * 6u];
static CommandReject g_parity_rejects_b[PARITY_TICKS * 6u];

static bool record_live(uint64_t seed, CommandReject* reject_log, uint32_t* reject_total,
                        uint64_t* out_final, size_t* out_size) {
    CommandFixture f{};
    if (!fixture_init(&f, 1u)) return false;   // one damage event per tick -> CAPACITY rejects
    ByteWriter writer;
    byte_writer_init(&writer, g_parity_replay, sizeof(g_parity_replay));
    ReplayHeader header = replay_header_make(seed, PLAYERS, PARITY_TICKS);
    if (replay_write_header(&writer, &header) != REPLAY_STATUS_OK) return false;
    *reject_total = 0u;
    for (uint32_t tick = 0u; tick < PARITY_TICKS; ++tick) {
        Command in[6];
        uint32_t n = generate_commands(seed, tick, &f.world, in);
        IntakeResult r{};
        CommandIntakeConfig config = config_for(tick, 1u);
        if (!run_intake(&f.world, config, in, n, &r)) return false;
        for (uint32_t i = 0u; i < r.reject_count; ++i) reject_log[(*reject_total)++] = r.rejects[i];
        SimCommand sims[6];
        if (command_batch_to_sim(&f.world, r.accepted, r.accepted_count, sims) != r.accepted_count) return false;
        SimCommandBuffer buffer{sims, r.accepted_count};
        if (!sim_tick(&f.world, &buffer)) return false;
        if (replay_write_tick(&writer, PLAYERS, &buffer, sim_hash_state(&f.world)) != REPLAY_STATUS_OK) return false;
    }
    *out_final = sim_hash_state(&f.world);
    *out_size = byte_writer_size(&writer);
    return true;
}

TEST(sim_command, live_intake_stream_replays_bit_identically_and_rejects_are_stable) {
    uint64_t final_a = 0u, final_b = 0u;
    size_t size_a = 0u, size_b = 0u;
    uint32_t rejects_a = 0u, rejects_b = 0u;
    CHECK(record_live(7u, g_parity_rejects_a, &rejects_a, &final_a, &size_a));
    uint8_t first_copy[PARITY_REPLAY_BYTES];
    std::memcpy(first_copy, g_parity_replay, size_a);
    CHECK(record_live(7u, g_parity_rejects_b, &rejects_b, &final_b, &size_b));
    // Intake is pure: identical reject logs and identical replay bytes across runs.
    CHECK(rejects_a > 0u && rejects_a == rejects_b);
    CHECK(std::memcmp(g_parity_rejects_a, g_parity_rejects_b, sizeof(CommandReject) * rejects_a) == 0);
    CHECK(size_a == size_b && final_a == final_b);
    CHECK(std::memcmp(first_copy, g_parity_replay, size_a) == 0);

    // Replay the recorded legacy stream and compare every per-tick hash (7.2 item 1).
    CommandFixture f{};
    CHECK(fixture_init(&f, 1u));
    ByteReader reader;
    byte_reader_init(&reader, g_parity_replay, size_a);
    ReplayHeader header{};
    CHECK(replay_read_header(&reader, &header) == REPLAY_STATUS_OK);
    CHECK(header.tick_count == PARITY_TICKS && header.sim_logic_hash == SIM_LOGIC_HASH);
    uint32_t accepted_total = 0u;
    for (uint32_t tick = 0u; tick < PARITY_TICKS; ++tick) {
        SimCommand commands[SIM_MAX_COMMANDS_PER_TICK];
        uint32_t count = 0u;
        uint64_t expected = 0u;
        CHECK(replay_read_tick(&reader, PLAYERS, commands, SIM_MAX_COMMANDS_PER_TICK, &count, &expected) == REPLAY_STATUS_OK);
        SimCommandBuffer buffer{commands, count};
        CHECK(sim_tick(&f.world, &buffer));
        CHECK(sim_hash_state(&f.world) == expected);
        accepted_total += count;
    }
    CHECK(replay_require_end(&reader) == REPLAY_STATUS_OK);
    CHECK(sim_hash_state(&f.world) == final_a);
    CHECK(accepted_total > 0u);
    // Every reject reason class that the generator can provoke appeared at least once.
    bool seen[16] = {};
    for (uint32_t i = 0u; i < rejects_a; ++i) seen[g_parity_rejects_a[i].reason & 15u] = true;
    CHECK(seen[COMMAND_REJECT_MALFORMED]);
    CHECK(seen[COMMAND_REJECT_WRONG_TICK]);
    CHECK(seen[COMMAND_REJECT_STALE_ENTITY]);
    CHECK(seen[COMMAND_REJECT_DUPLICATE_SEQUENCE]);
    CHECK(seen[COMMAND_REJECT_CAPACITY]);
}
