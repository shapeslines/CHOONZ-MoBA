# MOBA-proto — next session

## State · `moba/slate-phase3-ecs` · PR #15 · 2026-08-12

Phase 3 M3.1 is implementation-complete. The branch replaces M3.0's fixed arrays with an
arena-backed generational entity manager, sparse-set SoA Transform/Velocity/Health pools, stable
64-slot replay mappings, deferred tick-boundary destruction, and canonical semantic ECS hashing.
M3.2 scheduling/events have not started.

## Verified

- MSVC `ci` preset (`/WX`) builds every Debug and Release target.
- 22/22 CTest entries pass in both configurations.
- Independent 10,000-tick streams match at every tick and end at `0x981212877a575730`.
- Controlled mutation reports exactly `tick=4321 field=position_x entity=7`.
- Replay v1 remains byte-exact with logic hash `0x7902599e173f87a6`; the old M3.0 hash is rejected.
- CLI record → inspect → verify and all public exit classes pass through platform-file persistence.
- `eng_sim` is clean under the 13-file boundary scan and directly links only core, math, serialize.

## First action

Owner-review and merge PR #15. The local Debug/Release matrix, GitHub CI/CodeQL, and fresh independent
acceptance verdict are green; merge remains separately owner-approved.

## M3.2 handoff

After PR #15 lands, create `moba/slate-phase3-systems` from updated `main` and execute
[`M3.2 systems slate`](slate-moba-phase3-m3.2.md). Use the M3.1 command generator, run-twice hash
stream, and tick-4321 field diff as the regression oracle. M3.2 owns ordered iteration caches,
typed append-only event queues, plain systems, and the explicit schedule only; keep M3.3 platform
accumulator/presentation, gameplay, networking, assets, and renderer work out.

## Residual owner gate

- Vulkan SDK installation in hosted CI before changing `find_package(Vulkan)` to `REQUIRED`.
