---
type: record
title: "SLATE - Phase 3 M3.4 structural determinism"
kind: slate
status: complete
project: moba
axis: "simulation build isolation"
queued: 2026-08-13
updated: 2026-08-13
worktree: "C:\\Users\\doton\\Desktop\\GITHUB\\MOBA-proto\\build\\m34-structural"
branch: "codex/m3.4-structural-determinism"
tags: [slate, simulation, build, determinism, isolation]
related:
  - "docs/ROADMAP.md M3.4"
  - "docs/slate-moba-phase3-m3.3.md"
  - "PR #28 / M3.3 acceptance repair"
---

# Slate - Phase 3 M3.4 structural determinism

**Status:** acceptance-complete on PR #38; its squash merge remains separately owner-approved.
Corrective PR #28 is squash-merged from exact green head `8cc42c1` as `b03f545`; docs closure PR #35
then advanced `main` to `11662ae` without engine changes. S0 captured that exact untouched baseline
before the M3.4 branch was created. Phase 4 implementation remains blocked until PR #38 merges and
synchronized `main` is green.

## Goal

Turn the already-behavioral determinism contract into one auditable build contract: central sim
compiler policy, identical game/test consumption of the sim binary, stronger dependency/source
isolation, and an explicit second-toolchain stretch proof. This milestone must not change gameplay,
replay encoding, tick order, or presentation behavior.

## Loop contract

Every slice states one observable done-condition, implements the smallest complete vertical change,
runs focused plus affected gates, fixes red before advancing, records evidence, and commits/pushes a
green checkpoint. Open a draft PR after the first implementation slice; mark it ready only after the
full local and exact-head GitHub gates pass. Merge remains separately owner-approved.

## Entry gate

- [x] PR #28 is squash-merged from recorded exact head `8cc42c1` as `b03f545`; local and origin
  `main` agree.
- [x] Post-merge Debug, RelWithDebInfo, Release, fresh-walk, and CodeQL checks are green.
- [x] Capture the untouched M3.3 10,000-tick oracle `0x637628abff59c823`, replay logic hash
  `0xab96814425ba80a4`, and all four current dependency seams before build changes.
- [x] Create a new M3.4 branch/worktree from that updated `main`; do not reuse the M3.3 branch.

## Invariants and fence

| May touch | Must not |
|-----------|----------|
| CMake compiler-policy targets/includes for deterministic code | authoritative sim schedule, commands, state, hash order, or replay layout |
| game/test executable parity harnesses | M4 asset implementation or Phase 5 gameplay |
| source/dependency/compile-command boundary checks | renderer, presentation, platform cadence behavior |
| clang-cl/UBSan configure and evidence scripts | force-pushes, rebases, or replacement of the MSVC required gate |

- `eng_sim` remains a platform-free static library with direct dependencies only on core, math, and
  serialize. Renderer and presentation remain downstream-only.
- One named CMake policy owns deterministic compile flags; no executable silently recompiles sim
  sources with different options.
- The test and runnable game paths consume the same `eng_sim` artifact and reproduce the same replay
  hash stream. Parity compares behavior, not object-file bytes or incidental linker layout.
- The existing MSVC Debug/RelWithDebInfo/Release matrix stays blocking. clang-cl/UBSan is a recorded
  stretch gate until toolchain support is proven; unsupported sanitizer combinations must report an
  explicit capability result rather than silently passing.
- `SIM_LOGIC_HASH` and replay format remain unchanged because build isolation is non-authoritative.
  Any observed behavior change stops the slate for review.

## Goal-loop slices

| ID | Slice | Observable done-condition | Status |
|----|-------|---------------------------|--------|
| S0 | Land and rebaseline | branch starts at merged PR #28 and untouched M3.3 matrix/oracle/boundaries are recorded | complete — exact `11662ae`; 28/28 in Debug, RelWithDebInfo, Release |
| S1 | Central compiler policy | one CMake-owned deterministic policy applies to every `eng_sim` source in every configuration, with a test that detects drift | complete — `moba_sim_determinism`; all 9 current sources x 3 configs audited |
| S2 | Binary-path parity | test and runnable game paths consume the same sim library and replay the same commands to the exact M3.3 hash stream | complete — probe + Vulkan/null sandboxes match the pinned stream digest |
| S3 | Strong isolation lint | source, include, link, compile-command, and accidental-source-recompile checks fail on forbidden sim/platform/render/presentation coupling | complete - configure contract + generated-build/source scans fail closed |
| S4 | Second-toolchain stretch | clang-cl config builds the oracle and UBSan runs where supported, with exact capability/evidence recorded | complete - clang-cl 19.1.5 + UBSan oracle/tripwire green |
| S5 | Close M3.4 | `/WX` matrix, ASan, parity, boundary scans, fresh walk, docs, independent acceptance, and exact-head GitHub gates are green | complete - all evidence green; exact-head checks and acceptance verdict are retained on PR #38 |

## S0 evidence

- Exact source baseline: `11662aee80c522cf7bf3ba071c858f386ca8a290`; local `main` and
  `origin/main` matched after a fresh fetch, with a clean worktree.
- `/WX` `build-ci` Debug, RelWithDebInfo, and Release each built successfully and passed all 28
  CTest entries. The shell was initialized through `vcvars64.bat`; an initial invocation without the
  Developer environment could find `cl.exe` but correctly failed to find the MSVC standard headers.
- `sim_determinism_tests --suite sim_determinism` passed 4 tests and 178,690 checks. It replayed
  10,000 ticks to `0x637628abff59c823` and reported the controlled first divergence as exactly
  `tick=4321 field=position_x entity=7`.
- `SIM_LOGIC_HASH` remains `0xab96814425ba80a4`.
- Direct seams before policy changes: `eng_sim -> core + math + serialize`; `eng_game -> core + math
  + sim + render_common`; renderer source is sim-free; platform fixed-step code has no game/sim
  include or link dependency. The focused scripts reported 17 sim files and 2 game files clean.
- The phase manifest at `docs/arc-m3.4-manifest.json` passed the ARC compiler self-test (1,169
  checks), schema validation, and deterministic render check before implementation began.

## S1 evidence

- `cmake/EngineOptions.cmake` is the sole owner of the named `moba_sim_determinism` policy. It emits
  `MOBA_SIM_DETERMINISTIC=1` and pins MSVC/clang-cl-compatible builds to `/fp:precise`; `eng_sim`
  consumes that policy privately and no longer owns a target-local floating-point option.
- `sim_compiler_policy` reads the generated `compile_commands.json` and proved all 8 authoritative
  sim sources have exactly one compile entry in each of Debug, RelWithDebInfo, and Release, all under
  `eng_sim`, all carrying the marker and exactly one `/fp:precise` option.
- `sim_compiler_policy_selftest` proved the check fails closed for a missing policy marker, a
  contradictory `/fp:fast`, and duplicate `/fp:precise` plus `/fp:fast` options.
- Focused `/WX` builds and the four-test determinism/policy/boundary set passed in Debug,
  RelWithDebInfo, and Release. The complete Debug build then passed 30/30 CTest entries.
- The 10,000-tick oracle remains `0x637628abff59c823`; `SIM_LOGIC_HASH` remains
  `0xab96814425ba80a4`. No authoritative simulation source or replay layout changed.

## S2 evidence

- `sim_oracle_run` is a platform-free `eng_sim` function that takes caller-owned fixed storage and
  runs the unchanged seed-1, two-player, 10,000-tick placeholder command stream. It observed 923
  commands, final state hash `0x637628abff59c823`, and little-endian hash-stream digest
  `0x6f381609f7e59f0c` under unchanged logic hash `0xab96814425ba80a4`.
- The direct `sim_oracle_probe`, the Vulkan-linked `sandbox`, and `sandbox_null` all call that same
  archive function through `--sim-self-check`, before window or renderer initialization. Their exact
  outputs matched in Debug, RelWithDebInfo, and Release.
- `sim_binary_parity` pins tick count, command count, final hash, stream digest, and logic hash while
  comparing all three outputs byte-for-byte. The generated compiler-policy check expanded
  automatically from 8 to all 9 current sim sources and still proves only `eng_sim` compiles them.
- The five-test determinism/parity/policy/boundary set passed in all three `/WX` configurations; the
  complete Debug build passed 31/31 CTest entries. No replay bytes, schedule, command, or state hash
  ordering changed.

## S3 evidence

- `moba_enforce_sim_target` executes during CMake configure and permits only `src/*.cpp`
  implementation files, the sim public/private include roots, the `core + math + serialize` public
  link seam, and the three private policy targets represented as static-library `LINK_ONLY` entries.
  Unexpected direct or exported links fail configuration before compilation.
- `sim_compiler_policy` now audits each actual sim compile command's include paths in addition to
  marker, floating-point option, configuration, source count, and object owner. Only sim, core,
  math, and serialize include roots are allowed; every sim implementation source must still compile
  exactly once per configuration and only into `eng_sim`.
- `sim_boundary` rejects platform, render, game/presentation, Vulkan, floating-point, wall-clock,
  unordered-container, and heap imports/usages across all sim headers and sources. Its diagnostics
  include the offending file and complete include token.
- Negative fixtures proved rejection of a forbidden implementation source, direct include root,
  direct link, exported link, source import, policy marker removal, conflicting/duplicate FP option,
  compile-command include injection, and accidental compilation of sim sources by `sandbox`.
- The five-test isolation/parity set passed in Debug, RelWithDebInfo, and Release after affected
  targets built with `/WX`. The complete Debug suite passed 32/32. The canonical oracle and logic
  hash remain unchanged.

## S4 evidence

- Visual Studio's installed clang-cl reported version 19.1.5 targeting
  `x86_64-pc-windows-msvc`. The capability probe compiled, linked, and executed an intentional
  signed-overflow tripwire; it exited nonzero with the expected UBSan runtime diagnostic.
- The first fail-closed runs exposed two real Windows toolchain constraints before the oracle could
  pass: clang-cl diagnoses MSVC-only `/Zc:preprocessor` as unused under `/WX`, and the standalone
  UBSan C++ runtime requires the static release CRT. The final policy keeps `/Zc:preprocessor` on
  cl.exe only and selects `/MT` only when `MOBA_UBSAN=ON`; normal MSVC builds retain their ABI.
- The capability-aware script performs a fresh CMake configure in a dedicated Ninja Multi-Config
  tree with clang-cl, `/WX`, the
  named `/fp:precise` sim policy, no RTTI/exceptions, and UBSan when the compile/link/runtime probe
  succeeds. Unsupported installations report an explicit `UNAVAILABLE`/`ubsan=off` classification;
  `-RequireCompiler -RequireUbsan` turns either condition into a hard local failure.
- The instrumented RelWithDebInfo oracle completed without a sanitizer diagnostic and emitted the
  unchanged 10,000-tick line: 923 commands, final `0x637628abff59c823`, stream
  `0x6f381609f7e59f0c`, logic `0xab96814425ba80a4`. The UBSan oracle/tripwire plus four structural
  policy/isolation tests passed 6/6.

## S5 closure evidence (in progress)

- Exact head `0a29f12807d27c3d11b620146e2ba08ec69756bb` completed full `/WX` Debug,
  RelWithDebInfo, and Release builds; each configuration passed all 32 CTest entries.
- The first Debug-ASan pass exposed missing app-local runtime staging for the two sandbox binaries:
  the game parity leg exited `0xc0000135` while the test probe succeeded. Adding the same
  `moba_stage_asan_runtime()` contract already used by tests and replay made the complete ASan suite
  pass 32/32, including binary parity and replay fixtures.
- The local Vulkan sandbox ran exactly 90 frames with validation enabled on an NVIDIA GeForce RTX
  4070 Ti, exited cleanly, and produced a 2,764,854-byte `1280x720` BMP accepted by the strict smoke
  classifier. No validation warning or error was logged.
- A clean clone of checkpoint `8dcd385` configured and built from the documented path, passed 32/32,
  repeated the 90-frame strict screenshot contract, and remained clean. Hosted CI run `31675675948`
  passed Debug/RelWithDebInfo/Release, clang-cl/UBSan capability, and fresh-walk; CodeQL run
  `31675672904` passed C/C++ and workflow analysis on the same exact head.
- A final scope audit hardened relative-source traversal and `/external:I`/`-isystem`/`-imsvc`
  include spellings; the expanded negative fixtures pass under MSVC and clang-cl. ROADMAP,
  architecture, README, journal, next-session, ADR-0002, and this slate now reflect the observed
  closure evidence.
- Fresh-context read-only acceptance verified the compiler policy, three binary paths, generated
  isolation evidence, clang-cl/UBSan result, local/hosted matrix, unchanged authoritative behavior,
  and Phase 4 fence. Its sole P1 finding was this slate's stale `active/pending` wording; that
  contradiction is repaired here. Fresh-context PASS on the final pushed head remains a PR-readiness
  control, with the exact verdict retained on PR #38 rather than encoded as mutable source state.

## Exit gate

M3.4 acceptance is complete because deterministic flags have one owner, game/test paths prove the
same M3.3 oracle through the same sim artifact, isolation drift is mechanically rejected, and all
blocking MSVC plus stretch gates are green. PR merge remains separately owner-approved. Phase 4 may
open only from synchronized `main` after that merge and green post-merge checks.
