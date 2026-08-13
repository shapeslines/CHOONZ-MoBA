# ROADMAP — MOBA Engine (from scratch, C++, raw Vulkan)

> A phased, milestone-driven build plan from **empty repo** to **playable MOBA prototype** and beyond.
> Derived from the eight subsystem designs, their dependency order, and the three review lenses
> (consistency, technical-correctness, pragmatism). No calendar dates — only ordered milestones and
> relative effort (S / M / L / XL).

---

## Guiding principles

1. **Own the metal, but make visible progress.** Every phase ends in something you can run, see, or
   hash. Bring-up order is chosen so the screen shows pixels early and the simulation proves itself
   early — not so that "the architecture is done" but nothing runs.
2. **Front-load the long poles, behind seams.** The four hardest, highest-risk subsystems — **Vulkan
   bring-up**, the **custom allocator**, the **deterministic simulation**, and **netcode** — are
   sequenced deliberately and flagged 🔴. Each sits behind a thin seam so the deferred half can land
   later without an API break.
3. **Determinism is a contract, not a feature.** Fixed tick, fixed-point sim math, deterministic
   iteration, seeded RNG, no wall-clock/pointers/float in sim. The **state-hash + record/replay
   harness is built before there is sim content to break**, and runs forever after.
4. **One source of truth for shared facts.** Tick rate, fixed-point format, handle ABI, the platform
   seam, the Vulkan loader strategy, and module/target names are decided **once** in Phase 0 and
   cited everywhere. The reviews found these specified 2–3 incompatible ways across the designs; the
   roadmap resolves them up front because they are the most expensive things to change later.
5. **Defer aggressively, on purpose.** Render graph, bindless, compute, archetypes, rollback-style remote prediction, async
   I/O, compression, NAT traversal, matchmaking — all explicitly deferred, each behind a named
   seam, none built before the milestone that needs it.
6. **Avoid both traps.** No naive shortcuts that rot determinism; no premature generalization
   (no ECS framework, no test framework, no scripting VM). Right-sized for a solo dev over years.

---

## How to use this roadmap

- **Work top to bottom.** Milestones are ordered by dependency. Don't start a milestone until its
  predecessors' **Definition of Done (DoD)** is met.
- **A milestone is "done" only when its DoD is observably/testably true** — a window opens, a triangle
  shows, `ctest` is green, two replay runs hash-match. Vague "it compiles" is not done.
- **Effort tags are relative, not absolute:** **S** ≈ a sitting or two; **M** ≈ a focused week-ish of
  evenings; **L** ≈ multiple weeks; **XL** ≈ a month-plus / a true long pole. Use them to plan
  energy, not dates.
- **🔴 = long-pole / high-risk.** Expect these to take longer than they look and to teach the most.
- **Keep the living docs alive.** `docs/ARCHITECTURE.md`, this file, and `docs/DECISIONS/*.md` (ADRs)
  are updated in the **same commit** that changes a seam. If they're stale, that's a bug.
- **Re-read the "Now / Next / Later" at the top of each phase** at the start of a session to stay
  aimed.

---

## Phase & dependency map (ASCII)

```
 PHASE 0  Foundations & Contracts ......... CMake spine, shared headers, ADRs, window
            │
            ▼
 PHASE 1  Core Libraries (leaf-up) ........ core/mem, math (incl. fix), containers, tests
            │              │
            ▼              ▼
 PHASE 2  Vulkan Bring-up 🔴 .............. instance→device→swapchain→clear→triangle→quad→instanced
            │
            ▼
 PHASE 3  Deterministic Simulation 🔴 ..... ECS, fixed-tick loop, state-hash + record/replay
            │              │
            ▼              ▼
 PHASE 4  Assets ......................... TGA/WAV/SPIR-V direct, cooker, .mba, glTF, hot-reload
            │
            ▼
 PHASE 5  Gameplay (single-player) ....... map grid, flow-field movement, units, abilities, combat, fog
            │
            ▼
 PHASE 6  Netcode 🔴 ..................... UDP transport, server-authoritative (predict + interp + lag-comp), delta + interest, divergence detect
            │
            ▼
 PHASE 7  Vertical Slice → Prototype ..... HUD/text, audio, polish, packaging — a PLAYABLE match
            │
            ▼
 PHASE 8+ Hardening & Optionals .......... block allocator, reversed-Z, rollback?, server?, portability

 DEPENDENCY RULES (enforced by the CMake link graph = the architecture):
   core, math               → leaves (depend on nothing but platform page-alloc)
   platform (seam)          → core
   render                   → core, math, platform        (ONLY module that sees Vulkan)
   sim                      → core, math                  (NO platform, NO render, NO float)
   net                      → core, math, platform(sockets), sim   (calls sim_tick)
   assets                   → core, math, platform(file)  (uploads via render seam)
   game / sandbox / tests   → link the above; own only the loop wiring + main()
```

---

# PHASE 0 — Foundations & Contracts

**Goal:** stand up the build spine, lock the cross-cutting decisions the reviews flagged as
load-bearing, and get a Win32 window with a clean message loop on screen. Nothing here is gameplay —
this is the bedrock that prevents foundation-level rework.

> **Now:** M0.0 → M0.3 in order. **Next:** Phase 1. **Later:** Phase 2.

---

### M0.0 — Repo hygiene & decision log skeleton  · S
**Goal:** make the repo ready to grow and ready to record decisions.
**Deliverables:**
- Add `*.spv`, `baked/`, `*.mba`, `*.pak` to `.gitignore` (reviews: currently missing).
- `docs/` with stubs: `ARCHITECTURE.md`, `ROADMAP.md` (this), `CONVENTIONS.md`, `DECISIONS/`.
- `tools/hooks/pre-push` stub (runs `ctest`; wired for real in M1.4).
**DoD:** `git status` clean; `docs/DECISIONS/` exists; ignore rules verified to exclude a touched
`build/foo.spv`.
**Risks:** none material. **Exercises:** Tooling/workflow. 

---

### M0.1 — 🔑 Shared-contracts ADRs (decide once, cite everywhere)  · M
**Goal:** resolve the cross-subsystem conflicts the reviews found *before* any subsystem code is
written. These are effectively irreversible once code is built on them.
**Deliverables — one ADR file each in `docs/DECISIONS/`:**
- **`0001-tick-rate.md` → 30 Hz.** The sim/net/gameplay trio already agree; Platform's 60 Hz is the
  outlier. Define `SIM_HZ = 30`, `SIM_DT_SECONDS`, and the catch-up clamp in
  `engine/core/include/core/sim_config.h`; derive `SIM_DT_FIXED` in `sim/sim_config.h`, where core's
  rate and math's `FIX_ONE` first meet. The platform loop never defines its own `FIXED_DT`.
- **`0002-fixed-point-format.md` → Q16.16 (`typedef int32_t fix`, `int64_t` intermediate multiply).**
  Reasons: compiles cleanly on MSVC (Q32.32's `__int128` does **not** — it's a GCC/Clang extension;
  MSVC would need `_mul128`/`__mulh`); halves sim-state / snapshot / wire / hash size; the gameplay
  map (256 cells × 0.5u ≈ 128 units) fits ±32768 with huge margin. **Gate:** if a quick world-scale
  spike shows precision is too tight, switch to Q32.32 *with the intrinsic multiply* — but decide now,
  don't leave open. Single typedef lives in `engine/math/include/math/fix.h`; sim/net/gameplay/tooling
  all reference it.
- **`0003-handle-abi.md` → one generational-handle convention** in
  `engine/core/include/core/handle.h`. Pick a bit split (24-bit index + 8-bit gen is used by render &
  assets; ECS wants 20+12 for more reuse generations — **decide deliberately and document**) and one
  null/invalid sentinel rule. `EntityId`, `MeshHandle`, `TextureHandle`, `AssetHandle` all derive from
  the same macro so index/gen extraction is identical. Gameplay's `int32_t EntityId` is fixed to the
  **unsigned** ECS type.
- **`0004-vulkan-loader.md` → choose ONE.** Either (a) link the official loader (`Vulkan::Vulkan`)
  and call `vk*` directly — the loader *is* the sanctioned API and this removes a class of opaque
  null-pointer bring-up failures; or (b) hand-load `vulkan-1.dll` and route **every** call through a
  dispatch table the renderer owns (then Build must **not** link the import lib, only its includes, and
  the Renderer's direct `vk*()` calls become `vk.Xxx()` table calls). Recommendation for first
  bring-up: **(b) hand-load** `vulkan-1.dll` via the renderer's own two-tier dispatch table (Build links Vulkan includes only, never `vulkan-1.lib`; all calls go through `vk.Xxx()`), per the pure-from-scratch ethos and ARCHITECTURE section 5.3. Whichever is chosen, all
  three of Build/Platform/Renderer must agree.
- **`0005-platform-seam.md` → single canonical `platform.h`.** Add the page-reservation API
  (`plat_mem_reserve/commit/release/page_size`) the Memory subsystem depends on (it is its *only*
  dependency and currently absent from Platform's header). File reads take a **caller-supplied
  Arena/Allocator** (`platform_file_read(vpath, Arena*, PlatformFile*)`) so "the arena owns the bytes"
  holds. **Surface creation lives in the platform** (`platform_vk_create_surface`); HWND never crosses
  into render — delete `platform_win32_handles`. The asset layer's `fs_open_asset(asset_id)` sits
  **above** platform.h (resolves id→vpath, then calls platform file I/O).
- **`0006-module-layout.md` → one canonical layout & target names.** Adopt the Build System's
  `engine/<module>/` with `include/<module>/*.h` (public) vs `src/**` (private), static libs
  `eng_core`/`eng_math`/`eng_platform`/`eng_render`/`eng_sim`/`eng_net`/`eng_assets` with `eng::*`
  aliases. **Reconcile the headless-test split:** `engine_core` is **not** a new lib — it is an
  INTERFACE/aggregate target link-grouping the non-Win32/non-Vulkan libs (`core, math, sim, net,
  assets/serialization`) for the test binary. `net`'s OS-free channel/reliability logic links the
  platform **seam header** only; the Win32 socket impl lives in the app layer so `net` is testable
  headlessly.
- **`0007-sim-rng.md` → PCG32 for the sim** (`pcg32_t` in `math/rng.h`), xoshiro256** strictly for
  presentation/tools. ECS's xoshiro-for-sim is corrected to the canonical type. RNG state is hashed and
  seeded over the wire, so it must be identical everywhere.
- **`0008-shader-build.md` → one shader contract.** `add_shader_library()` + glslc `-MD/-MF` depfiles
  + `MOBA_SHADER_DIR` (Build System's approach). Shaders are **loose `.spv`** in `build/shaders/` for
  the engine (delete the Renderer's GLOB snippet); decide explicitly that shaders are **not** wrapped
  in `.mba` (keeps the render seam simple). One output dir, one discovery mechanism.
- **`0009-exceptions-rtti-flags.md` → exact flag string.** `/GR- /EHs-c-` and the decision on
  `_HAS_EXCEPTIONS=0`. State the invariant: **any static lib linked into both `engine_core` and a
  tools/tests target must use identical `_HAS_EXCEPTIONS`/`/EH` settings and expose only POD C
  interfaces** (the shared `asset_parsers` lib is the contamination path). Decide STL-in-tests:
  **tests use the own `MOBA_CHECK`/`test.h` harness, STL-free**, to match the no-exceptions posture.
- **`0010-math-type-names.md` → ban aliases.** `vec2/vec3/vec4/mat4/quat` (float, namespace `mm`),
  `fvec2/fvec3` (fixed). Forbid reusing `v2`/`m4`/`v3` short aliases and forbid `v2` meaning a
  fixed-point vector in gameplay (use `fvec2`). Every subsystem uses the math lib's exact names so the
  float/fixed boundary is obvious at every call site.

**DoD:** ten ADRs committed; `core/sim_config.h`, `core/handle.h`, `math/fix.h` typedef stubs exist and
compile; `docs/ARCHITECTURE.md` references each ADR.
**Risks:** 🔴 these are the determinism/ABI contracts — getting tick rate, fixed-point width, or handle
layout wrong here is the single most expensive class of rework in the project. Worth a slow, deliberate
pass. **Exercises:** every subsystem (by contract).

---

### M0.2 — CMake spine + compiler posture  · M
**Goal:** the build graph that *is* the architecture, with day-one hygiene.
**Deliverables:**
- Top-level `CMakeLists.txt`; `cmake/CompilerWarnings.cmake` (`moba_warnings`: `/W4 /WX /permissive-
  /Zc:preprocessor /Zc:__cplusplus /utf-8 /wd4201`), `cmake/EngineOptions.cmake` (`moba_options`:
  `/GR- /EHs-c-`, per-config defines), `cmake/CompileShaders.cmake` (`add_shader_library` w/ depfiles).
- `CMakePresets.json` — **Ninja Multi-Config**, single build dir; explicitly set
  `CMAKE_CONFIGURATION_TYPES`; **Release flags via generator expressions**
  (`$<$<CONFIG:Release>:/O2 /GL>` + `/LTCG` link option applied uniformly across all targets — CMake's
  default MSVC Release has neither, and `/GL` needs matching `/LTCG` everywhere or LTO silently
  fails/won't link).
- Empty module skeletons (`engine/core`, `engine/math`, `engine/platform`, `engine/render`) so the
  link graph exists.
- `find_package(Vulkan REQUIRED)` wired per ADR-0004; clear fatal message if `VULKAN_SDK` is unset.
- **Pin the Vulkan SDK version** and document it (target-env `vulkan1.3` assumed; resolve the
  "unpinned SDK" open question now for reproducibility).
**DoD:** `cmake --preset dev` configures; `cmake --build` produces empty libs for all four modules in
Debug + RelWithDebInfo + Release; `/WX` active on own targets only (never on `Vulkan::Vulkan`).
**Risks:** Ninja Multi-Config per-config flags are easy to get wrong (Release `/GL`+`/LTCG`); `/WX`
will eventually fire on a new SDK/MSVC header — keep the scoped `/wd` escape hatch and wrap `vulkan.h`
in a warning push/pop in `render/src/vk.h`. **Exercises:** Build System.

---

### M0.3 — Win32 window + clean message loop (the `sandbox` exe)  · M
**Goal:** the literal next runnable thing — a window that opens, pumps messages, and closes cleanly.
**Deliverables:**
- `engine/platform/src/win32/`: `WNDCLASSEXW`, `CS_OWNDC`, custom `WndProc`, `WM_ERASEBKGND`→1,
  `PeekMessageW` non-blocking pump, `WM_CLOSE/DESTROY` quit path.
- `platform.h` seam per ADR-0005 (window lifecycle, `platform_pump_events` → `PlatformFrameInput`,
  page-alloc, file-read-into-arena, dll, time stubs).
- `QueryPerformanceCounter` clock; `timeBeginPeriod(1)` paired with `timeEndPeriod` on shutdown.
- `tools/sandbox` exe that opens the window and runs the loop (no Vulkan yet).
- **Single-sourced minimize/resize policy** (ADR note): platform's outer loop keeps calling the sim
  hook while minimized and **skips render/acquire** when extent is 0×0; **no render thread** (drop the
  "recreate swapchain on its own thread" claim — everything is single-threaded first). Resize only
  flags `resized`; renderer recreates lazily inline at top of render.
**DoD:** `sandbox.exe` opens a titled window, stays responsive (drag/move OK), exits cleanly with no
leaked handles; `WM_KILLFOCUS` flushes held keys (no stuck-key on alt-tab).
**Risks:** modal resize/move loop blocks `PeekMessage` (drag-stutter accepted; smooth live-resize
deferred); Raw Input key decode edge cases (E0/E1, L/R modifiers) — defer full Raw Input until M3,
basic `WM_KEY*` is fine to prove the loop. **Exercises:** Platform Layer.

---

# PHASE 1 — Core Libraries (leaf-up)

**Goal:** build the dependency-free leaves every other subsystem stands on — memory, math (including
the deterministic fixed-point core), the minimal STL replacement, and the test harness — and pin them
with tests from day one.

> **Now:** M1.0 → M1.4. **Next:** Phase 2 (Vulkan). **Later:** Phase 3 (sim).

---

### M1.0 — Memory: page backend + arena + scratch  🔴  · L
**Goal:** "arenas everywhere, heap rarely" foundation.
**Deliverables:**
- `plat_mem_reserve/commit/release` (VirtualAlloc reserve + incremental commit) behind the platform
  seam.
- `Arena` (bump, O(1) reset, page-commit on growth, always-on high-water counter), `TempMemory`
  save/restore marker, double-buffered per-frame scratch.
- `Allocator` tagged-struct interface (fn ptr + state + kind), `mem_alloc/realloc/free` wrappers,
  alignment as a first-class param (default 16).
- `ASSERT` (debug) / `ENSURE`/`ASSERT_ALWAYS` (always-on) split; OOM policy: fixed-arena overrun is a
  hard assert, only page backend / general heap surface recoverable `Result`.
**DoD:** unit tests pass for alloc/alignment/reset/temp-restore/high-water; arena commits only touched
pages (verify committed << reserved for a lightly-used 256 MB arena).
**Risks:** 🔴 manual memory is where the nastiest bugs live. **ASan note:** make the **default Debug
preset NON-ASan** so Phase-2 Vulkan bring-up isn't fighting the sanitizer; add a separate `Debug-ASan`
preset and add arena poison/unpoison hooks (poison on reset, unpoison on push) so ASan earns its
slowdown later. **Exercises:** Memory.

---

### M1.1 — Math: scalar + float vectors/matrices + Vulkan projection  · L
**Goal:** the float side of the math lib, with the Vulkan clip-space contract baked in.
**Deliverables:**
- `math_types.h` POD (`vec2/3/4`, `mat3/4` column-major 64-byte, `quat`, `rect/aabb/ray/plane`).
- vec/mat/quat free functions; `mat4_trs` (T*R*S), `mat4_look_at_rh`, **`mat4_perspective_vk`** (Y-row
  negated, depth [0,1] — the Y-flip lives in exactly one place), `mat4_ortho_vk`.
- Pass-by-value for vec, by `const*` for mat. Designated initializers flagged as **C++20** (correct
  per ADR; bump affected TUs to `/std:c++20` or verify MSVC accepts under `/std:c++17 /permissive-`).
**DoD:** convention tests pass — `mat4_mul(M, inverse(M)) ≈ I`; a known world point projects to clip
**Y-down** with depth ∈ [0,1] (catches an accidental GL projection immediately).
**Risks:** column-major + Y-flip + RH is easy to get subtly wrong (mirrored/upside-down scenes);
the convention test is the guard. **Exercises:** Math.

---

### M1.2 — Math: fixed-point core + deterministic RNG  🔴  · L
**Goal:** the determinism bedrock — `fix` (Q16.16 per ADR-0002) and the seeded sim PRNG.
**Deliverables:**
- `fix.h`: `fix` typedef, `FIX_ONE`, `fix_mul`/`fix_div` (via `int64_t` intermediate — **not**
  `__int128`), table-driven `fix_sin`/`fix_cos`/`fix_sqrt` (integer Newton/LUT, libm-free), `fvec2/3`
  and ops.
- `pcg32_t` PRNG (`pcg32_seed/next/range/fix01`) per ADR-0007.
- **Determinism golden-hash test from day one:** run a fixed `fix_*` + `pcg32` sequence, hash output,
  assert identical across MSVC `/fp:precise` and `/fp:fast` (and later clang). Only the fixed/PRNG path
  is held to bit-identity, not the float lib.
**DoD:** golden hashes match across both `/fp:` modes; `fix_sqrt`/`fix_sin` within tolerance of
reference; **builds on MSVC** (the `__int128` trap is avoided).
**Risks:** 🔴 transcendental LUT resolution vs gameplay feel (revisit with real gameplay); world-scale
range — confirm the M0.1 spike answer holds. **Exercises:** Math, Determinism.

---

### M1.3 — Containers: Array / HashMap / Str / InlineArray / FreeList  · M
**Goal:** the ~95% STL replacement, allocator-aware, no exceptions, no global alloc.
**Deliverables:** `Array<T>` (geometric growth, `remove_swap`), open-addressing Robin-Hood `HashMap`
(backward-shift delete, **non-deterministic iteration — banned from sim**), length-prefixed
`Str/StrView`, `InlineArray<T,N>`, intrusive `FreeList`. Pool/`HandlePool` (handle+generation per
ADR-0003) backing object tables.
**DoD:** tests for grow/remove-swap/collision/backward-shift-delete and handle staleness
(generation-mismatch detected, not crashed); generation-0/null sentinel honored.
**Risks:** backward-shift delete + Robin Hood is subtle; pointer-as-key is a determinism landmine —
HashMap is documented as presentation/tool-only. **Exercises:** Memory/Containers.

---

### M1.4 — Test harness + CTest + headless `engine_core` + pre-push hook  · S
**Goal:** the discipline gate that makes everything above trustworthy.
**Deliverables:** ~200-line single-header `tests/test.h` (`TEST`/`CHECK`, self-registering, no
exceptions); `engine_core` aggregate target (no Win32/Vulkan) linked by the test binary; CTest
registration; the committed `pre-push` hook now actually runs `ctest`.
**DoD:** `ctest --output-on-failure` runs all Phase-1 suites green headlessly in seconds; a deliberate
failing `CHECK` makes the hook block a push.
**Risks:** harness scope-creep into a framework — resist (deferral list). `engine_core` must stay
Win32/Vulkan-free or headless testing breaks. **Exercises:** Tooling, Build (the `engine_core`/app seam).

---

# PHASE 2 — Vulkan Bring-up  🔴 (the first long pole)

**Status:** complete 2026-08-12. The scripted and owner-interactive Vulkan validation gates passed
with zero warnings/errors; Phase 3 is next.

**Goal:** the milestone ladder that takes raw Vulkan from nothing to "hundreds of instanced units on
screen." Each rung is a runnable program and a learning checkpoint. The renderer is behind the thin
seam (`renderer.h`, opaque handles, `DrawItem[]`) from the very first rung.

> **Complete:** M2.0 → M2.5. **Now:** Phase 3. **Later:** Phase 4.
> Hardware coverage beyond NVIDIA discrete is tracked in
> [`docs/testing-hardware.md`](testing-hardware.md) (G26).
> 🔴 **This whole phase is the #1 technical-risk cluster.** Synchronization correctness and the
> custom allocator are the sub-poles. Keep **validation + synchronization-validation on, always, in
> dev.**

---

### M2.0 — Instance, device, swapchain → CLEAR the screen  🔴  · XL
**Status:** complete 2026-08-12. Hand-loaded loader, instance/device/swapchain, and the
per-frame sync recipe (frames-in-flight=2 + per-image fences) are in; the full scripted
and owner-interactive validation gate (resize, minimize/restore, alt-tab, F1) passed with
zero validation messages (JOURNAL Sessions 03, 06).
**Goal:** full bring-up; clear the window to a color every frame. Proves sync + present end to end.
**Deliverables:**
- Instance (API 1.3), debug-utils messenger routing into the log, **assert only on the validation
  layer's ERROR severity — NOT on best-practices/performance warnings** (those are advisory and fire
  on correct code).
- Surface via `platform_vk_create_surface` (HWND stays in platform, per ADR-0005).
- Physical-device **scoring** (not `[0]`); queue families (graphics+present, dedicated transfer if
  present, compute claimed-idle).
- **Resolve the 1.3 / dynamic-rendering gate now:** either make `dynamicRendering`+`synchronization2` a
  documented **hard minimum spec** with a clean fatal error, or provide the classic
  `VkRenderPass`/`VkFramebuffer` path behind the same seam. Don't leave it "open."
- Swapchain: `B8G8R8A8_SRGB`/`SRGB_NONLINEAR` preferred, **MAILBOX with FIFO fallback** (MAILBOX is
  not guaranteed), `minImageCount+1` clamped. Depth image via **`vkGetPhysicalDeviceFormatProperties`
  preference list** (`D32_SFLOAT → D32_SFLOAT_S8 → D24_UNORM_S8 → D16_UNORM`), stored and recreated
  with the swapchain — do not hardcode `D32_SFLOAT`.
- **Frames-in-flight = 2.** Per-frame: command pool, primary cmd buffer, `image_available` semaphore,
  `in_flight` fence. **`render_finished` semaphore per swapchain image** (not per frame). **Add the
  `images_in_flight[]` fence array:** after acquire, wait on `images_in_flight[img]` if set, then set
  it to this frame's fence before submit — this closes the images-than-frames gap (the recipe
  half-identifies it but the per-image fence is the actual fix).
- Swapchain recreation as a first-class path: trigger on `OUT_OF_DATE`/`SUBOPTIMAL`/resize; Phase-1
  `vkDeviceWaitIdle` brute-force; **inline on the main thread** (no render thread); minimized = skip
  render, **sim keeps ticking** (do not block in `wait_events` on a netcoded client).
- **Allocator Phase 1 (naive):** one `VkDeviceMemory` per resource (`vk_alloc_dedicated`), asserts
  **conservatively below** the 4096 `maxMemoryAllocationCount` guaranteed minimum (e.g. ~3500) to leave
  headroom for swapchain/depth; live count surfaced on the debug overlay later.
**DoD:** window clears to an animated color at vsync; **zero validation errors** across a resize, a
minimize/restore, and an alt-tab; clean shutdown (`vkDeviceWaitIdle` then destroy, no leaks reported).
**Risks:** 🔴 synchronization is the single most defect-prone area in the engine — document the
per-frame wait/signal recipe in code; 1.3/dynamic-rendering hardware gate is a real runtime gate, not a
deferral. **Exercises:** Renderer, Platform (surface), Memory (alloc Phase 1), Build (shaders not yet).

---

### M2.1 — First triangle (pipeline + offline SPIR-V)  · M
**Status:** complete 2026-06-11. First graphics pipeline + offline SPIR-V (`add_shader_library`,
depfile-driven) + on-disk pipeline cache; triangle screenshot-verified by in-process readback
(JOURNAL Session 04).
**Goal:** first graphics pipeline; hardcoded verts in the shader.
**Deliverables:** `add_shader_library` producing `triangle.vert/.frag.spv` (ADR-0008,
`MOBA_SHADER_DIR`); pipeline created from a small static registry; on-disk `VkPipelineCache` loaded at
startup / saved at shutdown.
**DoD:** a colored triangle renders; editing/rebuilding the shader and re-running shows the change;
pipeline cache file appears and is reused on second run.
**Risks:** vertex-input vs hardcoded-in-shader confusion — keep it hardcoded here. **Exercises:**
Renderer, Build (shader pipeline).

---

### M2.2 — Textured quad (buffers + image upload + descriptors)  🔴  · L
**Goal:** first real GPU memory traffic — VB/IB, an image, a sampler, a descriptor set.
**Deliverables:** vertex/index buffers (DEVICE_LOCAL via staging + transfer copy); a texture image
(create/allocate/upload/layout-transition/view); a tiny fixed sampler set; descriptor pool + `set=1`
(per-material texture+sampler). Pixels come from a **hardcoded/direct TGA** loader — **bypass the asset
manager**, which doesn't exist yet (reviews: the "textured quad day one via TGA" must not presuppose
the renderer upload path before this rung).
**DoD:** a textured quad renders with correct UVs and sRGB color; staging round-trip validated; no
validation errors on upload/barrier.
**Risks:** 🔴 image-layout barriers and `bufferImageGranularity` (handle it in the staging/dedicated
paths, not only the future block allocator); **HOST_COHERENT memory needs no flush** — drop
`nonCoherentAtomSize` from coherent-arena alignment math (it only governs non-coherent flush ranges).
**Exercises:** Renderer, Memory (staging/alignment).

---

### M2.3 — Camera UBO + instanced meshes (the first "MOBA-looking" frame)  🔴  · L
**Status:** implemented 2026-08-12; validation-clean scripted DoD complete.
**Goal:** hundreds of instances of one mesh+material in **one** draw call.
**Deliverables:** `set=0` per-frame view/proj UBO (persistently-mapped HOST_VISIBLE|HOST_COHERENT ring,
written each frame, no staging); per-frame storage-buffer instance stream; public `DrawItem` sort by
`(pipeline, material, mesh)` → coalesced `vkCmdDrawIndexed(instanceCount=N)`; model matrices live in
the instance stream and the push constant is the 4-byte `instance_base`. Mesh data remains
programmatic (real glTF arrives in Phase 4).
**DoD:** 500+ instances of a cube render at vsync from one batched draw; moving the camera updates the
UBO; frame time stable.
**Risks:** instance data as storage buffer (`gl_InstanceIndex`) vs vertex-input — pick storage buffer
per the design; descriptor-pool sizing. **Exercises:** Renderer (the core perf move).

---

### M2.4 — Debug-draw + minimal overlay  · M
**Status:** complete 2026-08-12; scripted visual DoD and owner F1 interaction verified.
**Goal:** the daily debugging workhorse, rendered last in the same pass.
**Deliverables:** immediate-mode `dbg_line/sphere/aabb/text_2d` queued into a frame arena and drained by
a flat-color `debug_line` pipeline + a `dbg_text_2d` path (placeholder font ok); F1 overlay showing
FPS/frame-time, per-arena bytes, **live VkDeviceMemory allocation count**, draw calls.
**DoD:** debug lines/text appear over the scene and reset each frame; overlay toggles with F1.
**Risks:** debug-draw verts must come from the frame arena (no per-call alloc) and never feed back into
sim. **Exercises:** Tooling (debug systems), Renderer.

---

### M2.5 — Renderer seam audit + null backend  · S
**Status:** implemented 2026-08-12; Vulkan and null backends build together and have headless coverage.
**Goal:** prove the seam is real before sim/assets depend on it.
**Deliverables:** confirm the game/sandbox includes only `renderer.h` (no `vulkan.h` on its include
path — compiler-enforced via PRIVATE link); a stub **null backend** implementing the seam (validates
handle logic, draws nothing). Unify the **upload/creation API** here: Renderer owns
`renderer_create_mesh/texture` → typed handles; **pull deferred-destroy (`frames_until_free`) into the
destroy signatures** (asset hot-reload and normal teardown both need it); `TextureDesc/MeshDesc` are
single shared structs in `renderer_types.h`.
**DoD:** swapping to the null backend compiles and runs (blank window, valid handles); grep confirms no
Vulkan symbol leaks above the seam.
**Risks:** the Asset↔Renderer upload API mismatch (the reviews' tightest-seam finding) — settle it here
so Phase 4 consumes exactly this. **Exercises:** Renderer seam, Build (PRIVATE Vulkan link).

---

# PHASE 3 — Deterministic Simulation  🔴 (the second long pole)

**Goal:** a fixed-tick, fixed-point, handle-based ECS with a hard SIM/PRESENTATION boundary — and the
**determinism harness built before there is content to break it.** This is the project's spine and the
reviews' most-watched risk.

> **Complete:** M3.0–M3.4. **Gate:** land the interphase security consolidation. **Next:** Phase 4
> (assets). **Later:** Phase 5 gameplay begins on the cooked asset seams.
> 🔴 Determinism is global and fragile: one stray float, unordered iteration, or wall-clock read
> desyncs. The state-hash self-check is the only humane way to catch it — **build it first.**

---

### M3.0 — Determinism harness FIRST: state-hash + record/replay  🔴  · L  ✅ COMPLETE 2026-08-12
**Goal:** the desync detector, standing up before the sim has any real systems.
**Deliverables:**
- A trivial placeholder `SimWorld` (a few SoA arrays of `fix`) + `sim_init(seed)` / `sim_tick(world,
  cmds)`.
- `sim_hash_state` = FNV-1a over the live ranges of every gameplay-affecting array **in fixed order**
  (positions/velocities/health/cooldowns/RNG state) — fixed-point bytes only, **never** render/timing/
  debug data.
- `Replay_Header` (magic, version, **sim_logic_hash**, seed, tick_rate, player_count) + per-tick
  command stream record/playback through the **platform file API**.
- `test_determinism.cpp`: run N ticks twice from the same seed+inputs, assert hash streams identical
  every tick; on mismatch print the first divergent tick.
**DoD:** the run-twice self-check passes for 10,000 ticks of placeholder sim; a deliberately-injected
nondeterminism (e.g. an uninitialized field) is caught at the exact tick.
**Observed:** Debug and Release `/WX` builds pass all 18 CTest entries. The recorded/replayed
10,000-tick stream ends at `0xb85d4b632571948c`; an injected mutation reports exactly
`tick=4321 field=position_x unit=7`. Replay record/inspect/verify and corrupt-file exit classes are
covered through CTest fixtures, and the boundary scan confirms `eng_sim → core + math + serialize`.
**Risks:** 🔴 hash-input layout is keyed to the fixed-point ADR — must be settled (it is, M0.1). Full-
state hash every tick is fine at 30 Hz / hundreds of units; revisit dirty-hashing only if profiled.
**Exercises:** ECS, Tooling (the centerpiece), Math (fixed), Memory.

---

### M3.1 — Entity model + sparse-set SoA component pools  · L  ✅ COMPLETE 2026-08-12
**Goal:** the real ECS storage.
**Deliverables:** `EntityId` (handle+generation per ADR-0003), `EntityManager` free-list, deferred
end-of-tick destruction; `ComponentPool` (sparse + dense + back-ref); concrete SoA pools (Transform/
Velocity/Health) with `fix` fields; `pool_add/remove/has/get`.
**DoD:** add/remove/has/get unit-tested; generation guards a stale `EntityId`; pools live entirely in
arenas (no malloc).
**Observed:** `SimWorldConfig` defaults to 16,384 entities and 64 stable replay units; transactional
arena sizing, 16-bit generations plus explicit liveness, ascending-fresh/LIFO-recycled identity,
typed Transform/Velocity/Health pools, and ordered deferred destruction are implemented. Replay v1
retains 16-byte slot commands and deliberately bumps `SIM_LOGIC_HASH` to `0x7902599e173f87a6`.
Canonical ECS hashing excludes pointers, unused capacity, and sparse/dense storage order while
covering lifecycle, mappings, queue order, membership, and every typed value. Debug and Release
`/WX` builds pass all 22 CTest entries; the 10,000-tick golden is `0x981212877a575730`, and the
controlled mutation reports `tick=4321 field=position_x entity=7`. M3.2 follows on its separate
completed execution record in [`slate-moba-phase3-m3.2.md`](slate-moba-phase3-m3.2.md).
**Risks:** swap-remove reorders dense — see M3.2. **Exercises:** ECS, Memory.

---

### M3.2 — Systems + explicit schedule + ordered iteration  🔴  · M  ✅ COMPLETE 2026-08-12
**Goal:** the fixed-order system schedule with **deterministic iteration as the default**.
**Deliverables:** `SimWorld` aggregate (entities, pools, `tick`, `Rng`, event queues,
`pending_destroy`); plain free-function systems; a hand-written ordered `sim_tick` schedule;
double-buffered event queues drained in append order (no callbacks).
- **Iteration rule made literal:** order-sensitive systems iterate by **ascending entity index** via a
  per-pool sorted index array rebuilt only on membership change (not every tick). Document which
  systems are commutative (safe to iterate dense — e.g. pure integration) vs order-dependent, so the
  sort cost isn't paid where it isn't needed. (Reviews: the flagship `sys_movement` example violated
  the doc's own rule — fix the pattern here.)
**DoD:** a movement + a damage-event system run under the schedule; determinism self-check (M3.0) still
green with real pools; no `HashMap`/pointer-order iteration anywhere in sim.
**Observed:** Each component pool owns an arena-backed derived ordered view rebuilt atomically after
membership changes. A fixed-capacity, phase-buffered 12-byte `DamageEvent` queue feeds prefixed
free-function command, movement, combat, cooldown, RNG, and destruction systems through one literal
`sim_tick` schedule. Damage resolves in append order in the same tick; moving only queue publication
retains the future next-tick experiment seam. Queue overflow is preflighted before any command
mutation. Replay remains format v1 with 16-byte commands and logic hash `0xab96814425ba80a4`;
canonical state includes logical event phases while excluding physical buffers and ordered caches.
Debug and Release `/WX` builds pass all 25 CTest entries, the new 10,000-tick golden is
`0x637628abff59c823`, and the controlled mutation still reports exactly
`tick=4321 field=position_x entity=7`. M3.3 followed on its separate completed execution record in
[`slate-moba-phase3-m3.3.md`](slate-moba-phase3-m3.3.md).
**Risks:** 🔴 accidental dense-order iteration in an order-dependent system; per-tick sort budget at
scale. **Exercises:** ECS, Determinism.

---

### M3.3 — Fixed-tick loop + SIM/PRESENTATION snapshot boundary  🔴  · M  ✅ COMPLETE 2026-08-13
**Goal:** the accumulator loop and the single, one-directional fixed→float interpolation seam.
**Deliverables:**
- The accumulator lives in the **platform's outer loop** (it owns the OS pump + clock); the ECS
  "loop.c" and netcode "Stage 0 loop" are re-described as **what `engine_frame`/`engine_render` call
  into**, not independent loops (reviews: three "the loop" specs reconciled to one).
- `SIM_DT_FIXED` from `sim/sim_config.h` (derived from core's 30 Hz); core's 0.25s clamp
  remains the spiral-of-death guard.
- `RenderSnapshot` (slim, interpolatable presentation fields); `snapshot_extract` (fixed) at tick end;
  double-buffered prev/curr.
- **One named owner for interpolation + fixed→float + DrawItem building: the game/present glue.** It
  takes `(snap_prev, snap_curr, alpha)` → `DrawItem[] + FrameView`. The **renderer stays a pure
  DrawItem consumer and never sees `SimWorld`**; fixed→float happens in exactly this one place
  (reviews: ownership was specified three ways — settle here).
**DoD:** sim ticks at fixed 30 Hz independent of render rate; objects render smoothly interpolated;
minimized window keeps ticking (sim) while skipping render; determinism self-check unaffected by render
rate.
**Observed:** `eng_platform` now owns a window-independent `PlatformFixedStep` helper that clamps
total debt to 0.25 seconds, reports/consumes owed ticks, and computes alpha without seeing game or
sim. The sandbox generates a fresh same-tick command buffer inside every owed-tick iteration, then
runs `sim_tick` → `present_capture` → consume; failure retains debt and terminates the loop.
`eng_game` transactionally allocates two fixed 64-slot snapshots from an arena, extracts through
const Transform views, records actual live count, interpolates previous→current, and uses current
state directly across an `EntityId` change. Debug, RelWithDebInfo, Release, and Debug-ASan pass all
28 CTest entries, including 14 adversarial hosted-smoke classifications. Render-rate grouping and
minimized-loop tests preserve the unchanged
`0x637628abff59c823` oracle; a two-tick catch-up frame proves distinct per-tick command generation;
the controlled mutation remains `tick=4321 field=position_x entity=7`. The fresh-clone path and a
90-frame validation-clean RTX 4070 Ti run produce a 1280×720 screenshot with 64 objects.
**Risks:** 🔴 the fixed→float boundary is exactly where determinism silently rots — enforce
one-directional flow by construction (renderer can't see sim). The sim and presentation boundary
CTest scans plus the run-twice hash are the current backstops; M3.4 centralizes compiler policy and
proves the runnable/test binary paths cannot drift.
**Exercises:** ECS, Platform (loop), Renderer (consumes glue output), Tooling.

---

### M3.4 — Sim lib isolation + flag pinning  · S  ✅ COMPLETE 2026-08-13
**Goal:** make the deterministic island structurally enforced.
**Deliverables:** `eng_sim` static lib links **nothing** from render/present; pinned `/fp:strict` (or
`/fp:precise`) for sim in **one** toolchain include so the test binary and the game binary compile sim
**identically** (reviews: Debug/Release and test/game must be bit-identical); grep-lint for floats in
sim wired into the pre-push hook.
**DoD:** `eng_sim` builds with no render/platform-OS deps; the float-grep lint is part of `ctest`/hook;
self-check passes from both the test binary and the game binary.
**Observed:** `moba_sim_determinism` is the sole `/fp:precise` policy owner for all nine current
`eng_sim` implementation sources. Generated compile-command checks prove every source is compiled
once per Debug/RelWithDebInfo/Release configuration, only into `eng_sim`, with only sim/core/math/
serialize include roots. Configure-time source/include/direct/exported-link contracts and adversarial
fixtures fail closed on platform/render/game imports, path traversal, system-include injection,
policy drift, and accidental recompilation. A caller-storage-backed oracle runs through the direct
test probe and both Vulkan/null game binaries from the same archive; all emit 923 commands, final
`0x637628abff59c823`, stream digest `0x6f381609f7e59f0c`, and unchanged logic hash
`0xab96814425ba80a4`. Full `/WX` Debug, RelWithDebInfo, Release, and Debug-ASan suites pass 32/32;
clang-cl 19.1.5 plus UBSan passes its runtime tripwire and instrumented oracle; local and hosted
fresh walks complete 90 validation-clean frames. See the closed execution record in
[`slate-moba-phase3-m3.4.md`](slate-moba-phase3-m3.4.md). PR #38 landed as `63f0b1de`; the
interphase security gate below is the remaining pre-Phase-4 boundary.
**Risks:** flag drift between binaries is a silent desync source — single toolchain include prevents it.
**Exercises:** Build (sim isolation), Tooling, Determinism.

---

### Interphase security consolidation — boundary hardening  · S  🔒 FINAL LOCAL ACCEPTANCE COMPLETE 2026-08-13
**Goal:** reconcile the accepted security drafts into one reviewable head before asset APIs expand the
runtime boundary.
**Observed:** PR #51 retains or strengthens every approved hunk from #27 and #29–#46. Core allocation
arithmetic and pool corruption fail before mutation; platform atomic writes refuse pre-existing
temporaries; Vulkan and shader paths fail closed; sandbox/replay/test/visualizer inputs are canonical
and bounded; renderer capacity checks are overflow-safe; CI has a manual recovery entrypoint,
read-only token scope, immutable checkout pins, and an explicit winget source. Full `/WX` Debug,
RelWithDebInfo, and Release suites pass 39/39 in each configuration; Debug-ASan passes 39/39;
clang-cl/UBSan passes 6/6; a clean fresh walk and an RTX 4070 Ti run complete 90 validation-clean
frames. Replay v1, command encoding, `SIM_LOGIC_HASH = 0xab96814425ba80a4`, final oracle
`0x637628abff59c823`, and exact tick-4321 divergence remain unchanged.
**Gate:** fresh-context security/acceptance verdicts and exact-head CI/CodeQL must pass before PR #51
becomes ready. Merge and stale-draft retirement remain separately owner-approved. No M4.0 asset work
is part of this consolidation.
**Review correction:** candidate `a89caf6` was kept draft after security review found a cleanup
path-swap interval, a real-renderer corrupt-count gap, and fail-open CLI grammar. Object-bound cleanup,
shared renderer preflight, and complete fail-closed grammar repairs now pass the full local matrix at
code checkpoint `f0a56c4`; fresh full security/acceptance reviews remain before ready state.
Fresh full review of documentation head `7a0d130` then found descendants were still classified and
recursively removed by path. Every node is now handle-bound with reparse points treated as leaf
links; the exact child-replacement/outside-sentinel regression and focused security rereview pass.
The first committed-head walk passed configure/build/tests/render and then failed closed on Git's
read-only pack files. Bound-handle `READONLY` clearing and a permanent read-only/junction fixture now
pass the complete local matrix at `6ae70c9`; its committed-head walk builds 92 targets, passes
39/39, renders 90 validation-clean frames, and completes secure cleanup with `FRESH-WALK OK`.

---

# PHASE 4 — Asset Pipeline

**Goal:** the path from source files to GPU/CPU memory behind stable handles, built bottom-up: direct
trivial loaders first, then the offline cooker and the unified baked format, then glTF and hot-reload.

> **Blocked:** owner-merge PR #51 and synchronize a green `main`. **Then:** M4.0 → M4.4. **Next:**
> Phase 5 (gameplay needs real meshes/maps). **Later:** hot-reload polish.

---

### M4.0 — Direct TGA / WAV / SPIR-V loaders + handle registry  · M
**Goal:** the simplest runtime loaders + the SoA asset registry, replacing M2.2's hardcoded TGA.
**Deliverables:** direct TGA (uncompressed/RLE) and WAV (header+PCM) parsers; SPIR-V already loaded as
raw `.spv`; `AssetRegistry` (SoA, handle+generation per ADR-0003, `state` field reserving async later);
**arena-per-level** lifetime + a small refcounted global pool; consumes the **Renderer upload API
unified in M2.5**. **`tools/sandbox/src/tga_direct.*` moves into `eng_assets` here (G39)** — the
app-layer decoder that tests currently compile from the sandbox folder is promoted to the real parser,
and the tests' include path follows it (closing the ADR-0009 contamination path in miniature).
**DoD:** the textured quad now loads its texture through the asset manager (not hardcoded); a WAV loads
to PCM; `assets_unload_level` bulk-frees and bumps generations (stale handles detected).
**Risks:** determinism rule — sim references assets by **stable id only**, never pointer/load-order.
**Exercises:** Assets, Renderer (upload seam), Memory (arenas).

---

### M4.1 — The cooker + unified `.mba` container  · L
**Goal:** move all heavy parsing offline into a separate tool; one runtime loader, one format.
**Deliverables:** `cooker.exe` (separate target, links the shared `asset_parsers` lib — **POD C
interface only**, identical exception flags per ADR-0009); `MbaHeader` (magic+version+type+id) + typed
payloads; one runtime `asset_load` switching on type tag; **brute-force re-cook everything** (no
incremental yet); byte-deterministic output (determinism reproducibility); CMake `content` target gates
the game.
**DoD:** cooker bakes a TGA→`.mba` texture; the runtime loads it via the single loader; version/magic
mismatch hard-rejects and triggers re-cook.
**Risks:** over-engineering the cooker (no dep-graph/incremental yet); `asset_parsers` is the
STL-contamination path — keep it POD-only. **Exercises:** Assets, Build (cooker target).

---

### M4.2 — PNG (own inflate) in the cooker  · L
**Goal:** production texture authoring without runtime DEFLATE.
**Deliverables:** own DEFLATE/inflate (contained sub-project, **cooker-only**); PNG parse → RGBA8 + mip
generation → `.mba`. Runtime is unchanged (still memcpy-and-upload).
**DoD:** a PNG cooks to the same `.mba` shape as TGA and renders identically; inflate fuzz-tested
against reference output.
**Risks:** inflate is a real time sink — it's off the critical path (TGA covers early needs) and
perf-irrelevant (cooker). **Exercises:** Assets.

---

### M4.3 — glTF (.glb) → baked SoA mesh + GPU mesh upload  · L
**Goal:** real models, never parsed at runtime.
**Deliverables:** cooker parses a **strict glTF subset** (triangle lists, single UV, standard PBR-ish
material refs, optional single skin; hard-error on the rest — morph targets, sparse accessors, exotic
extensions); bakes flat SoA vertex streams + index buffer + material refs in the **vertex layout the
renderer defines**; `renderer_create_mesh` consumes it. Decide interleave-hot-core
(pos/normal/uv) vs split rarely-used streams against the renderer's vertex-input.
**Frustum culling owns a milestone here (G27):** the baked AABBs are consumed by a camera-frustum
test against each `DrawItem`'s mesh AABB — culled items are dropped before batching. The AABB bakes
in M4.3, so the cull lands in M4.3, not later.
**DoD:** a `.glb` cooks and renders as instanced meshes (replacing M2.3's programmatic cube); AABB baked
and used for frustum culling.
**Risks:** glTF spec is large — the strict-subset + hard-error discipline is the guard against stalling.
**Exercises:** Assets, Renderer, Math.

---

### M4.4 — Hot-reload: shaders, then textures  · M
**Goal:** the high-iteration-value dev feature.
**Deliverables:** `ReadDirectoryChangesW` behind a generic `platform_watch_dir` seam; debounce →
re-cook the changed source → atomic swap behind the **same stable handle** → defer old-GPU-resource
destroy by `FRAMES_IN_FLIGHT` (the destroy signature from M2.5). Shaders first (recompile + rebuild
affected pipeline; **keep old pipeline + log on compile failure**, never crash), then textures.
Dev-only (`#if MBA_HOT_RELOAD`).
**DoD:** editing a `.glsl` updates the rendered result without restart; a shader typo logs and keeps the
last-good pipeline; editing a source PNG updates the texture live.
**Risks:** atomic-swap race (swapping a resource a command buffer in flight still references) — the
`FRAMES_IN_FLIGHT` deferral + frame-boundary swap is the mitigation. **Exercises:** Assets, Platform
(watcher), Renderer.

---

# PHASE 5 — Gameplay (single-player)

**Goal:** turn the deterministic ECS + renderer + assets into an actual playable arena — terrain,
mass-unit movement, the order/command model, abilities, combat, AI, fog — **all single-player and
deterministic**, ready to drop the local command source into netcode in Phase 6.

> **Now:** M5.0 → M5.6. **Next:** Phase 6 (netcode). **Later:** vertical-slice polish.
> Everything here is fixed-point and runs inside `sim_tick`; the determinism self-check stays green
> throughout (it is the regression net for all of this).

---

### M5.0 — Map grid (sim authority) + derived render heightfield  · M
**Goal:** the single fixed-extent tile grid that feeds path/collision/vision; render mesh decoupled.
**Deliverables:** `MapGrid` SoA (flags: walkable/block-vision/ramp, `height_band` as a small integer
**not a float**, `cost`); integer cell↔world conversions; baked from an authored source (PNG-as-tilemap
via the M4.2 parser) into `.gamedata`; a derived low-res heightfield mesh uploaded once to the renderer.
**DoD:** the map renders as ground geometry; cell flags queryable; sim never reads a float elevation.
**Risks:** map dims/cell-size (256² @ 0.5u assumed) drive flow-field cost/memory — validate against real
arena design. **Exercises:** Gameplay (map), Assets, Renderer (static mesh).

---

### M5.1 — Tiered movement: flow fields + A* + grid avoidance  🔴  · XL
**Goal:** hundreds of units moving without stacking, deterministically.
**Deliverables:** goal-keyed **flow field** (integer Dijkstra with a bucketed priority queue, ties by
cell index — no floats/unordered containers; small LRU cache ~16 fields) for mass movement; grid **A***
(binary heap keyed `(f, cell_index)`, octile heuristic in fixed-point) for sparse hero pathing;
fixed-point **grid-bucketed RVO-lite** separation pass (uniform spatial hash, ascending-`EntityId`
order) for anti-overlap. **Quantized 8-direction movement** acceptable for v1; path smoothing deferred.
**DoD:** 200+ units flow to a shared goal in one field build; heroes A* to arbitrary points; units don't
permanently overlap; determinism self-check green with movement active.
**Risks:** 🔴 flow-field rebuild spike if many distinct goals in one tick (cache thrash vs tick budget);
visible clumping/jitter is a known trade for determinism (ORCA/boids rejected as float-heavy/order-
sensitive). **Exercises:** Gameplay (movement), Math (fixed), ECS.

---

### M5.2 — Selection (client-only) + command/order system  🔴  · M
**Goal:** the determinism boundary — player intent becomes ordered `Command`s.
**Deliverables:** **client-side** `Selection` (box/click picking against rendered float positions — view
concern, **never** in sim); per-entity `OrderQueue` (move/attack-move/attack-unit/cast/hold/stop,
shift-queue); `Command` struct (issuing player, tick, target entities, order) as the **only** thing that
crosses into sim (and later the wire). Raw input → quantized `Command`, never raw mouse pixels/floats
into sim.
**Raw Input owns a home here (G38):** the M0.3-deferred upgrade from `WM_KEY*`/`WM_LBUTTON*` to
`RegisterRawInputDevices` (keyboard + mouse, E0/E1 and L/R modifier decoding) lands in this milestone —
selection and commands need unaccelerated, low-latency mouse state and reliable multi-key state.
**DoD:** click-to-move and attack-move work; selection is provably outside the hashed state (two
different local selections produce identical sim hash); commands apply on a known tick.
**Replay seam note (G23):** `SIM_MAX_UNITS = 64` caps per-tick commanded units (the replay codec
addresses unit slots). If a scale test shows >64 commanded units per tick, widening is a
deliberate replay-format + logic-hash break — schedule it as its own reviewed change, not a
silent bump.
**Risks:** 🔴 selection leaking into sim would desync — keep it strictly view-side. `EntityId` is the
**unsigned** ECS type (ADR-0003), not gameplay's old `int32_t`. **Exercises:** Gameplay, ECS, Platform
(input→command).

---

### M5.3 — Data-driven abilities + projectiles + status effects  · L
**Goal:** abilities as data, not code — a fixed effect vocabulary, no scripting VM.
**Deliverables:** flat `AbilityDef` (targeting/cost/cooldown + ordered `Effect[]` from a fixed
vocabulary: damage/heal/spawn-projectile/apply-status/dash/aoe), baked into `.gamedata`; per-entity
`AbilityState` cooldowns (SoA); `ProjectileSoA` (fixed-point advance, spatial-hash collision, on-hit
effect list); timed `StatusInstance` modifiers with stacking rules (refresh/stack/unique). AoE/spatial
queries return neighbors **sorted by `EntityId`** before applying.
**DoD:** a few hand-authored abilities (a skillshot, a heal, a slow) work end to end; cooldowns and
status durations tick in fixed-point; determinism self-check green with abilities firing.
**Risks:** fixed vocabulary may need periodic expansion as designs grow (accepted vs a VM); stacking
taxonomy needs enumeration once real abilities exist. **Exercises:** Gameplay (abilities), Assets, Math.

---

### M5.4 — Event-driven combat resolution  · M
**Goal:** one central deterministic damage pipeline.
**Deliverables:** systems emit `DamageEvent`s into a per-tick queue; a single `combat_resolve` drains
them in deterministic order — mitigation (`amount * 100/(100+armor)` in fixed-point), shields,
lifesteal, on-death (gold/XP drop, despawn, hero respawn timer), kill credit — all in one place.
**DoD:** units damage and die deterministically; kill credit correct; respawn timers tick;
self-check green.
**Risks:** ad-hoc damage application elsewhere would break the single-resolution-point invariant —
enforce by convention. **Exercises:** Gameplay (combat), ECS (events).

---

### M5.5 — Minion/tower AI (tiny state machines)  · M
**Goal:** lane life — deterministic, no behavior trees/GOAP.
**Deliverables:** minion FSM (lane-push via flow field → attack-nearest, tie-break by `EntityId` then
distance → return); tower target priority (enemy attacking allied hero > nearest minion > nearest
hero), fire on cooldown. Hero/bot AI deferred.
**DoD:** minion waves push lanes and fight; towers acquire and fire; self-check green with AI active.
**Risks:** AI iteration order must be deterministic (ascending `EntityId`). **Exercises:** Gameplay
(AI), ECS.

---

### M5.6 — Vision / fog-of-war (authoritative sim state, server-filtered)  · M
**Goal:** deterministic fog computed as **authoritative sim state** for both teams — the foundation that
the **server** later filters per client so fog is **real** (a client is never told what it shouldn't see).
**Deliverables:** per-team `vis_count` grid + sticky `explored` bitset; integer line-of-sight respecting
`CELL_BLOCKVISION`/`height_band`; all of it lives **inside the hashed `sim_tick` state** (deterministic).
The vision result is exposed as a clean query — "is entity/cell visible to team T?" — that serves two
consumers: (1) **single-player / listen-local rendering** reads only the local team's slice; (2) the
**server's interest-management filter** (Phase 6, M6.3) reads it to decide which entities each client is
allowed to receive. **No deferred maphack ADR:** under the server-authoritative model the hidden slice is
**withheld at replication time by the server**, so the anti-maphack mechanism *is* interest management —
it is **resolved by Phase 6, not deferred to "untrusted public matchmaking."** This milestone delivers
the deterministic vision authority; M6.3 delivers the per-client withholding.
**DoD:** fog hides/reveals correctly for the local team; both teams' fog is in the hashed sim state
(deterministic, replayable); the vision query is the **single source** consumed by both the local
renderer slice and (later) the server's per-client filter — so when netcode lands in M6.3, fog is real
with no sim rework.
**Risks:** vision must stay sim-authoritative (never recomputed client-side from data a client shouldn't
have) — the renderer slice is a *presentation read* of authoritative state, and the *security* boundary is
the server's M6.3 filter, not the client. Integer LOS cost at map scale (budget per tick). **Exercises:**
Gameplay (vision), Renderer (local slice), Netcode (M6.3 consumes the query as the interest filter).

---

# PHASE 6 — Netcode  🔴 (the third long pole)

**Goal:** a **server-authoritative** netcode over a self-built UDP reliability layer — one authoritative
deterministic sim on the server, clients that **predict** their own controlled entity and **interpolate**
everything else, with the server **correcting** them. The hard, valuable work (determinism + replay) is
already done (Phases 3/5), and it is now a *leveraged asset*: the server's deterministic sim gives
portable replays and reproducible debugging, and the client predicting with the **same** fixed-point
`sim_tick` reconciles drift-free (bit-for-bit agreement on the same inputs+state). No socket code until
the sim is bit-stable — which it is.

> **Now:** M6.0 → M6.7 in order. **Next:** Phase 7 (slice). **Later:** lag-compensation tuning,
> NAT traversal, matchmaking, advanced congestion control, reconnect.
> 🔴 **The hard truth of this model:** the client does **not** hold full world state (interest
> management/fog withholds it), so it **cannot** simulate the whole match. It predicts **only** the
> locally-controlled entity/units — and only against the **static map** and its **own inputs**, never
> against remote/last-known entities — and **interpolates** remote/visible entities between server
> snapshots; it never predicts what it can't see. The full deterministic sim is the **server's**. Get
> this distinction right; everything downstream depends on it. The transport must never silently drop a
> command (input loss = an unresponsive hero); the desync detector from M3.0 now runs **live** as a
> prediction-divergence + server replay-integrity check.

---

### M6.0 — Command source abstraction + replay parity  · S
**Goal:** make the sim's command input swappable (local ↔ replay ↔ network) with one seam.
**Deliverables:** route `sim_tick`'s commands through a single source interface; confirm the
**replay codec is the exact same wire codec** (one source of truth — `Cmd_Packet` shared by
replay + net); replay-version policy = **version-locked debugging artifacts** (record in ADR). In the
server-authoritative model the network source is "**client → server commands**" on the client side and
"**merged per-tick command set**" on the server side; both reduce to the same source interface feeding
`sim_tick`. Per ADR-0014, M6.0 also replaces today's atomic whole-buffer rejection with
per-command rejection reason codes; that deliberate behavior/encoding change requires a reviewed
`SIM_LOGIC_HASH` bump.
**DoD:** a recorded match replays bit-identically (the M3.0 self-check, now over real gameplay
commands); swapping the command source (local / replay / network) requires no sim change.
**Risks:** codec drift between replay and net — single shared codec prevents it. **Exercises:** Netcode,
Replay, ECS.

---

### M6.1 — Platform UDP seam (Winsock2) + reliability layer  🔴  · L
**Goal:** raw UDP transport with our own sequence/ack/reorder, behind the platform seam.
**Deliverables:** `platform_net.h` (OS-free seam) + `platform_net_win32.c` (Winsock2, non-blocking) —
**the OS-free channel/reliability logic lives in `eng_net` (testable headlessly), the Win32 impl in the
app layer** (ADR-0006, so `net` links into `engine_core` for tests). 12-byte header
(protocol-id/seq/ack/ack-bits); reliable-ordered channel (the `Command` stream client→server, and
reliable control messages server→client — handshake/match setup — piggybacked retransmit until acked) +
unreliable channel (snapshots, pings, divergence-hashes — newest-wins, stale dropped); keepalive +
timeout. Little-endian byte writers/readers with a protocol version byte; **`wr_fixed` writes
`sizeof(fix)`** per ADR-0002.
**DoD:** a loopback/LAN two-process test exchanges reliable commands with zero loss under an artificial
loss/latency/reorder shim; headless `eng_net` channel tests pass in `ctest`.
**Risks:** 🔴 subtle ack/reorder/retransmit bugs silently corrupt the command stream — **fuzz the
transport with the loss/latency/reorder shim before trusting it.** **Exercises:** Netcode, Platform
(sockets), Build (net headless seam).

---

### M6.2 — Client↔server connection, handshake, headless server loop & snapshot baseline  🔴  · L
**Goal:** the authoritative-server backbone — a client connects, the server admits it, runs the **one**
authoritative `sim_tick`, and pushes a first full-state snapshot the client can render. This is the
first end-to-end "I joined a server and see its world" rung.
**Deliverables:**
- **Server loop, two owners:** the authoritative `sim_tick` (Phase 3 accumulator, 30 Hz, no rendering)
  driven by the merged per-client command set — the **same deterministic sim** as single-player. A
  **listen-server** runs it inside the existing windowed loop (§4.2); a **dedicated headless server**
  (`tools/server` exe) runs it under a **headless accumulator** that reads `SIM_HZ` from
  `core/sim_config.h` and `platform_time_ticks` + sockets but owns **no window and no message pump**
  (a `platform_run_headless(tick_fn)` seam, or `tools/server`'s own main). Both loop owners read
  `SIM_HZ`; neither hardcodes the rate. The **server tick driver lives in `eng_net`**, so it is the
  same code in both. Add **net→sim** to the build dep graph (reviews: `net` calls `sim_tick`/links
  `eng_sim` — the graph must show it). The dedicated server links `eng_platform` for sockets+clock but
  **not** `eng_render` — that link path is explicit (it is not `eng_core_group` alone).
- **Topology, one code path:** dedicated and listen-server are the same server module; the
  listen-server's local client is just a client on a loopback transport.
- **Handshake:** connect request → challenge/response (anti-spoof, cheap) → accept carrying
  `match_seed`, `tick_rate` (from `core/sim_config.h`), assigned `player_id`/team, and protocol version;
  reject on version/seed mismatch. The **server owns the match seed** (replaces lockstep's "all peers
  agree on a seed").
- **`NetSnapshot` baseline:** server serializes a full **net snapshot** — the replicated field set
  (network id ↔ `EntityId`, transform, facing, hp, hp_max, anim_state, team, visible flag,
  spawn/despawn), distinct from the local-sim `RenderSnapshot` — via `eng_serialize`/`wr_fixed`,
  stamped with its authoritative `world.tick`; the client deserializes it and reconstructs a
  `RenderSnapshot` for the present glue. (Per-client filtering arrives in M6.3; here every client gets
  the full baseline.)
- **Client tick-clock estimate:** client tracks server tick + RTT so it can later time prediction and
  interpolation; no prediction yet — client renders the latest received snapshot directly.
**DoD:** a client connects to a dedicated server (and, separately, to a listen-server) over LAN, completes
the handshake, and renders the server's authoritative world; killing/reconnecting a client is clean;
the server keeps ticking with zero, one, or several clients attached; the dedicated server runs with no
window open.
**Risks:** 🔴 connection state machine + version/seed gating is fiddly; listen-server vs dedicated must be
**one** sim path (divergence here reintroduces desync). NAT traversal explicitly **out of scope**
(LAN/direct-IP only — deferred). **Exercises:** Netcode, ECS (server loop), Platform (sockets, headless
loop), Build (net→sim dep, headless link path).

---

### M6.3 — Delta replication + interest management (REAL fog)  🔴  · XL
**Goal:** the bandwidth-and-secrecy core of server-authority — the server sends each client a **per-client
delta** of only the entities that client is **allowed to see**. This is where fog becomes real and
maphack is structurally impossible: the hidden slice is **never transmitted**.
**Deliverables:**
- **Baseline + delta snapshots (in `eng_net`):** per client, the server diffs the current `NetSnapshot`
  against the last snapshot that client **reported receiving** (the snapshot sequence the client
  piggybacks on its input packets — *not* a reliable-channel ack), sending only changed
  fields/entities; if the reported baseline is unknown/too old, it falls back to a full baseline.
  Snapshots/deltas are **unreliable** (newest wins). Compact `eng_serialize` encoding, `wr_fixed` for
  fixed-point fields.
- **Interest management = the per-client filter (in `eng_net`):** the server computes the **set of
  entities visible to this client** from the authoritative **vision/fog state** (M5.6) — entities
  outside the team's vision are excluded from that client's snapshot entirely (not zeroed, **absent**).
  Entities leaving vision are signaled "leave-PVS" (the client holds last-known then hides them);
  entities entering are sent as fresh baselines.
- **Entity replication model:** stable network ids ↔ `EntityId`; spawn/despawn events; per-entity
  replicated field set declared once (transform/facing/hp/hp_max/anim/team/visible), iterated in
  ascending id for determinism of the **server's** snapshot build.
- **Anti-maphack is now a property, not a hope:** because the client never receives hidden-entity state,
  a tampered client cannot reveal what it was never sent. This **resolves** the maphack limitation M5.6
  previously deferred.
**DoD:** two clients on opposing teams connected to one server see **different** worlds — each sees only
its team's vision; a unit moving out of an enemy's vision **stops updating on that enemy's client** and
its hidden movements are provably **not present** in that client's received bytes (packet-inspected);
bandwidth scales with *visible* entities, not total. The two-clients-see-different-worlds check runs
in-process (Stage 2) with no UDP because the snapshot/delta/interest builder is in `eng_net`.
**Risks:** 🔴 the highest-complexity rung — per-client delta state, baseline tracking, and
PVS-enter/leave correctness are each subtle; a leak in the filter is a maphack. Snapshot-build cost on the
server scales with clients × visible entities (budget it). **Exercises:** Netcode (replication/interest),
Gameplay (vision as the filter source), Serialize, ECS.

---

### M6.4 — Client prediction (controlled entity) + server reconciliation  🔴  · XL
**Goal:** responsiveness without lag — the client predicts its **own** controlled entity/units locally by
running the **same** deterministic `sim_tick` over its un-acked inputs **against the static map only**,
then **reconciles** against the server's authoritative state, while **interpolating** every
remote/visible entity between snapshots.
**Deliverables:**
- **The client timeline:** a **prediction/command-issue clock** = `estimated_server_tick + send_ahead_lead`
  (inputs arrive just-in-time) and a **render/interpolation clock** = `latest_received_snapshot_tick −
  interp_delay`, with `interp_delay` an **integer tick count** (~2 snapshots). Commands carry the
  **issue tick**; targeted actions additionally carry the **render tick** (for M6.5 lag comp).
- **Local prediction (own units only, static collision only):** client tags each command with its issue
  tick, applies it immediately to a **local predicted copy of only the entities it controls**,
  resolving collision **against the static map grid (shipped in full) and its own inputs — never against
  interpolated or last-known remote entities** (those are server-authoritative). It keeps a ring of
  un-acked inputs. The exact sim step/code is reused — fixed-point guarantees the prediction matches
  what the server computes for the same inputs + static state.
- **Reconciliation:** when a snapshot arrives echoing "last input I processed from you = seq S," the
  client **snaps** its predicted entity to the server's authoritative starting state in that snapshot,
  then **replays** its still-un-acked inputs (> S) forward to now. With matching fixed-point math and
  static-only collision this is normally a no-op (drift-free). Corrections from unpredictable
  interactions (a collision/hit by a remote unit, a server-rejected input, an enemy that was outside the
  snapshot) are **expected**, smoothed visually, exact in state — and are **distinguished from true sim
  drift** by M6.6's tolerance rule (a legitimate correction is not a determinism bug).
- **Entity interpolation (everything else):** remote/visible entities are **never predicted** — the
  client renders them at the render/interpolation clock by interpolating between the two bracketing
  received snapshots (the present-glue `(snap_prev, snap_curr, alpha)` seam from M3.3, now fed by
  reconstructed `NetSnapshot`s instead of local sim). Leave-PVS entities hold last-known then hide; since
  prediction never collides against them, the flicker is purely cosmetic.
- **One presentation seam:** prediction output (own units) + interpolation output (remote units) merge
  into the same `DrawItem[]` the renderer already consumes; fixed→float still happens exactly once in
  present glue.
**DoD:** the local hero responds to input with **zero perceived input delay** under injected latency,
while remote units move smoothly (interpolated, slightly in the past); a forced server correction
(teleport/contested move) snaps the predicted hero to truth and smooths visually without desyncing;
toggling prediction off falls back to pure-interpolation rendering of the same match.
**Risks:** 🔴 reconciliation correctness depends on the client's predicted step being bit-identical to
the server's — flag drift between server and client sim builds reintroduces constant mis-prediction
(the M3.4 single-toolchain-include guard must hold across **both** binaries). Distinguishing a
legitimate correction (unseen interaction) from real sim drift needs the M6.6 tolerance rule.
Interp-delay vs responsiveness is a tuning trade. **Exercises:** Netcode (prediction/reconciliation),
ECS (client predicted sim), Determinism (cross-binary parity), Renderer (interp + correction smoothing).

---

### M6.5 — Lag compensation (server-side, hit validation)  🔴  · L
**Goal:** make fast actions feel fair under latency — when the server validates a client's targeted
action (skillshot/attack), it evaluates against the world **as the client saw it**, not as it is "now"
on the server.
**Deliverables:** the server keeps a **bounded ring of recent authoritative states** (positions/hitboxes
per tick, ~a few hundred ms) in `eng_net`; on a client action stamped with the **render tick** the client
perceived the world at, the server **rewinds** the relevant entities to that tick — **clamped to the
ring** and **validated to lie within `[now − max_rewind, now]` and to be plausible given measured RTT**
(reject implausible rewinds) — tests the hit there, then applies the result on the current tick. The
server trusts the stamped, validated render tick; it does **not** re-derive `now − RTT − interp_delay`
(that double-counts the interp the client already baked in). Fixed-point throughout, deterministic on the
server.
**DoD:** under injected latency, a client's well-aimed skillshot that visually connected **registers**;
an action claiming an out-of-bounds or implausible rewind is rejected; lag-comp on/off is a server
toggle and the match stays deterministic/replayable either way.
**Risks:** 🔴 the classic "shot-around-a-corner" fairness trade (favoring shooter vs target) — pick and
document the rule; rewind window vs memory/cheat-surface. **Exercises:** Netcode (lag comp), ECS
(historical states), Gameplay (hit resolution).

---

### M6.6 — Prediction-divergence + server replay-hash diagnostics  🔴  · M
**Goal:** catch divergence the instant it happens — both **client mis-prediction** and any break in the
**server's** deterministic replay integrity — and name the cause.
**Deliverables:**
- **Prediction-divergence monitor (client):** on each reconciliation, compare the client's predicted
  state for its controlled entity at the reconciled tick against the server's authoritative value; a
  delta **beyond the legitimate-correction tolerance** (a correction caused by an unseen-entity
  interaction is benign; persistent/structural divergence means the client's sim is drifting from the
  server's — build/flag drift, a stray float) is flagged. Log the **first divergent tick + field**, show
  the live divergence count on the F1 overlay.
- **Server replay-hash integrity:** the server records its input stream + per-tick `sim_hash`; replaying
  the recorded inputs offline must reproduce the same hash stream tick-for-tick (the M3.0 self-check, now
  guaranteeing the authoritative match is perfectly reproducible). On mismatch, dump the first divergent
  tick and field-diff the state.
- **Field-diff comparator** (reused from the determinism harness) names the exact array/entity/tick that
  diverged, for both the client-vs-server and server-replay cases.
- **Tolerance rule:** an explicit predicate separating "benign correction" (unseen interaction; resolves
  in one snapshot) from "true drift" (recurs, grows, or appears on inputs that touched only static
  state) — so the monitor reports drift, not normal corrections.
**DoD:** an intentionally-injected nondeterminism in the **client's** predicted step is reported as
"predicted entity X field Y diverged from server at tick T" (not just "feels wrong") and is **not**
masked by the tolerance rule; a recorded server match re-simulates to an identical hash stream, and an
injected server nondeterminism is caught at the exact tick.
**Risks:** 🔴 a divergence tells you *that*, not always *why* — the field-diff comparator reduces but
doesn't eliminate debugging cost; the benign-correction-vs-true-drift tolerance is the subtle part.
**Exercises:** Netcode, Tooling (the comparator), Determinism.

---

### M6.7 — Determinism-leak audit + cross-binary parity + CI gate  · S
**Goal:** harden the contract now that it's exercised across the client↔server boundary.
**Deliverables:** sweep sim for float/wall-clock/pointer-order leaks (the grep-lint from M3.4 + manual
audit); ensure RNG state + `fix` cross the wire with `sizeof(fix)` layout; verify the **server and client
build sim with identical flags** (single toolchain include) so client prediction reconciles bit-for-bit;
optionally a single GitHub Actions job (configure + build + `ctest` on Windows) to catch `/WX` and
determinism/parity regressions.
**DoD:** lint clean; the server replay-hash reproduces across Debug and Release builds; a client built
Debug predicting against a server built Release reconciles with zero spurious corrections (cross-binary
parity holds).
**Risks:** flag drift between the server binary and the client binary is now the primary silent-desync
source (it directly corrupts prediction) — the M3.4 single-toolchain-include guard must hold for both.
**Exercises:** Netcode, Tooling, Build.

---

# PHASE 7 — Vertical Slice → Playable Prototype

**Goal:** the smallest thing that is recognizably a MOBA match — HUD/text, audio, a win condition,
two teams, and packaging — turning the systems into something a friend can play end to end.

> **Now:** M7.0 → M7.4. **Next:** Phase 8 hardening as needed.

---

### M7.0 — Bitmap-font text + HUD  · M
**Goal:** real on-screen text (replacing M2.4's placeholder), HUD, minimap.
**Deliverables:** baked **bitmap-font atlas** (cooker or hand-made) + glyph metrics → textured-quad text
path; HUD (hero HP/mana/cooldowns, gold/XP, timers); minimap reading the server-filtered visible slice (real fog). **TTF rasterization
deferred** (atlas first).
**DoD:** readable HUD and floating combat text render; minimap shows the local team's vision.
**Risks:** SDF text deferred (atlas may look soft when zoomed — accepted for v1). **Exercises:** Gameplay
(UI), Renderer, Assets (font).

---

### M7.1 — Audio (WAV mixer, presentation-side)  · M
**Goal:** sound — strictly downstream of the sim, observing events.
**Deliverables:** a simple mixer playing baked WAV PCM; sounds triggered by **sim events read on the
presentation side** (death/hit/cast), using the **presentation RNG** (xoshiro), never poking sim.
OGG/Vorbis deferred.
**DoD:** ability/hit/death sounds play in time with gameplay; muting/changing audio cannot affect the
sim hash.
**Risks:** audio code must never read or mutate sim state (determinism). **Exercises:** Audio
(presentation), Gameplay (events).

---

### M7.2 — Match rules: two teams, towers, nexus, win condition  · M
**Goal:** an actual game with a start and an end.
**Deliverables:** team spawns, lane minion waves on a timer, tower/inhibitor/nexus objectives, victory
on nexus destruction; match-start seed owned by the authoritative server, feeding the sim RNG (embedded in replays).
**DoD:** a full match can be won/lost; the result is identical on all peers (hash-verified at match end).
**Risks:** content/balance scope creep — keep one lane + minimal jungle for the slice. **Exercises:**
Gameplay, Netcode (seed agreement).

---

### M7.3 — Packaging (loose-baked → single pak, installable)  · M
**Goal:** a distributable build.
**Deliverables:** cooker emits a single `pack.pak` (concatenated `.mba` + TOC, mmap'd) for ship; the
`fs_open_asset(asset_id)` file-source abstraction hides loose-vs-pak so the loader is identical;
`install()` rules copy `.spv` + pak + exe into a clean layout. Compression deferred (TOC reserves a
`compressed_size` field).
**DoD:** a Release build runs from the install dir on a clean machine (with the Vulkan loader present),
loading all content from the pak.
**Risks:** install layout + shader/asset placement — decide before first distributable. **Exercises:**
Assets, Build (install).

---

### M7.4 — Slice polish + ADR/ARCHITECTURE refresh  · M
**Goal:** make the prototype feel real and the docs honest.
**Deliverables:** camera (steep-pitch perspective per math design, edge-pan, zoom clamps), basic VFX via
presentation RNG, frame-pacing pass; refresh `ARCHITECTURE.md` and ADRs to match what was actually
built; tag `v0.x-playable-prototype`.
**DoD:** a friend can play a full LAN match start to finish; docs match reality; milestone tagged.
**Risks:** polish is bottomless — timebox to "playable," not "shipped." **Exercises:** all.

---

# PHASE 8+ — Hardening & Optionals (revisit when measured pain or scope demands)

Each item sits behind an existing seam and is built **only when its trigger fires** — never
prematurely. No ordering between them beyond "when needed."

> **Shipped, not optional:** CI (GitHub Actions, Windows/MSVC, Debug + Release, `/WX` +
> `ctest`) landed in Session 02 and is enforced on `main` by the `main-gate` ruleset
> (required checks `windows-msvc (Debug)`/`(Release)`, 2026-08-12). Items below are the
> remaining Phase 8 options; none is built until its trigger fires.

| Item | Trigger | Effort | Notes |
|---|---|---|---|
| **Vulkan allocator Phase 2 (block sub-allocator)** 🔴 | Phase-1 dedicated-alloc assert fires (~3500 allocs) or real assets grow | L | 256 MB blocks per memory-type, free-list + coalesce, alignment + `bufferImageGranularity`, persistent staging ring; stable `vk_alloc` interface unchanged. |
| **Dedicated transfer queue + ownership transfers** | streaming/upload cost shows on profile | M | Move uploads off graphics queue; queue-family ownership barriers. |
| **Reversed-Z depth** | depth-fighting appears at map scale | S | One-function change in `mat4_perspective_vk` (swap zn/zf) + `GREATER` compare + clear to 0. |
| **SIMD batch transforms (SSE/AVX)** | profiler flags `mat4_mul`/transform batch | M | Drop into `math/simd/`, identical signatures; SoA/16-byte alignment already committed. |
| **Incremental cooking** | full cook time hurts daily | M | Content-hash + dep tracking; brute-force was correct first. |
| **PCH / unity build** | clean engine build >~30s or incremental render >~5s | S | One CMake line each; per-heaviest-module (render), never global. |
| **Async/streaming asset I/O + loader thread** | streaming-scale content | L | `state` field + `LOADING` already reserved; sync + loading screen until then. |
| **TTF rasterization / SDF text** | UI needs scalable/crisp fonts | L | Bitmap atlas first; outline parse + raster in cooker. |
| **Texture compression (BCn)** | VRAM/distribution size matters | M | Cooker-side. |
| **Rollback / GGPO (NOT the v1 model)** 🔴 | n/a (not planned) | XL | Server-auth + prediction is the model; the sim is rollback-*ready* if ever revisited — additive, not a rewrite. |
| **Hardened anti-cheat (signing/encryption, statistical detection)** 🔴 | untrusted public matchmaking | XL | Authoritative server + interest-management fog ship in Phase 6; this is the hardened layer on top — a topology change, not a rewrite. |
| **NAT traversal (hole-punch/relay)** | internet (non-LAN) public play | L | Scoped out of v1 deliberately. |
| **Linux / macOS backends** | a second platform is actually wanted | XL | Designed-for: new `src/linux/`/`src/macos/` implementing the same `platform.h`/`platform_vulkan.h`; **do not build until the Windows engine is real** (premature portability is the over-engineering trap). |
| **Hot-reload game-code DLL (Handmade-style)** | iteration on gameplay logic demands it | M | `platform_lib_*` enables it; engine stays static otherwise. |
| **Bot heroes / advanced AI (behavior trees/GOAP)** | single-player practice / filling matches | L | Tiny FSMs sufficed for minions/towers. |
| **HPA* / hierarchical pathing, path smoothing** | maps grow beyond small arena, or 8-dir looks bad | M | Small map made plain flow/A* sufficient. |

---

## Long-pole risk summary (watch these)

1. **🔴 Vulkan synchronization (M2.0)** — wrong fence/semaphore/barrier → hangs/corruption/validation
   errors. Mitigation: validation + sync-validation always on in dev; the documented per-frame recipe;
   the per-image `images_in_flight[]` fence.
2. **🔴 Determinism (M1.2, M3.0, M3.3, Phase 6 parity)** — one stray float/unordered-iteration/wall-clock read
   desyncs, and the hash says *that*, not always *why*. Mitigation: fixed-point + state-hash self-check
   built first + the field-diff comparator + sim-lib isolation + grep-lint.
3. **🔴 Custom allocators (M1.0, M2.0/M2.2, Phase 8)** — manual memory + Vulkan device memory are
   subtle. Mitigation: arenas dominate (small heap surface); two-phase Vulkan allocator (naive unblocks
   everything); ASan-on-arena hooks.
4. **🔴 Netcode reliability + cross-binary prediction parity (Phase 6)** — silent command corruption; laggiest-
   peer stall. Mitigation: no socket code until replay is bit-stable; fuzz the transport before trusting
   it; single-toolchain sim flags across both binaries; live prediction-divergence + server replay-hash detectors.
5. **The cross-subsystem contract conflicts (M0.1)** — tick rate, fixed-point width, handle ABI,
   platform seam, Vulkan loader, module naming were each specified 2–3 incompatible ways. Resolved up
   front in ADRs; **do not start subsystem code until these ten ADRs are written**, because they are the
   most expensive things in the project to change after the fact.

---

## Critical path (the spine, in one line)

`window (M0.3) → core/mem + math/fix (M1.0–1.2) → Vulkan clear→triangle→quad→instanced (M2.0–2.3) →
determinism harness + ECS + fixed-tick loop + structural isolation (M3.0–3.4) → assets (M4.x) → gameplay single-player
(M5.x) → UDP + server-authority + prediction/reconciliation + interest/delta + divergence detection (M6.x) → vertical slice match (M7.x)`

Everything else hangs off this spine or is explicitly deferred to Phase 8+.
