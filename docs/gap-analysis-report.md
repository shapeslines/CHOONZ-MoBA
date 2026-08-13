# Gap Analysis Report — MOBA-proto 40-gap sweep (2026-08-12/13)

Three-part record of the multi-lens gap analysis and its execution: **PRE** (the gaps
as found), **PLAN** (the closing waves), **POST** (what landed, with evidence). The
execution ledger with the full incident log is [`gap-close-ledger.md`](gap-close-ledger.md).

---

# PRE — the analysis (2026-08-12)

Forty gaps across nine lenses, grounded in the repo docs (README, ARCHITECTURE,
ROADMAP, JOURNAL, next-session, ADRs 0001–0012, slates), the source tree, the CI
workflow, CMake presets, and git state at the time (origin/main at `7ce2b45`, PR #17
unmerged, working tree on `moba/slate-phase3-systems`).

## Lens 1 — Release & project state

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G1 | PR #17 (M3.2) unmerged; docs claim completion main lacks | HIGH | `origin/main` @ `7ce2b45` (M3.1) vs README "M3.0–M3.2 complete" |
| G2 | No LICENSE in a public repo | HIGH | `LICENSE` absent from repo root |
| G3 | No tags/releases pinning milestone states | MED | only M7.4 mentions tagging; nothing tagged |
| G4 | Retired slate branches left on origin | LOW | 4 `moba/*` refs on origin, content fully merged |

## Lens 2 — Documentation consistency (the "stale = bug" contract)

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G5 | `docs/ARCHITECTURE.md:120` misnumbers fixed-point ADR as 0007 (is 0002) | HIGH | grep-verified misreference |
| G6 | Root ARCHITECTURE.md map stale (ADR-index note, status) | MED | claims ADR-0012 missing from index; index lists it |
| G7 | ROADMAP Phase 8 table lists CI as untriggered | LOW | CI shipped in Session 02 |
| G8 | `docs/sessions/` lacks retrospectives for sessions 03–09 | LOW | one file exists (2026-06-11) |
| G9 | ROADMAP M2.0/M2.1 lack the `Status:` convention | LOW | inconsistent milestone headers |

## Lens 3 — Build & toolchain

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G10 | No Debug-ASan preset; poison hooks stubbed | HIGH | presets = dev/ci only; M1.0 promised `debug-asan` |
| G11 | clang-cl/UBSan determinism run never done | MED | deferred since Session 01 |
| G12 | Vulkan SDK version unpinned | MED | M0.2 deliverable unmet; README has no version |
| G13 | `/fp` pinning for eng_sim (M3.4) | MED | sim flags default-inherited; M3.4 not built |
| G14 | `plat_mem_*` still in the Win32 window TU | LOW | engine_core_group links window code for tests |

## Lens 4 — CI & quality gates

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G15 | CI builds null renderer only; no Vulkan/validation | HIGH | runner has no SDK; `find_package` soft |
| G16 | RelWithDebInfo absent from CI matrix | MED | matrix = Debug + Release only |
| G17 | CodeQL claimed-but-uncommitted | MED | next-session claims checks; no workflow file |
| G18 | No perf/benchmark regression gate | MED | no timing tests anywhere |
| G19 | No enforced merge policy | LOW | no protection on main; owner gate by convention |

## Lens 5 — Determinism contract

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G20 | Cross-binary parity unproven | HIGH | no second binary exists until Phase 6 |
| G21 | Hash cost at capacity never profiled | MED | M3.0 trigger "if profiled" — no profiler |
| G22 | RNG draw policy undocumented | LOW | `sys_rng_advance` unconditional; no rule for conditional draws |
| G23 | Replay 64-slot cap migration undocumented | MED | `SIM_MAX_UNITS = 64` is a v1 seam with no plan |
| G24 | Pool-hash inclusion guard missing | MED | field list in tests is manual; new pools could skip hash coverage |

## Lens 6 — Renderer / Vulkan

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G25 | Dedicated-allocator cap is abort-first | HIGH | `ENSURE` at 3500; no pressure warning |
| G26 | Hardware coverage NVIDIA-only | MED | GTX 1070 / RTX 4070 Ti only; no IGP/AMD protocol |
| G27 | Culling has no owning milestone | MED | M4.3 bakes AABBs "usable for culling"; no cull owner |
| G28 | No DPI awareness | LOW | no `SetProcessDpiAwarenessContext` anywhere |
| G29 | Pipeline-cache defense is size-bound only | LOW | checker tests: size/header fields only |

## Lens 7 — Simulation / ECS

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G30 | Stale-target policy undefined | MED | atomic reject today; M3.3 queue says "define first" |
| G31 | Event backpressure policy unspecified | LOW | overflow preflighted; no sustained-overflow rule |
| G32 | No capacity-pressure test (16,384 entities) | LOW | proofs run at 64 units |
| G33 | No command rejection-reason field | MED | M6.4 needs server-rejected-input reporting |

## Lens 8 — Robustness & security

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G34 | No fuzz infrastructure | MED | M4.2/M6.1 planned; nothing exists |
| G35 | No hardening-flag audit | LOW | no `/guard:cf`; MSVC defaults only |
| G36 | Fresh-clone sweep is manual prose | LOW | FIXES.md is a narrative, not a script |

## Lens 9 — Ownership & architecture

| # | Gap | Severity | Evidence |
|---|---|---|---|
| G37 | Present glue has no home | HIGH | M3.3 assigns "game/present glue"; no target exists |
| G38 | Raw Input upgrade lost its owner | MED | "defer until M3" — M3.0–M3.2 shipped without it |
| G39 | `tga_direct` contamination path interim policy | LOW | tests compile it from `tools/sandbox/src` |
| G40 | No `.editorconfig` / templates | LOW | absent despite multi-agent work |

---

# PLAN — the closing waves (approved 2026-08-12)

Owner decisions: proprietary LICENSE · Vulkan SDK installed in CI · gap waves land
before M3.3. Wave ordering with gates, merging bottom-up through PRs on the
`main-gate`-protected `main`:

- **Wave 0 — owner gates:** merge PR #17, tag `v0.3.0-m3.2`, retire branches, enforce
  merge policy (G1, G3, G4, G19).
- **Wave 1 — doc truth & hygiene (one PR):** ADR misrefs, root-map refresh, ROADMAP
  fixes, LICENSE, .editorconfig/templates, session retrospectives, README truth
  (G2, G5–G9, G40).
- **Wave 2a — presets & flags:** `debug-asan` preset + poison verification, SDK pin,
  `/guard:cf` (G10, G12, G35).
- **Wave 2b — CI:** SDK on the runner + `MOBA_VULKAN_REQUIRED`, RelWithDebInfo matrix,
  CodeQL verification, perf ceiling, scripted fresh-walk (G15–G18, G36).
- **Wave 3 — determinism & sim policy:** ADR-0013 RNG policy, replay-cap note,
  hash-coverage guard, 16,384-entity capacity test, window-free platform TUs
  (G14, G22–G24, G32).
- **Wave 4 — renderer & robustness:** DPI, cache-header fuzz, allocator pressure
  warning, culling owner (M4.3), hardware-test protocol (G25–G29).
- **Wave 5 — ownership decisions:** ADR-0014 (stale targets/backpressure), present-glue
  home, Raw Input owner (M5.2), tga_direct policy (M4.0) (G30, G31, G33, G37–G39).
- **Wave 6 — M3.3 slate execution:** `eng_game` present glue, accumulator, snapshots,
  interpolation, sim-driven sandbox, headless render-rate-independence tests.

Explicitly deferred (owned by later phases): G11, G13 (M3.4); G20 (M6.7); G34
(M4.2/M6.1).

---

# POST — execution results (merged 2026-08-13)

## What landed on `main`

| Wave | PR | Merge | Content |
|---|---|---|---|
| 1 | #18 | `bf81e64` | doc truth, LICENSE, templates, retrospectives |
| 2a | #19 | `e85ea82` | debug-asan, /guard:cf, SDK pin |
| 2b+3 | #21 | `8defa10` | Vulkan CI, RelWithDebInfo, perf ceiling, fresh-walk, ADR-0013, hash guard, capacity test, window-free TUs |
| 4 | #22 | `4d250b2` | renderer robustness (DPI, fuzz, pressure, culling owner, HW protocol) |
| 5 | #23 | `ab774ed` | ADR-0014, ownership amendments |
| 6 | #24 | `ca5ad22` | **M3.3 complete** — eng_game present glue, sim-driven sandbox |
| ledger close | #26 | open | this report + session close |

(#20 auto-closed and #25 superseded when a stacked base was deleted; wave 2b landed
through #21 per the owner's merge plan.)

## Gap status — 40/40 addressed

- **Closed by code/doc/test (33):** G1–G10, G12, G14–G19, G21–G33, G35–G40.
- **Deferred with named owners (3):** G11 (clang-cl, M3.4), G20 (cross-binary parity,
  M6.7), G34 (fuzz, M4.2/M6.1).
- **Partial → superseded (1):** G13 (`/fp` pinning is M3.4's exact scope — queued as
  the next slate).
- **Closed by verification (2):** G17 (CodeQL confirmed via GitHub default setup),
  G4 (branch retirement; `gapfix` left to its owner session).

## Verification evidence

- `/WX` ci preset, Debug **and** Release: **26/26 CTest entries green** (25 prior +
  new `present` suite).
- Determinism oracle untouched: 10,000-tick golden `0x637628abff59c823`; mutation
  still `tick=4321 field=position_x entity=7`.
- `debug-asan` preset: full suite green under `/fsanitize=address` (arena poison
  gated to virtual arenas — stack poison leaked `f7` across frames under ASan).
- `/guard:cf` verified in the Release binary via `dumpbin /headers`.
- `engine_tests.exe` proven window-free: no `user32` import (platform TUs split).
- Sandbox: validation-clean on the RTX 4070 Ti, `objects=64 batches=1`, screenshot +
  clean exit; deterministic orbit demo.
- `fresh-walk.ps1`: FRESH-WALK OK end to end (fresh clone → configure → build →
  tests → validation-clean screenshot), locally and as a CI job.
- CI on runners with no Vulkan driver: sandbox step skips only on the renderer's
  explicit device-gate diagnostics (`VK_ERROR_INCOMPATIBLE_DRIVER` classifier added
  in `04ae177`); every other failure stays red.

## Incident log (all resolved; full detail in the ledger)

SDK winget install root (`C:\VulkanSDK`), pwsh `*>` redirect vs ErrorActionPreference,
runner has no Vulkan driver, merge-conflict `--theirs` inversion, UTF-16/BOM churn
from shell redirects (CMakeLists restored byte-clean), stacked-PR base deletion,
ruleset PATCH 404 quirk (delete-and-recreate, brief absence windows, nothing pushed
in them), `git add -A` committing conflict markers (verified + fixed), and the
owner's concurrent co-edits — credited in the PRs and ledger.

## Remaining for the project

- Merge #26 (this report). 
- M3.4 slate: `/fp:strict` pinning for `eng_sim`, float-grep lint in the pre-push
  path, game-binary self-check, clang-cl/UBSan golden (G11, G13).
- Sweep the ledger against reality after #26 (its "closed" rows all match main now).
- `build/m33-acceptance` worktree still holds `moba/gap-close-w6` — retire it when
  the owner session is done.
