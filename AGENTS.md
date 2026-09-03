# AGENTS.md — CHOONZ-MoBA

Global Codex/Claude guidance applies. This file is the one-screen contract for any agent opening this
repo. Read it, then `docs/next-session.md`, then `docs/groundwork.md`. Everything else is a pointer.

## 1. System Affinity & Boundaries

- **Role:** the from-scratch C++17 / raw-Vulkan 1.3 deterministic MOBA engine. Off the Shapes//Lines
  data spine by design (no auth, no database, no GromDB). See `ARCHITECTURE.md`.
- **Owns:** `engine/` (one static lib per module), `tools/` (sandbox, cooker, replay, visualize, CI
  scripts), `tests/`, `assets/`, `cmake/`, `docs/`.
- **Delegates:** fleet law and shape lint → GromCodebase (`docs/decisions/0025-docs-surface-standard.md`,
  `tools/docs-surface-lint.py`, `docs/fleet/GAP-REGISTER.md`, `docs/fleet/FLEET-INDEX.md`); design
  authority (spec trilogy, `moba_engine_decision_index.json`, planning slices) → vault
  `$OIP_VAULT/20 Projects/moba/` (`Y:\GromBrain` on this machine); PM board → vault `_Projects.md`.
- **Invariants (fail-closed):**
  - `eng_sim` is Q16.16 fixed-point only, links `core + math + serialize` only, and never sees the
    platform, renderer, or floats (ADR-0002, ADR-0006; enforced by `tests/sim/check_sim_*.cmake`).
  - The renderer never sees `SimWorld`; `eng_game` is the sole fixed→float owner (ADR-0005, M3.3).
  - The 10,000-tick oracle is bit-identical in every configuration: final `0x637628abff59c823`,
    stream `0x6f381609f7e59f0c`, `SIM_LOGIC_HASH 0xab96814425ba80a4`; the controlled mutation reports
    exactly `tick=4321 field=position_x entity=7`. A slate must record any deliberate logic-hash bump.
    (PR #67 / M5.0 bumps these to final `0xff4e1ca0c779455b`, stream `0x218da333e6834496`,
    logic `0xcef8548df2b2a518`; the values above are pre-M5.0 until that lane merges.)
  - RNG is `pcg32` inside `SimWorld`, advanced unconditionally once per tick (ADR-0007, ADR-0013).
  - `.mba` v1 is the only baked container and is texture-only until a slate widens it (ADR-0015);
    loose `.spv` stays renderer-owned (ADR-0008).
  - No exceptions, no STL in engine code; `asset_parsers` is POD-C only (ADR-0009).
  - Worktrees live under `GITHUB-ROOT/_worktrees/`, never in-tree. `main` is ruleset-gated; land via PR.
  - Never commit secrets, `build*/`, `out/`, or `.advisor/`.

## 2. Deterministic Verification Matrix

The one command that must pass before any wrap (any shell; finds Visual Studio itself):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/local-ci.ps1 -Configuration Debug
```

Full acceptance for a code milestone adds: RelWithDebInfo + Release via the same script; the
`debug-asan` preset; `tools/check-clang-cl-determinism.ps1 -RequireCompiler -RequireUbsan`; and the
Vulkan smoke `build-ci\tools\sandbox\Debug\sandbox.exe --frames 90 --screenshot out\smoke.bmp`
(validation-clean; see `docs/testing-hardware.md`). Developer-shell equivalents: `cmake --preset ci`,
`cmake --build build-ci --config Debug`, `ctest --test-dir build-ci -C Debug --output-on-failure`.
Docs-only changes must keep `python <GromCodebase>/tools/docs-surface-lint.py --repo .` green.
Hosted Actions is billing-locked; `docs/ci-runner-handoff.md` (PR #66) is the fleet-runner handoff and
the interim local-green merge path. Fleet local-green instrument: `tools/local-green.manifest.json`.

## 3. Inference Allocation Matrix

- **Deep inference:** anything inside `engine/sim/` (determinism, ordering, hash contract), new
  serialized formats, renderer synchronization, and any change to a fence or gate in `docs/plans/`.
- **Reflex / low inference:** doc pointer upkeep, slate evidence tables, test registration in
  `tests/CMakeLists.txt`, running the matrix and pasting results.

## 4. Objects map (what the nouns are and where they live)

| Object | Lives in | Contract |
|---|---|---|
| `SimWorld`, `sim_tick`, schedule | `engine/sim/include/sim/sim.h`, `systems.h` | ADR-0001, ADR-0013; proto-design §4 |
| Entity handles (18 index + 14 generation) | `engine/sim/include/sim/entity.h`, `engine/core/include/core/handle.h` | ADR-0003 |
| SoA component pools, typed events | `engine/sim/include/sim/component_pool.h`, `events.h` | ADR-0014 |
| Canonical state hash / diff | `engine/sim/include/sim/sim_hash.h` | M3.0 slate |
| Replay v1 codec + `moba_replay` CLI | `engine/sim/include/sim/replay.h`, `tools/replay/` | M3.0 slate; proto-design §3.5 |
| Fixed-step cadence, interpolation alpha | `engine/platform/` (owner), `engine/game/` (snapshots, fixed→float, `DrawItem`) | ADR-0005, M3.3 |
| `AssetId`, asset registry, direct TGA/WAV | `engine/assets/`, `engine/asset_parsers/` | ADR-0010, M4.0 slate |
| `.mba` container, `asset_load`, cooker | `engine/asset_parsers/include/assets/mba.h`, `tools/cooker/`, CMake `content` target | ADR-0015, M4.1 slate |
| Renderer seam, null backend, shaders | `engine/render/`, `engine/render/shaders/` | ADR-0004, ADR-0008, ADR-0012 |
| `MapGrid`, `MapCell`, `LaneDef` (planned, M5.0) | `engine/sim/` map-owned files | proto-design §3.2; `docs/plans/m5.0-map-navigation.md` |
| `Command`, `CommandReject`, `SimEvent` (planned, M5.1) | `engine/sim/` command-owned files | proto-design §3.4; `docs/plans/m5.1-command-replay.md` |

## 5. Session protocol

- **START:** vault project note frontmatter (`20 Projects/moba/moba.md`: `status`, `next_action`,
  `gates`) → `docs/next-session.md` (trust its FIRST action only if
  `python <GromCodebase>/tools/next-session-audit.py --checkout .` says CURRENT; otherwise `git log`)
  → `docs/groundwork.md` Now / Up next → claim one row of `docs/WORK-FRONTLOG.md` and its plan under
  `docs/plans/`. Mailbox: inbox → status → posture → start (global rules).
- **DURING:** one milestone = one `docs/plans/<id>.md` (fence, gate, slice ledger) + one
  `docs/slate-moba-<phase>-<id>.md` (evidence) on a worktree branch `lane/moba-<id>/<yyyymmdd>`.
- **END:** `/wrap`. Replace (never append) `docs/next-session.md` in the ADR-0025 shape, keep it ≤40
  lines, run the lint, prepend a `docs/JOURNAL.md` session, patch the vault note's projection
  fields, post the mailbox wrap with commit + PR + next pickup.

## Canonical References

- Discovery: [FLEET-INDEX.md](https://github.com/shapeslines/GromCodebase/blob/main/docs/fleet/FLEET-INDEX.md)
- Resume pin: `docs/next-session.md` · Build status: `docs/groundwork.md` · Tickets: `docs/WORK-FRONTLOG.md`
- Sequence: `docs/ROADMAP.md` · Theory→repo bridge: `docs/plans/README.md` · Docs map: `docs/README.md`
- Decisions: `docs/decisions/README.md` (ADR-0001…0015) · Design contract: `docs/slate-moba-proto-design.md`
