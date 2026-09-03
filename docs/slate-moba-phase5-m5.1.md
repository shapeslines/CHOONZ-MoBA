# Phase 5 M5.1 Slate - Command Intake and Replay Parity (`m5-command-replay`)

**Status:** complete on the lane; PR open, owner merge gated (Actions billing lock)

**Branch:** `lane/moba-m5.1-command/20260903` (worktree `GITHUB-ROOT/_worktrees/CHOONZ-MoBA-m5.1-command`)

**Plan of record:** `docs/plans/m5.1-command-replay.md` (PR #65)

**Base:** `main` at `4f66af17e9b8ad65c9b5238426775f656d2811fe` (M4.1). Independent of the M5.0 lane
(PR #68): no shared files except one adjacent line each in `engine/sim/CMakeLists.txt` and
`tests/CMakeLists.txt`.

## Goal

Make player intent a contract: the proto-design §3.4 `Command` and `CommandReject` shapes; a
deterministic intake that validates (malformed, wrong tick, stale generation, unauthorized actor,
match over), orders by the canonical key `(tick, player_id, sequence, actor.index, command_kind)`,
rejects duplicate `(player_id, sequence)`, and enforces damage-event capacity at the source; and
proof that a live intake stream recorded through replay v1 replays bit-identically.

## Design decision recorded

The §3.4 key needs `tick`, `sequence`, and an `EntityId actor`, none of which fit the 16-byte legacy
`SimCommand` that replay v1 encodes. Carrying `Command` on the wire is replay v2 (own ADR; M6.0).
So M5.1 lands the intake **in front of** the existing `sim_tick` seam: accepted commands are
translated to `SimCommandBuffer` (`command_to_sim`), the legacy path keeps literal order and its
validator, replay v1 bytes are unchanged, and the oracle is unchanged (no `SIM_LOGIC_HASH` bump).
Ownership uses a documented placeholder partition of unit slots per player until `TeamDef` lands.

## Fence nuances recorded against the plan

- `engine/sim/CMakeLists.txt` gets `src/command.cpp`; `tests/CMakeLists.txt` gets the source line and
  `add_test(NAME sim_command …)`. `systems.cpp` is **not** touched (the plan allowed a call inside
  `sys_apply_commands`; the intake produces a buffer instead, which keeps the oracle path byte-identical).
- The test lives in `engine_tests` and carries its own recorder (the determinism recorder is `static`
  in a separate executable).

## Baseline evidence (S0)

- Untouched worktree at `4f66af1`, `cmake --preset ci` (`/WX`). Debug CTest 48/48.
- `sim_oracle_probe --sim-self-check`: `ticks=10000 commands=923 final=0x637628abff59c823
  stream=0x6f381609f7e59f0c logic=0xab96814425ba80a4` (pre-M5.0 values; this lane does not move them).

## Slice ledger

- [x] **S0** Baseline recorded above.
- [x] **S1** `command.h/.cpp`: `Command` (40 bytes, §3.4 field order), `CommandReject` (12 bytes),
  reason enum + strings, `command_key_compare`, placeholder ownership partition, actor→slot lookup.
- [x] **S2** `command_intake_run`: per-command validation in input order, branch-stable insertion sort
  by key, duplicate `(player_id, sequence)` rejection keeping the first in key order; pure (no world
  mutation, no RNG, no allocation).
- [x] **S3** Capacity at intake (`damage_capacity_remaining`, `SIM_MAX_COMMANDS_PER_TICK`) in key
  order; the seam's atomic over-capacity rejection re-proven with arena-byte equality.
- [x] **S4** Live/replay parity: 2,000 ticks of a hostile deterministic `Command` stream → intake →
  `command_batch_to_sim` → `sim_tick` recorded via `replay_write_tick`; replay via `replay_read_tick`
  matches every per-tick hash; reject logs of two live runs are byte-identical; every provoked reason
  class observed.
- [x] **S5** Gates: `/WX` Debug/RelWithDebInfo/Release, `debug-asan` (`-vcvars_ver=14.44`),
  clang-cl/UBSan, isolation lint, oracle unchanged; ROADMAP M5.2/M6.0 notes; ADR-0014 consequences
  line; JOURNAL; PR.

## Locked decisions

- `USE_ACTION` is rejected as `MALFORMED` until an action table exists (`m5-hero-combat`);
  `COOLDOWN` and `RANGE` are defined but not produced here.
- `BASIC_ATTACK` translates to the legacy `DAMAGE` command with a placeholder amount of 1 against the
  target's unit slot; `MOVE` translates to `SET_VELOCITY` with the point as the desired velocity until
  navigation lands (M5.x flow fields).
- `rejects[]` is reported in input order for pass-1 reasons, then duplicates, then capacity (each in
  key order), so a given stream always yields one byte pattern.

## Slice evidence

### S1–S4

- `engine/sim/include/sim/command.h` / `src/command.cpp`: `Command` 40 bytes (36 of fields, 8-aligned
  by `action_id`), `CommandReject` 12 bytes, 10 reasons with strings, `command_key_compare` (strict
  total order; generation excluded, index included), placeholder ownership partition
  (`SIM_MAX_UNITS / player_count`, last player takes the remainder), `command_actor_slot`,
  `command_intake_run` (three passes: validate in input order; branch-stable insertion sort + dedup
  keeping the first in key order; capacity in key order), `command_to_sim` / `command_batch_to_sim`.
- `tests/sim/command_tests.cpp` suite `sim_command`, 5 tests / 6,069 checks in Debug: every reason
  produced once with arena bytes and state hash unchanged; shuffled and reversed inputs give the same
  accepted order and byte-identical outputs; capacity rejected at intake and the seam's atomic reject
  re-proven; 2,000-tick hostile stream (2–6 commands per tick, duplicates, stale generations, wrong
  owner/tick/player, one-damage budget) recorded through replay v1 replays with every per-tick hash
  equal, two live runs give byte-identical replay bytes and reject logs, and all five provokable reason
  classes are observed.
- `engine/sim/CMakeLists.txt` + `tests/CMakeLists.txt` wired; `systems.cpp`, `sim.h`, `replay.*`,
  `sim_hash.*` untouched.

### S5 gates

- `/WX` matrix: Debug **49/49**, RelWithDebInfo **49/49**, Release **49/49**; `sim_oracle_probe
  --sim-self-check` prints the unchanged pre-M5.0 oracle in every config
  (`final=0x637628abff59c823 stream=0x6f381609f7e59f0c logic=0xab96814425ba80a4`).
- `debug-asan` (`vcvars64 -vcvars_ver=14.44`, `build-asan-1444`): **49/49**.
- `tools/check-clang-cl-determinism.ps1 -RequireCompiler -RequireUbsan`: `CLANG_CL_DETERMINISM=PASS:
  ubsan=on`, 6/6, oracle reproduced.
- Isolation: `check_sim_boundary`, compiler policy (+selftest), binary parity, isolation self-test,
  UBSan tripwire green in every configuration over `command.cpp`.
- No Vulkan smoke required (no renderer change). Hosted CI cannot run (billing lock).
