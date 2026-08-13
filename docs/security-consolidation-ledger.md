# Security consolidation provenance ledger

This is the tracked execution/provenance record for `codex/security-consolidation`. Source pull
requests remain immutable audit inputs. Each final source hunk will be marked `retained`, `repaired`,
or `intentionally superseded` before the consolidated PR becomes ready.

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

In progress. This section must account for every source diff before ready state.

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
- S3 disposition: PASS pending checkpoint publication.

## NEXT

Reconcile PRs #31/#32/#37/#39 only; preserve asset work, PRs #47-#50, and unfinished S22 separately.
