# ADR 0002 — Simulation number system: fixed-point Q16.16

- **Status:** Accepted (2026-06-05)
- **Related:** [0001](0001-tick-rate.md), [0007](0007-sim-rng.md), [0011](0011-server-authoritative-netcode.md)

## Context

Determinism requires bit-identical arithmetic across machines and across Debug/
Release builds. IEEE float is a determinism tar pit (x87 vs SSE, `/fp:fast` vs
`/fp:precise`, FMA contraction, divergent `sinf`/`sqrtf`). Drafts disagreed on the
format, and the Q32.32 variant used `__int128` — **which MSVC, our locked
compiler, does not support.**

## Decision

**Q16.16 fixed-point.** Defined once in `engine/math/include/math/fix.h`:

```c
typedef int32_t fix;            // 16 integer bits . 16 fractional bits
#define FIX_ONE 65536           // 1.0
// MSVC-correct multiply/divide via 64-bit intermediates (NOT __int128):
static inline fix fix_mul(fix a, fix b){ return (fix)(((int64_t)a * b) >> 16); }
static inline fix fix_div(fix a, fix b){ return (fix)(((int64_t)a << 16) / b); }
```

- Defined once; sim/net/gameplay/tooling reference this typedef and `FIX_ONE`.
  Wire size and `sim_hash` layout follow from `sizeof(fix) == 4`.
- **Transcendentals are table-driven** (`fix_sin`/`fix_cos`/`fix_sqrt` via LUTs) —
  no `<math.h>` in sim.
- **Float is banned in sim code**, enforced by convention + a grep/CI lint +
  the run-twice state-hash self-check (see ARCHITECTURE §2.1), not by pretending a
  compiler flag can ban the type. As a separate build invariant, the named
  `moba_sim_determinism` CMake policy pins every authoritative sim translation unit
  to `/fp:precise` and emits a marker checked against `compile_commands.json`.
  M3.4 additionally runs the same canonical hash stream through clang-cl and, when
  its compile/link/runtime capability probe succeeds, UBSan. That second toolchain
  is a structural diagnostic gate, not an alternate authoritative number system.

**World-scale convention:** 1 world unit ≈ 1 "meter"; tile-grid cells = 0.5 unit;
maps may run to a few hundred units across (a MOBA/RTS hybrid map can exceed a
small MOBA arena). Q16.16 range is ±32768 — ~64× headroom over a 512-unit map —
and resolution is ~1.5e-5 (sub-millimeter). Headroom confirmed at the larger scale.

## Consequences

- Bulletproof cross-machine/cross-build determinism → portable server replays and
  drift-free client prediction parity (the reason determinism is kept under
  server-authority, per [0011]).
- All sim math is fixed-point (a real discipline cost; mitigated by the LUTs and
  the float-ban lint).
- **Escape hatch:** the single typedef allows widening to Q32.32 *with the MSVC
  `_mul128`/`__mulh` intrinsics* if a genuine range/precision problem ever appears.
  Deferred, not pre-built; the world-scale headroom makes it unlikely.
