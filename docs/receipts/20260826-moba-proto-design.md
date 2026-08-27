---
type: lane-receipt
title: "MOBA-PROTO design lane receipt"
id: 2026-08-26-moba-proto-design-receipt
status: owner-required
lane_id: moba-proto-design
workflow_pattern: workflow.lean-lane-mint/v1
operating_pattern: RAIL
---

# MOBA-PROTO Design Lane Receipt

This receipt records the provenance, scope fence, verification status, and
future handoff for the CHOONZ-MoBA M4.1 one-lane design packet. It is a design
receipt only; it does not authorize engine implementation or PR merge.

## Delivery identity

| Field | Evidence |
|---|---|
| Source packet | GromAtlas PR 139, `fc56c2932053a7d4b06fcf229c35642605a18cc0` |
| Source branch | `lane/ct-program/20260826-mint-packet` |
| Canonical repository | `https://github.com/shapeslines/CHOONZ-MoBA.git` |
| Accepted design base | `6571ee40adfd7e99a49564a7c0d21d4702ee80c8` |
| Main merge-base | `e4b3369b36267dcef9fdbcebe2f67b227959d3bd` |
| Active M4.1 branch | `codex/m4.1-cooker-restart` |
| Design branch | `lane/moba-proto-design/20260826` |
| Design worktree | `C:\Users\doton\Desktop\GITHUB-ROOT\_worktrees\CHOONZ-MoBA-design-20260826` |
| PR target | `codex/m4.1-cooker-restart` |
| Packet commit | `b8020c822abaff3cfb370a991c3daf73e4681979` |
| Pull request | NOT CREATED; OWNER_REQUIRED network/auth gate |

## Mailbox lifecycle evidence

Mailbox transport used only `C:\Users\doton\Desktop\GITHUB-ROOT\_mailbox\mailbox.py`
with repo stream `choonz-moba`.

| Frame | Timestamp returned by helper | Frame ID |
|---|---|---|
| posture | `2026-08-27T03:50:37Z` | UNAVAILABLE; helper returned no separate ID |
| start | `2026-08-27T03:50:38Z` | UNAVAILABLE; helper returned no separate ID |
| report | `2026-08-27T03:56:04Z` | `wid-20260826-moba-proto-design-b8020c` |

The inbox had no targeted frames, conflicts, requests, or reports for
`moba-proto-design`. Mailbox status reported an unrelated incomplete registry
because `C:\Users\doton\Desktop\GITHUB-ROOT\GROM\GromCodebase\docs\fleet\layout.toml`
was unavailable; this did not create a duty for this lane.

## Artifacts

- `docs/slate-moba-proto-design.md`
- `docs/arc-moba-proto-design-manifest.json`
- `docs/receipts/20260826-moba-proto-design.md`

The intended diff from the accepted design base contains only these three new
docs artifacts. No existing source, phase, build, asset, CMake, shader, or
generated file is in the write fence.

## Approved contract

- Custom C++/raw Vulkan engine retained.
- Two-team, one-lane roadmap-baseline fixture.
- One mirrored hero per team, lane creeps, one tower and core per side.
- Projectile damage, self-heal, and area slow action examples.
- Kill/objective gold and XP ledgers; no item shop, item modifiers, neutral
  objective, or fog-of-war implementation in this slice.
- Local deterministic simulation and replay first; server-authoritative,
  filtered snapshot contracts remain ready for the next lane.
- M4.1 `.mba` v1 remains texture-only; typed gameplay payloads and GUI editor
  work are future lanes.

## Verification record

| Check | Status | Evidence or limitation |
|---|---|---|
| GromAtlas PR 139 fetched in clean worktree | PASS | Source worktree at `fc56c293...` |
| Active M4.1 exact head exists | PASS | `6571ee40...` at `codex/m4.1-cooker-restart` |
| Accepted base is a clean descendant of main | PASS | Merge-base is `e4b3369...` |
| Primary dirty checkout preserved | PASS | `C:\Users\doton\Desktop\GITHUB\CHOONZ-MoBA` was not modified |
| Active M4.1 worktree preserved | PASS | `C:\Users\doton\Desktop\GITHUB\MOBA-proto\.worktrees\m41-cooker-current` was not modified |
| Design diff path fence | PASS (pre-commit) | Staged path list contains only the three artifacts relative to `6571ee40...` |
| ARC manifest JSON/schema validation | PASS | Direct parser validation passed; five tasks and all dependency references are valid |
| ARC compiler availability | UNAVAILABLE | No ARC compiler/checker is present in the target checkout or permitted local GitHub workspaces |
| Documentation/schema checks | PASS (direct) | Required contract markers, task fields, dependency integrity, and forbidden-engine scan passed |
| `git diff --cached --check` | PASS | Staged whitespace check exits zero |
| Final commit path fence | PASS | `b8020c822abaff3cfb370a991c3daf73e4681979` contains only the three artifacts |
| Branch push | PASS | `origin/lane/moba-proto-design/20260826` updated successfully |
| GitHub PR creation | OWNER_REQUIRED | GitHub GraphQL endpoint unreachable; configured CLI token is invalid |
| CMake/CTest implementation matrix | NOT RUN | Design-only lane; fresh implementation gates required later |
| Hardware validation | NOT RUN | Existing M3.4/M4.0 evidence is cited; no fresh M4.1 claim |

## Future implementation handoff

The implementation order is:

1. `m5-map-navigation` — integer map grid, lane waypoints, passability, and
   spawns; `engine/sim/` and `tests/sim/` map-owned files; builds on M4.1 for
   baked identity/loading.
2. `m5-command-replay` — command ordering, rejection, and live/replay parity;
   `engine/sim/` and `tests/sim/` command/replay-owned files; no direct M4.1
   runtime dependency.
3. `content-typed-payloads` — typed validated cooker payloads;
   `engine/assets/`, `tools/cooker/`, and `tests/assets/`; builds on M4.1 only
   after its implementation acceptance.
4. `m5-hero-combat` — unified effects, projectiles, status, cooldowns, and
   damage events; serial simulation fence.
5. `m5-lane-objectives` — creep waves, tower/core state, victory, gold, and XP;
   serial simulation fence.
6. `m6-authority-replication` — server commands, filtered snapshots, and replay
   diagnostics; `engine/net/` and `tests/net/`.
7. `m7-presentation` — camera, selection, input translation, interpolation,
   HUD/minimap seams; `engine/game/`, `engine/render/`, and `tests/game/`.

The active M4.1 owner remains the writer for its cooker implementation. No
future lane may overlap a mutable fence without an explicit dependency edge.

## Owner-required delivery gate

The docs-only packet is committed and pushed, but the requested PR was not
created. `gh auth status` reports the configured GitHub CLI token as invalid,
and the non-interactive PR creation attempt failed to connect to
`https://api.github.com/graphql`.

Required pickup: restore approved GitHub CLI authentication and network access,
then create a PR from `lane/moba-proto-design/20260826` at packet commit
`b8020c822abaff3cfb370a991c3daf73e4681979` against
`codex/m4.1-cooker-restart`. Do not merge the PR from this lane. The mailbox
report is WID `wid-20260826-moba-proto-design-b8020c` at
`2026-08-27T03:56:04Z`.

## Held gates

Exact map dimensions and art scale, hero balance values, item/neutral-objective
rules, full fog-of-war behavior, final typed `.mba` encoding, GUI editor scope,
and transport reliability details remain HELD as non-blocking decisions. Any
base drift, product-scope change, engine replacement request, or ownership
conflict requires owner/Architect escalation.
