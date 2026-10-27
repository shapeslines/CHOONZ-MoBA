# ADR 0014 — Command validation, stale targets, and event backpressure

- **Status:** Accepted (2026-08-12)
- **Related:** [0009](0009-error-handling.md), [0011](0011-server-authoritative-netcode.md),
  [0013](0013-sim-rng-draw-policy.md)

## Context

Phase 3 established the deterministic input boundary (`sim_validate_commands` +
`sim_tick`) but left two behaviors unstated: what happens when a command targets an
entity that is dead/stale (the "stale-target policy" the M3.3 queue flagged), and what
happens when a tick's events overflow their fixed-capacity queue. Netcode (Phase 6)
turns this boundary into the wire contract, so the policy must be decided before M6.0.

## Decision

### Stale targets: atomic reject today, per-command at M6.0

- **Today: atomic packet rejection.** `sim_tick` validates the whole command buffer
  first; any command that is non-canonical, out of range, or targets an entity that is
  not alive fails validation and the **entire tick is rejected without mutation**
  (`sim_tick` returns false, world unchanged). There is never a silent no-op.
- **At M6.0: per-command rejection with reason codes.** When the network command stream
  lands, a single stale command must not drop the whole packet. M6.0 introduces a
  rejection-reason field on the command seam (G33) and changes validation to
  apply-valid/skip-invalid per command. **This is a deliberate sim-behavior change and
  requires the reviewed logic-hash bump** (the M3.0 oracle discipline) — it is not a
  silent fix.
- **Until M6.0, no sim code may special-case stale targets**; the atomic rule is the
  contract and the hash oracle depends on it.

### Event backpressure: reject at source, never drop

- The damage-event queue is fixed capacity and preflighted: commands that would overflow
  the current phase are rejected before any mutation (already implemented, M3.2).
- **Drop-oldest / overwrite is forbidden**: losing a damage event deterministically would
  fork combat resolution. The only sanctioned pressure response is rejecting the
  publishing command.
- A rejected-command counter or diagnostic may be added later on the presentation side,
  but it is not part of authoritative state and is not implemented by this decision.

## Consequences

- Replays of valid streams are unaffected; the oracle `0x637628abff59c823 (M3.2; M5.0 re-pinned the oracle to 0xff4e1ca0c779455b with logic key 0xcef8548df2b2a518)` stands.
- M6.0 owns the per-command refinement + rejection codes + hash bump as one reviewed
  change; ADR-0014's "today" half is the authority until then.
- A server that wants "drop the bad command, keep the packet" must wait for M6.0 — it
  cannot ship today without desyncing against the oracle.
- **M5.2 (`m5-hero-combat`, 2026-09-03) extends reject-at-source to a second queue.**
  Alongside the frozen 12-byte `DamageEvent` wire, gameplay events (heal, status applied,
  status expired, death) now ride one fixed-capacity `SimEventQueue` of 32-byte `SimEvent`
  envelopes (`tick`, `event_kind`, `payload_size`, `append_ordinal`, 16 payload bytes;
  proto-design §3.4 field order). Both queues obey this ADR identically: `sys_hero_actions`
  measures a whole tick's event demand in a mutation-free pass before it commits anything,
  and a write phase that cannot hold the demand fails the **source operation** — `sim_tick`
  returns false, `world.tick` does not advance, and no queued event is ever dropped,
  reordered, or overwritten. One envelope rather than three typed queues keeps the
  canonical-hash, diff, and `SimStateField` surface to a single block as M5.3 adds kinds.
  This bumped the oracle once more, to `0xac06a80d7f71b503` with logic key
  `0x46e9e287878ba88c`.
- **M5.5 (`m5-lane-objectives`, 2026-09-04) adds four kinds to that same envelope and one
  more reject-at-source producer, with no queue change.** `SimEventKind` gains
  `OBJECTIVE_DAMAGED = 5` (payload 16), `OBJECTIVE_DESTROYED = 6` (12), `ECONOMY = 7` (16),
  and `MATCH_OVER = 8` (12). `SIM_EVENT_PAYLOAD_MAX` stays 16 and `sizeof(SimEvent)` stays
  32, so the phases, `sim_event_is_canonical`, and this ADR's policy are all untouched — the
  payoff M5.2 predicted when it chose one envelope over three typed queues. The slice also
  makes one standing rule explicit and enforced: **payload offset 0 is always a `u32`
  `EntityId` slot**, `HANDLE_NULL` when the kind names no entity, because
  `sys_effects_resolve` already reads offset 0 for every kind before branching.
  `SIM_EVENT_DEATH` is **not** widened — its existing 8-byte payload already carries `source`
  at offset 4, and M5.5 simply populates it from `HealthPool.last_damage_source`, so a
  `DEATH` event now names its killer for heroes, minions, and objectives alike.
  Three new producers obey the measure-then-commit rule verbatim: `sys_waves` pre-flights an
  **entire** wave (entity headroom plus every pool's free room) before it creates the first
  entity, so a wave is never partially spawned; `sys_death` and `sys_economy` each run one
  walk twice, counting the events the verdict or the award needs and failing the **source
  operation** when the write phase cannot hold them all. In every case `sim_tick` returns
  false, the world and the arena are unchanged, and nothing is dropped. This bumped the
  oracle to `0x36e6de56cb662dba` with logic key `0x5b47e648953a63fc`.
