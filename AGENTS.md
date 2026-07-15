# MOBA-proto repository guidance

Global Codex guidance applies. Read `README.md`, `ARCHITECTURE.md`, `docs/ROADMAP.md`, and the
relevant ADR before changing engine or renderer code.

## Scope

- This is a C++ Vulkan/RTS engine prototype with determinism and validation-layer requirements.
- Preserve the CMake preset contract, offline SPIR-V workflow, and deterministic test behavior.
- Treat renderer, asset, and pipeline-cache changes as platform-sensitive; do not commit secrets or
  machine-local build output.

## Validation

- Configure: `cmake --preset dev`
- Build: `cmake --build --preset debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`

Use the `ci` preset when validating warning-as-error behavior.
