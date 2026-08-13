# MOBA-proto — next session

## State @ `ca5ad22` · 2026-08-13 · DESKTOP-BK4F0OA/opencode

The 40-gap sweep is fully merged: waves 1-6 landed as #18 `bf81e64` · #19 `e85ea82` ·
#21 `8defa10` (2b+3) · #22 `4d250b2` · #23 `ab774ed` · #24 `ca5ad22`. Phase 3 M3.0-M3.3
complete. `main` is gated by the `main-gate` ruleset (Debug + RelWithDebInfo +
Release) + `safety`. The pre/plan/post gap analysis report is
`docs/gap-analysis-report.md`; the execution ledger is `docs/gap-close-ledger.md`.

## Shipped (this sweep, all on main)

- #18 wave 1 — doc truth (ADR misrefs fixed), proprietary LICENSE, .editorconfig,
  issue/PR templates, sessions 03-09 retrospectives.
- #19 wave 2a — `debug-asan` preset (full suite green under `/fsanitize=address`;
  arena poison gated to virtual arenas), `/guard:cf` (dumpbin-verified), SDK
  1.4.357.0 pin.
- #21 waves 2b+3 — Vulkan in CI (`MOBA_VULKAN_REQUIRED`, SDK on runner, sandbox
  validation run with renderer device-gate skip), RelWithDebInfo in the matrix,
  10k-tick perf ceiling, `tools/fresh-walk.ps1` (FRESH-WALK OK live), ADR-0013 RNG
  policy, SimStateField hash-coverage guard, 16,384-entity capacity test,
  window-free platform TUs (engine_tests user32-free).
- #22 wave 4 — DPI awareness, cache-header fuzz (every header byte load-bearing),
  allocator pressure warning (90% of cap), frustum-culling owner (M4.3),
  `docs/testing-hardware.md`.
- #23 wave 5 — ADR-0014 (atomic reject today; per-command + reason codes at M6.0;
  never drop events), Raw Input owned by M5.2, tga_direct owned by M4.0.
- #24 wave 6 — **M3.3 complete**: `engine/game` present glue (accumulator,
  double-buffered fixed-only `RenderSnapshot`s, single fixed→float), sim-driven
  sandbox orbit demo, headless render-rate-independence tests (`present` suite).
- #26 — gap analysis report + ledger session close (OPEN; docs-only).

## Signals

- **state/flags:** ADR index now 0001-0014. Presets: `dev`, `ci`, `debug-asan`
  (build-asan dir). CI requires the Vulkan SDK on the runner; sandbox step skips
  only on the renderer's explicit device-gate diagnostics
  (`VK_ERROR_INCOMPATIBLE_DRIVER` classifier, `04ae177`). `main-gate` ruleset id
  20780727 (3 required checks); `safety` id 20777653.
- **communicated:** owner session co-drove merges #21/#22 and authored the device-gate
  classifier; credited in the ledger. `moba/gap-close-w6` is checked out in the
  owner's `build/m33-acceptance` worktree — retire it when their session is done.
- **raised for /custodian:** none (no `docs/custodian-queue.md` in this repo yet).
  Ledger/next-session/report are current as of this wrap.
- **FOR /brain:** distill → `brain/moba-sim-present-boundary.md` ← `MOBA-proto@ca5ad22`:
  M3.3 accumulator, snapshot extraction (const, hash-neutral), the single fixed→float
  owner, render-rate independence proof. Also `brain/moba-gap-close-sweep.md` ←
  `MOBA-proto@ca5ad22`: the 40-gap sweep pattern (9 lenses), wave plan, and the
  incident catalog (UTF-16/BOM churn, --theirs inversion, stacked-base deletion).
- **DEFERRED / unresolved:** merge #26 (docs); M3.4 slate (`/fp:strict` pinning,
  float-grep lint in pre-push, game-binary parity, clang-cl golden — closes G11/G13);
  G20 cross-binary parity @ M6.7; G34 fuzz @ M4.2/M6.1; retire the owner worktree;
  sweep the ledger against reality post-#26.

## Next — FIRST action

1. Merge #26 (docs-only, green-checked), then start the M3.4 slate from updated
   `main`: `/fp:strict` for `eng_sim` in one toolchain include, the float-grep lint
   wired into the pre-push path, and the game-binary self-check parity proof.

## Queue

- M3.4 (above), then Phase 4 (assets — tga_direct moves into `eng_assets`, G39) and
  Phase 5 (gameplay — Raw Input, G38) per ROADMAP.
- Watch the allocator-pressure warning (G25) as Phase 4 assets land; the block
  allocator is a Phase 8 trigger.
