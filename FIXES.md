# FIXES.md — MOBA-proto fresh-checkout sweep (2026-08-09)

Sweep pattern: fresh `git clone` -> walk the documented core path exactly as a
stranger would -> write every break/gap here, above the line = blocks the core
path, below = hygiene. Root causes only. Evidence: every step below was executed
in `%TEMP%\opencode\moba-fresh` (byte-clean clone, `core.autocrlf false`).

## Done-line (core path)

Clone -> `cmake --preset dev` -> `cmake --build build --config Debug` ->
`ctest --test-dir build -C Debug` -> `sandbox --frames 90 --screenshot out.bmp`
-> a 1280x720 BMP of the textured quad appears.

## Verdict

Core path WORKS from a fresh clone: configure green (3.5s), build 37/37,
ctest 8/8 in 0.36s, sandbox renders the quad + saves the BMP with validation on
(GTX 1070, Vulkan 1.3). The engine itself is healthy. What is broken is the
DOCUMENTED path into it.

---

## ABOVE THE LINE — break the stranger's walk

### 1. README: Ninja prerequisite is wrong on this machine (configure fails out of the box)
- Symptom: `cmake --preset dev` dies: "CMake was unable to find a build program
  corresponding to 'Ninja Multi-Config'".
- Root cause: README claims "Visual Studio 2026 ... provides MSVC, plus bundled
  CMake >= 3.28 and Ninja". This VS 18 Enterprise install has NO CMake extension
  component at all (`Common7\IDE\CommonExtensions\Microsoft\CMake` missing) and
  `ninja` is on no PATH, no winget/choco/scoop install. The premise is false here.
- Fix: README must state an explicit prerequisite + one-liner:
  `winget install Ninja-build.Ninja` (verified working; ninja 1.13.2).
- Evidence: install -> configure green in 3.5s.

### 2. README: `sandbox --screenshot out.bmp` HANGS forever
- Symptom: the documented "Run it" command never exits; process must be killed.
- Root cause: `main.cpp` only writes the screenshot when `quit_requested` is set
  (Esc or `--frames N` reached). `--screenshot` alone = window loops forever.
- Fix: document `--frames N --screenshot out.bmp` (verified: 90 frames -> BMP
  written, clean exit, pipeline cache saved).
- Optionally make `--screenshot` imply auto-quit — left to the project owner.

### 3. README: "Run it" path points at a dir the build instructions never create
- Symptom: `build-ci\tools\sandbox\Debug\sandbox.exe` does not exist after
  following the build instructions (which build `build`, not `build-ci`).
- Root cause: copy-paste from the CI preset doc line.
- Fix: point "Run it" at `build\tools\sandbox\Debug\sandbox.exe`.

---

## BELOW THE LINE — hygiene, fix when cheap

### 4. .gitattributes does not pin Vulkan shader extensions -> CRLF checkouts
- Symptom: `git ls-files --eol` shows `*.frag`/`*.vert` check out `w/crlf` while
  the repo's LF discipline is explicit for every other source type.
- Root cause: `.gitattributes` pins `*.glsl` and `*.hlsl` but the repo's actual
  shader files use `.vert`/`.frag`; they fall through to `* text=auto`, which is
  native-CRLF on Windows.
- Impact: cosmetic today (glslc accepts CRLF), but breaks byte-identical
  checkouts and is the same bug family as the GromDesign EOL incident.
- Fix: add `*.frag text eol=lf`, `*.vert text eol=lf` (plus the rest of the
  Vulkan stage set for future shaders).

### 5. Stale local build dirs
- `build/` and `build-ci/` in the worktree were configured against a Ninja that
  no longer exists anywhere on the machine; they cannot incrementally build.
  Reconfigure after installing Ninja (or delete and let CMake regenerate).
- Not a repo bug; local state only.
