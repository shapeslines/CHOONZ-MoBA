# ADR 0008 — Shader build: GLSL → SPIR-V offline via glslc

- **Status:** Accepted (2026-06-05)
- **See also:** ARCHITECTURE.md §3.4; [0004](0004-vulkan-loader.md)

## Context

Raw Vulkan consumes SPIR-V, not shader source. We need a build path for shaders
and a stance on runtime compilation.

## Decision

- Author shaders in **GLSL** (`.vert` / `.frag` / `.comp`); compile **offline to
  `.spv`** with **`glslc`** from the Vulkan SDK (located via `find_package(Vulkan)`).
- A CMake helper, **`add_shader_library()`**, compiles shaders as a build step and
  emits `-MD/-MF` **depfiles** so incremental rebuilds are correct (edit a shader →
  only it recompiles). `.spv` outputs land in a known build directory and are loaded
  at runtime as opaque bytes. Every declaration must resolve to a real file under the source root,
  and output basenames are unique under Windows case-folding; missing, outside-root, directory, or
  duplicate inputs fail configure before a build rule is emitted.
- **No runtime GLSL→SPIR-V compilation, ever.** Shader errors are build errors.

## Consequences

- Shader bugs are caught at build time, not at first draw.
- Dev hot-reload (Phase 4) re-runs `glslc` on file change and reloads the `.spv`.
- A future shading-language change is contained to the cooker/build, not the
  runtime.
