# Gap-close ledger — 40-gap sweep (2026-08-12)

Tracks the multi-wave effort closing the 40 gaps found in the 2026-08-12 multi-lens gap
analysis. One row per gap; status: `open` / `in-progress` / `closed` / `partial` /
`deferred` (with owner phase). The analysis itself lives in the session record; this file
is the execution ledger.

## Decisions taken (user-approved)

- LICENSE: **proprietary / all rights reserved** (G2).
- Vulkan in CI: **yes — install the LunarG SDK on the runner** (G15).
- Sequencing: **gap waves land before M3.3 starts**.
- G4 branch retirement: temporarily bypass the `safety` ruleset (authorized 2026-08-12).
- G19: enforced via ruleset, CODEOWNERS skipped (repo is solo).

## Wave 0 — owner gates (complete 2026-08-12)

- PR #17 (M3.2) squash-merged as `d9f5230`; local + origin `main` updated.
- Tag `v0.3.0-m3.2` pushed.
- Retired branches deleted: `moba/slate-2026-08-11`, `moba/slate-phase3-determinism`,
  `moba/slate-phase3-ecs`, `moba/slate-phase3-systems` (content verified fully merged).
- Rulesets: `main-gate` (active, required checks `windows-msvc (Debug)` + `(Release)`)
  created; `safety` (deletion + non-fast-forward) restored after recreation.
- **Incident (logged):** the original `safety` ruleset (id 20581937) was briefly absent
  during a delete-and-recreate bypass — a name collision blocked the replacement POST
  before the old ruleset's DELETE ran. Restored byte-identical (id 20777653) within the
  same command sequence; nothing was pushed in the window. The `deletion` rule then
  blocked nothing further: all four refs were removed. Lesson: `if ($?)`-guard destructive
  sequences.
- Untouched: `origin/gapfix/2026-08-12/env-ignore` — a concurrent session's branch (its
  content landed via PR #16); not part of this sweep.

## PR log

| Wave | Branch | PR | Merged as | Notes |
|---|---|---|---|---|
| — | `moba/gap-close` | — | — | worktree ledger base |

## Gap status

### Lens 1 — Release & project state

| # | Gap | Status |
|---|---|---|
| G1 | PR #17 (M3.2) unmerged; docs claim completion main lacks | **closed** (merged `d9f5230`, Wave 0) |
| G2 | No LICENSE (public repo) | **closed** (proprietary LICENSE, Wave 1) |
| G3 | No tags/releases pinning milestones | **closed** (`v0.3.0-m3.2`, Wave 0; convention noted in Wave 1 docs) |
| G4 | Retired branches left on origin | **closed** (deleted, Wave 0) |

### Lens 2 — Documentation consistency

| # | Gap | Status |
|---|---|---|
| G5 | `docs/ARCHITECTURE.md:120` misnumbers fixed-point ADR as 0007 (is 0002) | **closed** (Wave 1) |
| G6 | Root ARCHITECTURE.md map stale (ADR-index note; status) | **closed** (Wave 1) |
| G7 | ROADMAP Phase 8 table still lists CI as untriggered | **closed** (Wave 1) |
| G8 | `docs/sessions/` lacks retrospectives for sessions 03–09 | **closed** (Wave 1: 03, 06–09 backfilled; 04–05 already existed) |
| G9 | ROADMAP M2.0/M2.1 lack `Status:` convention | **closed** (Wave 1) |

### Lens 3 — Build & toolchain

| # | Gap | Status |
|---|---|---|
| G10 | No Debug-ASan preset; arena poison hooks stubbed | **closed** (Wave 2-A: `debug-asan` preset; 25/25 under `/fsanitize=address`; poison gated to virtual arenas) |
| G11 | clang-cl/UBSan determinism run never done | open (deferred: optional stretch; re-trigger at M3.4) |
| G12 | Vulkan SDK version unpinned | **closed** (Wave 2-A: pinned 1.4.357.0 in README + CMake version warning) |
| G13 | `/fp` pinning for eng_sim (M3.4) | open (owned by M3.4 slate) |
| G14 | `plat_mem_*` still in Win32 window TU | **closed** (Wave 3: win32_mem/file/log TUs; engine_tests proven user32-free) |

### Lens 4 — CI & quality gates

| # | Gap | Status |
|---|---|---|
| G15 | CI builds null renderer only; no Vulkan/validation in CI | **closed** (Wave 2-B: SDK 1.4.357.0 installed on runner, `MOBA_VULKAN_REQUIRED=ON`, sandbox validation run w/ device-gate skip) |
| G16 | RelWithDebInfo absent from CI matrix | **closed** (Wave 2-B: matrix = Debug, RelWithDebInfo, Release) |
| G17 | CodeQL claimed-but-absent workflow | **closed** (verified 2026-08-12: CodeQL runs via GitHub **default setup**, state=configured — no committed workflow needed; documented) |
| G18 | No perf/benchmark regression gate | **closed** (Wave 2-B: 10k-tick CPU ceiling test, ~100x headroom, also re-pins oracle) |
| G19 | No enforced merge policy | **closed** (`main-gate` ruleset, Wave 0) |
| G40 | No `.editorconfig` / templates | **closed** (Wave 1: .editorconfig + issue/PR templates) |

### Lens 5 — Determinism contract

| # | Gap | Status |
|---|---|---|
| G20 | Cross-binary parity unproven | open (owned by M6.7) |
| G21 | Hash cost at capacity never profiled | **closed** (Wave 2-B perf ceiling + Wave 3 capacity test) |
| G22 | RNG draw policy undocumented | **closed** (Wave 3: ADR-0013 — fixed per-tick script, positional draws) |
| G23 | Replay 64-slot cap migration undocumented | **closed** (Wave 3: sim.h comment + M5.2 roadmap note) |
| G24 | Pool-hash inclusion guard missing | **closed** (Wave 3: every SimStateField enum member proven hash-sensitive by test) |

### Lens 6 — Renderer / Vulkan

| # | Gap | Status |
|---|---|---|
| G25 | Dedicated-allocator cap is abort-first; pressure warning missing | **closed** (Wave 4: warning at 90% of cap + every 100; Phase 8 trigger text refined) |
| G26 | Hardware coverage NVIDIA-only; no IGP/AMD protocol | **closed** (Wave 4: `docs/testing-hardware.md` protocol + results table) |
| G27 | Culling has no owning milestone | **closed** (Wave 4: frustum culling owned by M4.3) |
| G28 | No DPI awareness | **closed** (Wave 4: SYSTEM_AWARE in sandbox; PER_MONITOR_V2 noted as follow-up) |
| G29 | Pipeline-cache defense is size-bound only | **closed** (Wave 4: every header byte load-bearing + truncation/garbage cases) |

### Lens 7 — Simulation / ECS

| # | Gap | Status |
|---|---|---|
| G30 | Stale-target policy undefined | **closed** (Wave 5: ADR-0014 — atomic reject today, per-command + reason codes at M6.0) |
| G31 | Event backpressure policy unspecified | **closed** (Wave 5: ADR-0014 — reject at source, never drop) |
| G32 | No capacity-pressure test (16,384 entities) | **closed** (Wave 3: twin-run + churn test) |
| G33 | No command rejection-reason field | **closed** (Wave 5: ADR-0014 reserves it for M6.0) |

### Lens 8 — Robustness & security

| # | Gap | Status |
|---|---|---|
| G34 | No fuzz infrastructure | open (deferred: M4.2 inflate / M6.1 transport) |
| G35 | No hardening-flag audit (`/guard:cf`) | **closed** (Wave 2-A: `/guard:cf` on RelWithDebInfo+Release, dumpbin-verified) |
| G36 | Fresh-clone sweep is manual prose | **closed** (Wave 2-B: `tools/fresh-walk.ps1` + CI job; run live 2026-08-12: clone→build→25/25→validation-clean screenshot) |

### Lens 9 — Ownership & architecture

| # | Gap | Status |
|---|---|---|
| G37 | Present glue has no home (`moba_game` absent) | **closed** (Wave 6: `eng_game` module owns the present glue; `moba_game` exe deferred to Phase 7 with the glue lib in place — slate amended) |
| G38 | Raw Input upgrade lost its owner | **closed** (Wave 5: owned by M5.2 — selection/commands need it) |
| G39 | `tga_direct` contamination path interim policy | **closed** (Wave 5: M4.0 moves it into `eng_assets`) |
| G40 | No `.editorconfig` / templates | **closed** (Wave 1: .editorconfig + issue/PR templates) |
