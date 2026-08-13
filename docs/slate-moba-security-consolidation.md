---
title: "Interphase Security Consolidation Slate"
status: active
phase: interphase
branch: codex/security-consolidation
base: 63f0b1dedad1c3f509843f68f32c24f6f52aa55e
created: 2026-08-13
updated: 2026-08-13
---

# Interphase Security Consolidation Slate

## Goal

Land the approved security set from PR #27 and PRs #29-#46 through one coherent, reviewed exact-green
head before M4.0. Merged milestone PRs #35/#38 are base provenance, not source changes. PRs #47-#49
are explicitly excluded and remain independent drafts.

## Loop contract

For every slice: state the observable done-condition, implement the smallest complete change, run
focused tests, repair failures without widening scope, update this slate and the provenance ledger,
commit/push the green checkpoint, and advance only on evidence.

## Gates

- [x] Owner authorized implementation and PR publication.
- [x] Isolated worktree created from synchronized `main` at `63f0b1de`.
- [x] Dirty root checkout and source branches left untouched.
- [x] Exact PR heads, files, and historical checks inventoried.
- [x] ARC compiler self-test passed: 1169 checks.
- [x] Static manifest validates and renders deterministically.
- [x] Untouched Debug baseline and M3.4 oracle captured.
- [x] Draft consolidation PR opened after first implementation slice.
- [ ] Full local acceptance green.
- [ ] Fresh security and independent acceptance verdicts green.
- [ ] Exact-head CI and CodeQL green; PR ready.
- [ ] Separate owner merge approval.
- [ ] Tree-identical landing, synchronized green `main`, and stale-PR retirement.

## Slices

### S1 — Ownership, provenance, and baseline

Goal: establish one writer and a reproducible starting point.

Done means: clean isolated worktree, complete provenance ledger, valid/renderable manifest, green
Debug suite, and unchanged final oracle `0x637628abff59c823`.

Status: complete. Debug built 88 targets; 32/32 CTests passed. The direct probe reported
`ticks=10000 commands=923 final=0x637628abff59c823 stream=0x6f381609f7e59f0c
logic=0xab96814425ba80a4`; the focused determinism suite passed 4 tests and 178,690 checks with the
controlled divergence at tick 4321, `position_x`, entity 7.

### S2 — CI recovery and fresh-walk

Goal: add manual recovery and fail-closed cleanup.

Done means: unsafe paths fail before mutation; push/manual workflow contracts, all checkout pins, and
explicit winget source are tested; full fresh-walk passes; draft PR is open.

Status: complete. PowerShell parse checks pass. The focused CTest
gate passes 2/2 and covers system root, temp root, source/outside root, nested directory, file, and
junction rejection without sentinel mutation. The first full walk exposed and then drove a repair for
a source checkout that itself lives as a direct temp child. The corrected exact commit completed a
fresh 88-target Debug build, 34/34 CTests, and a 90-frame validation-clean Vulkan run with a
2,764,854-byte screenshot. Workflow contract coverage proves push, pull request, manual dispatch,
global `contents: read`, all three verified checkout pins, and both explicit winget sources. Commit
`0ed4b6e919b48ced0f8f018487ff64ea62696141` is pushed; draft PR #51 is open and mergeable, with
exact-head MSVC Debug/RelWithDebInfo/Release, clang-cl/UBSan, fresh-walk, and CodeQL green.

### S3 — Core memory and containers

Goal: reconcile PRs #41/#42/#43/#46 and add no-mutation coverage.

Done means: normal order is unchanged and every overflow/corruption follows its specified failure path.

Status: complete locally. The accepted PR #41/#42/#43/#46 guards are retained and extended so Array
and HashMap allocation-byte multiplication, arena initialization and all pointer/end/rounding
arithmetic, and intrusive free-list state fail before mutation. Existing container APIs and normal
fresh/LIFO allocation order are unchanged. Persistent tests cover 13 fatal invariant diagnostics plus
mutation-free Pool/HandlePool null storage, full/overfull counts, invalid heads/links, missing
generation storage, and invalid generation state. Dev and `/WX` Debug/Release focused gates pass 3/3;
the complete dev Debug suite passes 35/35.

### S4 — Platform and shader boundaries

Goal: reconcile PRs #31/#32/#37/#39.

Done means: temporary collisions and malformed loader/shader paths fail closed while normal offline
SPIR-V and Vulkan paths remain green.

Status: complete locally. Predictable temporary files use create-only semantics and a collision test
proves destination and sentinel bytes remain unchanged. Vulkan loader discovery is isolated from the
window TU, restricted to System32 or a canonical existing drive-absolute `VULKAN_SDK` root, and its
path helper is transactional. Shader declarations require real in-root files and case-insensitively
unique output basenames; valid plus missing/directory/outside/duplicate/case-duplicate fixtures pass.
Runtime shader paths use a tested bounded formatter and return before file access on truncation. Dev
and `/WX` Debug/Release focused gates pass 4/4; full dev Debug passes 36/36; the real Vulkan sandbox
completed 90 validation-on frames and wrote a visually inspected 2,764,854-byte screenshot.

### S5 — CLI and tooling inputs

Goal: reconcile PRs #29/#33/#34/#36/#40.

Done means: every malformed input returns the documented exit class with no output/platform side
effect; log boundaries pass at 4 MiB and fail at 4 MiB plus one.

Status: pending.

### S6 — Renderer capacity arithmetic

Goal: reconcile PRs #44/#45.

Done means: valid draws are unchanged; first/last/full/overflow-adjacent and corrupt-state cases fail
without item reads or count mutation.

Status: pending.

### S7 — Provenance and exact-head acceptance

Goal: account for every source hunk and prove the full permanent regression battery.

Done means: retained/repaired/superseded map is complete, all local gates pass, two fresh reviews pass,
and exact-head CI/CodeQL are green before ready state.

Status: pending.

### S8 — Owner-gated landing and retirement

Goal: land only the accepted head and retire only the named stale drafts.

Done means: separate approval is recorded, squash tree equals accepted tree, `main` is synchronized and
green through automatic push or explicit manual recovery, and supersession comments name the landing
SHA while branches/worktrees remain.

Status: blocked on future owner gate by design.

## Permanent sentinels

- Final oracle: `0x637628abff59c823`.
- Stream oracle: `0x6f381609f7e59f0c`.
- Logic hash: `0xab96814425ba80a4`.
- Injected divergence: `tick=4321 field=position_x entity=7`.
- No asset API or M4.0 implementation in this branch.

## NEXT

Reconcile PR #29/#33/#34/#36/#40 into the CLI/tooling slice; keep replay encoding, asset APIs,
PRs #47-#50, and unfinished S22 outside the branch.
