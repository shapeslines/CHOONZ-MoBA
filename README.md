# MOBA (working title)

A multiplayer online battle arena built **from scratch in C++**, on a custom
RTS-class game engine.

> 🚧 **Status:** Early development. **Phases 0–2 and Phase 3 M3.0–M3.2 complete.** Build spine, ADRs,
> Win32 window, memory arenas, float/fixed-point
> math, containers, and a self-registering test harness on CTest + a pre-push gate
> (determinism golden across `/fp:precise` + `/fp:fast`), plus GitHub Actions CI
> (Windows MSVC, `/WX`, Debug + Release).
> **The renderer draws a 500-cube instanced field:** a hand-loaded raw-Vulkan renderer (ADR-0004)
> on **dynamic rendering + synchronization2** (the hard minimum spec, ADR-0012) with
> offline-compiled SPIR-V (ADR-0008), an on-disk pipeline cache, depth, per-frame
> camera/instance buffers, typed resources, deterministic batching, deferred destruction,
> debug draw/F1 overlay, and an always-built null backend. An in-process
> readback (`sandbox --screenshot out.bmp`) captures what it rendered, **validation-
> clean** (verified on an RTX 4070 Ti), including an owner-run resize, minimize/restore,
> alt-tab, and F1-overlay interaction check. The Phase 3 simulation now has an arena-backed
> generational entity manager, typed sparse-set SoA Transform/Velocity/Health pools, stable replay
> unit slots, derived ascending-entity query caches, phase-buffered typed damage events, a literal
> plain-function schedule, deferred boundary destruction, canonical ECS state hashing/diffing, and the
> `moba_replay` record/inspect/verify CLI. Its 10,000-tick self-check is bit-identical in Debug and
> Release at `0x637628abff59c823`, and the mutation proof reports exactly
> `tick=4321 field=position_x entity=7`.
> **Next: Phase 3 M3.3 fixed-tick platform loop and SIM/PRESENTATION snapshot boundary.**
> Run it: `build\tools\sandbox\Debug\sandbox.exe --frames 90 --screenshot out.bmp`
> (the `--frames N` is required — `--screenshot` alone captures only on quit and
> will run forever).
>
> See [`docs/JOURNAL.md`](docs/JOURNAL.md) for the session log,
> [`docs/ROADMAP.md`](docs/ROADMAP.md) for the plan, and
> [`docs/DECISIONS/`](docs/DECISIONS/) for the architecture decisions.

## Goals

- A custom C/C++ game engine sized for an RTS/MOBA: many units, deterministic
  simulation, top-down 3D rendering, and competitive multiplayer.
- Built deliberately over the long term, learning and owning each layer.

## Building

**Prerequisites**
- Visual Studio 2026 with the **Desktop development with C++** workload (provides
  MSVC). Standalone CMake ≥ 3.28 also works.
- **Ninja** (required by the `dev` preset, a Ninja Multi-Config build). Visual
  Studio does not bundle it in every install, so install it explicitly:
  `winget install Ninja-build.Ninja`.
- The **LunarG Vulkan SDK** (sets `VULKAN_SDK`) — needed for the real renderer and
  the shader build (`glslc`). Without it the build still works but produces the
  **null render backend** (blank window; this is what CI builds for now).

**Build** — `cl`, `cmake`, and `ninja` must be on `PATH`, so build from a *Developer*
shell (or `call vcvars64.bat` first):

```bat
cmake --preset dev                          :: configure (Ninja Multi-Config)
cmake --build build --config Debug          :: or RelWithDebInfo / Release
```

`--preset ci` builds with warnings-as-errors (`/WX`). Configs: **Debug** (daily),
**RelWithDebInfo** (profiling / the build you play), **Release** (`/O2 /GL`+`/LTCG`).
See `docs/DECISIONS/` for the build contracts (ADR-0004/0006/0008/0009).

## Testing

Tests use a small self-registering harness (`tests/test.h`: `TEST()`/`CHECK`, no
exceptions/STL) and run under CTest:

```bat
cmake --preset ci                                   :: /WX build dir
cmake --build build-ci --config Debug
ctest --test-dir build-ci -C Debug --output-on-failure
```

CTest covers `mem`, `math`, `containers`, `platform`, `serialize`, entity/component/event/system/
schedule/sim suites, `render`,
`render_null`, `tga`, the `/fp:precise` + `/fp:fast` golden, the 10,000-tick replay proof,
the sim-boundary scan, and replay CLI fixtures. To run the replay tool directly:

```bat
build-ci\tools\replay\Debug\moba_replay.exe record --out match.mbr
build-ci\tools\replay\Debug\moba_replay.exe inspect match.mbr
build-ci\tools\replay\Debug\moba_replay.exe verify match.mbr
```

A **pre-push hook** runs the same `/WX` build + `ctest` and blocks the push on red.
Activate it once per clone (it shells out to `vcvars` so it works from any shell):

```bat
git config core.hooksPath tools/hooks
```

## Layout

```
engine/        the engine, one static lib per module (the CMake link graph = the architecture)
  core/        arenas, containers, handle.h, cadence config, log/assert (leaf)
  math/        fix.h (Q16.16), rng.h, vec/mat/quat                     (leaf)
  serialize/   bounded little-endian byte readers/writers              (leaf-up)
  platform/    the OS seam (Win32): window, input, timing, files, sockets, Vulkan surface
  render/      raw Vulkan behind a thin renderer seam; GLSL sources in render/shaders/
  sim/         arena-backed deterministic ECS, typed events/systems, schedule + replay codec
  (assets / net arrive in their phases)
cmake/         CompilerWarnings, EngineOptions, CompileShaders helpers
game/ tools/   the game exe, sandbox, asset cooker
assets/ tests/
docs/          ARCHITECTURE.md, ROADMAP.md, DECISIONS/ (ADRs)
```
