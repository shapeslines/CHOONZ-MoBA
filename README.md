# MOBA (working title)

A multiplayer online battle arena built **from scratch in C++**, on a custom
RTS-class game engine.

> 🚧 **Status:** Early development. **Phases 0–2 and Phase 3 M3.0–M3.4 complete** (M3.2 tagged
> `v0.3.0-m3.2`; `main` is gated by required CI checks via the `main-gate` ruleset). Build spine, ADRs,
> Win32 window, memory arenas, float/fixed-point
> math, containers, and a self-registering test harness on CTest + a pre-push gate
> (determinism golden across `/fp:precise` + `/fp:fast`), plus GitHub Actions CI
> (Windows MSVC, `/WX`, Debug + RelWithDebInfo + Release).
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
> M3.3 adds a platform-owned fixed-step accumulator, fresh same-tick commands for every owed tick,
> and arena-backed previous/current presentation snapshots. `eng_game` is the sole interpolation and
> fixed→float owner; the renderer still cannot see `SimWorld`.
> **Phase 3 is complete through M3.4 on `main`.** Central compiler ownership, test/game-binary
> parity, fail-closed isolation, and clang-cl/UBSan proofs are green. Interphase security
> consolidation landed as `a2565ca`. Phase 4 M4.0 now adds portable normalized-path 64-bit asset
> IDs, a disjoint-arena generational SoA registry, handle-bound asset-root reads, bounded direct
> TGA/WAV loaders, and a Vulkan-free renderer upload callback. The sandbox texture is loaded and
> released through that registry; raw loose SPIR-V remains renderer-owned under ADR-0008.
> Run it: `build\tools\sandbox\Debug\sandbox.exe --frames 90 --screenshot out.bmp`
> (the `--frames N` is required — `--screenshot` alone captures only on quit and
> will run forever).
> Headless sim parity: `build\tools\sandbox\Debug\sandbox.exe --sim-self-check`.
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
  the shader build (`glslc`). **Pinned at 1.4.357.0** for this project
  (`winget install KhronosGroup.VulkanSDK`); the code gate is Vulkan API 1.3
  (`dynamicRendering` + `synchronization2`, ADR-0012). Without it the build still
  works but produces the **null render backend** (blank window; CI installs the SDK
  and builds the real backend).

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
exceptions/STL) and run under CTest.

**Canonical local CI gate (any shell)** -- this is the same /WX gate used by the
pre-push hook, and it discovers the Visual Studio environment itself:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/local-ci.ps1 -Configuration Debug
```

The runner always reconfigures the `ci` preset before building and testing. It
writes a non-secret, ignored evidence report at
`out/local-ci-<Configuration>.json` with the revision, tool versions, stage
timings, and outcome; copy its console summary into the PR when relevant.

**Developer-shell alternative** -- for an ad hoc manual run from a shell where
`cl`, `cmake`, and `ninja` are already on `PATH`:

```bat
cmake --preset ci
cmake --build build-ci --config Debug
ctest --test-dir build-ci -C Debug --output-on-failure --no-tests=error
```

Optional second-toolchain gate (installed clang-cl plus Visual Studio build tools):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-clang-cl-determinism.ps1 -RequireCompiler -RequireUbsan
```

The script first proves the installed UBSan runtime with a deliberate overflow, then builds the
caller-storage-backed oracle with clang-cl `/WX` in RelWithDebInfo and requires the same pinned
10,000-tick hash stream. RelWithDebInfo intentionally matches Visual Studio's static release UBSan
runtime ABI; normal MSVC builds retain their existing CRT selection.

CTest covers `mem`, `math`, `containers`, `platform`, `serialize`, `asset_id`, the asset registry,
bounded TGA/WAV parsing and file loads, entity/component/event/system/schedule/sim/presentation
suites, `render`, `render_null`, the `/fp:precise` + `/fp:fast` golden, the 10,000-tick replay proof,
the generated sim-policy/include/source-owner audit, test/game-binary hash-stream parity, the
configure-time sim target contract, adversarial isolation self-tests, the sim/presentation boundary
scans, replay/CLI malformed-input matrices, create-only atomic-write collisions, shader declaration
contracts, guarded fresh-walk cleanup, and renderer capacity corruption fixtures.
The hosted renderer smoke classifier is also tested adversarially: only exact Vulkan device gates
may skip; nonzero exits, Vulkan validation warnings/errors, incomplete runs, and missing/invalid
screenshots fail.

To run the replay tool directly:

```bat
build-ci\tools\replay\Debug\moba_replay.exe record --out match.mbr
build-ci\tools\replay\Debug\moba_replay.exe inspect match.mbr
build-ci\tools\replay\Debug\moba_replay.exe verify match.mbr
```

The memory-safety gate uses the dedicated preset; the ASan runtime is staged beside test, replay,
sandbox, and visualizer executables so CTest does not depend on a developer-shell `PATH`:

```bat
cmake --preset debug-asan
cmake --build build-asan --config Debug
ctest --test-dir build-asan -C Debug --output-on-failure
```

A **pre-push hook** invokes the canonical local CI runner and blocks the push on
red. Activate it once per clone:

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
  assets/      normalized IDs, arena-backed registry/lifetimes, direct TGA/WAV loading
  game/        arena snapshots, interpolation, fixed→float, and DrawItem construction
  render/      raw Vulkan behind a thin renderer seam; GLSL sources in render/shaders/
  sim/         arena-backed deterministic ECS, typed events/systems, schedule + replay codec
  (net arrives in its phase)
cmake/         CompilerWarnings, EngineOptions, CompileShaders helpers
game/ tools/   the game exe, sandbox, replay, and visualization tools
assets/ tests/
docs/          ARCHITECTURE.md, ROADMAP.md, DECISIONS/ (ADRs)
```
