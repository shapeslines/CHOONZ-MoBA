# MOBA-proto — next session

## State · `moba/slate-phase3-systems` · PR #17 · 2026-08-12

Phase 3 M3.2 is implementation-complete. The branch adds derived ascending-entity component views,
phase-buffered typed damage events, prefixed plain-function systems, and one literal deterministic
tick schedule. M3.3 platform accumulator and presentation-snapshot work has not started.

## Verified

- MSVC `ci` preset (`/WX`) builds every Debug and Release target.
- 25/25 CTest entries pass in both configurations.
- Independent 10,000-tick streams match every tick and end at `0x637628abff59c823`.
- Controlled mutation reports exactly `tick=4321 field=position_x entity=7`.
- Replay v1 and 16-byte commands remain byte-exact with logic hash `0xab96814425ba80a4`;
  exact M3.1 files are rejected.
- CLI record → inspect → verify and all public exit classes pass through platform-file persistence.
- `eng_sim` is clean under the 17-file boundary scan and directly links only core, math, serialize.

## First action

Owner-review and merge PR #17. Local Debug/Release gates are green; GitHub CI/CodeQL and fresh
independent acceptance must also be green before the PR is marked ready. Merge remains separately
owner-approved.

## M3.3 handoff

After PR #17 lands, create `moba/slate-phase3-presentation` from updated `main` and execute
[`M3.3 presentation slate`](slate-moba-phase3-m3.3.md). Preserve the M3.2 command generator,
run-twice hash stream, and tick-4321 field diff as the regression oracle. M3.3 owns the platform
accumulator, fixed-state snapshot extraction, presentation interpolation, and the one fixed→float
edge only; keep gameplay, networking, assets, and M3.4 build-isolation closure out.

## Retained event-timing experiment

M3.2 uses same-tick damage. A later targeted experiment can defer only
`damage_event_queue_publish` to the next tick. Measure one-tick combat latency and specify how stale
source/target handles are rejected or ignored before changing the schedule or logic hash.

## Residual owner gate

- Vulkan SDK installation in hosted CI before changing `find_package(Vulkan)` to `REQUIRED`.
