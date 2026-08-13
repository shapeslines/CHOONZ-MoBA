---
title: "Interphase Security Consolidation Slate"
status: active
phase: interphase
branch: codex/security-consolidation
opened_base: 63f0b1dedad1c3f509843f68f32c24f6f52aa55e
base: dc380a1742b71e3337196aad8676fc80a6c60fc3
created: 2026-08-13
updated: 2026-08-13
---

# Interphase Security Consolidation Slate

## Goal

Land the approved security set from PR #27 and PRs #29-#46 through one coherent, reviewed exact-green
head before M4.0. Merged milestone PRs #35/#38 are base provenance, not source changes. PR #26
landed independently during final acceptance and is now base provenance too. PRs #47-#50 are
explicitly excluded and remain independent drafts.

## Loop contract

For every slice: state the observable done-condition, implement the smallest complete change, run
focused tests, repair failures without widening scope, update this slate and the provenance ledger,
commit/push the green checkpoint, and advance only on evidence.

## Gates

- [x] Owner authorized implementation and PR publication.
- [x] Isolated worktree created from synchronized `main` at `63f0b1de`.
- [x] Current `main` at `dc380a1` reconciled by merge commit; PR #26 report preserved unchanged.
- [x] Dirty root checkout and source branches left untouched.
- [x] Exact PR heads, files, and historical checks inventoried.
- [x] ARC compiler self-test passed: 1169 checks.
- [x] Static manifest validates and renders deterministically.
- [x] Untouched Debug baseline and M3.4 oracle captured.
- [x] Draft consolidation PR opened after first implementation slice.
- [x] Full local acceptance green on the final repaired head.
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

Status: complete and published at `216342d`. Predictable temporary files use create-only semantics and a collision test
proves destination and sentinel bytes remain unchanged. Vulkan loader discovery is isolated from the
window TU, restricted to System32 or a canonical existing drive-absolute `VULKAN_SDK` root, and its
path helper is transactional. Shader declarations require real in-root files and case-insensitively
unique output basenames; valid plus missing/directory/outside/duplicate/case-duplicate fixtures pass.
Runtime shader paths use a tested bounded formatter and return before file access on truncation. Dev
and `/WX` Debug/Release focused gates pass 4/4; full dev Debug passes 36/36; the real Vulkan sandbox
completed 90 validation-on frames and wrote a visually inspected 2,764,854-byte screenshot. Every
exact-head MSVC, clang-cl/UBSan, fresh-walk, and CodeQL check passed.

### S5 — CLI and tooling inputs

Goal: reconcile PRs #29/#33/#34/#36/#40.

Done means: every malformed input returns the documented exit class with no output/platform side
effect; log boundaries pass at 4 MiB and fail at 4 MiB plus one.

Status: complete and published at `e4fbd09`. Sandbox frame counts now accept only canonical positive decimal integers
and reject 14 malformed forms with exit 2 before DPI, window, or renderer activity. Replay numeric
options reject signed, spaced, trailing, leading-zero, overflowing, and out-of-range values with exit
1 and no output mutation. The test harness rejects seven missing/unknown selector cases before any
test runs, and the visualizer validates all three output paths before creating a file. Classifier
input is read through a bounded 4 MiB + 1 probe: exactly 4 MiB passes and 4 MiB + 1 fails, with all
15 adversarial cases represented. Dev Debug and `/WX` Debug/Release focused gates pass 7/7; the full
dev Debug suite passes 39/39. Every exact-head check is green. The first fresh-walk attempt failed
before code on transient winget community-source error `0x8a15000f`; its targeted retry completed the
SDK install and full walk successfully.

### S6 — Renderer capacity arithmetic

Goal: reconcile PRs #44/#45.

Done means: valid draws are unchanged; first/last/full/overflow-adjacent and corrupt-state cases fail
without item reads or count mutation.

Status: complete and published at `4ef49f3`. Debug-line and AABB capacity checks now use checked subtraction, and sphere
segment multiplication is guarded before evaluation. Exact first/last/full and forged
overflow-adjacent states are covered without count or vertex mutation. The null renderer rejects
over-capacity and corrupt draw counts before touching a deliberately unreadable item pointer, while
valid submissions remain unchanged. `render` passes 12 tests/113 checks and `render_null` passes 2
tests/35 checks; dev Debug and `/WX` Debug/Release focused gates pass 2/2; full dev Debug remains
39/39.

### S7 — Provenance and exact-head acceptance

Goal: account for every source hunk and prove the full permanent regression battery.

Done means: retained/repaired/superseded map is complete, all local gates pass, two fresh reviews pass,
and exact-head CI/CodeQL are green before ready state.

Status: repaired local acceptance complete at implementation checkpoint `6ae70c9`. Code checkpoint `f0a56c4` and documentation head
`7a0d130` passed the complete local/GitHub matrices and independent acceptance, but the fresh full
security review correctly found that only the cleanup root was object-bound: descendants were still
classified and recursively deleted by path. The walker now opens every current child with
`OPEN_REPARSE_POINT`, delete access, and no delete sharing before classifying from its handle; it
holds normal-directory handles through recursion, never traverses reparse points, and deletes every
node through its own verified handle. A post-child-validation hook attempts a rename plus
outside-sentinel junction replacement and proves the rename is blocked and sentinel unchanged.
The focused fixture and complete Debug suite passed.
The first committed-head fresh walk of `0cedd80` then configured, built 92 targets, passed 39/39, and
rendered 90 frames, but cleanup failed safely on Git's read-only pack files. A superseded repair
opened objects with write-attributes access, cleared only `READONLY` through the verified handle,
and then applied disposition without path fallback. The preserved partial
clone was removed by this repaired walker and verified absent. The fixture now also deletes a real
descendant junction as a leaf while preserving its outside sentinel.
On committed `6ae70c9`, all 39 CTest entries pass in Debug, RelWithDebInfo, Release, and Debug-ASan;
clang-cl 19.1.5 proves UBSan active and passes 6/6; Debug/Release direct determinism each pass four
tests and 178,690 checks; and the fresh walk configures, builds 92 targets, passes 39/39, renders 90
validation-clean frames with a 2,764,854-byte screenshot, removes read-only Git packs, and reports
`FRESH-WALK OK`.
After PR #26 advanced `main`, merge `227fc80` preserved its report as the sole base-tree addition;
the reconciled tree passes Debug 39/39 and `git diff --check`.

The first local-acceptance candidate (`a89caf6`) passed the `/WX`
Debug/RelWithDebInfo/Release builds, all 39 CTest entries in each configuration, Debug-ASan,
clang-cl/UBSan, determinism, fresh-walk, and local Vulkan gates. Its fresh security review correctly
rejected three remaining boundary gaps: a cleanup path-swap interval, missing corrupt-count defense
in the real renderer submission path, and fail-open sandbox/replay grammar. The renderer and CLI
repairs are now full-matrix green. Fresh-walk cleanup holds a non-delete-sharing handle on the verified
lease through child cleanup and deletes the empty root through that same handle; the adversarial test
attempts a directory move exactly after validation and proves it is blocked. On the final implementation head,
all three `/WX` builds and all 117 configuration test executions pass; Debug-ASan passes 39/39;
clang-cl 19.1.5 proves UBSan active and passes 6/6; direct Debug/Release determinism passes 4 tests
and 178,690 checks each; the committed-head fresh walk builds 92 targets, passes 39/39, renders 90
frames, and completes handle-bound cleanup. Two full passing reviews and exact-head GitHub checks
remain.

Fresh full security review of `78055bc` found three deeper object-binding gaps, so the PR stayed
draft despite its eight green GitHub checks and conditional acceptance. Cleanup handles still
allowed write sharing, permitting an in-place reparse conversion after validation; clearing
`READONLY` could mutate an outside hard link; and atomic writes closed their create-only temporary
before path-based commit. Root and child cleanup handles now allow `ShareRead` only, deletion uses
`FileDispositionInfoEx` with `IGNORE_READONLY`, and permanent fixtures issue a real
`FSCTL_SET_REPARSE_POINT` at the exact post-parent-validation interval and preserve an outside
read-only hard link. Platform writes retain the exclusive temporary handle through handle-based
rename or disposition and test both post-flush rename-away and replacement. At committed
implementation checkpoint `6716796`, `/WX` Debug/RelWithDebInfo/Release pass 39/39 each, Debug-ASan
passes 39/39, clang-cl/UBSan passes 6/6, direct Debug/Release determinism retains the exact oracle and
tick-4321 diagnostic, and the fresh walk builds 92 targets, passes 39/39, renders 90 validation-clean
frames with a 2,764,854-byte screenshot, and completes secure cleanup. Focused security rereview
passes. Fresh full exact-head reviews/checks remain before ready state.

Debug-ASan initially exposed that the visualizer negative fixture could not
start because its executable directory lacked the repository's app-local MSVC ASan runtime; the
shared staging helper is now applied there and the complete Debug-ASan suite passes 39/39. The
clang-cl/UBSan gate passes 6/6, and the unchanged direct oracle plus controlled-divergence proof pass.
A guarded fresh clone of `f0a56c4` builds 92 targets, passes 39/39, completes a classified 90-frame Vulkan
run, and removes its leased directory. A separate validation-on RTX 4070 Ti run produced a visually inspected 2,764,854-byte 1280x720
screenshot. All 17 approved source heads/paths were freshly reconciled; PR #26's independently
landed report is preserved through current `main`, excluded #47-#50 are absent, and no
sim/replay-codec/asset/logic-hash file changed.
All focused repair rereviews are green; both full reviews must pass on the final exact head before
ready state.

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

Publish the final evidence record, obtain fresh full security and acceptance verdicts,
require exact-head CI/CodeQL, and mark PR #51
ready only after all are green. Stop before the separate owner merge gate;
keep simulation/replay encoding, asset APIs, #47-#50, and unfinished S22 outside the branch.
