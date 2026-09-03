# Development Journal

A running log of work sessions — what was built, what was decided, what was learned.
Newest session first. Architectural decisions live in `docs/DECISIONS/` (ADRs); this
is the narrative record. Session-level retrospectives (process, decision-making, and
cross-cutting nuance, one level up from per-milestone entries) live in
[`docs/sessions/`](sessions/).

---

## Session 15 — 2026-09-02 — M4.1 close-out and PM baseline

**Scope:** land the last two M4.1 DoD pieces, make the repo self-explaining for the next agent, and
translate the vault's theoretical roadmap into claimable repo plans.

**Outcome:** PR #63 squash-merged as `4f66af1` (CMake `content` target bakes `uv_test.tga`; sandbox
loads `uv_test.mba` through `asset_load`), closing M4.1. PR #64 (legacy-root rescue) closed as
superseded. Docs-only lane `lane/moba-pm-baseline/20260902` adds `CLAUDE.md` → `AGENTS.md` shim,
reshapes `AGENTS.md` (boundaries, verification matrix, objects map, session protocol), fills
`groundwork.md` and `WORK-FRONTLOG.md`, replaces the pin, adds `docs/README.md`,
`docs/custodian-queue.md`, `docs/plans/` (bridge table, M4.1 close-out, M5.0 map-navigation, M5.1
command-replay), the M5.0 ARC manifest, the M4.1 slate, and this entry. Vault `moba.md` and the
`_Projects.md` row were patched to the real state.

### What changed

- ROADMAP: M4.0/M4.1 marked complete; Phase 4 banner → "Phase 5 open, M4.2–M4.4 on demand"; M5.0
  carries an execution note that the bake path is `.mapdesc` + typed payloads, not the M4.2 parser.
- `docs/sessions/` retired in favour of slates + `session-archive/`.
- GAP-011 (vanished in-tree `MOBA-proto` clone) raised to GromCodebase via mailbox; marker in the
  custodian queue.

### Verification

- Canonical local CI on `4f66af1` and the docs-surface lint / next-session audit results are recorded
  in the lane PR body and `docs/receipts/20260902-pm-baseline.md`.

### Next

Claim `m5-map-navigation` per `docs/plans/m5.0-map-navigation.md` on
`lane/moba-m5.0-map/<yyyymmdd>`; M5.1 may run in parallel with disjoint files.

---

## Session 14 — 2026-08-27 — Phase 4 M4.1 cooker, `.mba` v1, and the design packet

**Scope:** the offline cooker and unified container (M4.1 core) plus the one-lane MOBA design packet.
Reconstructed 2026-09-02 from PR #57 and PR #60; no journal entry was written at the time.

**Outcome:** PR #57 (`codex/m4.1-cooker-restart`, accepted base `6571ee4`) moved TGA parsing,
`asset_id`, and a new `.mba` v1 codec into POD-C `engine/asset_parsers/`, added `tools/cooker/`
(brute-force TGA → `.mba`, `/EHsc` STL-private per ADR-0009), the unified `asset_load`, ADR-0015, and
the `mba` / `cooker_cli` tests. PR #60 (`lane/moba-proto-design/20260826`, docs-only) landed
`docs/slate-moba-proto-design.md` (schemas, 11-step tick, seven-slice DAG, nine acceptance tests),
its ARC manifest, and receipt. PR #58/#59 added the FLEET STATUS line and the portable local-CI
evidence gate; PR #61/#62 reshaped the pin under ADR-0025.

### What was left open

The `content` target and runtime consumption of a baked `.mba` (closed by PR #63 in Session 15);
the M4.1 slate and this entry.

---

## Session 13 — 2026-08-13 — Phase 4 M4.0 direct asset foundation

**Scope:** establish stable asset identity and lifetimes, promote the bootstrap TGA path into the
engine, add strict direct WAV loading, and route the sandbox texture through the asset registry
without starting the M4.1 cooker.

**Outcome:** implementation and local acceptance are complete on draft PR #52. `eng_assets` now
owns normalized relative-path FNV-1a/64 IDs, a transactional fixed-capacity arena-backed SoA
registry, deterministic generational handles, level bulk-unload, small global refcounts, bounded
direct TGA/WAV loads, and a Vulkan-free renderer callback. The existing renderer-owned loose SPIR-V
path is unchanged.

### What changed

- Canonical path normalization lowercases ASCII, normalizes separators, removes repeated and `.`
  segments, and rejects traversal, absolute/drive paths, controls, truncation, nonportable names,
  trailing-dot/space aliases, and Windows device stems. Stored canonical paths turn a runtime hash
  collision into a hard rejection instead of an alias.
- Persistent registry metadata, level payloads, global payloads, and I/O scratch use distinct
  caller-owned arenas. Every backing/control range must be pairwise disjoint, including the registry
  control; invalid/under-budget/overlapping initialization and malformed/capacity failures are
  mutation-free. Level unload invalidates every stale handle and rewinds in O(1).
- TGA type 2/type 10 true-color decoding covers 24/32-bit pixels and both vertical origins. WAV
  accepts a strict RIFF PCM subset with exact chunk padding, rates, alignment, and sample widths.
- Loose loads bind the root and every descendant without following reparses, reject multi-link files,
  and derive size and bytes from the same final handle under the loader budget. Scratch rewinds after
  every result and durable state commits only after complete decode.
- The sandbox no longer compiles its own TGA decoder or reads the texture file directly. It binds
  registry upload/destroy callbacks to the existing renderer seam and preserves GPU ownership order.
- `assets_boundary` permanently rejects renderer, Vulkan, sim, or direct sandbox decode coupling.

### Verification

- CI-preset `/WX` Debug, RelWithDebInfo, and Release pass 43/43 tests each after the final security
  corrections. Debug-ASan also passes 43/43; clang-cl 19.1.5 plus UBSan passes 6/6.
- Debug/Release retain the exact 10,000-tick oracle: 923 commands, final
  `0x637628abff59c823`, stream `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`.
- An RTX 4070 Ti Vulkan run completed 90 validation-clean frames through the final rooted-read seam
  with the asset-managed 64×64 texture, 64 objects, one scene draw, clean shutdown, and a visually
  inspected screenshot.
- Two independent focused security reviews pass the portable identity, pairwise arena ownership,
  rooted junction/hard-link rejection, and uniquely owned fixture-cleanup contracts.
- Fresh full review of checkpoint `82c4213` then found the loose-load regression still pre-deleted
  fixed working-directory filenames. Tests now share one identity-bound helper that atomically
  creates both its outer scope and working child through `NtCreateFile`, denies replacement for the
  full custody window, verifies the child file ID before handle disposition, and deletes the outer
  scope through its original creation handle. The asset fixture writes and removes only recorded
  files. A hostile rename regression, focused rereviews, and the complete Debug suite pass with no
  residue. A new exact head must earn the final hosted and full-review gates.
- GitHub CI/CodeQL passed all eight checks on implementation head `dc5e208`; the final documentation
  head must repeat that exact-head result before readiness.

### Next

Commit and push the reviewed security/evidence closure, obtain final exact-head acceptance, and
promote PR #52 only after hosted CI and CodeQL are green. Owner squash-merge authorization is
recorded; land only the recorded green head, verify the merged tree, and synchronize `main`. Then
open M4.1 as a separate cooker/`.mba` slate; do not stack it on this branch.

---

## Session 12 — 2026-08-13 — Interphase security consolidation

**Scope:** consolidate PR #27 and PRs #29–#46 from M3.4 `main`, repair their acceptance gaps, and
prove the hardened boundary without changing simulation behavior, replay encoding, asset APIs, or
starting Phase 4.
**Outcome:** repaired implementation and local acceptance are complete on draft PR #51 at `6716796`. Candidate
`a89caf6` passed its full local matrix and GitHub checks, but its required security review found three
real gaps, so it was not promoted. Focused repairs for cleanup object binding, real-renderer corrupt
state, and fail-closed CLI grammar passed the complete local matrix. Independent acceptance and
exact-head GitHub checks then passed, but fresh full security review found descendants still deleted through paths
after classification. The walker is now handle-bound for every node; the exact child-swap test and
focused security rereview pass.
The first committed-head fresh walk then passed configure/build/tests/Vulkan but failed safely during
cleanup on Git's read-only pack files. A superseded repair cleared only `READONLY` through the bound
handle; it successfully removed the preserved partial clone, and the permanent fixture covers a
read-only file plus a descendant junction with an unchanged outside sentinel.
That candidate's fresh full security audit subsequently found three deeper binding issues: cleanup
handles still shared writes during name enumeration, read-only deletion could alter an outside hard
link's shared attributes, and the create-only file-write temporary was closed before path-based
commit. Cleanup now excludes write/delete sharing, uses `FileDispositionInfoEx` with
`IGNORE_READONLY`, and attacks the exact interval with a real `FSCTL_SET_REPARSE_POINT` plus an
outside read-only hard link. Atomic writes retain the exclusive temporary through handle-based
rename/disposition and an exact post-flush replacement attack. On committed checkpoint `6716796`,
`/WX` Debug/RelWithDebInfo/Release pass 39/39 each, Debug-ASan passes 39/39, clang-cl/UBSan passes
6/6, direct Debug/Release determinism retains the exact oracle and tick-4321 diagnostic, and a fresh
92-target/39-test/90-frame walk completes secure cleanup. Focused security rereview also passes;
fresh full exact-head reviews and hosted checks remain.
Merge remains a separate owner action.

### What changed

- CI now supports manual recovery, has an explicit `contents: read` scope, pins all three checkout
  uses to the verified v5.0.1 commit, and selects the winget community source explicitly. Fresh-walk
  cleanup accepts only a non-root direct temp child and rejects roots, outside/nested paths, files,
  source-self targets, and reparse points before deletion.
- Array, arena, HashMap, Pool, and HandlePool guards cover allocation multiplication, capacity/address/
  alignment/end arithmetic, transactional initialization, and corrupt intrusive free lists while
  preserving their public signatures and normal allocation order.
- Platform atomic writes create their predictable `.tmp` without overwrite, retain that exclusive
  object through handle-based rename or failure disposition, and never clean it up by path; Vulkan loads only from
  System32 or a validated explicit SDK path; offline declarations and runtime shader paths fail
  closed on missing, outside, duplicate, or truncated inputs.
- Sandbox, replay, test-harness, visualizer, and classifier inputs have explicit canonical/bounded
  contracts and permanent no-side-effect fixtures. The classifier reads at most 4 MiB plus one probe
  byte and reports the corrected 15-case adversarial matrix.
- Debug draw and null-renderer submission capacity checks use ordered subtraction and guarded
  multiplication. Corrupt or over-capacity state is rejected before caller-item reads or count/vertex
  mutation.
- Fresh-walk final cleanup is object-bound: a non-delete-sharing handle stays open from lease
  validation through child removal, and the verified root receives its delete disposition through
  that handle. A post-validation test hook attempts the precise race and proves the move is blocked.
- Descendant cleanup is object-bound too: each child is opened without delete sharing and with
  `OPEN_REPARSE_POINT`, classified from that handle, retained through recursion, and deleted through
  the same handle. Reparse points are leaf links; an exact child-replacement attempt cannot reach its
  outside sentinel.
- Cleanup handles also deny write sharing so a validated empty directory cannot become a junction in
  place before enumeration. Read-only entries are removed using disposition's ignore-read-only flag,
  leaving attributes on outside hard links unchanged.
- Sandbox and replay command grammars now reject missing, duplicate, option-shaped, unknown, and
  extra arguments before DPI/window/Vulkan or replay file effects.

### Verification and loop findings

- `/WX` Debug, RelWithDebInfo, and Release builds pass; all 39 CTest entries pass in each
  configuration. Debug-ASan passes 39/39 and clang-cl 19.1.5 plus UBSan passes 6/6.
- The first ASan pass exposed a missing app-local runtime beside `visualize.exe` (`0xc0000135`). The
  target now uses the repository's existing staging helper, after which the complete suite passed.
- A guarded fresh clone of final implementation checkpoint `6ae70c9` configured, built 92 targets, passed 39/39, and
  completed `SANDBOX_SMOKE=PASS`. A retained RTX 4070 Ti run completed 90 validation-clean frames;
  its visually inspected 1280×720 screenshot is 2,764,854 bytes.
- The direct 10,000-tick proof still reports 923 commands, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, and logic `0xab96814425ba80a4`. Controlled mutation still first diverges at
  `tick=4321 field=position_x entity=7` across 178,690 checks.
- Every source path from #27/#29–#46 is present and semantically retained or strengthened. While
  final checks ran, PR #26 independently landed its gap-analysis report and advanced `main` to
  `dc380a1`; merge `227fc80` preserves that report unchanged. #47–#50 touch four explicitly excluded
  paths absent from the consolidation. The reconciled tree passes Debug 39/39 and `git diff --check`.

### Next

Publish the final evidence record, obtain fresh-context full security and acceptance verdicts on the
final recorded head, require exact-head
CI and CodeQL, and mark PR #51 ready only when all are green. The owner may then separately authorize
the squash merge and supersession closure of #27/#29–#46. PR #26 is already merged; retain all
branches/worktrees and open
M4.0 only from the resulting synchronized green `main`.

---

## Session 11 — 2026-08-13 — M3.4 structural determinism closure

**Scope:** centralize the mature Phase 3 simulation compiler contract, prove the same `eng_sim`
archive through test and runnable game paths, fail closed on dependency drift, and add the
clang-cl/UBSan stretch proof without changing authoritative behavior or starting Phase 4.
**Outcome:** M3.4 is acceptance-complete on PR #38; merge remains owner-gated. Replay v1, command
encoding, schedule, canonical state order, `SIM_LOGIC_HASH = 0xab96814425ba80a4`, and the final
oracle remain unchanged.

### What landed

- A named `moba_sim_determinism` interface policy owns the marker and `/fp:precise`; every current
  sim implementation source consumes it only through `eng_sim`.
- Configure-time target introspection and generated compile-command audits enforce implementation
  ownership, allowed direct/exported links, include roots, configuration coverage, policy marker,
  and non-conflicting FP flags. Source scans retain the float/libm/clock/heap/unordered/platform/
  render/game bans. Adversarial fixtures cover path traversal, system-include injection, duplicate or
  conflicting flags, forbidden links/imports, and accidental recompilation.
- `sim_oracle_run` reuses the placeholder replay generator and simulation object code from caller-
  owned fixed storage. A direct test probe plus Vulkan and null sandbox `--sim-self-check` entrypoints
  compare one exact hash-stream line before window or renderer initialization.
- The optional clang-cl gate performs a compile/link/runtime capability probe, deliberately trips
  UBSan on signed overflow, then runs the clean instrumented oracle. MSVC-only `/Zc:preprocessor`
  stays on cl.exe; the UBSan-only RelWithDebInfo tree selects the static CRT required by the Windows
  standalone runtime. Normal MSVC ABI and flags are unchanged.
- Both sandbox variants now stage the Debug-ASan runtime beside their shared output directory, so
  cross-binary parity executes inside the sanitizer suite instead of failing DLL discovery.

### Verification and loop findings

- Full MSVC `ci` `/WX` builds passed 32/32 CTest entries in Debug, RelWithDebInfo, and Release.
  Debug-ASan also passed 32/32 after its first run correctly exposed the missing sandbox runtime
  (`0xc0000135`).
- Local clang-cl 19.1.5 + UBSan passed the overflow tripwire, the instrumented RelWithDebInfo oracle,
  and four structural gates (6/6). The hosted clang-cl capability job passed as well.
- All three binary paths report `ticks=10000 commands=923 final=0x637628abff59c823
  stream=0x6f381609f7e59f0c logic=0xab96814425ba80a4`; controlled mutation still reports exactly
  `tick=4321 field=position_x entity=7`.
- A local RTX 4070 Ti run completed 90 validation-clean frames and wrote a 2,764,854-byte 1280×720
  BMP. A fresh clone of checkpoint `8dcd385` independently configured, built, passed 32/32, and
  repeated the same strict screenshot contract. Hosted CI run `31675675948` and CodeQL run
  `31675672904` are green on that checkpoint.
- Two early stretch attempts failed for useful reasons: clang-cl rejects `/Zc:preprocessor` as
  unused under `/WX`; Debug/`/MD` cannot link the static-release Windows UBSan C++ runtime. The final
  RelWithDebInfo `/MT` stretch posture follows observed toolchain capability rather than masking
  either failure.

### Next

Owner squash-merges exact green PR #38, synchronizes `main`, and verifies post-merge CI/CodeQL. Then
open Phase 4 M4.0 as a separate asset-pipeline slate; keep the M3.4 hash stream and isolation checks
as permanent regression gates.

---

## Session 10 — 2026-08-13 — M3.3 acceptance repair and presentation boundary

**Scope:** land gap-close PRs #21–#24 in order, repair the hosted Vulkan gate, and close M3.3 without
starting M3.4. Commands remain same-tick; the runtime loop is shaped for a future per-tick input/net
source by generating one fresh buffer for every owed tick.
**Outcome:** M3.3 is complete. PRs #21–#24 reached `main` in order. PRs #23/#24 were externally
merged before their acceptance corrections landed, so history was preserved and corrective PR #28
carried the complete non-rewriting repair. Its accepted exact head `8cc42c1` squash-merged as
`b03f545`; the complete post-merge CI and CodeQL gates are green.

### What landed

- The gap stack landed as PR #21 → `8defa10`, #22 → `4d250b2`, #23 → `ab774ed`, and #24 →
  `ca5ad22`. PR #20 and recovery #25 are superseded by #21. Remote wave branches are retained.
- The Vulkan smoke classifier now skips only exact “no physical device/compatible driver” or
  ADR-0012 minimum-spec rejection logs. Any other initialization, validation, exit, or missing-image
  outcome fails and prints the sandbox log. CI and the fresh-walk script invoke one shared classifier.
  A first independent acceptance pass caught that matching the phrase before the exit status could
  mask an unrelated failure; the repaired classifier checks exit and failure diagnostics first and
  has 14 adversarial CTest cases, including Vulkan validation WARN and ERROR records.
- `PlatformFixedStep` is window-independent platform code: initialize, clamp accumulated frame debt,
  query/consume whole 30 Hz ticks, and calculate alpha. It has no game or sim dependency.
- The sandbox generates orbit commands inside each owed-tick iteration and consumes debt only after
  `sim_tick` and post-tick snapshot capture succeed. Minimized rendering does not pause simulation.
- `eng_game` owns two arena-backed fixed snapshots, const extraction over all 64 stable unit slots,
  actual live counts, previous→current interpolation, identity-change suppression, and the sole
  fixed→float conversion. Presentation no longer owns clocks or an accumulator.
- clang-cl’s dynamic ASan runtime is staged beside CTest/replay executables, closing the Windows
  environment ambiguity where CTest could drop the compiler bin from a duplicated `Path`/`PATH`.

### Verification

- MSVC `ci` preset (`/WX`): complete Debug, RelWithDebInfo, and Release builds; **28/28 CTest** in
  each configuration. Debug-ASan also passes **28/28**.
- The unchanged 10,000-tick oracle ends at `0x637628abff59c823`; controlled divergence remains
  `tick=4321 field=position_x entity=7`; replay v1 and logic hash `0xab96814425ba80a4` are unchanged.
- Focused cadence/presentation tests cover 60 Hz, 30 Hz, mixed grouping, catch-up clamping,
  minimized rendering, two distinct command generations in a two-tick frame, retained debt on tick
  failure, under-budget initialization atomicity, alpha 0/midpoint/near-1, destruction, and reuse.
- `sim_boundary` and `present_boundary` prove `eng_sim → core + math + serialize`,
  `eng_game → core + math + sim + render_common`, no renderer→sim dependency, and no
  platform-cadence→game/sim dependency.
- A fresh clone of the final corrective tree completed configure, build, all 28 Debug tests, and a
  90-frame validation-clean sandbox run. RTX 4070 Ti evidence produced a 2,764,854-byte 1280×720
  screenshot with 64 objects, one batch, one scene draw, three total draws, and 12 allocations.
- Fresh-context read-only acceptance returned PASS on exact PR #28 head `8cc42c1`. Its Debug,
  RelWithDebInfo, Release, fresh-walk, CodeQL C/C++, and workflow-analysis checks passed; after the
  squash merge, CI run `31671492172` and CodeQL run `31671491991` passed on `b03f545`.

### Next

Open [`slate-moba-phase3-m3.4.md`](slate-moba-phase3-m3.4.md) separately for centralized sim flags,
game/test binary parity, stronger isolation linting, and the clang-cl/UBSan stretch gate. Phase 4
remains blocked until M3.4 closes.

---

## Session 09 — 2026-08-12 — M3.2: deterministic systems and ordered schedule

**Scope:** move the M3.1 ECS through explicit ordered systems and typed events without beginning the
platform accumulator, presentation snapshot, or gameplay layers.
**Outcome:** M3.2 is implementation-complete on PR #17. The simulation now has derived ascending-
entity views, a phase-buffered `DamageEvent` seam, and one literal plain-function tick schedule.

### What landed

- Each `ComponentPool` owns an arena-backed ordered `EntityId` cache. Successful membership changes
  invalidate it; a validated ascending sparse-index scan rebuilds it atomically. Cache state and dense
  history are not hash authority.
- The arena-backed two-buffer `DamageEvent` queue has explicit append, publish, read, and consume
  phases. Invalid events, overflow, and unread-phase publication are mutation-free.
- `sys_apply_commands`, `sys_movement`, `sys_combat_resolve`, `sys_cooldown_tick`,
  `sys_rng_advance`, and `sys_flush_destroy` are plain prefixed free functions. `sim_tick` calls them
  in one source-visible schedule with no registry, query DSL, dependency graph, or virtual dispatch.
- Command damage publishes and resolves in the same tick to retain M3.1 behavior. The phase API
  scaffolds a later next-tick experiment by moving only publication; stale-target policy and the
  extra combat latency remain the explicit tradeoffs to measure.
- Replay format remains v1 with 16-byte commands. Logic hash deliberately moves to
  `0xab96814425ba80a4`; canonical state covers logical event phases and excludes physical buffers,
  query caches, pointers, padding, unused capacity, and dense order.

### Verification

- MSVC `ci` preset (`/WX`): complete Debug and Release builds clean.
- **25/25 CTest entries green** in both configurations, adding event, system, and schedule suites.
- Debug and Release independently end the 10,000-tick stream at `0x637628abff59c823`.
- Controlled mutation remains `tick=4321 field=position_x entity=7`.
- Replay record→inspect→verify remains byte-exact; exact M3.1 logic-hash files reject with exit 2.
- `sim_boundary` scans 17 sim headers/sources and the link seam remains core + math + serialize.

### Next

PR #17 is independently accepted, ready, mergeable, and green under GitHub CI/CodeQL. Owner-review
and merge it as the remaining gate. Then branch M3.3 from updated `main` and execute
[`slate-moba-phase3-m3.3.md`](slate-moba-phase3-m3.3.md). Do not fold M3.3 platform/presentation
work into this branch.

---

## Session 08 — 2026-08-12 — M3.1: arena entity model and sparse-set SoA

**Scope:** replace the M3.0 placeholder arrays with deterministic arena-backed identity and typed
component storage without beginning M3.2 scheduling.
**Outcome:** M3.1 is complete and independently accepted on PR #15. Replay-v1 unit-slot commands remain stable,
while `SimWorld` now owns a runtime-sized entity manager, typed sparse-set pools, deferred
destruction, and a canonical semantic ECS hash. M3.2 is queued on a separate slate.

### What landed

- `SimWorldConfig` defaults to 16,384 entities and 64 initial replay units; exact memory sizing and
  staged initialization make invalid and under-budget attempts atomic.
- The dedicated `EntityManager` preserves all 14 generation bits, tracks liveness separately,
  allocates fresh slots ascending, recycles LIFO, and rejects exhausted, stale, forged, and double
  releases without mutation.
- `ComponentPool` owns sparse↔dense membership; Transform, Velocity, and Health own arena-backed SoA
  values and repair every lane after swap-removal. Stable 64-slot replay mappings remain the command
  seam.
- Deferred destruction commits at the tick boundary in request order, removes Health→Velocity→
  Transform, clears mappings, releases identity, and preserves stale-handle safety after reuse.
- Replay format remains `1` with 16-byte commands. The deliberate M3.1 logic hash is
  `0x7902599e173f87a6`; M3.0 recordings are intentionally incompatible.
- Canonical FNV-1a/64 now covers explicit little-endian config, RNG, lifecycle, free-stack, mapping,
  queue, membership, and component fields in semantic entity-index order. Pointers, padding, unused
  capacity, allocator state, and sparse/dense storage order are excluded.

### Verification

- MSVC `ci` preset (`/WX`): complete Debug and Release builds clean.
- **22/22 CTest entries green** in both configurations, including entity lifecycle, sparse-set
  churn, typed values, deferred destruction, canonical hash/diff, replay CLI, and boundary suites.
- Debug and Release independently end the 10,000-tick stream at `0x981212877a575730`.
- Controlled mutation: `tick=4321 field=position_x entity=7` in both configurations.
- Replay record→inspect→verify remains byte-exact; the exact M3.0 logic hash is rejected, corrupt or
  incompatible files return exit 2, divergence 3, and usage/I/O 1.
- `sim_boundary` scans all 13 sim headers/sources; `eng_sim` still links directly only to core,
  math, and serialize.

### Next

Owner-review and merge PR #15; local and GitHub gates plus independent acceptance are green, while
merge remains separately owner-approved. After it lands, branch M3.2 from updated `main` and execute
[`slate-moba-phase3-m3.2.md`](slate-moba-phase3-m3.2.md). Do not stack scheduling/event work on the
M3.1 branch.

---

## Session 07 — 2026-08-12 — M3.0: deterministic simulation oracle and replay CLI

**Scope:** build the Phase 3 desync detector before starting the real ECS.
**Outcome:** M3.0 is complete on PR #14. A fixed-capacity, platform-free Q16.16 simulation now has
one canonical hash/diff order, a version-locked replay container, and a runnable recorder,
inspector, and verifier. M3.1 remains unstarted and is queued on its own slate.

### What landed

- `core/sim_config.h` owns the 30 Hz cadence and accumulator constants; sim derives the Q16.16
  timestep where core and math first meet.
- `eng_serialize` provides bounded, sticky, allocation-free little-endian reads/writes for signed
  and unsigned 8/16/32/64-bit values.
- `eng_sim` owns the 64-slot placeholder `SimWorld`, PCG32 state, canonical command validation,
  fixed-order tick, FNV-1a/64 state hash, and first-field comparator.
- Replay format `1` uses `MOBARPLY` and logic hash `0xf1b4e2b29b1e9643`; each tick records the
  expected post-tick hash and canonical commands. `moba_replay record|inspect|verify` persists via
  the atomic platform file seam with exit classes for I/O, invalid input, and divergence.
- A dedicated test target records/replays 10,000 ticks and a CTest boundary scan rejects float,
  libm, wall-clock, platform/render/Vulkan imports, and unordered containers under `engine/sim`.

### Verification

- MSVC `ci` preset with `/WX`: full Debug and Release builds clean.
- **18/18 CTest entries green** in both configurations.
- The independent replay matches every recorded post-tick hash; final hash
  `0xb85d4b632571948c`.
- Controlled mutation: `tick=4321 field=position_x unit=7` in Debug and Release, with 47,301
  determinism checks per configuration.
- Replay CLI fixture: 10,000 ticks, 923 commands, 134,812 bytes; record, inspect, and verify pass.
  Corrupt/incompatible variants return exit 2, divergence returns 3, and usage/I/O returns 1.
- `sim_boundary`: seven sim headers/sources clean; direct link seam is core + math + serialize.

### Next

Review and merge PR #14 after GitHub CI/CodeQL are green. Then create
`moba/slate-phase3-ecs` from updated `main` and execute the queued M3.1 slate. The M3.0 replay hash
stream is the regression oracle; do not fold M3.2 scheduling or M3.3 presentation work into M3.1.

---

## Session 06 — 2026-08-12 — M2.3–M2.5: instanced field, debug workhorse, real renderer seam

**Scope:** finish the Phase 2 pickup slate from camera UBO through the renderer seam audit.
**Outcome:** Phase 2 is complete. A 20×25 programmatic cube field renders
depth-correctly as one scene batch/draw; immediate world lines and 5×7 stroke text render last; the
sandbox F1 overlay reports FPS, frame time, arena bytes, live device allocations, and draw calls; and
Vulkan/null backends implement the same typed-handle frame contract. The owner interaction gate
passed with explicit resize, minimize/restore, focus loss/return, and F1 overlay transitions.

### What landed

- `set=0` now holds a per-frame camera UBO and storage-buffer instance stream. Public `DrawItem`s sort
  deterministically by pipeline/material/mesh, models pack into the stream, and a 4-byte
  `instance_base` push constant selects each coalesced batch.
- One depth image per swapchain image, cleared every frame; mesh pipelines use `LESS` with depth
  test/write. The scripted sandbox reports `objects=500 batches=1 scene_draws=1`.
- `dbg_line/sphere/aabb/text_2d` allocate one fixed vertex block from the current frame arena. World
  lines depth-test without writing depth; the pixel-space overlay bypasses depth. F1 uses rising-edge
  input in the sandbox.
- The provisional single-texture/draw API is gone. `renderer_types.h` is the Vulkan-free authority for
  typed descriptors, `FrameView`, `DrawItem`, and stats. Destroy calls retire handles for at least two
  frames before releasing GPU objects.
- `eng_render_common`, `eng_render`, and always-built `eng_render_null` make the seam testable on a
  Vulkan machine. `sandbox_null` runs a blank window with valid handles; a dedicated headless suite
  pins stale-handle rejection and no-draw stats.

### Verification

- `ci` preset, MSVC `/WX`: clean.
- **9/9 CTest suites green**, including batching/debug-frame-arena/null-backend coverage.
- Validation-layer orbit/readback run: zero warnings/errors; screenshot shows the cube field, debug
  geometry, and overlay. Stats: 500 objects, one batch, one scene draw, three total draws, 12 live
  dedicated allocations.
- Owner S6 run (76.00s): event log captured resize, minimize/restore, alt-tab focus loss/return, and
  F1 overlay off/on; validation enabled on the RTX 4070 Ti; zero validation warnings/errors; clean
  exit with the same 500-object/one-batch/three-draw/12-allocation stats.
- Seam grep: no Vulkan identifiers/includes in the sandbox, tests, or public renderer headers.

### Next

Review and squash-merge PR #13, then create the queued M3.0 determinism branch from updated `main`.
Vulkan-in-hosted-CI remains separately owner-gated.

---

## Session 05 — 2026-06-11 — M2.2: the textured quad (first real GPU memory traffic)

**Scope:** M2.2 — vertex/index buffers, an image upload, a sampler, and a descriptor
set: the first time the engine moves real data to the GPU. Plus the two enablers the
rung needs — `vk_alloc` Phase 1 (the naive dedicated allocator) and an app-layer direct
TGA loader (the asset manager doesn't exist until Phase 4).
**Outcome:** **M2.2 complete** in one squash (**PR #10**, `c62bb3f`): a textured quad
renders over the M2.1 triangle, **validation-clean**, with pixel-exact UV orientation
and a bit-exact sRGB round-trip — both confirmed by the engine's own readback
screenshot. A multi-lens adversarial review confirmed **5 findings (all fixed
pre-merge)**, including a major one in this session's *own* hardening.

### What was built (PR #10)

| Piece | Detail |
|---|---|
| `vk_alloc` Phase 1 | One dedicated `VkDeviceMemory` per resource (`alloc_buffer`/`alloc_image_2d`/`free_*`/`find_memory_type`); live count hard-`ENSURE`d under 3500 (the 4096 `maxMemoryAllocationCount` floor). The M2.1 capture buffer refactored onto it. Buffers/images never share memory → no `bufferImageGranularity`; `HOST_COHERENT` staging → no flush math. |
| One-shot submits | `begin`/`end_one_shot`: transient cmd buffer + fence wait for startup uploads. Later-frame visibility comes from the `barrier2`s recorded *inside* the one-shot, not the fence. |
| Quad VB/IB | `DEVICE_LOCAL` via one staging buffer (verts then indices) + `CmdCopyBuffer`×2 + buffer `barrier2`s into `VERTEX_ATTRIBUTE_INPUT`/`INDEX_INPUT`. |
| `renderer_upload_texture` | `R8G8B8A8_SRGB` `OPTIMAL` image, staging copy, `barrier2`s `UNDEFINED→TRANSFER_DST→SHADER_READ_ONLY`, view, descriptor write. Replace-path device-idles first. **Provisional** single-texture seam → M2.5 unifies into typed handles. |
| Descriptors | `set=0` empty placeholder + `set=1` combined image sampler (material at set 1 per the roadmap; set 0 reserved so M2.3's per-frame UBO slots in **without renumbering**); one pool/set, one linear sampler. Quad binds only set 1. |
| Pipeline registry | Grows to 2 entries with per-entry vertex layout (`NONE` vs `POS2_UV2`) + layout kind (`EMPTY` vs `MATERIAL`); `quad.vert`/`quad.frag`. |
| TGA decoder | App-layer `tools/sandbox/src/tga_direct`: type-2 uncompressed 24/32 bpp, both origins → RGBA8 via a caller `Allocator`; hard-rejects RLE/palettes/16 bpp/right-origin/interleave + truncation (u64-bounded, *before* allocation). `assets/uv_test.tga` (64×64 UV checker, R/G/B/Y corner markers) loaded + uploaded. New headless `tga` suite. |
| Dispatch | +21 procs (images, samplers, descriptors, copies, indexed draw). |

### The adversarial review (5 lenses → 2 skeptics/finding)

The first run was **crippled by a session-limit cutoff**: the three Vulkan/seam lenses
(upload-sync, allocator, seam-arch) never ran and their would-be findings defaulted to
"refuted", so the run reported a misleading *0 confirmed / 8 refuted*. **Resuming** the
same workflow (cached lenses return instantly; the dead ones run live) produced the real
result. Lesson: **a review truncated by the session limit is not a clean review** — its
"refuted" tallies are an artifact, not a verdict; resume before trusting them.

**Confirmed & fixed (5):**

1. **Texture arena budget broken (major) — a bug in this session's own M2.1-style
   hardening.** The sandbox bounded the *file* at 32 MiB before reading it into a 64 MiB
   arena — but the raw file **and** its decoded RGBA8 share that arena, and 24 bpp
   (3 B/px) decodes to 4 B/px, so worst-case live bytes ≈ 7/3 × file. A valid 24 bpp TGA
   between ~27 and 32 MiB passed the gate then **hard-aborted the arena** — the exact bug
   class M2.1 fixed for the pipeline cache, re-introduced by a gate that only *looked*
   like the M2.1 one. Three lenses caught it independently. Fixed by deriving the file
   cap from the arena size (3/8 < the 3/7 worst case) so the invariant holds by
   construction.
2. **(minor)** `renderer_upload_texture` now rejects dims above `maxImageDimension2D`
   (a `vkCreateImage` valid-usage violation / UB) — the app can't query the limit across
   the seam, so the check lives in the renderer.
3. **(minor)** Interleave descriptor bits (`0xC0`) now hard-rejected by the decoder.
4. **(minor)** Added a 3×2 24 bpp bottom-left decode test: source stride `width*3` is
   not a multiple of 4, pinning the tight-pack path a `width*4` regression would slip
   through. A skeptic *proved* the gap by mutation — the bad stride passed the old tests
   in Release (only Debug's `/RTC1` stack-fill caught it, incidentally).
5. **(minor)** Doc fix: M4.0 is the runtime parser; the cooker is M4.1.

**Refuted** included a 32-bit `size_t` overflow (x64-unreachable), a TOCTOU file-grow
window (theoretical for a dev tool reading a committed asset), and the null-backend
"silent false" path.

### Key decisions & gotchas

- **A fixed-arena size bound must cover every allocation that lands in it, not just the
  first.** Bounding the input file is necessary but not sufficient when a later decode
  expands it in the same arena. Derive coupled budget constants from one source so they
  can't drift (the reviewers' standing complaint, now applied).
- **`set=0` left deliberately empty.** Reserving the per-frame-UBO slot now means M2.3
  adds it without recreating the material pipeline layout or renumbering `set=1`.
- **One-shot upload visibility is device-side, via barriers — the fence is host-side.**
  `end_one_shot`'s `WaitForFences` only tells the *host* the copy finished; later frame
  submissions see the data because the `barrier2` into `VERTEX_ATTRIBUTE_INPUT` /
  `SHADER_READ_ONLY` was recorded inside the one-shot.
- **TGA is BGR(A) and bottom-left by default.** The decoder swizzles to RGBA and flips
  rows to top-down; the uploaded image is `*_SRGB`, so `texture()` returns linear and the
  `*_SRGB` swapchain re-encodes on write — no manual gamma anywhere.
- **CRT `/RTC1` can mask a missing test.** A stride regression that reads one byte past
  written data was caught in Debug only because the runtime checks fill the stack with
  `0xCC`; Release sailed through. Tests that depend on uninitialized-read *values* are
  not real coverage — pin the packing explicitly.

### Verification

- `/WX` clean on Vulkan + null backends; **8/8 ctest suites green** (new: `tga`).
- **Zero validation messages** over 60-frame runs, validation ON.
- DoD: quad renders with correct UVs (pixel-exact R/G/B/Y markers via readback) and
  bit-exact sRGB round-trip (texture 255/240/32 land identically in the capture); staging
  round-trip validated; **3 dedicated allocations live** after upload (VB, IB, texture;
  staging freed). 20 MB-class robustness now lives in the derived cap.

### Deferred backlog (carried forward)

- **Interactive M2.0/M2.1/M2.2 DoD:** zero validation errors across a real resize,
  minimize/restore, alt-tab (needs a display; paths implemented).
- Vulkan SDK in CI → `find_package(Vulkan REQUIRED)`, keep the null backend as the
  deliberate M2.5 seam-audit backend.
- M2.5 will unify `renderer_upload_texture` (+ mesh creation) into typed handles with
  deferred-destroy; the provisional single-texture seam folds in there.
- Split `plat_mem_*` out of the Win32 window TU; clang-cl/UBSan determinism run.

### Where we are / next

**Phase 2: M2.0–M2.2 done.** The engine clears, draws a pipeline triangle, and now
moves real geometry + a texture to the GPU through staging, with a working (if naive)
allocator and descriptor set. **Next: M2.3 — camera UBO + instanced meshes** (the first
"MOBA-looking" frame: a per-frame view/proj UBO at the reserved `set=0`, a per-instance
buffer, and 500+ instances of one mesh in a single batched `vkCmdDrawIndexed`).

---

## Session 04 — 2026-06-11 — M2.1: the first triangle

**Scope:** M2.1 — the first graphics pipeline: offline SPIR-V (ADR-0008), an on-disk
pipeline cache, dynamic rendering + synchronization2 as the committed minimum spec
(new ADR-0012), plus the two enablers the rung needed — the platform **file I/O seam**
(deferred since M0.3) and the **in-process readback screenshot** from the Session 03
backlog.
**Outcome:** **M2.1 complete** in one squash (**PR #8**, branch `9c7644f`): the triangle
renders **validation-clean** and the proof is a screenshot **the engine captured of
itself** — red top / green bottom-right / blue bottom-left, exactly the Y-down NDC the
shader encodes. A 45-agent adversarial review confirmed 5 findings (all fixed pre-merge)
and refuted 14.

### What was built (PR #8)

| Piece | Detail |
|---|---|
| Platform file I/O | `platform_file_read` (into a caller `Allocator`, 16-aligned), `platform_file_write` (atomic `.tmp`+`MoveFileExW`), `platform_file_size` (cheap stat — see gotcha below). ARCHITECTURE §4.1 updated in the same commit. |
| Shaders | `engine/render/shaders/triangle.{vert,frag}`, verts hardcoded in the shader (`gl_VertexIndex`), compiled by `add_shader_library` → `build/shaders/$<CONFIG>/*.spv`, located via `MOBA_SHADER_DIR`. |
| ADR-0012 | **Vulkan 1.3 + `dynamicRendering` + `synchronization2` is the hard minimum.** Gated at device *selection* (below-spec devices skipped; clean message if none qualify); both features enabled at device create. No render-pass/sync1 fallback — one barrier vocabulary, ever. |
| Pipeline | Static registry table (1 entry) + shared empty layout; viewport/scissor dynamic so **resize never rebuilds** (only a surface-format change does); `VkPipelineRenderingCreateInfo` instead of a render pass. |
| Pipeline cache | Loaded at startup **only after** a Vulkan-free header check (vendor/device/UUID — `pipeline_cache_check.cpp`, unit-tested headlessly); saved atomically at shutdown; size-bounded both directions. |
| Frame | barrier2 `UNDEFINED→COLOR_ATTACHMENT` (src stage = the acquire-semaphore wait stage) → `vkCmdBeginRendering` (loadOp=CLEAR keeps the animated color) → draw → barrier2 `→PRESENT_SRC` (dst NONE; the semaphore covers visibility) → `vkQueueSubmit2`. |
| Readback | `renderer_capture()` renders one frame and copies it to a host-visible buffer **between render and present** (a presented image can't be read), waits the frame fence, returns RGBA8. `sandbox --screenshot out.bmp` writes it. |
| Dispatch | +30 procs across instance/device tiers; all fatal-on-missing since ADR-0012 guarantees 1.3. |
| Tests | New `platform` suite (9: roundtrip, **the rename-failure half of atomicity**, UTF-8 + invalid-UTF-8 paths, zero-byte, missing-dir, size-stat) and `render` suite (5: cache-blob checker incl. unaligned source). 7 ctest entries total. |

### The adversarial review (5 lenses → 2 skeptics per finding)

**Confirmed & fixed (5):**

1. **Multi-config shader clobber (build, major).** Per-config glslc flags (`-g`/`-O`)
   wrote the **same** `.spv` path under Ninja Multi-Config → every config switch
   re-ran glslc and clobbered the other flavor; `MOBA_SHADER_DIR` pointed all binaries
   at whichever built last. A skeptic proved it had *already happened* from
   `.ninja_log`'s alternating command hashes. Fixed: outputs + `MOBA_SHADER_DIR` are
   per-`$<CONFIG>`.
2. **Oversized cache file = startup abort (seam-arch, major).** The cache is untrusted
   on-disk input, but it was read straight into the renderer's fixed 16 MiB arena —
   and arena overrun is an always-on hard abort (M1.0 policy). A >16 MiB file would
   crash-loop every launch *before* the blob checker ran. Fixed with
   `platform_file_size` + an 8 MiB bound on load **and** save; verified live with a
   planted 20 MB blob (logs "oversized — starting empty", then self-heals).
3. **Atomic-write failure branch untested (tests, major).** Only success paths were
   covered; a "delete destination, then rename" regression would have shipped green.
   Fixed: a read-only destination forces `MoveFileExW` to fail → assert original
   intact, `.tmp` removed, `false` returned.
4. **Uninitialized `PlatformFile` UB in two tests (tests, minor).** `CHECK` doesn't
   stop the body and a failed read leaves `out` untouched by contract → later CHECKs
   read indeterminate memory. Fixed with `= {}`.
5. **UTF-8 path contract untested (tests, minor).** All test paths were ASCII (same
   bytes under CP_UTF8/CP_ACP). Fixed: `é測` roundtrip + invalid-UTF-8 rejection.

**Refuted (14)** — including three sync claims (the capture/recreate flow doesn't
have the claimed staleness; the WSI present-semaphore gap is the accepted M2.0
recipe), concurrent-process `.tmp` races (single-instance dev tool), and
`MOBA_SHADER_DIR` non-relocatability (explicitly provisional until Phase 4).

### Key decisions & gotchas

- **First real use of M0.2 scaffolding finds the bugs.** `add_shader_library` was
  written in M0.2 but never invoked until now — and held two latent defects (the
  clobber above, plus `$<$<CONFIG:Debug>:-g> $<$<NOT:…>:-O>` leaving an **empty ""
  argument** in the non-matching config, which glslc read as a second input file:
  "linking multiple files is not supported"). Single-genex `$<IF:>` fixes the latter.
- **The arena hard-abort policy is for *budgets*, not external input.** Anything
  reading an untrusted/on-disk size into a fixed arena must bound it first
  (`platform_file_size`) — otherwise a file's length becomes a process-kill switch.
  This distinction (programmer-bug abort vs. expected-failure degrade) is ADR-0009's
  two-channel rule; the review caught it being violated.
- **Read back before present, not after.** A presented swapchain image belongs to the
  presentation engine; `renderer_capture` therefore copies between `CmdEndRendering`
  and present, inside the same submission the frame fence covers. HOST_COHERENT means
  the fence wait alone makes the copy host-visible.
- **sync2 niceties:** `UNDEFINED→COLOR_ATTACHMENT`'s src stage must be
  `COLOR_ATTACHMENT_OUTPUT` so it chains *after* the acquire semaphore (which waits at
  that stage); the present handoff uses dst stage `NONE` legally because the signal
  semaphore (stageMask `ALL_COMMANDS`) provides the visibility.
- **CRT narrow APIs are ANSI, not UTF-8** — `std::remove()` can't delete a file
  created via a UTF-8 path; tests clean up with `_wremove`. (Win32 wide APIs +
  `MB_ERR_INVALID_CHARS` everywhere in the platform impl.)
- **CHECK-style harnesses keep running after failure** — any out-param read by later
  CHECKs must be initialized, or the failure path is UB that can eat the diagnostics.
- Viewing BMPs from the agent session: convert to PNG (System.Drawing one-liner);
  the Read tool renders PNG natively. The screenshot loop (run → capture → look) is
  what finally lets the agent *see* the renderer's output — Session 03's blocker.

### Verification

- `/WX` clean on Vulkan + null backends; **7/7 suites green** (gate + pre-push hook +
  CI matrix on the PR).
- **Zero validation messages** across 60-frame runs, validation ON.
- DoD literally: triangle screenshot-verified; shader edit → only that `.spv`
  regenerated (depfile) → inverted-color capture confirmed → reverted; cache file
  appears run 1 (12,119 B), **primes run 2** (and grew when the edited shader variant
  entered it — the cache is demonstrably real).
- Robustness: 20 MB foreign cache → graceful "starting empty" → atomically replaced
  with a real cache at shutdown.

### Deferred backlog (carried forward)

- **Interactive M2.0/M2.1 DoD:** zero validation errors across a real resize,
  minimize/restore, alt-tab (needs a display; paths implemented).
- Vulkan SDK in CI → `find_package(Vulkan REQUIRED)`, keep the null backend only as
  the deliberate M2.5 seam-audit backend.
- Split `plat_mem_*` out of the Win32 window TU; clang-cl/UBSan determinism run;
  OpenCppCoverage HTML (all from earlier sessions).

### Where we are / next

**Phase 2: M2.0 + M2.1 done.** The engine has a real pipeline path: offline shaders,
cached pipeline creation, dynamic rendering, sync2 — and can prove its own output with
a screenshot. **Next: M2.2 — textured quad** (vertex/index buffers via staging, first
image upload + layout transitions, sampler, descriptor set — the first real GPU memory
traffic, and the rung the naive `vk_alloc_dedicated` allocator arrives for). Pixels
come from a hardcoded TGA loader; the asset manager stays out of it until Phase 4.

---

## Session 03 — 2026-06-06 — Test visualization + Phase 2 begun (M2.0: clear the screen)

**Scope:** Add ways to *see* what the tests/engine do, then begin Phase 2 — raw Vulkan
bring-up — all the way to a window that clears to an animated color.
**Outcome:** **M2.0 functionally complete.** The engine talks to the GPU through a fully
hand-loaded Vulkan stack and clears the swapchain every frame, **validation-clean**.
Six PRs merged to `main` this session (`#2`–`#6`); this entry covers `#4`–`#6` (M1.4 +
CI were Session 02). Working tree clean.

### Timeline (this session)

| PR | Squash | Summary |
|---|---|---|
| `#4` | `5eccf2a` | Test-visualization tooling: `tools/visualize` BMP dumps + CI test report |
| `#5` | `4f0644a` | M2.0 rung 1: hand-loaded loader + instance + debug messenger + device select |
| `#6` | `36b74f6` | M2.0 rung 2: surface + device + swapchain + per-frame sync → animated clear |

(Plus a Node-free CI rework, PR `#3`/`8ae3977`, logged under Session 02.)

### Visualizing the tests (PR #4)

- **`tools/visualize`** — a headless exe that renders the fixed-point math the `det`
  suite asserts numerically into 24-bit BMPs (Windows opens these natively): `fix_sin`/
  `fix_cos`/`fix_sqrt` vs libm. The standout is `fix_trig_error.bmp`, which draws the
  **±2e-3 band the `det` `CHECK_APPROX` asserts** in red so the LUT error is visibly
  inside tolerance — the numeric test made tangible. Outputs are gitignored.
- **CI test report** — `ctest --output-junit` + a native pwsh step renders a per-test
  PASS/FAIL table into the GitHub run summary (no third-party action). Read attributes
  via `GetAttribute` — on an `[xml]` element `$node.name` is the *tag* name, not the
  attribute.
- Fixed a **latent `.gitignore` bug**: it has no inline comments, so the trailing
  `# …` made `*.spv`/`baked/`/`*.mba`/`*.pak` match nothing (would have bitten in
  Phase 2/4). Comments moved to their own lines.

### Phase 2 — Vulkan bring-up (PRs #5, #6)

Installed the **LunarG Vulkan SDK** (`KhronosGroup.VulkanSDK` 1.4.350.0 via winget).

- **Hand-loaded loader (ADR-0004):** `platform_vk_get_loader()` `LoadLibrary`s
  `vulkan-1.dll` → `vkGetInstanceProcAddr`; the renderer routes **every** call through
  its own dispatch table (`Vk`) in three tiers — global (`gipa(NULL,…)`), instance, and
  **device** (re-resolved via `vkGetDeviceProcAddr` to skip the loader trampoline).
  Links Vulkan **headers only, never `vulkan-1.lib`**.
- **Instance** API 1.3 + `VK_LAYER_KHRONOS_validation` + a debug-utils messenger that
  logs WARN/ERROR and never aborts the triggering call.
- **Surface** in the platform (`platform_vk_create_surface` → `vkCreateWin32SurfaceKHR`,
  HWND never crosses into render — ADR-0005); physical-device scoring (discrete >
  integrated, must have graphics+present); logical device + `VK_KHR_swapchain`.
- **Swapchain:** `B8G8R8A8_SRGB`/`SRGB_NONLINEAR` preferred, `MAILBOX`→`FIFO` fallback,
  `minImageCount+1`, extent clamped to caps; brute-force `vkDeviceWaitIdle` recreation
  on resize/`OUT_OF_DATE`/`SUBOPTIMAL`.
- **Per-frame sync (the #1 risk):** frames-in-flight=2 (per-frame command buffer +
  `image_available` semaphore + `in_flight` fence) + a **per-swapchain-image**
  `render_finished` semaphore + the `images_in_flight[]` fence array. Clears with
  `vkCmdClearColorImage` + classic `UNDEFINED→TRANSFER_DST→PRESENT_SRC` barriers (no
  render pass / dynamic rendering needed *just* to clear).

### Key decisions & gotchas

- **Dual backend so CI stays green without an SDK.** `eng_render` compiles the real
  Vulkan backend when `find_package(Vulkan)` finds headers, else a **null backend**
  (`renderer_null.cpp`) so the `renderer.h` seam still links. The runner has no SDK, so
  CI builds null; the Vulkan path is verified locally. Flipping `find_package` to
  `REQUIRED` waits on installing the SDK in CI. (Early form of the M2.5 null backend.)
- **The acquire semaphore-reuse hazard, handled:** `vkAcquireNextImageKHR` returning
  `OUT_OF_DATE` does **not** signal the semaphore (no image), so we bail and recreate
  safely; `SUBOPTIMAL` *does* acquire+signal, so we must proceed through submit/present
  and recreate *after*. Conflating the two leaks a signaled semaphore → validation error.
- **`VULKAN_SDK` and pre-existing shells:** a session started before the SDK install
  doesn't see the env var; set `$env:VULKAN_SDK` for the build (a restart fixes it). The
  pre-push gate / CI therefore build the null backend unless the var is present.
- **Can't screenshot the live window from the agent:** the sandbox window is created in
  an isolated window station, so `FindWindow` returns 0 and `PrintWindow` can't capture
  it. Verification rests on the validation-clean multi-frame run; a real display (run
  `sandbox.exe`) or an in-process readback is needed to *see* it.
- `.bat` files must be ASCII (a stray em-dash broke `cmd`'s parser); use `git commit -F`
  / `gh --body-file`, not PowerShell here-strings (they intermittently mangle).

### Verification

- **`renderer: Vulkan 1.3 up | validation=on | GPU: NVIDIA GeForce GTX 1070 (discrete)
  | swapchain 1280x720 x3`** — 60 acquire→clear→present cycles, validation ON, **zero
  validation messages**, clean teardown. Builds `/WX` (Vulkan + null backends).
- CI green on every PR (Debug + Release matrix; null backend on the runner).

### Deferred backlog (carried forward)

- Install the Vulkan SDK in CI → flip `find_package(Vulkan)` to `REQUIRED`, drop the
  null path (or keep it as the deliberate M2.5 null backend).
- **Interactive M2.0 DoD:** verify zero validation errors across a real resize,
  minimize/restore, and alt-tab (paths implemented; needs a display).
- In-process **readback screenshot** (session-independent visual proof of the clear).
- Adopt `synchronization2` + **dynamic rendering** at M2.1 (the triangle needs a real
  color attachment, not `vkCmdClearColorImage`).
- Optional OpenCppCoverage HTML; split `plat_mem_*` out of the Win32 window TU.

### Where we are / next

**Phase 2 is underway and M2.0 (clear the screen) is functionally done** — the renderer
seam, hand-loaded loader, and a correct per-frame present loop are in place and proven
validation-clean. **Next: M2.1 — the first triangle** (graphics pipeline + offline
SPIR-V via `glslc`/`add_shader_library` per ADR-0008 + a pipeline cache), adopting
sync2/dynamic-rendering there.

---

## Session 02 — 2026-06-06 — M1.4: test harness + CTest + pre-push gate (Phase 1 done)

**Scope:** Finish Phase 1 by replacing the precursor test helpers with a real,
self-registering harness, wire it into CTest behind a headless aggregate target, and
stand up the pre-push gate that makes the whole suite a push-blocking contract.
**Outcome:** **M1.4 complete → Phase 1 complete.** `ctest` is green in ~0.5s; the gate
was proven to block on a deliberately-injected failing `CHECK` and pass once reverted.
Merged to `main` via **PR #2** (squash `91b9c1d`), with GitHub Actions CI now mirroring
the gate and an adversarial pre-merge review having found + fixed 5 issues first.

### What was built

| Piece | Detail |
|---|---|
| `tests/test.h` | Self-registering `TEST(suite, name)` + `CHECK`/`CHECK_APPROX`. No exceptions, no STL. ~150 lines. |
| `tests/test_main.cpp` | Shared `main()` → `test::run_all` with `--suite` / `--filter` / `--list`. |
| 4 suites refactored | `mem` (6 tests), `math` (12), `containers` (14), `det` (12). `int main()` + `section()` → `TEST()` blocks. |
| `engine_core_group` | INTERFACE aggregate (`core+math+platform`) the test binary links — the headless target. |
| CTest | One entry **per suite** (`--suite` filter) so a red test names its module: `mem`, `math`, `containers`, `det_precise`, `det_fast`. |
| `tools/hooks/pre-push` + `ctest-gate.bat` | sh shim → `cmd` → vcvars + `/WX` build + `ctest`. Activated via `core.hooksPath`. |

### Harness design — the two landmines handled

- **Static-init order across TUs is unspecified.** The intrusive-list head is a
  function-local `static` (`registry()`, construct-on-first-use), so a `Registrar` in
  any TU can link itself regardless of init order. Registrars append at the tail →
  cases list in source order (verified via `--list`).
- **The linker drops unreferenced object files**, which would silently delete the
  registrars ("my tests don't run"). This only bites when test objects live in a
  *static library*, so each `*_tests.cpp` is compiled **straight into the exe**, never
  into a lib. Contract is commented in both `test.h` and `tests/CMakeLists.txt`.
- `CHECK` attribution is by snapshotting the global `fails()` counter before/after each
  case, so the assert macros stayed byte-identical to the M1.0 versions (low-risk
  refactor; failures still self-report `file:line`).

### Verification

- `ci` (`/WX`) build clean; **`ctest` 5/5 green in ~0.5s.** Totals: `engine_tests`
  32 tests / 3,095 checks; `det_tests` + `det_tests_fastfp` 12 tests / **84,672 checks
  each** — golden `0x1808f09365745d5a` identical across `/fp:precise` + `/fp:fast`.
- **Gate proof:** injected `CHECK(1 == 2)` → `det`/`math`/`containers` stayed green,
  only `mem` went red, `ctest` failed, gate exit code **8** (push would be blocked).
  Reverted → green, exit 0. Ran the hook the way git invokes it (args + stdin) to
  confirm the sh→cmd→bat handoff.

### CI + adversarial pre-merge review

After the harness landed, CI and a pre-merge review were added before merging PR #2:

- **GitHub Actions CI** (`.github/workflows/ci.yml`): Windows/MSVC, the `ci` `/WX`
  preset + `ctest`, across a **Debug × Release** matrix — so the determinism golden is
  checked over `{/fp:precise, /fp:fast} × {Debug, Release}`. Verified each step actually
  ran (not a skipped/empty green); `/WX` is clean on the runner's VS2022 as well as local
  VS18. MSVC is set up **Node-free** via `vswhere` + `vcvars` (no third-party action —
  `ilammy/msvc-dev-cmd` has no Node 24 release and GitHub is retiring Node 20 actions;
  `actions/checkout` pinned to v5). This also survives the `windows-latest` → VS2026
  image migration and mirrors `tools/hooks/ctest-gate.bat`.
- **Adversarial pre-merge review** (a 12-agent workflow, 5 lenses, each finding then
  verified by a skeptic) found **5 real issues, all fixed before merge**:
  1. The gate could **report green while running zero tests** (`ctest` exits 0 on an
     empty set) → `--no-tests=error` in both the hook and CI.
  2. The harness now **fails (exit 2) on any zero-run** — catches `--suite`/`--filter`
     selector drift *and* the dropped-registrars landmine.
  3. `TestArena` zero-inits its `Arena` (latent UB on a reserve failure).
  4. CI no longer cancels a required `main` run (`cancel-in-progress` scoped to non-main).
  Two findings were correctly **dismissed** (the `cygpath` fallback works because
  `git rev-parse --show-toplevel` yields a `C:/…` path cmd accepts; floating action tags
  are a hardening preference, not a bug).

### Notes / gotchas

- **Keep hook/batch scripts ASCII-only:** a stray em-dash (`—`) in `ctest-gate.bat`
  made `cmd`'s byte-parser eat the leading chars of following lines (`rem`→`m`,
  `setlocal`→`tlocal`). YAML/C++ comments tolerate UTF-8; `.bat` files do not.
- **`ctest-gate.bat` paren trap:** `)` inside an `echo` text *inside* an `if (…)` block
  closes the block early (`. was unexpected`). Rewrote with `goto :fail` + a single
  `%MSG%` echo, and grouped `if cond ( set … & goto … )` (bare `&` runs the next
  command unconditionally — must be inside the parens).
- The `'vswhere.exe' is not recognized` line is emitted from **inside** `vcvars64.bat`
  itself, not our script (we call vswhere by full path); the build/tests still run.
- **`engine_core_group` includes `eng_platform`** for the page-allocator backend only
  (`plat_mem_*`, the one OS dep of core's arenas — ADR-0005). Its window/input code is
  linked-but-unused in tests. Splitting the page backend out of the Win32 window TU so
  the group is *literally* window-free is a deferred cleanup (see backlog).
- Hook activated locally via `git config core.hooksPath tools/hooks`; committed mode
  `100755` (`git update-index --chmod=+x`) and pinned `eol=lf` in `.gitattributes` so
  the `sh` shebang survives a fresh non-Windows clone.

### Deferred backlog (carried forward)

- Split `plat_mem_*` out of `win32_platform.cpp` so `engine_core_group` is truly
  window-free (the only impurity in the headless aggregate).
- clang-cl + UBSan determinism run asserting the same golden (clang-cl not installed).
- Poison/real-free allocator memory-safety run (Debug-ASan preset; arena poison hooks
  stubbed).
- `*_free` vs `*_free_all` naming standardization; `eng_sim` float/hashmap grep-fence
  (lands with the sim lib in Phase 3, M3.4).
- **Install the LunarG Vulkan SDK before Phase 2** (`find_package(Vulkan)` becomes
  `REQUIRED`).

### Where we are / next

**Phase 1 is done** — the dependency-free core is built, tested, and gated. **Next is
Phase 2: raw Vulkan bring-up** (M2.0 instance→device→swapchain→clear, the project's #1
technical-risk cluster). **Install the Vulkan SDK first** so `find_package(Vulkan)` can
flip to `REQUIRED`.

---

## Session 01 — 2026-06-05 — Foundations (Phase 0 + Phase 1 core)

**Scope:** From an empty repository to a deterministic, tested engine core — build
system, platform window, memory, math, fixed-point determinism, and containers.
**Outcome:** Phase 0 complete; Phase 1 milestones M1.0–M1.3 complete (M1.4 remains).
Nine commits on `main` (`be86d5d` → `339defe`), all pushed. Every layer builds clean
in Debug/RelWithDebInfo/Release and under `/WX`, with ~88k test checks passing.

### The locked stack

| Axis | Decision | Recorded in |
|---|---|---|
| Engine scope | Pure from scratch (own Win32, math, raw Vulkan, parsers, ECS/net) | design docs |
| Platform | Windows-first, clean portability seam | ADR-0005 |
| Language | C-style / data-oriented **C++17**, no exceptions/RTTI | ADR-0009 |
| Build | CMake + Ninja Multi-Config + MSVC | ADR-0006 |
| Graphics | Raw Vulkan, **hand-loaded** loader (own dispatch table) | ADR-0004 |
| Sim numbers | **Fixed-point Q16.16**, fully deterministic, 30 Hz tick | ADR-0001/0002 |
| Handles | 32-bit generational, **18 index + 14 generation** | ADR-0003 |
| Netcode | **Server-authoritative** (predict + reconcile; real fog) | ADR-0011 |
| Genre | MOBA/RTS hybrid (more units/projectile churn than a typical MOBA) | project note |

### Timeline

| Commit | Milestone | Summary |
|---|---|---|
| `be86d5d` | init | repo scaffolding (.gitignore/.gitattributes/README) |
| `be3e123` | design | `docs/ARCHITECTURE.md` (14 §) + `docs/ROADMAP.md` (Phases 0–8+) + ADR-0011 |
| `b6d5c93` | M0.1 | foundational ADRs 0001–0010 |
| `28096af` | M0.2 | CMake build spine: `eng_*` module link graph, compiler posture |
| `202f073` | M0.3 | Win32 window + clean message loop (`tools/sandbox`) |
| `7b7e703` | M1.0 | memory: arenas + allocator interface (high-water, scratch, TempMemory) |
| `f2f117a` | M1.1 | float math: vec/mat/quat + the Vulkan projection (Y-flip/[0,1]) |
| `ff723e8` | M1.2 | determinism bedrock: Q16.16 `fix` + PCG32 + golden-hash test |
| `339defe` | M1.3 | containers + handle ABI: Array/InlineArray/Str/Pool/HandlePool/HashMap |

### How it was built — multi-agent workflows

Substantial design and verification used parallel subagent workflows; the adversarial
reviews repeatedly caught real, subtle bugs **before** commit:

- **Architecture design** (15 agents): 10 subsystem designs → 3 review lenses →
  synthesis. Produced ARCHITECTURE.md + ROADMAP.md. Caught (and fixed) a 30 Hz/60 Hz
  tick contradiction, a fixed-point format that used `__int128` (which MSVC can't
  compile), and a classic Vulkan acquire/present sync hazard.
- **Netcode revision** (4 agents): reworked the design from lockstep →
  server-authoritative when that decision was made, handling the partial-state
  prediction subtleties.
- **M1.2 determinism review** (6 agents): independently re-derived the PCG32 stream
  (KAT verified) and found **4 signed-overflow UB sites** — the golden hash had been
  baked over UB. All fixed.
- **M1.3 container review** (6 agents): confirmed the HashMap Robin-Hood/backward-shift
  core correct (a reviewer ran a standalone 5,000-trial adversarial-collision harness)
  and HandlePool staleness sound; found a `str_append` `u32` overflow (heap-overflow
  path) and missing trivially-copyable guards. All fixed.

### Engineering notes & gotchas (for future sessions)

- **Build environment:** `cl`/`cmake`/`ninja` are **not** on the global PATH. Build
  from a VS Developer shell, i.e. `call vcvars64.bat` (at
  `C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat`)
  then `cmake --preset dev` / `cmake --build build --config Debug`.
- **Presets:** `dev` (lenient) and `ci` (`MOBA_WERROR=ON` → `/WX`). Always validate
  under `ci` before committing.
- **Determinism is real, not aspirational:** the M1.2 golden hash
  (`0x1808f09365745d5a`) is verified identical across `{/fp:precise, /fp:fast} ×
  {Debug, Release}`. Keep the float ban in sim code.
- **Two design corrections made during implementation:** (1) the docs had a latent
  `eng_core` ↔ `eng_platform` dependency cycle — broken by injecting a *commit
  callback* into the Arena so core stays a pure leaf; (2) the handle split changed
  24+8 → **18+14** (ADR-0003) once the hybrid genre was clarified, and §2.3 was
  updated to match.

### Test status

`tests/test.h` shared harness (precursor to M1.4's self-registering version). Suites,
all green in Debug + Release + `/WX`:

| Suite | Checks |
|---|---|
| `mem_tests` | 18 |
| `math_tests` | 297 |
| `det_tests` (×2: `/fp:precise` + `/fp:fast`) | 84,672 each |
| `container_tests` | 2,767 |

### Deferred backlog (pick up at M1.4 / CI)

- `tests/test.h` self-registering `TEST()` harness + **CTest** + `eng_core_group`
  headless aggregate; wire the `pre-push` hook to run `ctest` (this *is* M1.4).
- clang-cl + UBSan determinism runs asserting the same golden (clang-cl not installed).
- Poison/real-free allocator memory-safety run (alongside Debug-ASan, arena poison
  hooks already stubbed).
- `*_free` vs `*_free_all` naming standardization; eng_sim grep-fence for hashmap
  internals.
- Install the **LunarG Vulkan SDK** before Phase 2 (`find_package(Vulkan)` becomes
  `REQUIRED` there).

### Where we are / next

The dependency-free core (`eng_core`, `eng_math`) is essentially done and tested.
**Next: M1.4** finishes Phase 1 (test harness + CTest). **Then Phase 2** — the first
long pole: raw Vulkan bring-up (instance → device → swapchain → clear → triangle →
instanced meshes). Install the Vulkan SDK first.
