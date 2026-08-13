# Gap-close ledger â€” 40-gap sweep (2026-08-12)

Tracks the multi-wave effort closing the 40 gaps found in the 2026-08-12 multi-lens gap
analysis. One row per gap; status: `open` / `in-progress` / `closed` / `partial` /
`deferred` (with owner phase). The analysis itself lives in the session record; this file
is the execution ledger.

## Decisions taken (user-approved)

- LICENSE: **proprietary / all rights reserved** (G2).
- Vulkan in CI: **yes â€” install the LunarG SDK on the runner** (G15).
- Sequencing: **gap waves land before M3.3 starts**.
- G4 branch retirement: temporarily bypass the `safety` ruleset (authorized 2026-08-12).
- G19: enforced via ruleset, CODEOWNERS skipped (repo is solo).

## Wave 0 â€” owner gates (complete 2026-08-12)

- PR #17 (M3.2) squash-merged as `d9f5230`; local + origin `main` updated.
- Tag `v0.3.0-m3.2` pushed.
- Retired branches deleted: `moba/slate-2026-08-11`, `moba/slate-phase3-determinism`,
  `moba/slate-phase3-ecs`, `moba/slate-phase3-systems` (content verified fully merged).
- Rulesets: `main-gate` (active, required checks `windows-msvc (Debug)` + `(Release)`)
  created; `safety` (deletion + non-fast-forward) restored after recreation.
- **Incident (logged):** the original `safety` ruleset (id 20581937) was briefly absent
  during a delete-and-recreate bypass â€” a name collision blocked the replacement POST
  before the old ruleset's DELETE ran. Restored byte-identical (id 20777653) within the
  same command sequence; nothing was pushed in the window. The `deletion` rule then
  blocked nothing further: all four refs were removed. Lesson: `if ($?)`-guard destructive
  sequences.
- Untouched: `origin/gapfix/2026-08-12/env-ignore` â€” a concurrent session's branch (its
  content landed via PR #16); not part of this sweep.

## PR log

| Wave | Branch | PR | Merged as | Notes |
|---|---|---|---|---|
| â€” | `moba/gap-close` | â€” | â€” | worktree ledger base |

## Gap status

### Lens 1 â€” Release & project state

| # | Gap | Status |
|---|---|---|
| G1 | PR #17 (M3.2) unmerged; docs claim completion main lacks | **closed** (merged `d9f5230`, Wave 0) |
| G2 | No LICENSE (public repo) | **closed** (proprietary LICENSE, Wave 1) |
| G3 | No tags/releases pinning milestones | **closed** (`v0.3.0-m3.2`, Wave 0; convention noted in Wave 1 docs) |
| G4 | Retired branches left on origin | **closed** (deleted, Wave 0) |

### Lens 2 â€” Documentation consistency

| # | Gap | Status |
|---|---|---|
| G5 | `docs/ARCHITECTURE.md:120` misnumbers fixed-point ADR as 0007 (is 0002) | **closed** (Wave 1) |
| G6 | Root ARCHITECTURE.md map stale (ADR-index note; status) | **closed** (Wave 1) |
| G7 | ROADMAP Phase 8 table still lists CI as untriggered | **closed** (Wave 1) |
| G8 | `docs/sessions/` lacks retrospectives for sessions 03â€“09 | **closed** (Wave 1: 03, 06â€“09 backfilled; 04â€“05 already existed) |
| G9 | ROADMAP M2.0/M2.1 lack `Status:` convention | **closed** (Wave 1) |

### Lens 3 â€” Build & toolchain

| # | Gap | Status |
|---|---|---|
| G10 | No Debug-ASan preset; arena poison hooks stubbed | open |
| G11 | clang-cl/UBSan determinism run never done | open (deferred: optional stretch; re-trigger at M3.4) |
| G12 | Vulkan SDK version unpinned | open |
| G13 | `/fp` pinning for eng_sim (M3.4) | open (owned by M3.4 slate) |
| G14 | `plat_mem_*` still in Win32 window TU | open |

### Lens 4 â€” CI & quality gates

| # | Gap | Status |
|---|---|---|
| G15 | CI builds null renderer only; no Vulkan/validation in CI | open |
| G16 | RelWithDebInfo absent from CI matrix | open |
| G17 | CodeQL claimed-but-absent workflow | open (checks ran via GitHub default setup; workflow not committed) |
| G18 | No perf/benchmark regression gate | open |
| G19 | No enforced merge policy | **closed** (`main-gate` ruleset, Wave 0) |
| G40 | No `.editorconfig` / templates | **closed** (Wave 1: .editorconfig + issue/PR templates) |

### Lens 5 â€” Determinism contract

| # | Gap | Status |
|---|---|---|
| G20 | Cross-binary parity unproven | open (owned by M6.7) |
| G21 | Hash cost at capacity never profiled | open (perf-smoke ceiling, Wave 2-B) |
| G22 | RNG draw policy undocumented | open |
| G23 | Replay 64-slot cap migration undocumented | open |
| G24 | Pool-hash inclusion guard missing | open |

### Lens 6 â€” Renderer / Vulkan

| # | Gap | Status |
|---|---|---|
| G25 | Dedicated-allocator cap is abort-first; pressure warning missing | open |
| G26 | Hardware coverage NVIDIA-only; no IGP/AMD protocol | open |
| G27 | Culling has no owning milestone | open |
| G28 | No DPI awareness | open |
| G29 | Pipeline-cache defense is size-bound only | open |

### Lens 7 â€” Simulation / ECS

| # | Gap | Status |
|---|---|---|
| G30 | Stale-target policy undefined | **closed** (Wave 5: ADR-0014 — atomic reject today, per-command + reason codes at M6.0) |
| G31 | Event backpressure policy unspecified | **closed** (Wave 5: ADR-0014 — reject at source, never drop) |
| G32 | No capacity-pressure test (16,384 entities) | open |
| G33 | No command rejection-reason field | **closed** (Wave 5: ADR-0014 reserves it for M6.0) |

### Lens 8 â€” Robustness & security

| # | Gap | Status |
|---|---|---|
| G34 | No fuzz infrastructure | open (deferred: M4.2 inflate / M6.1 transport) |
| G35 | No hardening-flag audit (`/guard:cf`) | open |
| G36 | Fresh-clone sweep is manual prose | open |

### Lens 9 â€” Ownership & architecture

| # | Gap | Status |
|---|---|---|
| G37 | Present glue has no home (`moba_game` absent) | open (M3.3 amendment) |
| G38 | Raw Input upgrade lost its owner | **closed** (Wave 5: owned by M5.2 — selection/commands need it) |
| G39 | `tga_direct` contamination path interim policy | **closed** (Wave 5: M4.0 moves it into `eng_assets`) |
| G40 | No `.editorconfig` / templates | open |
