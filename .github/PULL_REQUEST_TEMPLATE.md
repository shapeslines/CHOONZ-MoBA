## What this PR does

<!-- One paragraph: the milestone/slate/gap this lands, and the seam it respects. -->

## Verification

- [ ] `tools/local-ci.ps1 -Configuration <Config>` green for affected configurations; the `out/local-ci-<Configuration>.json` summary is reviewed or copied into this PR
- [ ] `git status --short --branch` is clean and `git rev-parse HEAD` is recorded
- [ ] Determinism oracle untouched (`0xff4e1ca0c779455b`, logic `0xcef8548df2b2a518`) **or** deliberate hash bump reviewed
- [ ] Docs (ARCHITECTURE/ROADMAP/DECISIONS) updated in the same commit where seams change
- [ ] No secrets / machine-local build output committed
- [ ] PR publication used an already-authenticated GitHub path; unavailable `gh` authentication was not fixed by storing or replacing credentials

## Notes

<!-- Deferrals, follow-ups, or handoff signals for the next session. -->
