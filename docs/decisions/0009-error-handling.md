# ADR 0009 — Error handling: no exceptions, status codes + two assert tiers

- **Status:** Accepted (2026-06-05)
- **See also:** ARCHITECTURE.md §2.4

## Context

Exceptions and RTTI are disabled (`/EHs-c-`, `_HAS_EXCEPTIONS=0`, `/GR-`) for ABI
clarity, predictable control flow, and determinism. We still need a consistent way
to report fallible operations and to enforce invariants.

## Decision

- **Fallible operations return explicit status:** a small `Result`/error enum, or
  `bool` + out-parameter. No `throw`, no `dynamic_cast`/`typeid`. The caller checks
  and decides; there is no hidden unwinding.
- **Two assertion tiers:**
  - `ASSERT(cond)` — **debug-only**, compiled out in release. May *read* state for
    diagnostics but must **never alter computation**, so Debug and Release stay
    bit-identical (essential for the deterministic sim, [0002]).
  - `CHECK(cond)` / `FATAL(msg)` — **always-on (including release)** for
    unrecoverable invariants → log + `platform_fatal` (abort).
- **Memory failure policy:** arenas are pre-sized; sim out-of-memory is **fatal**
  (no recovery path — a deterministic sim cannot partially fail). Asset/IO failures
  degrade gracefully where reasonable (missing-asset placeholder, etc.).
- **Sim code must never branch on `ASSERT` or `#if BUILD_DEBUG` in a way that
  changes results.**

## Consequences

- Predictable, exception-free control flow; no unwinding paths to reason about.
- Invariant violations fail loud and early; the Debug/Release bit-identity rule is
  protected, which the determinism harness depends on.
