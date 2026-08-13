# Architecture Decision Records (ADRs)

Each ADR captures one significant, hard-to-reverse decision: the context, the
choice, and its consequences. They exist so settled decisions are not
re-litigated and so the *why* survives long after the *what* is code.

Format per file: `NNNN-short-slug.md` with **Status / Context / Decision /
Consequences**. Once accepted, an ADR is immutable — supersede it with a new ADR
rather than editing it (note the supersession in both).

## Index

| ADR | Title | Status |
|---|---|---|
| 0001 | [Simulation tick rate: fixed 30 Hz](0001-tick-rate.md) | Accepted |
| 0002 | [Sim number system: fixed-point Q16.16](0002-fixed-point-sim-math.md) | Accepted |
| 0003 | [Handle ABI: 32-bit, 18 index + 14 generation](0003-handle-abi.md) | Accepted |
| 0004 | [Vulkan loader: hand-loaded dispatch table](0004-vulkan-loader.md) | Accepted |
| 0005 | [The platform seam](0005-platform-seam.md) | Accepted |
| 0006 | [Module layout & target naming](0006-module-layout.md) | Accepted |
| 0007 | [Simulation RNG: pcg32 / xoshiro256**](0007-sim-rng.md) | Accepted |
| 0008 | [Shader build: GLSL → SPIR-V via glslc](0008-shader-build.md) | Accepted |
| 0009 | [Error handling: no exceptions, status + asserts](0009-error-handling.md) | Accepted |
| 0010 | [Asset id scheme: hashed path + generated constants](0010-asset-id-scheme.md) | Accepted |
| 0011 | [Server-authoritative netcode](0011-server-authoritative-netcode.md) | Accepted |
| 0012 | [Vulkan 1.3 hard minimum: dynamicRendering + synchronization2](0012-vulkan-13-minimum-spec.md) | Accepted |
| 0013 | [Sim RNG draw policy: fixed per-tick script](0013-sim-rng-draw-policy.md) | Accepted |
| 0014 | [Command validation, stale targets, event backpressure](0014-command-validation-and-backpressure.md) | Accepted |
| 0015 | [Unified baked asset container `.mba` version 1](0015-mba-v1-container.md) | Accepted |

ADR-0015 is implemented by the M4.1 shared codec, cooker, generated catalog, and baked-only runtime
path on PR #54. Later texture/mesh payload extensions must preserve or deliberately supersede its
versioning contract.

> Numbering note: `0011` was written first (the lockstep → server-authoritative
> change made during the design phase); `0001–0010` are the foundational
> cross-cutting contracts, drafted in Phase 0 · M0.1.
