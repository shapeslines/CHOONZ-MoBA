---
type: lane-receipt
title: "CHOONZ-MoBA PM baseline and roadmap translation receipt"
id: 2026-09-02-pm-baseline-receipt
status: owner-required
lane_id: moba-pm-baseline
workflow_pattern: workflow.lean-lane-mint/v1
operating_pattern: RAIL
---

# CHOONZ-MoBA PM Baseline Receipt

Records what the 2026-09-02 session reconciled, which owner decisions it exercised, and what stays
owner-gated. Docs-only; no engine, test, or generated file changed in this lane.

## Delivery identity

| Field | Evidence |
|---|---|
| Canonical repository | `https://github.com/shapeslines/CHOONZ-MoBA.git` |
| Base | `main` at `4f66af17e9b8ad65c9b5238426775f656d2811fe` (PR #63 squash) |
| Lane branch | `lane/moba-pm-baseline/20260902` |
| Pull request | [PR #65](https://github.com/shapeslines/CHOONZ-MoBA/pull/65) against `main`; owner merge required |

## Owner decisions exercised (authorized in-session 2026-09-02)

| Decision | Action | Evidence |
|---|---|---|
| Squash-merge PR #63 (M4.1 sandbox baked texture) | marked ready, merged pinned to head `70515c4` | `4f66af1` on `main` |
| Close PR #64 (legacy-root rescue) as superseded | closed with a comment naming the superseding commits | GitHub #64 |
| Patch vault `20 Projects/moba/moba.md` projection fields and the `_Projects.md` moba row | applied; board regenerated | vault commit (see wrap) |

## What changed in this lane

- New: `CLAUDE.md` (shim → `AGENTS.md`), `docs/README.md`, `docs/custodian-queue.md`,
  `docs/plans/README.md`, `docs/plans/m4.1-closeout.md`, `docs/plans/m5.0-map-navigation.md`,
  `docs/plans/m5.1-command-replay.md`, `docs/arc-m5.0-map-navigation-manifest.json`,
  `docs/slate-moba-phase4-m4.1.md`, this receipt.
- Reshaped: `AGENTS.md` (product-stub sections + objects map + session protocol).
- Filled: `docs/groundwork.md`, `docs/WORK-FRONTLOG.md` (placeholders removed).
- Replaced: `docs/next-session.md` (State @ `4f66af1`).
- Edited: `docs/ROADMAP.md` (M4.0/M4.1 status, Phase 4/5 banners, M5.0 bake-path note),
  `docs/JOURNAL.md` (Sessions 14, 15 prepended), `.gitignore` (`.advisor/`).

## Verification

- `docs-surface-lint.py --repo . --require-presence` — recorded in the PR body.
- `next-session-audit.py --checkout .` — recorded in the PR body.
- `tools/local-ci.ps1 -Configuration Debug` on `4f66af1` failed inside the Claude tool harness with a
  spurious `'vswhere.exe' is not recognized` (the report still resolved Visual Studio 17.14); the
  documented developer-shell equivalent (`vcvars64` → `cmake --preset ci` → build → `ctest`) passed
  48/48 in Debug on the same tree. Recorded in the PR body.
- The ARC compiler `validate` / `render --check` was not available in this session; the manifest was
  validated as JSON only (same posture the 2026-08-26 design receipt recorded).

## Still owner-gated / proposed

- **GitHub Actions is locked for the `shapeslines` account (billing).** Check-run annotation on every
  job of PR #65 and of the `main` push run for `4f66af1`: "The job was not started because your
  account is locked due to a billing issue." No step ran; no diff is at fault. The `main-gate` ruleset
  cannot go green until billing is restored and the runs are re-triggered.

- Merge of this lane's PR.
- Vault overlap `moba.md` / `game-design.md`: **resolved 2026-09-02** on owner authorization — record
  `05 Exchange/records/2026-09-02-moba-game-design-reconcile.md` (moba = engine record, game-design =
  area hub with `depends_on` edges; `project-doctor.py --check` PASS).
- GromCodebase GAP-011 closing receipt (the `CHOONZ/MOBA-proto` in-tree clone is gone from disk;
  `FLEET-MAP.md` rows 51/79 still describe it). Mailbox request posted; the GromCodebase seat edits.
- GromAgentKit `live/skills/moba/SKILL.md` line "maps to UE5/C++" contradicts its own frontmatter.
