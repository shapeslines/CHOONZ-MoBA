---
type: record
title: "SLATE - Phase 3 M3.4 structural determinism"
kind: slate
status: active
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

**Status:** active. Corrective PR #28 is squash-merged from exact green head `8cc42c1` as `b03f545`;
docs closure PR #35 then advanced `main` to `11662ae` without engine changes. S0 captured that exact
untouched baseline before the M3.4 branch was created. Phase 4 remains blocked until this slate closes.

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
| S3 | Strong isolation lint | source, include, link, compile-command, and accidental-source-recompile checks fail on forbidden sim/platform/render/presentation coupling | queued |
| S4 | Second-toolchain stretch | clang-cl config builds the oracle and UBSan runs where supported, with exact capability/evidence recorded | queued |
| S5 | Close M3.4 | `/WX` matrix, ASan, parity, boundary scans, fresh walk, docs, independent acceptance, and exact-head GitHub gates are green | queued |

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

## Exit gate

M3.4 closes only when deterministic flags have one owner, game/test paths prove the same M3.3 oracle
through the same sim artifact, isolation drift is mechanically rejected, and all blocking MSVC gates
remain green. The clang-cl/UBSan result must be explicit and reproducible. Only then may Phase 4 open.
