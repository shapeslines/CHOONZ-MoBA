# Gap-close ledger Ã¢â‚¬â€ 40-gap sweep (2026-08-12)

Tracks the multi-wave effort closing the 40 gaps found in the 2026-08-12 multi-lens gap
analysis. One row per gap; status: `open` / `in-progress` / `closed` / `partial` /
`deferred` (with owner phase). The analysis itself lives in the session record; this file
is the execution ledger.

## Decisions taken (user-approved)

- LICENSE: **proprietary / all rights reserved** (G2).
- Vulkan in CI: **yes Ã¢â‚¬â€ install the LunarG SDK on the runner** (G15).
- Sequencing: **gap waves land before M3.3 starts**.
- G4 branch retirement: temporarily bypass the `safety` ruleset (authorized 2026-08-12).
- G19: enforced via ruleset, CODEOWNERS skipped (repo is solo).

## Wave 0 Ã¢â‚¬â€ owner gates (complete 2026-08-12)

- PR #17 (M3.2) squash-merged as `d9f5230`; local + origin `main` updated.
- Tag `v0.3.0-m3.2` pushed.
- Retired branches deleted: `moba/slate-2026-08-11`, `moba/slate-phase3-determinism`,
  `moba/slate-phase3-ecs`, `moba/slate-phase3-systems` (content verified fully merged).
- Rulesets: `main-gate` (active, required checks `windows-msvc (Debug)` + `(Release)`)
  created; `safety` (deletion + non-fast-forward) restored after recreation.
- **Incident (logged):** the original `safety` ruleset (id 20581937) was briefly absent
  during a delete-and-recreate bypass Ã¢â‚¬â€ a name collision blocked the replacement POST
  before the old ruleset's DELETE ran. Restored byte-identical (id 20777653) within the
  same command sequence; nothing was pushed in the window. The `deletion` rule then
  blocked nothing further: all four refs were removed. Lesson: `if ($?)`-guard destructive
  sequences.
- Untouched: `origin/gapfix/2026-08-12/env-ignore` Ã¢â‚¬â€ a concurrent session's branch (its
  content landed via PR #16); not part of this sweep.

## PR log

| Wave | Branch | PR | Merged as | Notes |
|---|---|---|---|---|
| Ã¢â‚¬â€ | `moba/gap-close` | Ã¢â‚¬â€ | Ã¢â‚¬â€ | worktree ledger base |
| 1 | `moba/gap-close-w1` | #18 | `bf81e64` | merged 2026-08-13 |
| 2a | `moba/gap-close-w2a` | #19 | `e85ea82` | merged 2026-08-13 |
| 2b | `moba/gap-close-w2b` | #20 / #25 | Ã¢â‚¬â€ | #20 closed; recovery #25 superseded Ã¢â‚¬â€ Wave 2b content lands through #21 |
| 3 | `moba/gap-close-w3` | #21 | `8defa10` | merged 2026-08-13; includes Wave 2b except the CMake hard-require gate restored in #22 |
| 4 | moba/gap-close-w4 | #22 | `4d250b2` | merged 2026-08-13 (owner); renderer robustness plus recovery of the Wave 2b MOBA_VULKAN_REQUIRED CMake gate |
| 5 | moba/gap-close-w5 | #23 | `ab774ed` | merged 2026-08-13; ADR-0014, ownership amendments (wave-6 content stripped from the branch before merge) |
| 6 | moba/gap-close-w6 | #24 | `ca5ad22` | merged 2026-08-13; M3.3 complete (eng_game present glue) |

## Gap status

### Lens 1 Ã¢â‚¬â€ Release & project state

| # | Gap | Status |
|---|---|---|
| G1 | PR #17 (M3.2) unmerged; docs claim completion main lacks | **closed** (merged `d9f5230`, Wave 0) |
| G2 | No LICENSE (public repo) | **closed** (proprietary LICENSE, Wave 1) |
| G3 | No tags/releases pinning milestones | **closed** (`v0.3.0-m3.2`, Wave 0; convention noted in Wave 1 docs) |
| G4 | Retired branches left on origin | **closed** (deleted, Wave 0) |

### Lens 2 Ã¢â‚¬â€ Documentation consistency

| # | Gap | Status |
|---|---|---|
| G5 | `docs/ARCHITECTURE.md:120` misnumbers fixed-point ADR as 0007 (is 0002) | **closed** (Wave 1) |
| G6 | Root ARCHITECTURE.md map stale (ADR-index note; status) | **closed** (Wave 1) |
| G7 | ROADMAP Phase 8 table still lists CI as untriggered | **closed** (Wave 1) |
| G8 | `docs/sessions/` lacks retrospectives for sessions 03Ã¢â‚¬â€œ09 | **closed** (Wave 1: 03, 06Ã¢â‚¬â€œ09 backfilled; 04Ã¢â‚¬â€œ05 already existed) |
| G9 | ROADMAP M2.0/M2.1 lack `Status:` convention | **closed** (Wave 1) |

### Lens 3 Ã¢â‚¬â€ Build & toolchain

| # | Gap | Status |
|---|---|---|
| G10 | No Debug-ASan preset; arena poison hooks stubbed | open |
| G11 | clang-cl/UBSan determinism run never done | open (deferred: optional stretch; re-trigger at M3.4) |
| G12 | Vulkan SDK version unpinned | open |
| G13 | `/fp` pinning for eng_sim (M3.4) | open (owned by M3.4 slate) |
| G14 | `plat_mem_*` still in Win32 window TU | **closed** (Wave 3: win32_mem/file/log TUs; engine_tests proven user32-free) |

### Lens 4 Ã¢â‚¬â€ CI & quality gates

| # | Gap | Status |
|---|---|---|
| G15 | CI builds null renderer only; no Vulkan/validation in CI | open |
| G16 | RelWithDebInfo absent from CI matrix | open |
| G17 | CodeQL claimed-but-absent workflow | open (checks ran via GitHub default setup; workflow not committed) |
| G18 | No perf/benchmark regression gate | open |
| G19 | No enforced merge policy | **closed** (`main-gate` ruleset, Wave 0) |
| G40 | No `.editorconfig` / templates | **closed** (Wave 1: .editorconfig + issue/PR templates) |

### Lens 5 Ã¢â‚¬â€ Determinism contract

| # | Gap | Status |
|---|---|---|
| G20 | Cross-binary parity unproven | open (owned by M6.7) |
| G21 | Hash cost at capacity never profiled | **closed** (Wave 2-B perf ceiling + Wave 3 capacity test) |
| G22 | RNG draw policy undocumented | **closed** (Wave 3: ADR-0013 â€” fixed per-tick script, positional draws) |
| G23 | Replay 64-slot cap migration undocumented | **closed** (Wave 3: sim.h comment + M5.2 roadmap note) |
| G24 | Pool-hash inclusion guard missing | **closed** (Wave 3: every SimStateField enum member proven hash-sensitive by test) |

### Lens 6 Ã¢â‚¬â€ Renderer / Vulkan

| # | Gap | Status |
|---|---|---|
| G25 | Dedicated-allocator cap is abort-first; pressure warning missing | **closed** (Wave 4: warning at 90% of cap + every 100; Phase 8 trigger text refined) |
| G26 | Hardware coverage NVIDIA-only; no IGP/AMD protocol | **closed** (Wave 4: `docs/testing-hardware.md` protocol + results table) |
| G27 | Culling has no owning milestone | **closed** (Wave 4: frustum culling owned by M4.3) |
| G28 | No DPI awareness | **closed** (Wave 4: SYSTEM_AWARE in sandbox; PER_MONITOR_V2 noted as follow-up) |
| G29 | Pipeline-cache defense is size-bound only | **closed** (Wave 4: every header byte load-bearing + truncation/garbage cases) |

### Lens 7 Ã¢â‚¬â€ Simulation / ECS

| # | Gap | Status |
|---|---|---|
| G30 | Stale-target policy undefined | open (decision before M3.3) |
| G31 | Event backpressure policy unspecified | open (decision before M3.3) |
| G32 | No capacity-pressure test (16,384 entities) | **closed** (Wave 3: twin-run + churn test) |
| G33 | No command rejection-reason field | open (design note for M6.0) |

### Lens 8 Ã¢â‚¬â€ Robustness & security

| # | Gap | Status |
|---|---|---|
| G34 | No fuzz infrastructure | open (deferred: M4.2 inflate / M6.1 transport) |
| G35 | No hardening-flag audit (`/guard:cf`) | open |
| G36 | Fresh-clone sweep is manual prose | open |

### Lens 9 Ã¢â‚¬â€ Ownership & architecture

| # | Gap | Status |
|---|---|---|
| G37 | Present glue has no home (`moba_game` absent) | open (M3.3 amendment) |
| G38 | Raw Input upgrade lost its owner | open (M5.2 amendment) |
| G39 | `tga_direct` contamination path interim policy | open (M4.0 amendment) |
| G40 | No `.editorconfig` / templates | open |

## Session close (2026-08-13, all waves merged)

- #18 wave 1 -> `bf81e64`; #19 wave 2a -> `e85ea82`; #21 waves 2b+3 -> `8defa10`; #22 wave 4 -> `4d250b2`; #23 wave 5 -> `ab774ed`; #24 wave 6 (M3.3) -> `ca5ad22`.
- #20 auto-closed and #25 superseded (stacked-base deletion); wave 2b landed through #21 per owner plan.
- main-gate ruleset now requires Debug + RelWithDebInfo + Release (id 20780727); safety ruleset intact (id 20777653).
- Final state verified locally: ci preset 26/26 (Debug + Release), ASan preset 25/25, fresh-walk FRESH-WALK OK, sandbox validation-clean on RTX 4070 Ti.
- Incident log: SDK install root, pwsh redirect vs EAP, runner has no Vulkan driver, --theirs inversion, UTF-16/BOM churn from shell redirects, stacked-base deletion, ruleset PATCH 404 quirk, marker files committed by git add -A, and the owner's concurrent co-edits (credited in the PRs).
