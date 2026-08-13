## What this PR does

<!-- One paragraph: the milestone/slate/gap this lands, and the seam it respects. -->

## Verification

- [ ] `/WX` build clean (ci preset, Debug + Release)
- [ ] `ctest --test-dir build-ci -C <Config> --output-on-failure` green (suites affected: …)
- [ ] Determinism oracle untouched (`0x637628abff59c823`) **or** deliberate hash bump reviewed
- [ ] Docs (ARCHITECTURE/ROADMAP/DECISIONS) updated in the same commit where seams change
- [ ] No secrets / machine-local build output committed

## Notes

<!-- Deferrals, follow-ups, or handoff signals for the next session. -->
