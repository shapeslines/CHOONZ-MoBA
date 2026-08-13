# Security consolidation provenance ledger

This is the tracked execution/provenance record for `codex/security-consolidation`. Source pull
requests remain immutable audit inputs. Every final source hunk is marked `retained`, `repaired`, or
`intentionally superseded` before the consolidated PR becomes ready.

Inventory captured from GitHub on 2026-08-13 against `main` at
`63f0b1dedad1c3f509843f68f32c24f6f52aa55e`.

## Scope rulings

- Consolidation sources: #27, #29-#34, #36-#37, and #39-#46.
- Base provenance only: merged milestone PRs #35 and #38.
- Expected superseded: PR #26. Its older M3.3 code was repaired and landed through #28/#35/#38. Its
  unique `docs/gap-analysis-report.md` is an obsolete snapshot whose durable facts are represented by
  the current gap ledger, M3.3/M3.4 slates, journal, and handoff; it contains no required code or
  current state absent from `main`.
- Explicitly excluded and retained: PR #47 (`e3271bf`), PR #48 (`714e01c`), PR #49 (`c187dc7`), and
  PR #50 (`972f328`). They were opened after the approved consolidation boundary and are not
  retirement targets. Their one-path subjects are string aliasing, renderer-handle free-list state,
  TGA allocator callbacks, and rejected render-batch rollback respectively.
- Preserved unfinished work: `codex/security-platform-memory-boundary` at base `63f0b1de`, worktree
  `.worktrees/security-platform-memory`, with modified `engine/platform/src/win32/win32_mem.cpp` and
  untracked disposable S22 probe/receipt files. It was stopped before commit, push, or PR and is not a
  consolidation input.

## Exact source inventory

| PR | Exact head | Historical check state | Exact source files | Initial disposition |
|---|---|---|---|---|
| #27 | `d2389867e11b50d967b0a9ae769b4d59d0c181d1` | MSVC D/RWD/R + CodeQL green; fresh-walk red before code on winget/msstore `0x8a15000f` | `tests/CMakeLists.txt`; `tests/fresh_walk_path_guard.ps1`; `tools/fresh-walk.ps1` | retain + repair on current workflow |
| #29 | `86077c9e189be0f3e49a84b81715138ebc8b347a` | MSVC D/RWD/R + CodeQL green; fresh-walk red before code on winget/msstore `0x8a15000f` | `tests/CMakeLists.txt`; `tests/sandbox_cli_errors.ps1`; `tools/sandbox/src/main.cpp` | retain |
| #30 | `531a99b37848758c0d3f28945a235451e3720630` | all CI and CodeQL checks green | `.github/workflows/ci.yml` | retain + repair all three checkout sites and recovery entrypoint |
| #31 | `5bedf034781898dd651cce99b0cf661f13cfe7a5` | all CI and CodeQL checks green | `engine/platform/include/platform/platform.h`; `engine/platform/src/win32/win32_file.cpp`; `tests/platform/platform_tests.cpp` | retain + strengthen collision tests |
| #32 | `e0b41a20b7355c21760011bf6f89134aff720806` | all CI and CodeQL checks green | `engine/platform/src/win32/win32_platform.cpp` | retain + validate explicit SDK path |
| #33 | `655da204be77a59174b59db7443e0769a66a8cd7` | all CI and CodeQL checks green | `tests/replay/replay_cli_errors.ps1`; `tools/replay/src/main.cpp` | retain + complete malformed matrix |
| #34 | `50cbf238104f2ab617b438259a01cd76f28d4bd2` | all CI and CodeQL checks green | `tests/visualize/visualize_output_path_guard.ps1`; `tools/visualize/CMakeLists.txt`; `tools/visualize/src/main.cpp` | retain |
| #36 | `d0201552b625882b232f46cde03dbd12bb09c80c` | all CI and CodeQL checks green | `tests/platform/sandbox_smoke_classifier_tests.ps1`; `tools/classify-sandbox-smoke.ps1` | retain + fix expected count to 15 |
| #37 | `66b13fc93bdf7e284d2a2582d9cf4fd5304e8f7c` | all CI and CodeQL checks green | `cmake/CompileShaders.cmake` | retain + persist negative fixtures |
| #39 | `3c2c827db6518ed49c93d0cd745da5679903f496` | all CI and CodeQL checks green | `engine/render/src/renderer.cpp` | retain + focused path guard evidence |
| #40 | `efc5c121717b943cf9ae024b43d043eb78a35069` | all CI and CodeQL checks green | `tests/test.h` | retain + persisted argument tests |
| #41 | `a4cceee5403904f378106e8d96d80d807dce61a5` | all CI and CodeQL checks green | `engine/core/include/core/array.h` | retain + byte/capacity guards and tests |
| #42 | `6272f9302468905e624f8791c3a30bb920039bd2` | all CI and CodeQL checks green | `engine/core/src/mem/arena.cpp` | retain + no-mutation tripwires |
| #43 | `6698cffb1c9c7eebc6cf742cc080fa01c92537b9` | all CI and CodeQL checks green | `engine/core/include/core/hashmap.h` | retain + allocation-size guards and tests |
| #44 | `b6de5cf56b564e5744404096587d3f0b9ab6b9cc` | all CI and CodeQL checks green | `engine/render/src/render_debug_draw.cpp` | retain + boundary tests |
| #45 | `e34ff0448003fdab4b754f6f3b9af844088621c0` | all CI and CodeQL checks green | `engine/render/src/renderer_null.cpp` | retain + no-read/no-mutation tests |
| #46 | `ae73512781b208a5ba2a554f8bb1de530aa842db` | MSVC D/RWD/R, clang-cl/UBSan, fresh-walk, and CodeQL green | `engine/core/include/core/pool.h` | retain + persisted corruption tests |

Historical check shorthand: D = Debug, RWD = RelWithDebInfo, R = Release. Every final claim must use
fresh consolidated-head evidence, not these historical runs.

## PR #26 supersession audit

Exact head: `c35637108c2c58ddca747c4ab27cf62ccf2198d7`; open, conflicting, historical checks green.

Its PR diff names `CMakeLists.txt`, `docs/gap-analysis-report.md`, `docs/gap-close-ledger.md`,
`docs/next-session.md`, `engine/game/CMakeLists.txt`, `engine/game/include/game/present.h`,
`engine/game/src/present.cpp`, `tests/CMakeLists.txt`, `tests/present/present_tests.cpp`,
`tools/sandbox/CMakeLists.txt`, and `tools/sandbox/src/main.cpp`. Current `main` contains the accepted
and later-hardened versions of the code/doc contracts through M3.3/M3.4. The only absent file is the
historical report described above; its stale remaining-work claims make it unsuitable as current
authority. Initial disposition: intentionally superseded after final provenance recheck.

## Slice evidence

### S1 — ownership and baseline

- Isolated worktree: `.worktrees/security-consolidation`.
- Branch/base: `codex/security-consolidation` from exact `63f0b1de`.
- Root dirty checkout and all source worktrees untouched.
- ARC compiler: `SELFTEST PASS (1169 checks)`; manifest `VALID`; `render --check` exit 0.
- GitHub inventory: exact heads/files/checks above; historical #27/#29 failures confirmed external.
- Debug build: 88/88 targets completed.
- Debug CTest: 32/32 passed in 15.05 seconds.
- Direct oracle: `ticks=10000 commands=923 final=0x637628abff59c823
  stream=0x6f381609f7e59f0c logic=0xab96814425ba80a4`.
- Focused determinism: 4/4 tests and 178,690 checks passed; controlled divergence remained
  `tick=4321 field=position_x entity=7`.
- S1 disposition: PASS.

## Final hunk map

Complete. A fresh commit-by-commit comparison on 2026-08-13 verified every path changed by the 17
approved heads is changed by the consolidation, then compared each source guard with the current
implementation and its permanent fixture. The entries below are the semantic hunk dispositions;
none is carried solely by source-branch history.

- PR #27: retained and repaired. Direct-child temp, file, and reparse protections remain; persistent
  tests now cover root, temp root, source/outside root, nested, file, and junction. A newly found case
  where the source repository itself was a direct temp child is rejected before deletion.
- PR #30: retained and repaired. Least privilege and verified v5.0.1 SHA apply to all three current
  checkout uses; `workflow_dispatch` supplies the missing recovery path; both Vulkan SDK installs use
  `--source winget` to avoid the historical ambient `msstore` failure.
- PR #41: retained and repaired. The length-saturation guard remains before `len + 1`; capacity and
  allocation-byte multiplication now use always-on checked arithmetic, with persistent diagnostic
  tripwires instead of the source branch's structural-only proof.
- PR #42: retained and repaired. Offset/commit/reserved, address, alignment, end, and rounding guards
  remain before mutation; arena initialization is now transactional and rejects invalid committed or
  address-range state before replacing the destination.
- PR #43: retained and repaired. Capacity saturation is rejected before doubling, and both new and
  released slot-byte calculations use the same checked multiplication boundary.
- PR #46: retained and repaired. Pool and HandlePool allocation reject missing storage, invalid
  count/head/next state, and invalid generation state without changing slots, counts, generations, or
  free heads. Existing ascending fresh allocation and LIFO reuse remain unchanged.
- PR #31: retained and strengthened. `CREATE_NEW` enforces create-only predictable temporaries, while
  a permanent test now proves both an existing destination and attacker-owned `.tmp` remain byte exact.
- PR #32: retained and repaired. Ambient DLL lookup is removed; the loader lives in a window-independent
  TU and accepts only System32 or an explicit canonical existing drive-absolute SDK root, with
  restricted dependency search and transactional path tests.
- PR #37: retained and repaired. Missing, directory, real-path escape, duplicate, and Windows
  case-duplicate shader declarations fail during configure; the normal five-shader offline build is green.
- PR #39: retained and repaired. Bounded runtime shader path composition is factored into a
  Vulkan-free helper with exact-boundary and truncation tests; the caller returns before file access.
- PR #29: retained and repaired. `--frames` is canonical positive decimal only, overflow-safe, and
  parsed before DPI or platform initialization; 14 malformed forms prove exit 2 with no screenshot
  or platform/renderer marker.
- PR #33: retained and expanded. Replay numeric parsing is ASCII-digit-only canonical unsigned
  decimal. Signed, spaced, trailing, leading-zero, overflowing, and range-invalid values return exit
  1 without creating output or changing a pre-existing sentinel.
- PR #34: retained and repaired. The visualizer composes and validates all three bounded paths before
  drawing or opening any file, so one longer truncated name cannot leave shorter partial outputs.
- PR #36: retained and repaired. The classifier reads at most 4 MiB + 1 bytes before decoding;
  exactly 4 MiB succeeds, one byte over fails, and the permanent receipt reports 15 cases.
- PR #40: retained and expanded. Missing, empty, or option-shaped selectors and unknown harness
  options return exit 2 before any test body runs, while valid filter and list controls remain green.
- PR #44: retained and strengthened. Debug-line and AABB room checks use ordered subtraction after
  validating `count <= capacity`; sphere segment multiplication is rejected before evaluation.
  Exact-full and wrap-adjacent probes preserve vertex bytes and counters.
- PR #45: retained and strengthened. Null submission validates current draw-count state before
  subtraction or caller-item access. A private test seam injects full, over-capacity, and
  `UINT32_MAX` counts and proves rejection leaves each value unchanged.

### S2 — CI recovery and fresh-walk

- PowerShell parser: all three changed scripts parse.
- Direct negative matrix: PASS with all six unsafe path classes mutation-free.
- Focused CTest: 2/2 (`fresh_walk_path_guard`, `ci_workflow_contract`).
- Full fresh walk on the corrected committed tree: 88-target Debug build; 34/34 CTests; real Vulkan
  `SANDBOX_SMOKE=PASS`; 90 validation-clean frames; screenshot 2,764,854 bytes; cleanup complete.
- Repair loop: first full walk rejected S2 because the temp-root checkout could target itself; the
  source-repository equality guard was added and the complete walk rerun green.
- Published head: `0ed4b6e919b48ced0f8f018487ff64ea62696141` on draft PR #51.
- Exact-head GitHub snapshot: MSVC Debug/RelWithDebInfo/Release, clang-cl/UBSan, fresh-walk, CodeQL
  Actions/C++, and workflow CodeQL all green; PR open, draft, mergeable.
- Single-writer recovery: the older loop stopped at S21/PR #50 and handed off unfinished S22 without
  commit, push, PR, or external action in flight.
- S2 disposition: PASS.

### S3 — Core memory and containers

- Persistent fatal gate: 13/13 subprocess modes emitted their exact always-on invariant diagnostic in
  both Debug and Release, covering Array length/capacity/bytes, HashMap capacity/bytes, and arena
  initialization/state/address/alignment/end/commit-rounding failures.
- Direct mutation gate: Pool and HandlePool corruption matrices reject null storage, full and
  overfull counts, sentinel/out-of-range heads, out-of-range next links, missing generation storage,
  and invalid generation values without changing authoritative state.
- Normal behavior: `containers` passes 16 tests and 2,810 checks; existing ascending fresh allocation,
  LIFO reuse, stale-handle rejection, Array growth, and HashMap growth order remain green.
- Focused dev Debug/Release: 3/3 (`mem`, `containers`, `core_invariant_tripwire`) in each configuration.
- Focused `/WX` Debug/Release: affected targets build cleanly and the same 3/3 tests pass in each.
- Affected full dev Debug gate: all targets build and CTest passes 35/35, including the unchanged
  10,000-tick determinism and binary-parity gates.
- Checkpoint `1864012` was pushed; exact-head MSVC Debug/RelWithDebInfo/Release, clang-cl/UBSan,
  fresh-walk, and CodeQL all passed.
- S3 disposition: PASS.

### S4 — Platform and shader boundaries

- Platform collision: the existing destination and pre-created `.tmp` sentinel survive byte-exact;
  normal overwrite and temporary cleanup remain green.
- Vulkan path matrix: null, relative, drive-relative, UNC, nonexistent, and undersized-output cases
  reject without changing the output; the installed SDK root canonicalizes successfully. Source scan
  finds no bare ambient `LoadLibrary` path.
- Shader declarations: one valid fixture configures; missing file, directory, outside-real-path,
  duplicate basename, and case-only duplicate basename each fail with the expected diagnostic.
- Runtime shader path: exact capacity succeeds; one-byte truncation and invalid inputs fail before the
  renderer's `platform_file_read` call.
- Focused dev and `/WX` Debug/Release: 4/4 (`platform`, `render`, `render_null`,
  `shader_source_contract`) in each configuration; normal Debug/Release shader compilation succeeds.
- Full dev Debug: 36/36 CTests, including unchanged determinism, replay, and binary-parity gates.
- Real Vulkan evidence: NVIDIA RTX 4070 Ti, Vulkan 1.3, validation on, 90 frames, clean exit,
  2,764,854-byte 1280x720 screenshot visually inspected.
- Published checkpoint: `216342d`; exact-head MSVC Debug/RelWithDebInfo/Release, clang-cl/UBSan,
  fresh-walk, and CodeQL all passed.
- S4 disposition: PASS.

### S5 — CLI and tooling inputs

- Sandbox: 14 malformed `--frames` forms return exit 2 with the canonical diagnostic before any
  DPI/window/renderer marker and without creating the requested screenshot.
- Replay: 24 malformed canonical-number cases across ticks/seed/players, three range failures, and
  an existing-output sentinel all return exit 1 without file creation or mutation; corrupt replay
  exit classes remain unchanged.
- Harness and visualizer: seven invalid harness cases exit 2 before test output; valid filter/list
  controls pass; an overlong visualizer directory fails before any of three BMPs can be created.
- Classifier: the 15-case matrix passes with a valid hardware log padded to exactly 4,194,304 bytes
  and a recognized device-gate log at 4,194,305 bytes rejected before classification.
- Focused dev Debug and `/WX` Debug/Release: the five selected contracts expand through replay
  fixtures to 7/7 passing CTest entries in every configuration.
- Full dev Debug: all targets build and CTest passes 39/39, including unchanged determinism,
  binary parity, replay, boundary, shader, and fresh-walk contract gates.
- Published checkpoint: `e4fbd09`; exact-head MSVC Debug/RelWithDebInfo/Release, clang-cl/UBSan,
  both CodeQL checks, and fresh-walk passed. Fresh-walk attempt 1 failed before code on transient
  winget community-source error `0x8a15000f`; targeted job attempt 2 installed the SDK and completed
  the full walk green without a code change.
- S5 disposition: PASS.

### S6 — Renderer capacity arithmetic

- Debug draw: line and AABB use subtraction-based room checks after validating current state; sphere
  rejects `segments > UINT32_MAX / 6` before multiplication.
- Boundary matrix: first line, exact-full line/AABB/sphere, full rejection, corrupt `count > capacity`,
  line/AABB additive-wrap adjacency, and sphere multiplication overflow all preserve counters and
  vertex canaries on failure.
- Null renderer: exact-last submission succeeds; full, over-capacity count, corrupt current count,
  and `UINT32_MAX` state reject before dereferencing an intentionally unreadable `DrawItem*` and do
  not change `draw_count`.
- Focused results: `render` passes 12 tests/113 checks; `render_null` passes 2 tests/35 checks. Dev
  Debug and `/WX` Debug/Release CTest pass 2/2 in each configuration.
- Full dev Debug: all targets build and CTest remains 39/39 with determinism, replay, boundaries,
  fresh-walk contracts, and shader declarations unchanged.
- Published checkpoint: `4ef49f3`.
- S6 disposition: PASS.

### S7 - Provenance and exact-head acceptance

- `/WX` build matrix: Debug, RelWithDebInfo, and Release pass.
- Configuration matrix: all 39 CTest entries pass in Debug, RelWithDebInfo, and Release.
- Debug-ASan repair: the first 38/39 run exposed exit `0xC0000135` for the visualizer child because
  that executable alone did not stage the MSVC ASan runtime. Applying the existing
  `moba_stage_asan_runtime()` helper to the target preserves production behavior and makes the full
  Debug-ASan suite pass 39/39.
- clang-cl/UBSan: capability tripwire and all six structural/determinism entries pass.
- Direct determinism: 10,000 ticks retain final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, and logic `0xab96814425ba80a4`; the controlled mutation first diverges at
  tick 4321 on `position_x`, entity 7 (178,690 checks).
- Fresh walk: a guarded direct-temp clone of final code checkpoint `f0a56c4` configured cleanly, built 92 Debug
  targets, passed 39/39, classified a real 90-frame screenshot, and removed the clone successfully.
- Vulkan: local NVIDIA RTX 4070 Ti / Vulkan 1.3 / validation-on execution completed 90 frames and a
  clean exit; the retained 1280x720, 2,764,854-byte screenshot was visually inspected.
- Provenance: live GitHub heads still match every recorded SHA. PRs #27/#29 retain one historical red
  fresh-walk each from the pre-code winget source failure; their other six checks are green. Every
  approved source path is present in the consolidated diff. PR #26 has no required unique code or
  current authority; excluded #47/#48/#49/#50 change `str.h`, `render_handle_table.cpp`,
  `tga_direct.cpp`, and `render_batch.cpp`, none of which is changed here.
- Scope diff: no `engine/sim`, replay codec/container, asset API, or logic-hash file changed. The
  replay CLI parser is the only replay-facing implementation change and retains all on-disk bytes.
- Review correction on candidate `a89caf6`: independent acceptance passed and all eight GitHub checks
  were green, but the required security review failed. It found (1) a root path-swap interval between
  cleanup validation and recursive deletion, (2) missing corrupt-count preflight in the real Vulkan
  renderer, and (3) fail-open sandbox/replay grammar cases. That head remains superseded and draft.
- Repair checkpoint: both renderer backends now share a preflight that proves `current <= capacity`
  before subtraction or caller-item access. Sandbox and replay reject missing, duplicate,
  option-shaped, unknown, or extra grammar before platform/file effects. Fresh-walk keeps a
  non-delete-sharing handle on the exact leased directory from validation through cleanup and applies
  final deletion through that handle; a deterministic post-validation hook proves an attempted move
  is blocked, in addition to the pre-validation junction and same-path replacement cases.
- Repaired local acceptance: `/WX` Debug/RelWithDebInfo/Release builds pass; all 39 entries pass in
  each configuration (117 executions); Debug-ASan passes 39/39; clang-cl 19.1.5 proves UBSan with a
  live signed-overflow tripwire and passes 6/6; direct Debug and Release determinism each pass four
  tests/178,690 checks with the permanent oracle and exact divergence; committed-head fresh-walk and
  the retained validation-on RTX 4070 Ti run each complete 90 frames with a 2,764,854-byte image.
- Focused security rereview: PASS for all three repairs. A separate full final-head security verdict
  remains required alongside fresh independent acceptance.
- Review correction on exact head `7a0d130`: independent acceptance and all eight GitHub checks
  passed, but fresh full security review found a remaining descendant cleanup race. The root handle
  prevented root replacement, yet descendants were classified from cached path metadata and then
  recursively removed by path, leaving a child replacement interval. The head stayed draft.
- Descendant repair: the native walker opens every child with `OPEN_REPARSE_POINT`, delete/read-
  attributes access, and no delete sharing before handle classification. It retains directory
  handles across recursive walks, treats every reparse point as an untraversed leaf, and applies
  disposition through each verified handle. The exact post-child-validation hook attempts a rename
  and outside-target junction replacement; the move is blocked and the sentinel remains byte exact.
- Focused descendant fixture and complete Debug suite pass. Full final-head matrix and fresh reviews
  must be rerun.
- Committed-head fresh-walk finding on `0cedd80`: the clone configured, built 92 targets, passed
  39/39, and rendered 90 validation-clean frames, then the new walker failed closed with access
  denied on Git's read-only pack files. The exact partial clone was retained for diagnosis.
- Read-only compatibility repair: each already-bound object is opened with write-attributes access;
  if its handle-reported attributes include `READONLY`, the walker clears only that bit with
  `FileBasicInfo` on the same handle before disposition. There is no path fallback. The repaired
  walker removed the preserved residue and parent verification proved the exact path absent. The
  permanent fixture includes a read-only nested file and a real descendant junction whose outside
  sentinel remains exact.
- S7 disposition: IN PROGRESS. Candidate `7a0d130` is superseded; no head is ready yet.

## NEXT

Publish the read-only compatibility repair, rerun the complete matrix, obtain both independent verdicts, require exact-head CI/CodeQL,
and make PR #51 ready only when green. Preserve simulation/replay encoding, asset work, #47-#50, and
unfinished S22 separately; stop before owner merge.
