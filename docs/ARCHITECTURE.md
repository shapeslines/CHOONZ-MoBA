# MOBA Engine Architecture

> **Status:** Living document. This is the project's permanent technical north star — the single authoritative description of how the engine is built and why. When a seam, a shared constant, or a convention changes, it changes *here first*, in the same commit that changes the code. If this document is stale, that is a bug.
>
> **Scope:** A top-down RTS/MOBA — hundreds of units on screen, deterministic simulation, competitive multiplayer — built entirely from scratch in data-oriented C++ on a custom RTS-class engine. Solo, multi-year. The goal is to own and understand every layer, while making steady, deliberate progress.

---

## 1. Philosophy & The Locked Stack

The engine follows a Handmade-Hero / data-oriented ethos: **own the metal, understand every byte, ship deliberately.** We avoid both naive shortcuts and premature over-engineering. The rules below are *locked* — design within them; do not relitigate them.

### 1.1 Guiding principles

1. **The engine is a set of libraries; the game is a thin executable.** Modules are static libs. The game owns only `WinMain` and top-level wiring. This forces every seam to be a real API boundary.
2. **The simulation is a pure function.** `next = sim_tick(state, inputs)`. No wall-clock, no pointers-as-identity, no hardware float, no implementation-defined iteration order in anything that affects game state. Everything in the engine bends to make this true, so the server's authoritative sim is reproducible and the client's prediction reconciles against it bit-for-bit.
3. **The region owns the bytes, not the pointer.** Memory lifetime is managed in bulk by arenas, not per-object `free`. The allocator parameter *is* the ownership contract.
4. **Handles, not pointers, for anything destroyable.** A single index+generation handle ABI is shared across entities, GPU resources, and assets.
5. **Parse complexity belongs in tools, not the runtime.** Heavy, branchy format parsers live in an offline cooker; the runtime loads a known binary layout with near-zero parsing.
6. **Pay for tooling complexity only when measured pain demands it.** Unity builds, PCH, render graphs, bindless, archetypes, rollback, async I/O are all explicitly deferred behind clean seams.
7. **Build the safety nets first.** The determinism harness (replay + per-tick state hash) is built *before* there is a sim to break, because desyncs are correctness emergencies that cannot be retrofitted.

### 1.2 The locked stack

| Layer | Decision | Notes |
|---|---|---|
| Language | C-style / data-oriented C++ ("C with classes"), **C++17 baseline** | Structs + free functions, manual memory, minimal STL. Selective C++20 (designated initializers) opt-in per-TU — flagged where used. |
| Exceptions / RTTI | **Disabled** — `/EHs-c-` + `_HAS_EXCEPTIONS=0`, `/GR-` | No `throw`, no `dynamic_cast`/`typeid`. Errors via result codes + asserts. |
| Compiler / IDE | **MSVC, Visual Studio 2026** | Windows-first. `/W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8`. `/WX` on CI/test builds. |
| Build | **CMake (≥3.28; 4.2 bundled) + Ninja Multi-Config** | One build dir, configs by flag. |
| Windowing / input / timing / file I/O | **Own Win32** behind a platform seam | Raw Input, `QueryPerformanceCounter`, `VirtualAlloc`. |
| Math | **Own library** | Float for render, **Q16.16 fixed-point for sim**. No glm. |
| Graphics | **Raw Vulkan** behind a thin renderer seam | Official headers + loader + validation layers ARE the API. **No** vk-bootstrap, VMA, volk-as-crutch, SDL/GLFW. We hand-load `vulkan-1.dll` ourselves (§5.3). |
| Memory | **Own allocators** | Arenas / stack / pool / one TLSF-ish heap over a `VirtualAlloc` backend. |
| ECS / Simulation | **Own sparse-set SoA ECS** | Deterministic, fixed 30 Hz tick. No EnTT. |
| Networking | **Own server-authoritative netcode (client prediction + interpolation + lag compensation) over own UDP** | Winsock2 behind a seam. No TCP for the game stream. |
| Assets | **Own parsers + offline cooker** | PNG/glTF/WAV/TTF in the cooker; TGA/WAV/SPIR-V direct early. |
| Shaders | **GLSL → SPIR-V offline** via `glslc` (Vulkan SDK) | No runtime shader compilation, ever. |
| Third party | **Vulkan SDK only** (system install, never vendored) | `find_package(Vulkan)` for headers + tools + validation layers. |

### 1.3 Module map

The CMake link graph *is* the architecture. Each module is one static library (`eng_*`) in one folder with a public `include/<module>/` / private `src/` split. Cross-module coupling exists only via declared `target_link_libraries`.

```
                 moba_game (exe)      sandbox (exe)        engine_tests (exe)
                      │                    │                     │
        ┌─────────────┼──────────┬─────────┴─────────┐          │
        ▼             ▼          ▼                    ▼          ▼
     eng_sim      eng_render   eng_net           eng_assets   (links eng_core_group)
        │   │         │          │  │                 │
        │   └────┬────┘          │  └────────┬────────┘
        │        │               │           │
        ▼        ▼               ▼           ▼
     eng_platform   ◄────────────┘ (net Win32 impl + page alloc + sockets)
        │                                    
   ┌────┴───────────────┬──────────────┐
   ▼                    ▼              ▼
 eng_core            eng_math      Vulkan::Vulkan (INCLUDE dirs only; loader hand-loaded)
 (leaf: arenas,      (leaf:                 ↑ used by eng_render only
  containers, log,    fixed/float
  assert, handle)     vec/mat/rng)
```

`eng_core` and `eng_math` are dependency-free leaves. `eng_serialize` depends only on core;
`eng_sim` depends on core, math, and serialize. `eng_net` depends on `eng_sim` (it calls
`sim_tick`) and on `eng_platform` (sockets). Only `eng_render` sees Vulkan. Only `eng_platform`
contains Win32-specific `.cpp`.

**`eng_core_group`** is an INTERFACE/aggregate CMake target that link-groups exactly the OS-free, GPU-free modules — `eng_core`, `eng_math`, `eng_sim`, `eng_net` (core logic), `eng_assets` (parsers), `eng_serialize` — so the test binary and offline tools link the deterministic core **headlessly**. It is a grouping, not a separate compilation of those sources. (This reconciles the "module graph" and the "engine_core/engine_app" testing split into one architecture: the seven fine-grained libs exist; `eng_core_group` simply names the non-Win32/non-Vulkan subset.)

### 1.4 Module responsibilities

| Module | Target | Responsibility | Sees Win32? | Sees Vulkan? |
|---|---|---|---|---|
| Core | `eng_core` | Arenas/allocators, containers (`Array`/`HashMap`/`Str`), logging, assert, **handle ABI**, test harness, shared config headers | no | no |
| Math | `eng_math` | `fix` (Q16.16) + fixed vec/trig/rng for sim; `f32` vec/mat/quat/geom for render; deterministic PRNG | no | no |
| Platform | `eng_platform` | Win32 window/input/timer/file I/O/DLL load, **page allocator**, **VkSurface creation + Vulkan loader bootstrap**, UDP sockets, dir-watch. The outer loop + accumulator. | **yes (only here)** | headers only (surface) |
| Render | `eng_render` | Raw Vulkan backend behind the thin renderer seam; bring-up, frames-in-flight, own device-memory allocator, pipelines, instanced forward draw | no | **yes (only here)** |
| Sim | `eng_sim` | Sparse-set SoA ECS, deterministic fixed-tick simulation, events, snapshots, `sim_hash` | no | no |
| Net | `eng_net` | Server loop driving sim_tick, per-client baseline/delta snapshots + interest management, client prediction/reconciliation, lag-comp history ring, own UDP reliability, command codec, replay, divergence detection | via platform seam | no |
| Assets | `eng_assets` | Own PNG/glTF/WAV/TGA parsers, baked `.mba` container, runtime loader, resource registry | via platform seam | no (hands blobs to render seam) |
| Serialize | `eng_serialize` | LE byte readers/writers shared by net + replay + asset codecs | no | no |
| Game | `moba_game` | `WinMain` wiring, gameplay glue, selection/input→commands, present glue | links platform | links render |

---

## 2. Cross-Cutting Conventions

This section defines, **once**, the things every subsystem must agree on. Other sections reference these and never redefine them. The most expensive-to-change facts (tick rate, fixed-point format, handle ABI, hash layout) live in shared headers in `eng_core`/`eng_math` and are recorded as ADRs in `docs/DECISIONS/`.

### 2.1 The determinism contract (the SIM / PRESENTATION boundary)

Determinism is a global, engine-wide invariant, not a feature. It is enforced by construction and verified continuously.

**Cadence and accumulator constants live in `core/sim_config.h`:**

```c
// core/sim_config.h — included by platform loop, sim, net, gameplay.
#define SIM_HZ              30                       // fixed simulation rate
#define SIM_DT_SECONDS      (1.0 / (double)SIM_HZ)   // for the wall-clock accumulator only
#define SIM_MAX_CATCHUP_S   0.25                     // accumulator clamp (anti spiral-of-death)

// sim/sim_config.h — first layer that sees both core cadence and math's Q16.16 scale.
#define SIM_DT_FIXED        (FIX_ONE / SIM_HZ)       // fixed-point dt for sim integration
```

The split preserves `eng_core` and `eng_math` as independent leaves: core owns the
rate, math owns `FIX_ONE`, and sim derives the value that needs both.

> **Resolved — tick rate is 30 Hz.** The platform loop owns the accumulator but **reads `SIM_HZ`**; it never hardcodes its own rate. 30 Hz is the MOBA-proven value the netcode bandwidth and input-delay math (2–4 ticks ≈ 66–133 ms) are built on. The accumulator stays rate-agnostic so a later bump to 60 Hz is a one-line change.

**Fixed-point is the sim's number system. Float is banned in sim code.**

> **Resolved — Q16.16 (`int32_t`), MSVC-correct multiply.** Earlier drafts disagreed (Q16.16 vs Q32.32). We lock **Q16.16**: it compiles cleanly on MSVC, halves sim-state/snapshot/wire/hash size (matters for hundreds of units at 30 Hz), and the map fits its ±32768 range with huge margin (256 cells × 0.5 u = 128 units). The type is defined once in `math/fix.h`; sim/net/gameplay/tooling all reference that typedef and `FIX_ONE`, and wire size + hash layout follow from `sizeof(fix)`. Recorded as `DECISIONS/0007-fixed-point-sim-math.md`. If a future range problem appears, the single-typedef seam allows migrating to a wider format — but that is deferred, not pre-built.

The IEEE-float determinism tar pit (x87 vs SSE, `/fp:fast` vs `/fp:precise`, FMA contraction, divergent `sinf`/`sqrtf`) is sidestepped entirely: **all sim scalars are `fix`; all sim transcendentals are table-driven**.

**The SIM / PRESENTATION seam:**

```
            INPUT: quantized Commands only (never raw pixels, never float)
                                  │
   ┌──────────────────────────────▼──────────────────────────────┐
   │  SIMULATION  (eng_sim)  — deterministic, fixed 30 Hz          │
   │  SimWorld: entities, SoA pools, tick, pcg32 RNG, events       │
   │  Q16.16 math only. Advanced ONLY by sim_tick().               │
   └──────────────────────────────┬──────────────────────────────┘
                                   │ read-only RenderSnapshot (presentation fields)
   ┌──────────────────────────────▼──────────────────────────────┐
   │  PRESENT GLUE  (game/present)  — the ONE place fixed→float    │
   │  interpolate(prev, curr, alpha) → DrawItem[] + FrameView      │
   └──────────────────────────────┬──────────────────────────────┘
                                   │ float DrawItems + FrameView
   ┌──────────────────────────────▼──────────────────────────────┐
   │  RENDER / AUDIO / UI  — non-deterministic, never writes sim   │
   └──────────────────────────────────────────────────────────────┘
```

The rules, binding on every subsystem:
- **Into the sim flows only `Command`s** — serializable player intents with fixed-point targets.
- **Out of the sim flows only read-only `RenderSnapshot`s** — a slim copy of interpolatable fields (transform, hp, anim-state, team). The renderer **never** sees `SimWorld`.
- **Fixed→float conversion happens exactly once,** in the present glue (§9). Nothing downstream of that edge can feed back into the sim, so presentation float is harmless.
- **One RNG for the sim** (`pcg32`, §2.7), seeded from the match seed, its state inside `SimWorld` and therefore hashed and serialized. Presentation gets a *separate* RNG.
- **No wall-clock, no addresses, no pointer-as-key, no hash-bucket iteration** in sim logic. Time is `world.tick`; identity is the handle; iteration is by ascending index.
- **Debug and Release must be bit-identical.** Sim code must never branch on `ASSERT`/`#if BUILD_DEBUG` in a way that changes the computation (asserts may *read* state, never alter it).

**Enforcement is convention + hash, not a fantasy compiler flag.** There is no portable MSVC switch that bans the `float` *type*. Instead: (1) sim sources live in their own lib (`eng_sim`) with a CI/grep check rejecting `\bfloat\b|\bdouble\b|<math.h>` in `sim/*.cpp`; (2) the per-tick state-hash self-check (run-twice-compare, §10) is the real backstop and is built first; (3) the sim lib is pinned to identical `/fp:precise` flags in one toolchain include so the test binary and game binary compile sim identically.

### 2.2 Memory & ownership model

"Arenas everywhere, heap rarely." Most lifetimes are scoped to a frame, a tick, a match, or a subsystem — served by linear arenas that reset in O(1) and never fragment. Only the genuinely unpredictable, individually-freed minority reaches the one general heap.

- **The allocator is data, not an object:** a tagged struct of function pointers passed by value (no vtables, no RTTI). See §6.
- **Default ownership is the arena.** A function needing temporary memory takes a scratch `Allocator` and never frees. A function producing persistent data takes the persistent allocator explicitly.
- **Borrows are `T*` valid only within a region's lifetime; never stored long-term across frames.** Destroyable objects are referenced by handle.
- **No hidden global allocations.** Every container/subsystem is handed its allocator at init. No `new`/`delete`/`malloc` in subsystem code; placement `new` (via `<new>`) is allowed for non-trivial construction in arena memory.
- **Backend:** all big blocks come from the OS page allocator (`plat_mem_reserve`/`commit`/`release`) — reserve address space generously, commit pages on demand, so an arena's *virtual* footprint can be huge while its *physical* footprint tracks the high-water mark. This is the **only** OS dependency of the memory subsystem, and it lives in the canonical `platform.h` (§4.1).

### 2.3 The handle ABI (one definition, shared everywhere)

> **Resolved — one handle layout in `core/handle.h`.** Earlier drafts had 64-bit split, 20+12, and 24+8 encodings with inconsistent null sentinels. We standardize on a single 32-bit packed handle, and every typed handle (`EntityId`, `MeshHandle`, `TextureHandle`, `AssetHandle`) derives from it so index/generation extraction is identical everywhere.

```c
// core/handle.h — THE handle convention. Index + generation packed in 32 bits.
typedef uint32_t Handle;                      // generic 32-bit generational handle

#define HANDLE_INDEX_BITS  18u                // 262,144 live indices
#define HANDLE_GEN_BITS    14u                // 16,384 reuses before generation wrap
#define HANDLE_INDEX_MASK  ((1u << HANDLE_INDEX_BITS) - 1u)
#define HANDLE_NULL        ((Handle)0)        // index 0 + gen 0 is reserved "none"; gen 0 never valid

static inline uint32_t handle_index(Handle h) { return h & HANDLE_INDEX_MASK; }
static inline uint32_t handle_gen  (Handle h) { return h >> HANDLE_INDEX_BITS; }
static inline Handle   handle_make(uint32_t idx, uint32_t gen) {
    return ((gen & ((1u<<HANDLE_GEN_BITS)-1u)) << HANDLE_INDEX_BITS) | (idx & HANDLE_INDEX_MASK);
}

// Typed handles are distinct struct wrappers over the same ABI (compile-time safety, same bits).
typedef struct { Handle h; } EntityId;
typedef struct { Handle h; } MeshHandle;
typedef struct { Handle h; } TextureHandle;
typedef struct { Handle h; } MaterialHandle;
typedef struct { Handle h; } AssetHandle;
```

- **18+14 split** is the project-wide default (ADR-0003): 262,144 live indices — ample for a MOBA/RTS hybrid's low-thousands peak — and **16,384 generations per slot**, weighted toward generation bits because the hybrid's heavy projectile/effect churn recycles slots far more than a typical MOBA. Index exhaustion is a hard failure; generation wrap is soft and debug-asserted — so index is provisioned generously and generation amply. (The original draft used 24+8; superseded by ADR-0003 after the genre was clarified.)
- **Generation 0 is never valid;** a freshly allocated slot starts at generation 1. `HANDLE_NULL` (all zero) is the universal "none" sentinel — there is no all-ones variant.
- **`EntityId` is unsigned** everywhere (the earlier `int32_t EntityId` in gameplay is corrected to this wrapper).
- On destroy, a slot's generation increments, so any stale handle fails validation deterministically — use-after-free becomes a detected condition, and (critically) no allocation address ever leaks into sim state.

### 2.4 No-exceptions / no-RTTI policy & error handling

- **Exact flag string** (defined once in `cmake/EngineOptions.cmake`, cited verbatim everywhere): MSVC `/EHs-c- /GR-` with `_HAS_EXCEPTIONS=0`. Plus `_CRT_SECURE_NO_WARNINGS` (we own our string handling).
- **Error handling has exactly two channels, never mixed:**
  1. **Programmer bugs / broken invariants → assert.** `ASSERT(x)` is debug-only (logs, `__debugbreak`, then `abort`); it compiles to nothing in release. `ENSURE(x)` (a.k.a. `ASSERT_ALWAYS`) stays in release for "the universe is corrupt, do not continue" invariants. `STATIC_ASSERT` is free — use liberally.
  2. **Expected/recoverable failures → result codes / `{value, error}` structs.** `RESULT_OK`, `RESULT_OUT_OF_MEMORY`, `RESULT_NOT_FOUND`, `RESULT_IO_ERROR`, `RESULT_PARSE_ERROR`. The caller checks. There is no third path.
- **OOM policy:** overrunning a *fixed* arena/pool/stack is a hard `ENSURE` (budgets are sized up front; an overrun is a design bug). Only the **page backend** and the **general heap** surface OOM as a recoverable `RESULT_OUT_OF_MEMORY`.
- **STL hazard rule (binding):** with `_HAS_EXCEPTIONS=0`, any static lib linked into **both** `eng_core_group` (engine) **and** a tools/tests target must be compiled with **identical** `_HAS_EXCEPTIONS`/`/EH` settings and must **not** expose STL types across its headers, or ODR violations corrupt the build. Therefore: **STL is forbidden in all engine code and in `tests/`** (we use our own harness, §10). STL is permitted only in `tools/` (offline cookers), which never link into the engine. The shared `eng_assets` parser code exposes only POD C-style interfaces and is built `_HAS_EXCEPTIONS=0` everywhere.

### 2.5 Naming, file & comment conventions

| Kind | Convention | Example |
|---|---|---|
| Types | `PascalCase` | `EntityPool`, `RenderFrame`, `Arena` |
| Free functions | `snake_case` with **mandatory subsystem prefix** | `arena_push`, `vk_create_swapchain`, `sim_tick`, `net_send`, `log_info` |
| Variables / fields | `snake_case` | `frame_index`, `unit_count` |
| Constants / enums / macros | `SCREAMING_SNAKE` | `MAX_ENTITIES`, `FIX_ONE`, `ASSERT` |
| File-static globals | `g_` prefix, used sparingly | `g_log_state` |
| Fixed-width ints | own aliases | `u8 u16 u32 u64 i8…i64 f32 f64 b32 usize` |

- **The subsystem function prefix *is* the module system** (`arena_ vk_ sim_ net_ log_ dbg_ asset_ test_`): no namespaces-as-modules, call sites self-document, grep is trivial. A single optional outer `mm` namespace is used only by the math library for its types.
- **`.h` / `.cpp` split, not header-only.** Declarations in `.h`, definitions in `.cpp`. *Rationale: header-only kills incremental build times over a multi-year solo project — protect the inner loop.* Exception: small `static inline` accessors and the math library may live in `.h`/`.inl` (math is small, hot, benefits from inlining).
- **`#pragma once`** at the top of every header. Include order in a `.cpp`: own header first (proves self-sufficiency), then engine headers, then platform/Vulkan, then C stdlib; blank line between groups. **No transitive-include reliance.**
- **Type-name discipline (float vs fixed must be obvious at every call site):** use the math library's exact names everywhere — `vec2/vec3/vec4/mat4/quat` for **float** (render), `fvec2/fvec3` for **fixed** (sim). Local short aliases like `v2`/`m4`/`v3` are **banned** because `v2` previously meant a float vec in the renderer and a fixed vec in gameplay — exactly the confusion determinism reviews must catch.
- **Struct init:** C++20 designated initializers (flagged C++20 — the brief permits selective C++20; affected TUs use `/std:c++20` or are verified to compile clean under `/std:c++17 /permissive-`). A zeroed struct must always be a valid empty state, pairing with arena memset-zero.
- **Comments explain *why*, never *what*.** One banner per file; `// NOTE:` for invariants/rationale; `// TODO(carson):` / `// HACK:` tagged and greppable. No commented-out code in commits.

### 2.6 The platform seam

`eng_platform` is the only module containing OS-specific `.cpp`. Everything above it talks to a small set of portable headers (`platform.h` and friends). The platform **owns `WinMain` and the run loop** (it owns the OS message pump and the clock); the engine exposes hooks the platform calls. Porting to Linux/macOS = adding a sibling `src/linux/` and a CMake source switch, with zero consumer changes. The canonical seam is detailed in §4.

### 2.7 The renderer seam

`eng_render` is the only module that includes `vulkan.h`. The game thinks in meshes/textures/materials/draw-items addressed by handles; the backend thinks in `VkDevice`/command-buffers/descriptors. Everything crosses the seam as plain data: handles in, a flat `DrawItem[]` + `FrameView` out, one call per frame boundary. The seam is implementable by a null backend (validates handle logic) and a future second backend. Detailed in §8–§9.

### 2.8 The deterministic PRNG

> **Resolved — `pcg32` is the sim RNG everywhere.** Earlier drafts split between PCG32 (Math/Tooling/Net) and xoshiro256** (ECS). We lock **`pcg32`** for the sim: tiny, serializable (two `u64`), excellent quality, integer-only (composes with fixed-point). Defined once in `math/rng.h`. Its state lives in `SimWorld`, so it is hashed and snapshotted for free, and seeded from the match seed owned by the authoritative server (and embedded in replays). **`xoshiro256**`** is reserved strictly for the presentation/tools stream (VFX, UI), kept separate so cosmetic randomness can never perturb gameplay.

```c
// math/rng.h
typedef struct { uint64_t state, inc; } Rng;     // pcg32 — THE sim PRNG (lives in SimWorld)
void    rng_seed(Rng* r, uint64_t seed, uint64_t seq);
uint32_t rng_next(Rng* r);
uint32_t rng_range(Rng* r, uint32_t bound);      // unbiased (rejection)
fix     rng_fix01(Rng* r);                        // [0,1) in Q16.16 — sim-safe

typedef struct { uint64_t s[4]; } RngFx;          // xoshiro256** — presentation/tools ONLY
double  rngfx_next_f64(RngFx* r);
```

---

## 3. Build System & Project Structure

### 3.1 Repository layout

```
moba-game/
├─ CMakeLists.txt              # top-level: project(), include cmake/ helpers, add_subdirectory
├─ CMakePresets.json           # Debug / DebugASan / RelWithDebInfo / Release × MSVC+Ninja
├─ CMakeUserPresets.json       # per-machine overrides (git-ignored)
├─ README.md  .gitignore  .gitattributes
├─ cmake/
│  ├─ CompilerWarnings.cmake   # INTERFACE target moba_warnings (/W4 /WX /permissive- …)
│  ├─ EngineOptions.cmake      # INTERFACE target moba_options (no-exc/no-rtti/ASan, the ONE flag string)
│  └─ CompileShaders.cmake     # add_shader_library(): GLSL -> SPIR-V with depfiles
├─ engine/
│  ├─ core/      include/core/*.h   src/*.cpp     # arenas, containers, log, assert, handle.h, sim_config.h, test.h
│  ├─ math/      include/math/*.h   src/*.cpp     # fix.h, rng.h, vec/mat/quat/geom
│  ├─ serialize/ include/serialize/*.h src/*.cpp  # LE byte readers/writers
│  ├─ platform/  include/platform/*.h  src/win32/*.cpp   # the OS seam + impl
│  ├─ render/    include/render/*.h    src/vk/*.cpp       # raw Vulkan
│  ├─ assets/    include/assets/*.h    src/*.cpp          # parsers, .mba loader, registry
│  ├─ sim/       include/sim/*.h       src/*.cpp          # config derivative + deterministic sim/ECS
│  └─ net/       include/net/*.h       src/*.cpp          # server-auth + UDP
├─ game/         src/main_win32.cpp  src/game_*.cpp  src/present/*.cpp
├─ tools/
│  ├─ sandbox/   # day-one bring-up: window + clear
│  └─ cooker/    # offline asset baker (links eng_assets parsers; MAY use STL)
├─ shaders/      # *.vert/.frag/.comp (LF, committed)
├─ assets/       # source PNG/glTF/WAV/TTF (Git LFS candidate later)
├─ tests/        # engine_tests exe (own harness, links eng_core_group; NO STL, NO gtest)
└─ docs/         ARCHITECTURE.md  ROADMAP.md  CONVENTIONS.md  DECISIONS/0001-*.md …
```

> **Resolved — one layout, one naming scheme.** Earlier drafts proposed three (`engine/<mod>` `eng_*`, `src/<mod>` flat, `moba_*`, `engine_core/engine_app`). Canonical: `engine/<module>` with the `include/`+`src/` split, static libs named `eng_<module>` with `eng::<module>` aliases. The `engine_core/engine_app` testing split becomes the `eng_core_group` aggregate target (§1.3). All subsystem snippets in this document use these names.

### 3.2 Module CMake pattern

```cmake
# engine/render/CMakeLists.txt — the pattern, applied to every module
add_library(eng_render STATIC src/vk/vk_backend.cpp src/vk/vk_device.cpp
                              src/vk/vk_swapchain.cpp src/vk/vk_pipeline.cpp
                              src/vk/vk_alloc.cpp src/vk/vk_frame.cpp)
add_library(eng::render ALIAS eng_render)

target_include_directories(eng_render
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Vulkan REQUIRED)
target_link_libraries(eng_render
    PUBLIC  eng::core eng::math eng::platform
    PRIVATE Vulkan::Headers          # INCLUDE DIRS ONLY — we hand-load the loader (§5.3)
            moba_warnings moba_options)
```

> **Resolved — Vulkan linkage.** Earlier drafts contradicted each other (link `vulkan-1.lib` vs hand-load `vulkan-1.dll` vs direct static calls). Canonical, honoring the from-scratch ethos: **`eng_render` links Vulkan *headers only* (`Vulkan::Headers`, no import lib).** The loader is hand-loaded by the platform (§5.3) and the renderer obtains *all* entry points through a dispatch table — no `vk*` call is assumed statically linked. `/WX` still covers any inline code instantiated from Vulkan headers in our TUs, so `render/src/vk/vk.h` wraps the `#include <vulkan/vulkan.h>` in a `#pragma warning(push/pop)` and is the single place `VK_USE_PLATFORM_WIN32_KHR` is defined.

The consumer just declares intent and gets shader output wired in:

```cmake
# game/CMakeLists.txt
add_executable(moba_game WIN32 src/main_win32.cpp src/game_loop.cpp src/present/present.cpp)
target_link_libraries(moba_game PRIVATE
    eng::sim eng::render eng::net eng::assets eng::platform eng::core eng::math eng::serialize
    moba_warnings moba_options)
add_dependencies(moba_game shaders_core)
target_compile_definitions(moba_game PRIVATE
    MOBA_SHADER_DIR="$<TARGET_PROPERTY:shaders_core,SHADER_OUTPUT_DIR>")
```

### 3.3 Compiler posture & configs

`cmake/CompilerWarnings.cmake` defines `moba_warnings` (`/W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /diagnostics:caret /wd4201`; `/WX` on CI/test builds, lenient locally so iteration isn't blocked). `moba_warnings` is linked **only to our own targets**, never to Vulkan, with a short justified `/wd` list (e.g. 4201 for nameless unions in math).

`cmake/EngineOptions.cmake` defines `moba_options` — **the one flag string** for the whole project:

```cmake
add_library(moba_options INTERFACE)
target_compile_options(moba_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/GR- /EHs-c->)                          # no RTTI, no exceptions
target_compile_definitions(moba_options INTERFACE
    _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS                         # STL won't drag exception machinery
    $<$<CONFIG:Debug>:MOBA_DEBUG=1>  $<$<CONFIG:RelWithDebInfo>:MOBA_DEV=1>
    $<$<CONFIG:Release>:MOBA_RELEASE=1>)
# Release whole-program opt MUST be explicit and uniform (CMake's default Release lacks /GL /LTCG):
target_compile_options(moba_options INTERFACE $<$<CONFIG:Release>:/O2 /GL>)
target_link_options   (moba_options INTERFACE $<$<CONFIG:Release>:/LTCG>)
# ASan only in the dedicated DebugASan config (NOT the day-one default Debug). /RTC1 is incompatible with ASan.
add_library(moba_asan INTERFACE)
target_compile_options(moba_asan INTERFACE $<$<CONFIG:Debug>:/fsanitize=address>)
```

| Config | Opt | Debug info | Asserts | Sanitizer | Use |
|---|---|---|---|---|---|
| **Debug** | `/Od` | `/Zi` | on | **none** | daily dev, fast bring-up |
| **DebugASan** | `/Od` | `/Zi` | on | ASan | hunting memory bugs |
| **RelWithDebInfo** | `/O2` | `/Zi` | on | off | profiling, the build you actually play |
| **Release** | `/O2 /GL` + `/LTCG` | minimal | off | off | shipping; whole-program opt for the sim hot loop |

> **Resolved — ASan is opt-in, not the day-one default.** ASan slows bring-up and can fight Win32/arena paths exactly during the hardest milestones (Vulkan bring-up), while catching little inside arena-dominated code until arenas add poison hooks. So the default Debug is non-ASan; `DebugASan` is a separate preset. When the arena allocator is built it gains ASan poison-on-reset / unpoison-on-push so the sanitizer earns its slowdown. `CMAKE_CONFIGURATION_TYPES` is set in the cache; `/GL` requires `/LTCG` on every target's link, applied uniformly via `moba_options` or LTO silently disengages.

`CMakePresets.json` uses **Ninja Multi-Config** in one build dir, so switching configs is a build flag, not a reconfigure. MSVC toolchain comes from the Developer environment; machine-local tweaks go in the git-ignored `CMakeUserPresets.json`.

### 3.4 Shaders → SPIR-V (offline, one contract)

> **Resolved — one shader contract.** Canonical: `cmake/CompileShaders.cmake`'s `add_shader_library()` using `glslc` with `-MD -MF` depfiles (so editing an included `.glsl` correctly triggers dependent recompiles), emitting to `${CMAKE_BINARY_DIR}/shaders/*.spv`, located at runtime via the `MOBA_SHADER_DIR` compile def. The renderer's earlier GLOB-without-depfiles snippet is deleted. **Shaders are loose `.spv`, loaded directly through the platform file API — not wrapped in `.mba` / the asset system.** (SPIR-V *is* the engine-native shader format; the Vulkan SDK toolchain is its "cooker." Keeping shaders out of the asset pipeline simplifies shader hot-reload, which is dev-only.)

```cmake
function(add_shader_library TARGET)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    set(out_dir "${CMAKE_BINARY_DIR}/shaders"); file(MAKE_DIRECTORY "${out_dir}")
    set(spv_files "")
    foreach(src ${ARG_SOURCES})
        get_filename_component(name "${src}" NAME)
        set(spv "${out_dir}/${name}.spv")
        add_custom_command(OUTPUT "${spv}"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} $<$<CONFIG:Debug>:-g> $<$<NOT:$<CONFIG:Debug>>:-O>
                    --target-env=vulkan1.3 -MD -MF "${spv}.d" "${CMAKE_SOURCE_DIR}/${src}" -o "${spv}"
            DEPENDS "${CMAKE_SOURCE_DIR}/${src}" DEPFILE "${spv}.d" VERBATIM)
        list(APPEND spv_files "${spv}")
    endforeach()
    add_custom_target(${TARGET} ALL DEPENDS ${spv_files})
    set_target_properties(${TARGET} PROPERTIES SHADER_OUTPUT_DIR "${out_dir}")
endfunction()
```

`*.spv` is a compiled artifact, git-ignored. **(Action item: `*.spv` is not yet in `.gitignore` — add it.)** Release packaging (deferred) `install()`s the shader dir next to the exe.

### 3.5 Vulkan SDK & third-party posture

`find_package(Vulkan REQUIRED)` resolves the SDK via the `VULKAN_SDK` env var the installer sets, providing `Vulkan::Headers`, `Vulkan_GLSLC_EXECUTABLE`, and the validation layers (discovered at runtime by the loader; enabled only in dev builds). The build hard-fails with a clear message if the SDK is absent. **Pin the SDK version and the target API (`vulkan1.3`) in the README** for reproducibility. `third_party/` starts empty with a strict one-rule vendoring policy; the default answer is "write it ourselves."

### 3.6 Tests, unity builds, PCH

Tests are tiny CTest executables linking `eng_core_group`, using our own ~200-line `core/test.h` harness (§10). **No GoogleTest/Catch2, no STL in tests** (heavy headers + exception model fight `/EHs-c-` and the ODR rule of §2.4). **No unity builds, no PCH initially** — both deferred behind measurement triggers (~30 s clean / ~5 s incremental render change). Module isolation already quarantines the heavy headers PCH would target.

---

## 4. Platform Layer & Portability Seam

### 4.1 The canonical `platform.h`

> **Resolved — one `platform.h`.** Earlier drafts described two different files (one with windowing/input/file I/O, one with the page allocator) and disagreed on the file-read memory model. Canonical `platform.h` includes the page-reservation API the memory subsystem depends on, and the file-read API takes a caller-supplied allocator (the memory subsystem's arena-ownership model wins).

```c
// platform.h — THE OS seam. Engine modules include only this (+ platform_input.h, platform_vulkan.h).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "core/handle.h"
#include "platform_input.h"

typedef struct PlatformWindow PlatformWindow;     // opaque, per-backend
typedef struct { const char* title; int32_t width, height; bool resizable; bool fullscreen; } PlatformWindowDesc;

// ---- Window / loop ----
PlatformWindow* platform_window_open(const PlatformWindowDesc*);
void            platform_window_close(PlatformWindow*);
bool            platform_pump_events(PlatformWindow*, PlatformFrameInput* out); // false => quit
void            platform_window_size(PlatformWindow*, int32_t* w, int32_t* h);

// ---- High-res monotonic clock (raw ticks across the seam; seconds computed engine-side) ----
uint64_t platform_time_ticks(void);              // QPC
uint64_t platform_time_frequency(void);
double   platform_time_seconds(uint64_t ticks);
void     platform_sleep_ms(uint32_t ms);

// ---- OS page allocator (the ONLY thing the memory subsystem depends on) ----
typedef struct { void* base; size_t committed; size_t reserved; } PlatformMemoryBlock;
PlatformMemoryBlock plat_mem_reserve(size_t reserve_bytes);
bool                plat_mem_commit (PlatformMemoryBlock*, size_t new_committed);
void                plat_mem_release(PlatformMemoryBlock*);
size_t              plat_mem_page_size(void);

// ---- File I/O: caller supplies the allocator (arena owns the bytes) ----
typedef struct { void* data; size_t size; } PlatformFile;
struct Allocator;                                                 // fwd; see §6
bool platform_file_size (const char* vpath, size_t* out_size);    // cheap stat — bound untrusted reads BEFORE pushing into a fixed arena
bool platform_file_read (const char* vpath, struct Allocator alloc, PlatformFile* out);
bool platform_file_map  (const char* vpath, PlatformFile* out);   // mmap, read-only, immutable assets
void platform_file_unmap(PlatformFile*);
bool platform_file_write(const char* vpath, const void* data, size_t n); // atomic temp+rename
void platform_set_asset_root(const char* abs_dir);               // resolves "assets://"

// ---- Dynamic libraries (used to hand-load vulkan-1.dll, §5.3) ----
typedef struct PlatformLib PlatformLib;
PlatformLib* platform_lib_open(const char* name);   // "vulkan-1" -> .dll/.so.1/.dylib
void*        platform_lib_sym (PlatformLib*, const char* symbol);
void         platform_lib_close(PlatformLib*);

// ---- UDP sockets (net seam; OS-free reliability lives above this) ----
typedef struct { uint64_t handle; } NetSocket;
typedef struct { uint32_t addr_be; uint16_t port_be; } NetAddr;
bool      net_init(void);                            // WSAStartup on Win32
NetSocket net_open_udp(uint16_t bind_port);          // non-blocking
int       net_send(NetSocket, NetAddr to, const void* data, int len);
int       net_recv(NetSocket, NetAddr* from, void* buf, int cap);  // -1 = would-block
void      net_close(NetSocket);

// ---- Dir watch (dev-only hot-reload), diagnostics ----
typedef void (*PlatformWatchFn)(const char* changed_path, void* user);
void platform_watch_dir(const char* path, PlatformWatchFn, void* user);   // ReadDirectoryChangesW
void platform_log(const char* fmt, ...);
void platform_fatal(const char* fmt, ...);
```

The asset layer's `asset_id`→path resolution sits **above** `platform.h` (it resolves an id to a vpath, then calls `platform_file_read`/`_map`), so the platform stays string/handle-based and the id scheme stays in `eng_assets`.

### 4.2 Entry point, hooks & the run loop (one owner)

> **Resolved — the platform owns the one outer loop.** Earlier drafts presented three independent loops (Platform, ECS `loop.c`, Net Stage 0). Canonical: the platform owns `WinMain`, the message pump, the clock, and the accumulator. The ECS/net loop descriptions are *what the hooks call into*, not separate loops. The engine exposes exactly:

```c
// engine_entry.h — implemented by the game, called by the platform backend.
bool engine_init(PlatformWindow*, int argc, char** argv);
bool engine_frame(const PlatformFrameInput*);   // one deterministic 30 Hz sim tick (consumes Commands)
void engine_render(PlatformWindow*, float alpha);// present glue + renderer; interpolates by alpha
void engine_shutdown(void);
```

```c
// the loop, in win32_main.cpp — reads SIM_HZ, never its own rate.
const uint64_t freq = platform_time_frequency();
uint64_t prev = platform_time_ticks();
double accumulator = 0.0;
while (platform_pump_events(window, &input)) {
    uint64_t now = platform_time_ticks();
    double frame = (double)(now - prev) / (double)freq; prev = now;
    if (frame > SIM_DT_SECONDS_MAX_CATCHUP) frame = SIM_MAX_CATCHUP_S;  // anti spiral-of-death
    accumulator += frame;
    // Sim keeps ticking even when minimized (the authoritative/listen server must keep advancing).
    while (accumulator >= SIM_DT_SECONDS) {
        collect_local_commands();              // input -> Command stream (client: send to server; server: merge per tick)
        if (!engine_frame(&input)) goto quit;  // deterministic tick (integrates against SIM_DT_FIXED)
        accumulator -= SIM_DT_SECONDS;
    }
    if (!window_minimized) {                    // skip render only; never skip sim
        float alpha = (float)(accumulator / SIM_DT_SECONDS);
        engine_render(window, alpha);
    }
}
```

> **Resolved — minimize policy is single-sourced and never stalls the sim.** When minimized, the loop keeps calling `engine_frame` (sim) and merely skips `engine_render`. The renderer must **not** block in a wait-events loop on minimize (that belongs only to a standalone non-networked tool). Swapchain recreation is checked non-blocking at the top of the render path (§8.4). Dependency direction is clean: `eng_platform` depends on `engine_entry.h` (4 symbols); the game depends on `platform.h`. No cycle. A **dedicated headless server** (no window) runs the *same* accumulator via a `platform_run_headless(tick_fn)` seam (reads `SIM_HZ`, uses `platform_time_ticks` + sockets, no window/pump); both windowed and headless loops read `SIM_HZ`, neither hardcodes the rate.

### 4.3 Win32 specifics

- One `WNDCLASSEXW`, `CS_OWNDC`, `CS_HREDRAW|CS_VREDRAW` **off** (Vulkan owns the surface). `WM_ERASEBKGND` returns 1 (GDI never clears). Per-window state via `SetWindowLongPtrW(GWLP_USERDATA)`.
- `platform_pump_events` drains all messages with `PeekMessageW(PM_REMOVE)` (never block on `GetMessage`).
- `WM_SIZE` only flags `resized`/`minimized`; the renderer recreates the swapchain lazily, **synchronously on the main thread** at the top of the render path. (There is no render thread — see the threading note below.)
- `WM_KILLFOCUS` flushes held keys (no stuck-key bug) and releases cursor capture.
- `timeBeginPeriod(1)` at init, paired with `timeEndPeriod(1)` at shutdown.
- **Deferred:** smooth live-resize during the modal `WM_ENTERSIZEMOVE` loop (accept drag stutter first); exclusive fullscreen (borderless-fullscreen first, but `PlatformWindowDesc.fullscreen` anticipates it).

> **Resolved — single-threaded; no render thread.** Earlier text claimed swapchain recreation happens "on its own thread," contradicting the single-threaded loop everywhere else. Canonical: the engine is single-threaded first; swapchain recreation is synchronous on the main loop thread. A render thread would be a significant later addition needing its own design.

### 4.4 Raw input

Raw Input API (`WM_INPUT`) for mouse/keyboard — unaccelerated deltas, sample-accurate, deterministic — registered once at window creation. `WM_CHAR` only for text entry. Win32 VKeys are translated to a stable OS-independent `KeyCode` enum **inside the backend** so engine key codes never alias OS values; the notorious E0/E1-prefix and left/right-modifier disambiguation is handled there. Gameplay consumes a flat per-frame snapshot (data-oriented; one sample per tick), with a tiny UTF-32 ring only for text. Edge detection (pressed-this-frame) is computed engine-side by diffing snapshots.

```c
// platform_input.h
typedef struct { uint8_t down[KEY_COUNT]; uint32_t transitions; } KeyboardState;
typedef struct { int32_t dx, dy, x, y, wheel; uint8_t buttons; } MouseState;
typedef struct {
    KeyboardState keyboard; MouseState mouse;
    bool window_resized, window_minimized, window_focused;
    int32_t fb_width, fb_height;
    uint32_t text_utf32[16]; uint32_t text_count;
} PlatformFrameInput;
```

### 4.5 Virtual paths & file modes

Named-root virtual paths resolved in the backend (`assets://`, `user://`, `shaders://`); the engine never builds OS paths or sees separators, and `..` traversal is rejected. Three read modes: `platform_file_read` (default; caller's arena owns the bytes), `platform_file_map` (mmap, restricted to immutable shipped assets), and atomic `platform_file_write` (temp+rename). Async/overlapped I/O is **deferred** — synchronous reads on a worker behind a loading screen suffice; the signatures are additive-friendly for a later async path.

### 4.6 Porting (designed-for, deferred)

A Linux/macOS backend is a sibling folder implementing the same `platform.h`/`platform_vulkan.h` symbols; CMake swaps the `target_sources` block. Everything above `eng_platform` — including the fixed-timestep loop logic (only the clock primitive differs) and the virtual-path scheme — is portable. macOS Cocoa needs `.mm` files, isolated to `src/macos/`. Do not write a second backend until the Windows engine is real (premature portability is the over-engineering trap).

| Concern | Windows | Linux (deferred) | macOS (deferred) |
|---|---|---|---|
| Entry / window | `WinMain` / `HWND` | `main` / xcb | `main` / Cocoa+`CAMetalLayer` |
| Clock | `QueryPerformanceCounter` | `clock_gettime(MONOTONIC)` | `mach_absolute_time` |
| DLL | `LoadLibraryA` | `dlopen` | `dlopen` |
| VK surface ext / call | `VK_KHR_win32_surface` / `vkCreateWin32SurfaceKHR` | `VK_KHR_xcb_surface` / `vkCreateXcbSurfaceKHR` | `VK_EXT_metal_surface` / `vkCreateMetalSurfaceEXT` |

---

## 5. The Vulkan Boundary

This is the seam that most easily leaks Win32 into the renderer, so it gets a narrow explicit contract.

### 5.1 `platform_vulkan.h`

```c
// platform_vulkan.h — portable; uses ONLY <vulkan/vulkan.h>.
#include <vulkan/vulkan.h>

const char* const* platform_vk_required_instance_extensions(uint32_t* count); // {surface, win32_surface}

// Creates the surface WITHOUT the renderer ever seeing the HWND.
VkResult platform_vk_create_surface(VkInstance, PlatformWindow*, const VkAllocationCallbacks*,
                                    VkSurfaceKHR* out);

PFN_vkGetInstanceProcAddr platform_vk_get_loader(void);  // the one entry point the renderer bootstraps from
```

> **Resolved — the platform creates the surface; the renderer never sees the HWND.** Earlier the renderer wanted `platform_win32_handles(&hinst,&hwnd)` and called `vkCreateWin32SurfaceKHR` itself, contradicting the platform's stronger seam. Canonical: `platform_win32_handles` is deleted; the renderer calls `platform_vk_create_surface`. `vkCreateWin32SurfaceKHR` and `vulkan_win32.h` live only in `win32_vulkan.cpp`.

### 5.2 Surface creation (the only Windows-specific renderer-adjacent code)

`win32_vulkan.cpp` is the single file that includes `vulkan_win32.h`. It builds `VkWin32SurfaceCreateInfoKHR` from the window's `HINSTANCE`/`HWND` and resolves `vkCreateWin32SurfaceKHR` through the hand-loaded loader's dispatch table (cached, not re-resolved per call).

### 5.3 The hand-loaded Vulkan loader (honest scope)

> **Resolved — own the loader, but with the correct two-tier dispatch and no understatement.** Earlier text claimed "~40 lines, effectively what volk does." That understates it: doing it correctly is the actual content of volk.

The platform `platform_lib_open("vulkan-1")` loads `vulkan-1.dll`; `platform_vk_get_loader()` returns `vkGetInstanceProcAddr` resolved via `GetProcAddress`. The renderer then builds a **dispatch table**, in two tiers:

1. **Global functions** (`vkCreateInstance`, `vkEnumerateInstance*`) are resolved via `vkGetInstanceProcAddr(NULL, name)` — the NULL-instance rule, which is mandatory and easy to miss.
2. **Instance functions** are resolved via `vkGetInstanceProcAddr(instance, name)` after instance creation.
3. **Device functions** are re-resolved via `vkGetDeviceProcAddr(device, name)` after device creation, to skip the loader trampoline's per-call indirection.

**Every Vulkan call in the renderer goes through this table** (`vk.CreateSwapchainKHR(...)`, not a presumed-linked `vkCreateSwapchainKHR(...)`). A missing/incorrectly-resolved pointer is null-checked at table-build time and surfaces via `platform_fatal`, not as an opaque crash. We accept the loader-trampoline overhead for instance-level calls; device-level calls use the device table.

---

## 6. Memory Management & Data-Oriented Foundations

The bedrock subsystem (`eng_core`), depending only on the platform page allocator. "Arenas everywhere, heap rarely."

### 6.1 The allocator interface (data, not objects)

```c
typedef enum : uint8_t { ALLOC_ARENA, ALLOC_STACK, ALLOC_POOL, ALLOC_HEAP } AllocKind;
// fn handles alloc (ptr==0), free (new_size==0), and realloc (both set).
typedef void* (*AllocFn)(void* state, void* ptr, size_t old_size, size_t new_size, size_t align);
typedef struct Allocator { AllocFn fn; void* state; AllocKind kind; } Allocator;

static inline void* mem_alloc  (Allocator a, size_t n, size_t al) { return a.fn(a.state,0,0,n,al); }
static inline void* mem_realloc(Allocator a, void* p, size_t os, size_t ns, size_t al){ return a.fn(a.state,p,os,ns,al); }
static inline void  mem_free   (Allocator a, void* p, size_t n)  { a.fn(a.state,p,n,0,0); }
#define MEM_DEFAULT_ALIGN 16        // == alignof(max_align_t) on x64
#define ARENA_NEW(a, T)   (new (arena_push((a), sizeof(T), alignof(T))) T)   // placement new, <new>
```

A tagged function-pointer struct, not a C++ abstract base — same indirection cost, stays C-style, passable by value, no RTTI/vtable baggage. A thread-policy flag is reserved for a future job system.

### 6.2 The allocator family

| Allocator | Free granularity | Fragmentation | Primary use |
|---|---|---|---|
| **Arena** (linear) | bulk reset (O(1)) | none | per-frame / per-tick / per-level scratch & storage — ~80% of allocations |
| **Stack** (LIFO) | per-marker pop | none | genuinely nested temporary lifetimes |
| **Pool** (freelist) | per-slot O(1) | none (fixed) | handle-backed object tables |
| **Heap** (TLSF-ish) | per-block O(1) | bounded | the awkward residual minority |

- **Arena** bumps a pointer over a reserved page block that commits on growth; `arena_reset` zeroes the offset in O(1); an always-on `high_water` counter (even in release) catches under-budgeting early. Per-frame scratch is double-buffered (swap each frame; reset the now-oldest); nested temporaries use a `TempMemory { arena, saved_offset }` save/restore marker rather than a sub-arena.
- **Pool** carves a block into fixed slots threaded by an intrusive freelist (the next-free index lives *inside* freed memory — zero overhead). It is the backing store for handle tables: `HandlePool` pairs a `Pool` with a parallel `generations[]` array (§2.3 ABI) and hands out only `Handle`s, never `T*` long-term.
- **Heap:** exactly one process heap for variable-size, individually-freed, unpredictable-lifetime allocations. A TLSF-style allocator is the target; a simpler segregated free list is an acceptable first cut because the heap is exercised so little — **finalize after profiling real heap traffic** (deferred).

### 6.3 Alignment & GPU upload

Every alloc takes an alignment (power-of-two assert). Defaults: 16 (any scalar/SSE), 64 for cache-line/false-sharing-sensitive SoA columns. A dedicated **staging arena** aligns sub-allocations for GPU upload to the relevant `VkPhysicalDeviceLimits` buffer-offset alignments (`minUniformBufferOffsetAlignment`, `optimalBufferCopyOffsetAlignment`, etc.).

> **Resolved — `nonCoherentAtomSize` only when memory is non-coherent.** Per-frame upload memory is `HOST_VISIBLE|HOST_COHERENT` and persistently mapped, so **no flush is needed and `nonCoherentAtomSize` does not apply** — it is dropped from the upload arena's alignment math. `nonCoherentAtomSize` governs `vkFlushMappedMemoryRanges` *only* if we ever choose non-coherent memory. `bufferImageGranularity` is handled in the staging/dedicated paths too, not just the Phase-2 block allocator (§8.5).

### 6.4 Containers (the STL replacement)

All allocator-aware, POD-friendly, no exceptions, no hidden global allocation; built strictly on demand (this set covers ~95% of needs).

- **`Array<T>`** — dynamic, geometric growth, public `data/len/cap` (DOD, no getters).
- **`HashMap<K,V>`** — open addressing, Robin Hood, power-of-two, backward-shift delete (no tombstones). **Iteration order is not guaranteed → never iterate a HashMap from the sim;** use an `Array` for ordered sim data.
- **`Str`/`StrView`** — length-prefixed, non-null-terminated (null only at OS/Vulkan boundaries); views default non-owning.
- **`InlineArray<T,N>`** (no heap) and an intrusive **`FreeList`**.

Allowed std headers (freestanding/utility only): `<cstdint> <cstddef> <type_traits> <limits> <new>` and the `<cstring>/<cstdarg>` intrinsics. **Banned:** `<vector> <unordered_map> <string> <memory> <algorithm>`-as-crutch, `<iostream>`, anything that allocates globally or throws.

### 6.5 Debug tooling (`MEM_DEBUG`)

Behind one switch, the heap path gains: per-allocation tag (X-macro enum) + source site, guard/canary bytes, fill-on-alloc `0xCD` / fill-on-free `0xDD`, and an at-exit leak report grouped by tag. Compiled out in release; arenas keep their cheap always-on high-water counters, surfaced on the debug overlay (§10) and at shutdown.

**Deferred:** thread-safe/lock-free heap (single-threaded until the job system is real; policy flag reserved), heap defrag/compaction, large/huge pages, the final TLSF-vs-segregated decision, a full STL clone.

---

## 7. Math Library

The dependency-free leaf (`eng_math`, only `<stdint.h>`/`<math.h>`) every other subsystem speaks. Conventions are locked and stated once so nothing downstream Y-flips or rescales ad hoc.

### 7.1 Conventions (locked)

| Aspect | Decision | Reason |
|---|---|---|
| API | structs + free functions in namespace `mm`; minimal operators | reads like the math, types stay POD |
| Handedness | **right-handed world** (+Y up) | matches glTF (our asset format) |
| Matrix storage | **column-major, column-vector** (`v' = M·v`) | uploads to Vulkan with zero transpose |
| Clip space | **Vulkan-native: +Y down, depth [0,1]**, baked into projection | the Y-flip lives in exactly one function |
| Float | `f32` default; `f64` for tooling/asset import | renderer is `f32` |
| Sim math | **Q16.16 `fix`** (§2.1) | determinism |

`mat4` is exactly 64 bytes in the byte order Vulkan wants for a `mat4` uniform/push-constant — push it with no transpose.

### 7.2 The Vulkan projection (the one place the Y-flip lives)

```cpp
// Right-handed, +Y-down clip, depth [0,1]. THE projection for this engine.
mat4 mat4_perspective_vk(f32 fov_y_rad, f32 aspect, f32 zn, f32 zf) {
    f32 f = 1.0f / tanf(fov_y_rad * 0.5f);
    mat4 r = {};
    r.m[0][0] = f / aspect;
    r.m[1][1] = -f;                       // <-- Vulkan Y-flip, here and ONLY here
    r.m[2][2] = zf / (zn - zf);           // RH, depth [0,1]
    r.m[2][3] = -1.0f;
    r.m[3][2] = (zn * zf) / (zn - zf);
    return r;                             // column-major: m[col][row]
}
```

Reversed-Z (near→1, far→0, `GREATER` compare, clear 0.0) is a trivial later swap behind this one function — **deferred** until depth fighting actually appears.

### 7.3 Types, transforms, camera

POD `vec2/3/4`, `mat3/4`, `quat` (anonymous union of named fields + array). **Pass-by-value for vecs (≤16 B), by `const*` for matrices** (avoids the silent 36/64-byte copy). TRS composition is `M = T·R·S`; the ECS stores decomposed `pos/rot/scale` and composes per frame. The MOBA camera is **perspective with a fixed steep pitch (~55°)**, not orthographic, for depth readability while gameplay distances are measured in the ground plane.

> **Note — designated initializers are C++20.** Where used (`vec3 up = { .y = 1.0f }`), the TU opts into `/std:c++20` or is verified clean under `/std:c++17 /permissive-`. The earlier "C++17" labeling is corrected.

### 7.4 Fixed-point (`fix.h`) — MSVC-correct

```c
// math/fix.h — Q16.16. THE sim number type (§2.1). int64 intermediate, no __int128.
typedef int32_t fix;
#define FIX_ONE 65536
static inline fix fix_from_int(int32_t v){ return v << 16; }
static inline fix fix_from_f32(f32 v)    { return (fix)(v * 65536.0f); } // IMPORT ONLY (non-det)
static inline f32 fix_to_f32 (fix v)     { return (f32)v / 65536.0f; }   // PRESENT edge only
static inline fix fix_mul(fix a, fix b)  { return (fix)(((int64_t)a * b) >> 16); }  // compiles on MSVC
static inline fix fix_div(fix a, fix b)  { return (fix)((((int64_t)a) << 16) / b); }
fix fix_sqrt(fix v);                      // integer Newton — bit-exact
fix fix_sin(fix rad), fix_cos(fix rad);   // LUT + fixed interp — bit-exact, table is data
typedef struct { fix x, y; } fvec2;  typedef struct { fix x, y, z; } fvec3;
```

> **Resolved — no `__int128`.** MSVC (the locked toolchain) lacks `__int128`. Q16.16's `int64_t` intermediate compiles cleanly. (Had Q32.32 been chosen, multiply would have required `_mul128`/`__mulh` from `<intrin.h>` — another reason Q16.16 wins for this project.) All sim transcendentals are table-driven so they are deterministic and libm-independent. `fix_from_f32` is import-only; `fix_to_f32` is used only at the present edge (§9).

### 7.5 SIMD stance & testing

**Scalar-first**, SSE/AVX deferred behind identical signatures, but we commit *now* to the layout that makes SIMD pay off: hot per-entity data is SoA, `mat4`/`vec4` are 16-byte aligned, and batch entry points (`mat4_mul_batch(...)`) exist from the start. Intrinsics, runtime CPU dispatch, and AVX-512 are deferred until a profiler asks.

`math_test.cpp` enforces the contract: property tests (`normalize` length≈1, `M·M⁻¹≈I`, slerp endpoints), Vulkan-convention tests (project known points, assert clip-Y down and depth∈[0,1]), and the **determinism golden test** — a fixed `fix_*` + `pcg32` sequence hashed and asserted identical across `/fp:precise`, `/fp:fast`, and (later) clang. The float math lib is not held to bit-identity; only the fixed-point + PRNG sim path is.

---

## 8. Vulkan Renderer

A thin C-style seam between the game (meshes/textures/materials/draw-items by handle) and a raw-Vulkan backend (`VkDevice`/command-buffers/descriptors/device-memory). The game never includes `vulkan.h`.

### 8.1 The seam

```c
// render/renderer.h — the ENTIRE surface the game sees. No Vulkan types.
typedef struct Renderer Renderer;

Renderer* renderer_create(PlatformWindow*);
void renderer_destroy(Renderer*);

MeshHandle     renderer_create_mesh   (Renderer*, const MeshDesc*);
TextureHandle  renderer_create_texture(Renderer*, const TextureDesc*);
MaterialHandle renderer_create_material(Renderer*, const MaterialDesc*);
void renderer_destroy_mesh   (Renderer*, MeshHandle,    uint32_t frames_until_free);
void renderer_destroy_texture(Renderer*, TextureHandle, uint32_t frames_until_free);
void renderer_destroy_material(Renderer*, MaterialHandle, uint32_t frames_until_free);

bool renderer_begin_frame(Renderer*, const FrameView*, int width, int height, bool minimized);
bool renderer_submit(Renderer*, const DrawItem* items, uint32_t count); // copy into frame arena
void renderer_end_frame(Renderer*);                                  // sort, record, submit, present
```

```c
typedef struct { mat4 model; MeshHandle mesh; MaterialHandle material; } DrawItem;
typedef struct { mat4 view; mat4 proj; vec3 camera_pos;
                 f32 time_seconds; f32 delta_seconds; } FrameView;
```

> **Resolved — one creation/upload API, with deferred-destroy on the destroy calls.** Earlier the asset layer expected `r_upload_*` returning a generic `GpuHandle` while the renderer offered `renderer_create_*` returning typed handles, and only the asset side had a `frames_until_free` deferred-destroy. Canonical: GPU resources are addressed by the renderer's **typed handles** (`MeshHandle`/`TextureHandle`); the asset layer consumes exactly `renderer_create_*`; the deferred-destroy `frames_until_free` parameter is **on the renderer's destroy functions** (needed by both hot-reload and normal teardown). `MeshDesc`/`TextureDesc`/`MaterialDesc` are single shared structs in `render/renderer_types.h` onto which the baked asset blob maps (§11).

### 8.2 Bring-up

Strict linear pipeline in `vk_device.cpp`/`vk_swapchain.cpp`, run once in `renderer_init`:

```
loader/dispatch (§5.3) → instance + validation → debug messenger → surface(platform) →
pick physical device (score) → logical device + queues → own allocator → swapchain → per-frame sync → pipelines
```

- **Instance:** request API `1.3`. Required extensions from `platform_vk_required_instance_extensions`. In dev: `VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils` + `VkValidationFeaturesEXT` (synchronization validation on — sync bugs are the #1 risk).
- **Physical device:** enumerate and **score** (never grab `[0]`): reject anything lacking graphics+present + `VK_KHR_swapchain`; discrete +1000, bigger device-local heap = more points; required features present.
- **Queues:** find graphics(+present), a **dedicated transfer** family if one exists (async uploads), and a **compute** family (claimed now, left idle — GPU pathfinding/culling is a deferred future use).

> **Resolved — Vulkan 1.3 / dynamic rendering is a documented hard minimum-spec, not an open deferral.** Pin the SDK and require API 1.3 + `dynamicRendering` + `synchronization2`. On hardware lacking these, device creation fails with a **clean fatal message** (the classic `VkRenderPass` path stays available behind the same seam as a fallback if a real minimum-spec problem appears). Dynamic rendering is preferred (fewer objects to own, fits the thin seam).

> **Resolved — validation asserts only on the *validation layer's* ERROR severity, never on best-practices/performance warnings** (which are advisory and fire on correct code). Messages route into our log (§10) regardless.

### 8.3 Swapchain

- **Format:** prefer `VK_FORMAT_B8G8R8A8_SRGB` + `SRGB_NONLINEAR`; first-available fallback.
- **Present mode:** `MAILBOX` if offered, else `FIFO` (always available). FIFO is the safe default; MAILBOX is the upgrade and may be absent on some drivers.
- **Image count:** `minImageCount + 1`, clamped to `maxImageCount`.
- **Depth:** a depth image created alongside, recreated with the swapchain. **Format is queried, not hardcoded:** pick from `D32_SFLOAT → D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT → D16_UNORM` via `vkGetPhysicalDeviceFormatProperties` (`DEPTH_STENCIL_ATTACHMENT_BIT`), store the choice, reuse on recreation. (Hardcoding `D32_SFLOAT` is not spec-guaranteed.)

### 8.4 Frames-in-flight & synchronization (the highest-risk area)

`FRAMES_IN_FLIGHT = 2`. Two correctness rules the code enforces:

1. **`render_finished` semaphores are per swapchain image, not per frame-in-flight** (sized `MAX_SWAPCHAIN_IMAGES`).
2. **A per-image in-flight fence array closes the gap between per-frame fences and per-image semaphores.**

> **Resolved — add `images_in_flight[]`.** With 2 frames but typically 3 swapchain images, `vkAcquireNextImageKHR` can return an image whose previous work isn't done, because the in-flight *fence* is per-frame, not per-image. Fix (canonical "Frames in flight"): keep `VkFence images_in_flight[MAX_SWAPCHAIN_IMAGES]` initialized to `VK_NULL_HANDLE`; after acquire, if `images_in_flight[img] != NULL` wait on it, then set `images_in_flight[img] = frame.in_flight` before submit.

```c
typedef struct {
    VkCommandPool pool; VkCommandBuffer cmd;
    VkSemaphore image_available; VkFence in_flight;
    GpuBuffer view_ubo;      // persistently mapped, written each frame
    GpuBuffer instance_buf;  // per-frame instance transforms
    Arena draw_items;        // transient submit() target
} FrameData;
typedef struct {
    FrameData frames[FRAMES_IN_FLIGHT];
    VkSemaphore render_finished[MAX_SWAPCHAIN_IMAGES];  // per IMAGE
    VkFence     images_in_flight[MAX_SWAPCHAIN_IMAGES]; // per IMAGE (points at a frame fence)
    uint32_t frame_index;
} FrameRing;
```

The per-frame recipe (the documented, sacred sequence):

```
begin_frame(view):
  vkWaitForFences(frame.in_flight)
  result = vkAcquireNextImageKHR(..., frame.image_available, &img)
  if OUT_OF_DATE: recreate_swapchain; return RETRY
  if images_in_flight[img] != NULL: vkWaitForFences(images_in_flight[img])   // <-- the fix
  images_in_flight[img] = frame.in_flight
  vkResetFences(frame.in_flight); write view UBO; vkResetCommandPool(frame.pool)

submit(items): memcpy into frame.draw_items arena

end_frame():
  begin cmd; barrier UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL; begin dynamic rendering (+depth)
  sort draw_items by (pipeline, mesh); bind & instanced-draw
  end rendering; barrier COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR; end cmd
  vkQueueSubmit: wait=image_available @ COLOR_ATTACHMENT_OUTPUT, signal=render_finished[img], fence=frame.in_flight
  vkQueuePresentKHR: wait=render_finished[img]
  if OUT_OF_DATE/SUBOPTIMAL: recreate_swapchain
  frame_index = (frame_index+1) % FRAMES_IN_FLIGHT
```

Swapchain recreation is a first-class explicit path: Phase 1 uses a full `vkDeviceWaitIdle` then recreates views/depth/framebuffers, checked **non-blocking on the main thread** (never blocking the loop while minimized — §4.2). Refine to per-fence waits only after the path is proven.

### 8.5 Our own device-memory allocator (two phases)

The interface is stable across both phases so call sites never change:

```c
typedef struct { VkDeviceMemory memory; VkDeviceSize offset, size; void* mapped; } GpuAllocation;
typedef enum { MEM_GPU_ONLY, MEM_CPU_TO_GPU } MemoryUsage;   // DEVICE_LOCAL / HOST_VISIBLE|HOST_COHERENT
GpuAllocation vk_alloc(VkAllocator*, VkMemoryRequirements, MemoryUsage);
void          vk_free (VkAllocator*, GpuAllocation);
```

- **Placement:** vertex/index/static textures → `MEM_GPU_ONLY`, uploaded via staging + transfer copy. Per-frame view UBO + instance data → `MEM_CPU_TO_GPU`, persistently mapped, written directly (no staging, no flush — coherent).
- **Phase 1 (build first): naive dedicated** — one `VkDeviceMemory` per resource. Trivially correct; unblocks the whole renderer ladder. Asserts when the live count nears `maxMemoryAllocationCount` (spec min 4096) — set the threshold **conservatively (~3500)** to leave headroom for swapchain/depth, and surface the live count on the overlay. That assert *is* the signal to start Phase 2.
- **Phase 2 (when the assert fires / assets grow): block allocator** — large blocks (suggest 256 MB; smaller on integrated) per memory-type, suballocated by a coalescing free list honoring `req.alignment` and `bufferImageGranularity` (simplest correct rule: never mix buffers and optimal-tiled images in one block). **`bufferImageGranularity` is handled in the Phase-1 dedicated and staging paths too, not only here.**
- **Staging:** a persistent host-visible ring buffer; copy → `vkCmdCopyBuffer`/`BufferToImage` → fence. Phase 1 may upload on the graphics queue; Phase 2 moves to the dedicated transfer queue with queue-family ownership transfers.

### 8.6 Resources, descriptors, pipelines

- Buffers/images live in flat generation-tracked handle tables (SoA where it pays). A tiny fixed sampler set (linear/repeat, linear/clamp, nearest/clamp) referenced by index.
- **Descriptor strategy — frequency-based:** `set=0` per-frame (view/proj UBO), `set=1` per-material (texture+sampler, allocated once at create), per-draw model/instance via **push constants**. One pool sized up front. **Bindless/descriptor-indexing is deferred** (the handle seam keeps the later switch localized).
- **Push constants:** model matrix (64 B) + `instance_base` (4 B) = 68 B, under the 128-byte guaranteed minimum; total combined push range across all stages stays under `maxPushConstantsSize`.
- **Pipelines:** created up front from a small static registry (one per material class: `mesh`, `instanced_unit`, `text`, `debug_line`). A single on-disk `VkPipelineCache` blob is loaded at startup and saved at shutdown. SPIR-V is loaded raw via the platform file API from `MOBA_SHADER_DIR` (§3.4).

### 8.7 Rendering approach (RTS/MOBA)

Forward rendering (light- and overdraw-modest scenes; deferred is unjustified). **Instanced/batched draws are the core scaling move:** units sharing mesh+material draw as one `vkCmdDrawIndexed(instanceCount=N)`, pulling per-instance transforms from a per-frame host-visible instance buffer indexed by `gl_InstanceIndex + instance_base`. Draw items are sorted by `(pipeline, mesh)` to minimize state changes and coalesce batches. Frame structure is a single hardcoded forward pass with explicit barriers via dynamic rendering. **Deferred:** render graph, GPU compute, bindless, MSAA/shadows/post, async transfer queue, Phase-2 allocator, SDF text — each behind an existing seam.

---

## 9. ECS & Deterministic Simulation

The spine of the game (`eng_sim`): what exists in the world, and how it advances in time. Built backward from the determinism contract (§2.1).

### 9.1 Entities & sparse-set SoA pools

**Entities are 32-bit generational handles (§2.3); components live in per-type sparse-set pools stored SoA.** Not archetypes (over-engineering for hundreds of units, a small stable component set, and hand-picked driving pools), not OOP `GameObject`. Sparse sets give O(1) add/remove/has, dense cache-friendly iteration, and code you can read in one sitting.

```c
typedef struct {                  // EntityManager: free-list over generation slots
    uint8_t  *generations;        // [max], parallel to slot index (gen 0 never valid)
    uint32_t *free_indices; uint32_t free_count, next_fresh, max_entities;
} EntityManager;
EntityId entity_create(EntityManager*); void entity_destroy(EntityManager*, EntityId);
bool     entity_alive(const EntityManager*, EntityId);
```

```c
typedef struct {                  // one per component type; component DATA is SoA, declared per-type
    uint32_t *sparse;             // [max] entity index -> dense slot (+1; 0 = absent)
    EntityId *dense_entity;       // dense slot -> owning entity
    uint32_t count, capacity;
} ComponentPool;
// e.g. TransformPool { ComponentPool pool; fix *pos_x,*pos_y,*facing; }  -- Q16.16, NOT float.
```

**Destruction is deferred to a tick boundary** (a `pending_destroy` queue drained by one end-of-tick pass) so iteration order is never disturbed mid-tick. All pool memory comes from arenas, sized up front from `max_entities`.

### 9.2 Deterministic iteration (the one subtlety)

Swap-remove reorders the dense array by destruction history. **Rule: order-sensitive systems iterate by ascending entity index** (via a per-pool sorted index list rebuilt only when membership changes — *not* every tick), never raw dense order. The query API's default is ordered; unordered dense iteration is an explicit opt-in reserved for *commutative* systems (e.g. pure integration), which are documented as such so the sort isn't paid where it isn't needed.

> **Resolved — the flagship example uses the ordered API.** Earlier the `sys_movement` sample iterated raw dense order while the doc mandated ordered iteration. Canonical examples iterate the sorted index list; commutative-vs-order-dependent is stated per system, since paying an O(n log n) sort per pool per tick for commutative work is wasted budget at hundreds of units × many pools × 30 Hz.

### 9.3 The World, systems, schedule

`SimWorld` aggregates the entity manager, every pool, and the sim singletons (`tick`, `Rng` (§2.8), `EventQueues`, `pending_destroy`). Its flatness is what makes snapshot/hash/restore trivial. **Systems are plain free functions; the schedule is a hand-written fixed-order array** — no base classes, no auto-discovery, no dependency-graph solver. That array *is* the deterministic ordering.

```c
static void sim_tick(SimWorld* w, const CommandBuffer* cmds) {
    events_swap(&w->events);            // last tick's outputs become this tick's inputs
    sys_apply_commands(w, cmds);
    sys_ai(w);                          // minion/tower target selection (tiny state machines)
    sys_movement(w);                    // flow-field / A* desired velocity (§12)
    sys_avoidance(w);                   // grid-bucketed separation (RVO-lite)
    sys_abilities(w);                   // cast / cooldown
    sys_projectiles(w);                 // advance / collide
    sys_combat_resolve(w);              // drain DamageEvents in one place
    sys_status_tick(w);                 // buffs/debuffs
    sys_fog_update(w);                  // authoritative vision for ALL teams (§12)
    sys_death_cleanup(w); sys_flush_destroy(w);
    w->tick++;
}
```

Singletons (game clock, RNG, team gold) are **not** entities — they live directly in `SimWorld`. Queries are minimal: pick a primary pool, iterate ordered, `pool_has`/`pool_get` siblings. No query DSL until profiling demands it.

### 9.4 The game loop (sim side)

The fixed-timestep accumulator lives in the platform loop (§4.2) and calls `engine_frame` → `sim_tick` exactly `SIM_HZ` times per second. Between ticks, the present glue interpolates. **Interpolation, not extrapolation, by default** (smooth, never mis-guesses on the stops/turns a MOBA is full of). The 0.25 s frame clamp prevents the spiral of death.

### 9.5 Events without breaking determinism

**Double-buffered, append-only typed event queues drained in deterministic order. No immediate callbacks, no observers, no virtual dispatch.** Systems emit POD event records (`DamageEvent`, `DeathEvent`, "play hit sound"); consumers drain in append order (deterministic because producers ran in deterministic order). Combat is event-driven: systems emit `DamageEvent`s; one central `sys_combat_resolve` applies mitigation, on-death, and kill-credit in one place. Events leaving the sim (audio/UI cues) are read on the presentation side — that's how audio/UI stay subordinate without coupling in.

### 9.6 Snapshots & the present glue (one fixed→float owner)

> **Resolved — the present glue owns interpolation + fixed→float + DrawItem building.** Earlier three owners were named (ECS `loop.c`, net/sim glue, game layer) and the renderer's actual input (`DrawItem[]`+`FrameView`) didn't match the snapshot/alpha the sim/net assumed. Canonical: a **present glue** in `game/present/` takes `RenderSnapshot prev`, `RenderSnapshot curr`, and `alpha`, and emits `DrawItem[]` + `FrameView`. It is the **single place** `fix_to_f32` is called. The renderer stays a pure consumer of float `DrawItem[]`/`FrameView` and never sees `SimWorld`.

```c
typedef struct {                        // slim, flat, memcpy-friendly; presentation fields only
    uint64_t tick; uint32_t count;
    EntityId *id; fix *pos_x, *pos_y, *facing; int32_t *hp, *hp_max; uint16_t *anim_state;
} RenderSnapshot;
void snapshot_extract(RenderSnapshot* dst, const SimWorld*);  // per tick, sim-side (fixed)
// present_build(prev, curr, alpha) -> DrawItem[] + FrameView   // game/present, the ONE fixed->float edge
```

### 9.7 Determinism enforcement & deferrals

The float ban is convention+hash-enforced (§2.1): a CI/grep check on `sim/*.cpp` plus the run-twice state-hash self-check (§10), with the sim lib pinned to identical `/fp:precise` flags in one toolchain include. **Deferred:** archetypes, multithreaded systems/job system (sim stays single-threaded — fast enough at 30 Hz for hundreds of units; parallelize presentation first), rollback machinery (the design is rollback-*ready* via flat snapshot + pure `sim_tick` + `sim_hash`, but server-authoritative ships first; rollback/GGPO is explicitly NOT the model, latency hidden by local prediction + interpolation + lag compensation), generic query DSL, reflection/serialization codegen, scene-graph/parent-child entities.

---

## 10. Tooling, Testing, Debugging

The discipline layer (`eng_core` debug facilities + `tests/` + `tools/`). All hand-rolled, no third-party. Its crown jewel — the **determinism harness** — is built *first*.

### 10.1 Determinism harness (the centerpiece, built before sim content)

The sim is pure: a full match replays from `seed + input stream`. Three pieces:

1. **Input recording (replay):** record only per-tick `Cmd_Packet`s + a header (`{magic, version, sim_logic_hash, seed, tick_rate, player_count}`). Tiny. The replay codec is *shared* with the netcode (one source of truth for the wire format, §11).
2. **Per-tick state hash:** after every `sim_tick`, FNV-1a over the entire authoritative `SimWorld` (RNG state + every gameplay-affecting SoA array, fixed order) — fast, simple, ours. It hashes only deterministic sim state (never render/interp/debug/timing).

```c
uint64_t sim_hash(const SimWorld* s);   // FNV-1a over rng + all sim SoA arrays (Q16.16 bytes)
```

3. **Comparator (two modes):** *self-check* (record replay + per-tick hashes, replay later and assert hash equality every tick — catches non-determinism against the same machine, the most common early bug); *server replay-hash integrity* (the server records inputs + per-tick `sim_hash`; an offline re-sim must reproduce the hash stream) and *client prediction-divergence* (compare the client's predicted controlled-entity state at tick T against the server's authoritative value, beyond the benign-correction tolerance); on mismatch, log the **first divergent tick** and dump both states; a field-diff tool names the exact array/entity that diverged — turning "the game desynced" into "entity 47's cooldown differs at tick 5012").

This is the `test_determinism.cpp` integration test and the live overlay canary. It costs ~a day and catches determinism bugs the moment they're introduced — retrofitting after months is archaeology.

### 10.2 Unit test harness

A ~200-line single-header `core/test.h` (doctest-shaped, **no exceptions, no STL**, self-registering `TEST(...)` macros, `CHECK(...)`), one tiny `tests/main.cpp` calling `test_run_all()`. Wired into CTest:

```cmake
add_executable(engine_tests tests/main.cpp tests/test_math.cpp tests/test_arena.cpp
               tests/test_serial.cpp tests/test_determinism.cpp)
target_link_libraries(engine_tests PRIVATE eng_core_group)   # headless: no Win32, no Vulkan
add_test(NAME unit COMMAND engine_tests)
```

**Test, in priority order:** math (pure, regression-prone, everything depends on it — incl. property tests), allocators (alignment, free-list reuse, no overlap, scratch save/restore), containers, serialization (round-trip byte-identical), and the determinism replay-hash test. **Don't unit-test:** Vulkan (validation layers + visual + RenderDoc), Win32 glue (running the game exercises it), or UI. Don't chase coverage.

### 10.3 Debug stack

- **External:** Vulkan validation layers (dev builds; routed into our log, assert on validation ERROR only — §8.2); **RenderDoc** (primary GPU frame debugger; tag passes/buffers via `VK_EXT_debug_utils`); **PIX** (GPU timing, deferred until perf matters); **MSVC debugger** + a `.natvis` so `Array<T>`/`HashMap`/handles/`vec3` display nicely.
- **In-engine (`eng_core`):** leveled+channeled **logging** (fans to `OutputDebugStringA` + stdout + rotating `logs/engine.log`; `LOG_TRACE/DEBUG` compiled out in release; Vulkan messages join the same stream); two-tier **asserts** (`ASSERT` debug-only, `ENSURE` always-on, both `__debugbreak`; §2.4); immediate-mode **debug draw** (`dbg_line/sphere/aabb/text` queued into a frame arena the renderer drains — pushed from the sim side, drawn by the renderer, never affecting the hash); a scoped CPU **profiler** (QPC/`__rdtsc`, named scopes into a ring buffer; GPU timestamps via query pool read back one frame late); and a **debug overlay** (F1) showing FPS/frame-time, per-arena bytes, entity counts, draw calls, live alloc count, **current tick + state-hash** (the live desync canary), and net RTT once netcode exists.

### 10.4 Dev workflow (solo, multi-year)

Trunk-based-for-one: `main` always green; short `feat/`/`fix/`/`spike/` branches merge within days; milestone tags (`v0.1-triangle`, `v0.2-deterministic-sim`). Conventional-commit prefixes; commit messages end with the `Co-Authored-By` trailer. A committed **pre-push hook** runs `ctest` (the solo dev's only CI gate). **Living docs** in `docs/`, updated in the same commit that changes behavior: `ARCHITECTURE.md` (this file), `ROADMAP.md` (now/next/later milestones), `CONVENTIONS.md` (§2.4–2.5 verbatim), `DECISIONS/` (tiny dated ADRs — fixed-point format, handle ABI, Vulkan loader, render seam, etc.). **Deferred:** GitHub Actions CI, coverage, in-game console/cvars, GUI tooling.

---

## 11. Asset Pipeline & Resource Management

Two tiers: loose source files in dev, baked by an **offline cooker** (`tools/cooker`, may use STL — never links into the engine) into a single engine-native binary format the runtime loads with near-zero parsing.

### 11.1 Formats: what we parse, where

| Asset | Author | Parsed where | Runtime sees |
|---|---|---|---|
| Texture | **PNG** (prod) / **TGA** (bootstrap) | PNG: cooker only. TGA: cooker + direct runtime loader early | RGBA8 mip chain |
| Mesh | **glTF 2.0 binary `.glb`** (strict subset) | cooker only | flat SoA vertex streams + index buffer + material refs |
| Audio | **WAV** now, OGG later | WAV: direct runtime. OGG: deferred, cooker-only | raw PCM |
| Font | bitmap atlas first; **TTF** later | atlas: cooker. TTF: deferred, cooker-only | glyph atlas + metrics |
| Shader | **GLSL** | `glslc` at build time (§3.4) | **loose `.spv`**, loaded via platform file API (not `.mba`) |

Heavy parsers (DEFLATE for PNG, glTF JSON) live **only** in the cooker. The runtime has one loader and a switch on a type tag.

### 11.2 The baked container (`.mba`)

One outer header for every asset type; a typed POD payload, little-endian, naturally aligned, designed so GPU upload points directly at a loaded offset with no per-element fixup.

```c
#define MBA_MAGIC 0x41424D6Du
#define MBA_VERSION 3
typedef enum { ASSET_NONE, ASSET_TEXTURE, ASSET_MESH, ASSET_SOUND, ASSET_FONT } AssetType;
typedef struct { uint32_t magic, version, type, payload_bytes; uint64_t asset_id; uint32_t flags, _pad; } MbaHeader;
```

Every load is: read header → validate magic/version (mismatch hard-rejects → re-cook) → switch(type) → the payload is already in the engine's exact layout. The cooker output must be **byte-deterministic** (it feeds the deterministic sim's reproducibility). The cooker is brute-force (re-cook all) until cook times hurt; incremental cooking is deferred.

### 11.3 Runtime registry & lifetime

A SoA registry keyed by `AssetHandle` (§2.3 ABI): parallel `generation/type/state/asset_id/cpu_blob/gpu/refcount` arrays.

- **Level assets (the majority):** owned by a `level_arena`. `assets_unload_level()` resets it in O(1), bumps generations (invalidating dangling handles), and tells the renderer to free that GPU batch. Ideal for a MOBA (load match-start, free match-end).
- **Global assets (small set — fonts, UI atlas, common shaders/default textures):** manual `refcount`, freed at zero.

A `state`/`LOADING` field reserves the seam for a future job-system async loader; day-one loading is synchronous at level-load behind a loading screen.

### 11.4 GPU upload (rides the renderer seam)

The asset layer **never calls Vulkan.** It produces a CPU blob + a `TextureDesc`/`MeshDesc` (shared structs in `render/renderer_types.h`) and calls the renderer's `renderer_create_texture`/`renderer_create_mesh` (§8.1), storing back the typed handle. The renderer owns staging, the transfer queue, and the device-memory allocator. Teardown and hot-reload both use the renderer's deferred-destroy (`frames_until_free`) so nothing in flight references a freed resource.

### 11.5 Hot-reload (dev-only)

`platform_watch_dir` (`ReadDirectoryChangesW`) re-cooks the changed source, loads the new blob into a staging slot, uploads, and on the next frame boundary atomically swaps `cpu_blob`/`gpu` behind the **same stable handle**, deferring old-resource destruction by `FRAMES_IN_FLIGHT`. **Shaders first** (recompile to SPIR-V, rebuild the pipeline; on compile failure keep the old pipeline and log — never crash on a typo), then textures, then meshes. Compiled out of shipping builds.

### 11.6 Packaging

Dev: loose baked `.mba` in `baked/`. Ship: a single `pack.pak` (concatenated blobs + a head TOC of `{asset_id, offset, size}`), mmap'd; a `fs_open_asset(asset_id)→{ptr,size}` file-source hides loose-vs-pak so the loader is identical. A `compressed_size` TOC field is reserved; compression is deferred.

**Deferred:** OGG/Vorbis, TTF rasterization, archive compression, async/streaming I/O, BCn textures, incremental cooking, mesh LOD.

---

## 12. Networking & Gameplay Systems

### 12.1 Networking: server-authoritative

**An authoritative server owns the one true simulation; clients predict locally and the server corrects them.** This is the modern competitive-MOBA shape (LoL / Overwatch / Source lineage): the client sends *intent* (the same `Command` stream §2.1 already defines), never state; the server runs the single authoritative deterministic `sim_tick` and replicates back only the slice of the world each client is allowed to see. The cost is interest management + delta compression from day one (bandwidth scales with *visible* unit count, not with input count); the payoff is the two things lockstep could never buy — **real fog of war** (the server withholds what a client shouldn't see) and **anti-cheat by construction** (clients can't act on or render state they were never sent). Determinism stops being a hard correctness requirement holding the whole match hostage and becomes a *leveraged asset* (see the closing paragraph).

> **Resolved — server-authoritative, not lockstep.** Earlier drafts specified deterministic lockstep (P2P host-relay, send-only-inputs, run-at-the-laggiest-peer). That model is **replaced** by explicit decision (ADR-0011). Lockstep's headline win (O(inputs) bandwidth, unit-count-independent) is real but came with fatal-for-competitive costs: every client holds full world state (so maphacks are unpreventable — fog could only ever be presentational), the match runs at the worst peer, and there is no place to validate a cheating client. Server-authority inverts all three. The `sim_tick` purity, fixed-point math, `eng_serialize` codec, the two-channel UDP reliability seam, and the replay/`sim_hash` harness are **all preserved** — they were the right primitives; only who-runs-the-authority-and-what-crosses-the-wire changes.

- **Topology: one authoritative server; clients connect to it.** Two server shapes, the *same* `sim_tick`: a **dedicated headless server** (the deterministic core minus rendering — `eng_core_group` plus `eng_platform` for sockets/clock, no `eng_render`, no present glue) and a **listen server** (one player hosts authoritatively and also renders, running a co-located client against the in-process server over a loopback transport). Clients never simulate the whole match and never trust each other; there is no peer-to-peer game state and no host election. ≤10 connections is the design point.
- **Where the server logic lives (so it is headless-testable).** The **server tick driver**, the **per-client baseline/delta snapshot builder**, the **interest-management filter**, and the **lag-compensation history ring** all live in `eng_net` — OS-free, linked into `eng_core_group` — consuming the vision query from `eng_sim` and `eng_serialize` for encoding. Only the Winsock send/recv stays in `eng_platform`. This is what makes the staging below real: the Stage-2 in-process client/server split and the Stage-3 headless replication tests run with zero UDP because nothing server-side is stranded in `game/`.
- **Transport: raw UDP via Winsock2** behind the `platform_net.h` socket seam — never TCP (head-of-line blocking would freeze a player on one lost packet). We reuse, unchanged, our own sequence / ack / ack-bitfield reliability with two channels on one socket: **reliable-ordered** (connection handshake, match setup, the input/`Command` stream, anything that must never silently drop) and **unreliable** (the high-rate state snapshots, clock-sync pings, RTT probes — newest-wins, stale ones are simply dropped). The reliability/channel logic is model-agnostic and stays OS-free in `eng_net`; only the Winsock impl is in `eng_platform`.
- **Client → server: the input (`Command`) stream.** The client turns local actions into the same serializable `Command`s §2.1/§12.2 define (fixed-point targets, queued `Order`s) and sends them on the reliable channel tagged with a monotonic **client input sequence number** and the **issue tick** (the predicted-clock tick the command was issued on, below). The server validates each command (range/cooldown/ownership/fog — a client may only command units it owns and target things it can legitimately perceive), applies the survivors on a known authoritative tick, and **echoes the last input sequence it has consumed** inside every snapshot. That echoed number is the anchor for reconciliation. The same input packet **piggybacks the sequence of the most recent snapshot the client has received** (the delta baseline, below). `Command` remains the *only* gameplay data a client puts on the wire.
- **Server → client: state replication by baseline + delta.** The server snapshots authoritative state each tick and sends each client a **per-client delta against that client's last *received* snapshot** — the snapshot sequence the client most recently reported in its input packets; if that baseline is unknown or too old to retain, the server sends a full baseline instead. Fields are **quantized** before diffing — positions/facing as raw `fix` (or fewer bits where range allows), hp as integers, anim/team as small enums — so only changed, coarsened fields cost bytes. Snapshots/deltas ride the **unreliable** channel (newest wins; a dropped delta just means the next one diffs against an older still-known baseline). There is **no reliable-channel ack of baselines** — the snapshot baseline is reported, not reliably acked, exactly so we keep the head-of-line-free property the unreliable channel buys. The whole per-client payload is **bounded by interest management**, below.
- **Interest management / area-of-interest — this *is* fog of war.** The server computes authoritative vision for every team (the `sys_fog_update` pass, §9.3/§12.2) and, per client, includes in the snapshot **only the entities/cells that client's team currently perceives** (visible now, plus sticky-explored static terrain where appropriate). State a client may not see is *never serialized to that client* — hidden entities are **absent**, not zeroed. There is no separate "anti-maphack" feature: a maphack can only reveal bytes the client received, and the client received none of the hidden state. Interest management doubles as the bandwidth governor — payload tracks *visible* unit count, which is bounded by screen/vision, not by total match population.
- **The wire type: `NetSnapshot`, distinct from `RenderSnapshot`.** What crosses the wire is **not** the local-sim `RenderSnapshot` (§9.6). It is a `NetSnapshot`: the replicated field set — **stable network id ↔ `EntityId`**, transform, facing, hp, hp_max, anim_state, **team**, **visible flag**, and spawn/despawn events — built in ascending id for deterministic server-side encoding. The client deserializes a `NetSnapshot` and reconstructs the `RenderSnapshot` the present glue consumes. (`RenderSnapshot` gains nothing on the wire; do not conflate them — "RenderSnapshot-equivalent" was loose phrasing.)
- **The client timeline (one coherent definition).** The client runs **two** clocks off its server-tick estimate, both in integer ticks: (1) a **prediction / command-issue clock** = `estimated_server_tick + send_ahead_lead`, run slightly *ahead* so the client's inputs arrive just before the tick that needs them; (2) a **render / interpolation clock** = `latest_received_snapshot_tick − interp_delay`, run slightly *behind* so remote entities can be interpolated between two already-received snapshots. `interp_delay` is an **integer number of ticks** (a small multiple of the snapshot interval, ~2 snapshots) so it is deterministic and reproducible. Commands carry the **issue tick** (for reconciliation against the server's echoed input-seq); targeted actions *additionally* carry the **render tick** the client perceived the world at (for lag-comp rewind, below). No wall-clock leaks into the sim — time is still `world.tick` (§2.1); these clocks live entirely on the presentation/net side.
- **Client-side prediction (locally-controlled units only, against static state only).** To hide RTT on the player's *own* actions, the client runs the **same deterministic `sim_tick`** forward over its own un-acknowledged `Command`s, but **only over the entities it controls** — its hero/units — and **only against state the client authoritatively holds: the static map/collision grid** (the baked `.gamedata` tile grid, §12.2, which clients have in full) **and its own inputs**. The client does *not* hold full world state (interest management withholds it), so it **cannot and must not** simulate the rest of the match. Critically, **predicted units never resolve collision or combat against interpolated / last-known remote entities** — those are server-authoritative and arrive as corrections. This makes prediction a pure function of (own inputs + static map + own controlled state), and makes leave-PVS flicker (below) a presentation-only concern with zero effect on prediction.
- **Server reconciliation (snap + replay, drift-free where it can be).** When an authoritative snapshot arrives, the client (1) **snaps** its controlled entities to the authoritative state in that snapshot (the server-confirmed *starting* state), then (2) **replays** every local input *newer* than the snapshot's echoed input-sequence on top of that state, using the same `sim_tick`. Because the sim is fixed-point deterministic and the replay touches only own-inputs-against-static-map, a correct prediction produces *zero* visible correction, and a mispredict resolves cleanly with no accumulating drift; only the locally-owned subset is ever re-simulated (no whole-world rollback). **Expected (not a bug):** interactions with entities the client couldn't predict — an enemy that was outside the snapshot, a collision/hit from a remote unit, a server-rejected input — *will* produce a correction. These are smoothed visually, exact in state, and are **distinct from true sim drift** (build/flag divergence); M6.6's tolerance rule is what separates "legitimate correction" from "predicted step diverging from the server."
- **Entity interpolation (remote / visible entities).** Entities the client does *not* control are **never predicted** — they are **interpolated in the past** between the last two received snapshots at the render/interpolation clock above. The present glue (§9.6) already interpolates between two `RenderSnapshot`s by `alpha`; here the two endpoints are reconstructed from *received* `NetSnapshot`s rather than locally-ticked ones. Remote motion is therefore smooth and never extrapolated (no mis-guessing the stops/turns a MOBA is full of), at the cost of showing remote units slightly in the past — the standard, correct trade. Entities that leave the client's vision (leave-PVS) hold last-known then hide; since prediction never collides against them, the flicker is cosmetic.
- **Lag compensation (server-side, the server still owns the verdict).** The server keeps a **bounded ring of recent authoritative states** (positions/hitboxes per tick, a few hundred ms). When it validates a hit/ability whose outcome depends on where a target *was* from the shooter's perspective, it rewinds to the **render tick the client stamped on the action** — clamped to the history ring and validated to lie within `[now − max_rewind, now]` and to be plausible given measured RTT (implausible rewinds are rejected, per M6.5) — tests the hit against that historical state, then applies the result on the present tick. The server trusts the *stamped, validated* render tick; it does **not** re-derive `now − RTT − interp_delay` (that double-counts the interp the client already baked into its stamp). This lands well-aimed actions despite latency without letting the client assert outcomes.
- **Clock / tick sync.** The server's `world.tick` is the master clock. Clients estimate server-tick from periodic timestamped pings on the unreliable channel (RTT + smoothed offset) and drive the two-clock timeline above. Sequence numbers — input-seq client→server, snapshot-seq server→client — carry the correlation.
- **Anti-cheat posture.** The server is the only authority: it never trusts client-reported positions/results, validates every command against game rules, and — via interest management — physically withholds hidden state so the most common MOBA cheat (maphack) is impossible by construction rather than by detection. Speed/teleport/illegal-target cheats fail server-side validation; implausible lag-comp rewinds are rejected. This is a v1 posture (rule + visibility enforcement); signed builds, encryption, and statistical detection are deferred.
- **Bandwidth strategy (and its accepted cost).** Three multiplicative wins keep bytes low: **delta** (only changed, quantized fields), **interest management** (only visible entities), **quantization** (coarsened field widths). Together payload scales with *what one client can see*, not with total unit count — exactly the property a hundreds-of-units MOBA needs. The accepted cost versus lockstep is that bandwidth is no longer O(inputs): we pay per-visible-entity per-snapshot, and we pay the engineering of snapshots/deltas/interest from day one. That cost buys real fog and real anti-cheat, which the project now requires.

**How determinism is leveraged (not required).** Fixed-point determinism is no longer a hard, fragile match-wide invariant whose single violation desyncs everyone — it is now a tool used in two precise places. (1) **On the server**, the pure `sim_tick` + `sim_hash` + recorded input stream give perfect portable replays and reproducible-from-inputs debugging (§10.1), and a listen-server and a dedicated server compute identical results. (2) **On the client**, prediction reuses the *same* `sim_tick` over local inputs against static map state, so reconciling against the server is **drift-free for the predictable subset**: identical inputs + identical starting state + identical static map ⇒ identical output, bit-for-bit, which is exactly why keeping Q16.16 (§2.1) is worth it here. Determinism is leveraged where it pays (replays, reconciliation) and no longer load-bearing where it was brittle (every client lock-stepping the whole world).

**Staging (no socket code until replay is bit-stable):** Stage 0 fixed-step local sim → Stage 1 `Command` record/replay + the per-tick `sim_hash` oracle (the determinism harness, §10.1 — ~90% of the networking-enabling work, zero sockets) → Stage 2 in-process client/server split (the client talks to a co-located authoritative server through the net seam, exercising input→snapshot→prediction→reconciliation with no UDP) → Stage 3 UDP: dedicated + listen server, baseline/delta snapshots, interest management, entity interpolation, lag compensation, clock sync. **Deferred for years:** NAT traversal (LAN / direct-IP first), matchmaking, advanced congestion control, reconnect, hardened anti-cheat (encryption/signing/statistical detection), and predicting remote entities (rollback-style) — **rollback/GGPO is explicitly *not* the model** and is not a planned upgrade; the chosen latency-hiding tools are local prediction + interpolation + lag compensation.

### 12.2 Gameplay systems (RTS/MOBA)

All sim state is Q16.16 (§2.1), ticked at 30 Hz, ordered deterministically (§9.3).

- **Map:** a single fixed-extent uniform **tile grid** (SoA cell flags: walkable / blocks-vision / height-band; ~256² @ 0.5 u) is the *authority*. It is baked offline to `.gamedata` and **shipped in full to every client** (it is static, non-secret terrain — clients need it for local prediction collision, §12.1). The render heightfield mesh is a **decoupled, derived, cosmetic** product uploaded once via the renderer seam, so rendering never constrains the sim.
- **Movement (tiered):** goal-keyed **flow fields** for mass movement (cost is per-destination, so 200 units sharing a goal cost one field; built via a bucketed-integer Dijkstra with cell-index tie-break, small LRU cache) + per-agent **A\*** for sparse hero pathing. **HPA\*** deferred (small map). Local anti-overlap is a **fixed-point grid-bucketed separation pass (RVO-lite)** — determinism+simplicity chosen over ORCA/boids. Quantized 8-direction movement v1; path smoothing deferred. *(Note: this inter-unit separation runs only in the authoritative sim; client prediction collides against the static grid only, never against other units — §12.1.)*
- **Units & orders:** units are ECS entities with SoA components (`Vital`, `Stats`, `UnitTag`). **Selection is client/view-only and never enters the sim** (picking is float view-space; its only output is which entity ids the local player issues orders to). Player actions become **`Command`s** (queued `Order`s) that cross the netcode seam — now sent to the authoritative server (§12.1), validated, and applied on a known tick — the determinism boundary. Cross-tick reads sort spatial-query results by entity index before applying.
- **Abilities:** flat **data records** composed of a fixed effect vocabulary (`EFFECT_DAMAGE/HEAL/SPAWN_PROJECTILE/APPLY_STATUS/DASH/AOE`) loaded from baked tables — **no per-spell code, no scripting VM** (deferred). Projectiles and status effects are their own SoA systems sharing that vocabulary. Stacking taxonomy (refresh/stack/unique-source) is a field on `StatusDef`.
- **Combat:** event-driven (§9.5); damage formula `applied = amount · 100/(100+armor)` in fixed-point; death enqueues on-death (gold/XP, respawn timer).
- **AI:** tiny deterministic state machines for minions/towers (lane-push → attack-nearest with index tie-break → return; towers fire by fixed priority). Behavior trees/GOAP and bot heroes deferred.
- **Vision/fog:** computed as **authoritative sim state on the server** for every team — the simulation owns the truth and never hides information from itself — with cells visible / explored (sticky) / hidden via integer LOS respecting `BLOCKVISION`/`height_band` (`sys_fog_update`, §9.3). **The server then sends each client only the slice that client's team perceives** (interest management, §12.1): hidden entities and the state behind the fog are *never serialized* to a client that shouldn't see them. Fog is therefore real, not presentational — a client literally cannot render or act on what it was never sent.

> **Resolved — fog is real and anti-maphack is solved by construction.** This supersedes the prior callout (which kept fog *presentational*, accepted that pure lockstep cannot prevent maphacks, and deferred per-client filtering as a named future item). Under server-authority that deferral is **closed**: per-client state filtering is the server's interest-management pass (§12.1), it exists in v1, and it makes the maphack class of cheat impossible because the hidden bytes never leave the server. Gameplay computes authoritative vision for all teams; networking ships each client only its visible slice. There is no longer a separate anti-maphack to-do.

**Seams:** gameplay components are SoA arrays keyed by `EntityId`; the *only* inputs to the sim are `Command`s, the *only* requirement back is bit-identical state (`sim_hash`); the static tile grid is shared in full with clients (prediction collision) while dynamic entity state is the **server-filtered visible slice** — the client reconstructs a `RenderSnapshot` from received `NetSnapshot`s (locally-controlled units predicted, remote units interpolated — §12.1) for the renderer, alongside the static heightfield mesh uploaded once; map/unit/ability tables load from baked `.gamedata`.

---

## 13. The Milestone Ladder

> **Resolved — one canonical ladder** merging the build bring-up, the renderer ladder, and the "build-first" determinism harness, so ordering is unambiguous. Note the asset/renderer dependency: the *first* textured quad bypasses the asset manager (direct TGA + a hand-built upload), since the renderer's upload seam/descriptors aren't mature day one.

1. **Build spine + sandbox:** top-level CMake, `cmake/` helpers, `core`/`math`/`platform`/`render` skeletons, `CompileShaders.cmake` with one triangle shader, presets. `tools/sandbox` opens a window and clears to a color (exercises platform + the Vulkan loader/bring-up + present end-to-end). Add `*.spv` to `.gitignore`.
2. **Clear screen** (full bring-up: loader→instance→device→swapchain→frame loop→clear; proves sync + present).
3. **Triangle** (first pipeline, hardcoded verts; proves pipeline + dynamic rendering).
4. **Textured quad** (first buffer + image upload via staging + descriptor set; *direct TGA*, bypassing the asset manager; proves Phase-1 allocator + descriptors + samplers).
5. **Determinism harness** (replay record + per-tick `sim_hash` self-check) — built *now*, before sim content, against the first trivial `sim_tick`.
6. **Instanced meshes** (glTF→baked mesh in device-local buffer, hundreds of instances, camera UBO — the first "looks like a MOBA" frame; the asset manager + cooker come online here).
7. **Deterministic sim** (ECS pools, fixed-tick schedule, movement/flow-field, fog, the present glue's interpolation + fixed→float edge).
8. **Record/replay** as a first-class tool, then **server-authoritative netcode** (client/server split, delta + interest management, client prediction + reconciliation, lag compensation, live prediction-divergence + server replay-hash detector).
9. **Text/UI overlay + debug lines** (bitmap-font atlas; debug-draw pipeline).

---

## 14. Open Questions & Risks

**Honest unknowns** (to resolve with a spike before the relevant subsystem hardens, or with real profiling data):

- **World scale vs Q16.16 precision.** Q16.16 (~1.5e-5 step, ±32768) is locked, but confirm the chosen world-unit scale gives enough collision/pathfinding precision at map scale before the math lib hardens. The single-typedef seam allows a later widen, but it touches tuning values — so settle it early with the one-day spike.
- **Flow-field cache sizing/eviction** under worst-case many-distinct-goals (scattered micro) — could thrash the tick budget; LRU size (~16) needs validation against real arena design and 8-direction quantization may need smoothing promoted out of DEFER.
- **Full-state hash cost** at hundreds of units × 30 Hz — full-hash is simplest and likely fine; revisit dirty/incremental hashing only if it shows up in a profile.
- **Device-local block size / budget split** for the Phase-2 allocator on low-VRAM integrated GPUs — measure with real assets.
- **Per-frame instance data:** storage buffer indexed by `gl_InstanceIndex` (flexible) vs vertex-input instanced binding (simpler first) — pick at the instanced milestone.
- **Asset ID scheme:** hashed string path (friction-free, needs a cooker collision check + debug hash→name table) vs sequential manifest ints.
- **Prediction-divergence / server replay-hash cadence** (every tick vs every N).

**Top risks & mitigations:**

- **Determinism is fragile and global.** One stray float, unordered iteration, wall-clock read, or Debug/Release divergence desyncs the match, and the hash tells you *that* it broke, not always *why*. Mitigation: fixed-point sim, the grep/CI float ban, the run-twice self-check + first-divergent-tick field-diff comparator, identical `/fp` flags on the sim lib — all built first.
- **Synchronization & swapchain correctness** is the renderer's highest-risk area; mitigated by always-on sync validation in dev, the documented per-frame recipe with `images_in_flight[]`, and `vkDeviceWaitIdle`-on-recreate until proven.
- **The hand-loaded Vulkan loader** is more work than a one-liner (two-tier dispatch, NULL-instance global rule); a mis-resolved pointer fails opaquely — mitigated by null-checks at table build + `platform_fatal`.
- **Own allocators (heap/HashMap)** are subtle; mitigated by leaning on arenas (small heap surface), guard bytes/fill patterns, and fuzzed alloc/free unit tests.
- **Validation-only correctness** can still violate the spec on other vendors — test on at least one AMD/Intel/NVIDIA GPU before relying on behavior; keep the depth-format query and minimum-spec fatal message honest.
- **Solo-dev discipline decay** (living docs, green-main, the pre-push hook) silently erodes everything — the hook is the automated backstop; ADRs prevent re-litigating settled decisions.
- **Scope creep** (a generic ECS/test framework, premature SIMD/render-graph/rollback, an over-built cooker) is the biggest *time* risk for a solo multi-year build — honor the DEFER lists; the milestone ladder is the definition of done for each cut.
- **Anti-cheat by construction** (clients send only intent; the server validates commands and withholds unseen state via interest management, so map-hacks are structurally impossible) — hardened anti-cheat (packet signing/encryption, statistical detection) is the only deferred item.
