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
- Explicitly excluded and retained: PR #47 (`e3271bf`), PR #48 (`714e01c`), and PR #49
  (`c187dc7`). They were opened after the approved consolidation boundary and are not retirement
  targets. PR #49 changes only `tools/sandbox/src/tga_direct.cpp`; all its checks are green.

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

Pending implementation. This section must account for every source diff before ready state.

## NEXT

Checkpoint S1, then reconcile PR #27 and PR #30 only.
