# ADR 0011 — Server-authoritative netcode (not deterministic lockstep)

- **Status:** Accepted (2026-06-05)
- **Supersedes:** the deterministic-lockstep model assumed in the first
  architecture draft (P2P host-relay, send-only-inputs, run-at-the-laggiest-peer).

## Context

The first design synthesis converged on **deterministic lockstep**: clients
exchange only inputs and every client simulates the identical world. Its headline
win is bandwidth that is `O(inputs)` — independent of unit count, attractive for a
hundreds-of-units MOBA. But lockstep has three costs that are fatal for a
*competitive* MOBA:

1. **No cheat resistance.** Every client holds full world state, so map-hacks are
   unpreventable; fog of war can only ever be cosmetic.
2. **The match runs at the worst peer** — one laggy/stalled client degrades or
   freezes everyone.
3. **Nowhere to validate a cheating client** — there is no authority.

The project is aimed at competitive play, where real fog and basic cheat
resistance are requirements, not niceties.

## Decision

Adopt a **server-authoritative** architecture (the modern LoL / Overwatch /
Source lineage):

- One **authoritative server** runs the single source-of-truth deterministic
  `sim_tick` (a dedicated headless server, or a listen server hosted by a player).
- Clients send **intent** (the existing `Command` stream), never state. The
  server validates every command and replicates back only the slice each client
  may see.
- **Interest management** (area-of-interest) replication *is* the fog-of-war
  mechanism: hidden state is never sent, so map-hacks are impossible by
  construction.
- Latency is hidden by **client-side prediction** of the locally-controlled
  entity (re-simulated against the static map and own inputs only), **server
  reconciliation** (snap + replay un-acked inputs), and **entity interpolation**
  of remote entities. **Server-side lag compensation** validates hits.
- **Rollback/GGPO is explicitly NOT the model** and is not a planned upgrade.

**Determinism is kept** (fixed-point Q16.16, fixed 30 Hz, `sim_hash` + replay —
see the planned ADRs `0002`/`0001`). It is no longer a hard match-wide correctness
requirement, but a **leveraged asset**: the server's deterministic sim gives
portable replays and reproducible debugging, and the client predicting with the
*same* `sim_tick` reconciles drift-free (bit-for-bit on identical inputs + state).

## Consequences

**Positive:** real server-side fog; cheat resistance by construction (intent-only
clients, server validation); no laggiest-peer stall; the deterministic-sim and
replay/`sim_hash` work from Phases 3/5 is preserved and directly reused for
prediction + replays.

**Negative / accepted costs:** bandwidth is no longer `O(inputs)` — it scales with
*visible* unit count, requiring delta compression + interest management +
quantization from the start; the netcode is substantially more complex than
lockstep (prediction, reconciliation, interpolation, lag compensation, two-clock
timeline). NAT traversal, matchmaking, reconnect, and hardened anti-cheat
(signing/encryption/statistical detection) are deferred.

**Scope of change vs. the first draft:** only *who runs the authority* and *what
crosses the wire* changed. The `sim_tick` purity, fixed-point math, `eng_serialize`
codec, two-channel UDP reliability seam, and the determinism/replay harness are
all preserved. See `ARCHITECTURE.md` §12 (networking) and `ROADMAP.md` Phase 6.
