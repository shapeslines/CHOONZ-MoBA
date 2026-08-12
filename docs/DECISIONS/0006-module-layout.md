# ADR 0006 — Module layout & target naming

- **Status:** Accepted (2026-06-05)
- **See also:** ARCHITECTURE.md §1.3, §3.1

## Context

"The CMake link graph *is* the architecture." Earlier drafts used three different
directory conventions and target-naming schemes for the same modules, which would
make `target_link_libraries` unresolvable. There is also a need to run the
deterministic core (and the dedicated server) **headlessly** — no Win32, no Vulkan
— for tests.

## Decision

Layout: `engine/<module>/` with public `include/<module>/*.h` vs private `src/**`.
One static library per module, `eng::*` aliases, coupling **only** via declared
`target_link_libraries`:

`eng_core`, `eng_math`, `eng_platform`, `eng_render`, `eng_sim`, `eng_net`,
`eng_assets`, `eng_serialize`.

**`eng_core_group`** is an INTERFACE/aggregate target link-grouping exactly the
OS-free, GPU-free libs (`core`, `math`, `sim`, `net`, `assets`, `serialize`) for
the headless test binary and the dedicated-server core. It is a grouping, not a
separate compilation.

**Headless-test split for `net`:** the OS-free reliability/channel/server logic
lives in `eng_net` (links the platform *seam header* only); the Winsock impl lives
in the app/dedicated-server layer. So `eng_net` is testable headlessly.

Dependency rules (enforced by the link graph):

```
core, math            -> leaves (only the platform page-alloc seam)
platform              -> core
render                -> core, math, platform        (ONLY module that sees Vulkan)
sim                   -> core, math                   (NO platform/render, NO float)
net                   -> core, math, platform(sockets), sim   (drives sim_tick)
assets                -> core, math, platform(file)
serialize             -> core                          (shared LE codecs)
game / server / tests -> link the above + own only main()/wiring
```

Shared configuration follows the same boundary: `core/sim_config.h` owns the tick
rate and wall-clock accumulator constants, while `sim/sim_config.h` derives
`SIM_DT_FIXED` from core's `SIM_HZ` and math's `FIX_ONE`. Core never includes math.

## Consequences

- Architecture is enforced by the compiler/linker, not by convention.
- The deterministic core + server logic are headless-testable and CI-friendly.
