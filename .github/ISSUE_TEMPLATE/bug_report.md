---
name: Bug report
about: Something in the engine/repo behaves wrong
title: ''
labels: bug
assignees: ''
---

**What happened** — one sentence.

**Expected vs actual** — what should happen, what does.

**Evidence**
- Command(s) run (build preset, ctest suite, sandbox args)
- Log/assert/validation output
- Hash or replay evidence if the sim is involved (golden, divergent tick/field)

**Environment** — GPU/CPU, Vulkan SDK version, config (Debug/Release), backend (vulkan/null).

**Checklist**
- [ ] Reproduced on the `ci` preset (`/WX`) build
- [ ] Sim-related: determinism golden reproduced both configs
