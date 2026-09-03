# CI → fleet self-hosted runner: handoff to the runner manager

**Request:** `.github/runner-ci-request.json` (`runner-request/v1`, id `CHOONZ-MOBA-WIN-TOOLCHAIN-1`,
class `capability-change`, public repo). **Mailbox:** `kind: request` to `desk.runner-manager`, WID
`wid-20260903-choonz-moba-f56aa7`, stream `choonz-moba`. **Prepared:** 2026-09-02.

## Why

GitHub-hosted Actions is billing-locked for the `shapeslines` org (every hosted job dies with zero
steps; check-run annotation "account is locked due to a billing issue"). The `main-gate` ruleset
requires `windows-msvc (Debug | RelWithDebInfo | Release)` with no bypass actors, so nothing can
merge on hosted checks. The fleet already serves other repos from `grom-runner-windows`.

## What the workflow needs from the runner

`ci.yml` is Node-free and installs nothing on a self-hosted guest. It expects, pre-baked:

| Need | Detail | Verified by |
|---|---|---|
| Visual Studio 2022 Build Tools (or Community) | MSVC v143 x64, "Desktop development with C++" | `tools/verify-runner-toolchain.ps1` |
| CMake ≥ 3.28, Ninja | on the `vcvars64` PATH | same |
| LunarG Vulkan SDK 1.4.357.0 | `C:\VulkanSDK\1.4.357.0`, `VULKAN_SDK` set; `glslc` + validation layers | same |
| clang-cl 19.x (optional) | only for the `windows-clang-cl (UBSan capability)` job; it reports `UNAVAILABLE` cleanly without it | `tools/check-clang-cl-determinism.ps1` |
| Disk / time | ~3 GB per config under `build-ci/`; ~2–4 min per config on 2 vCPU; `timeout-minutes: 60` | — |
| GPU | none required; the sandbox step self-classifies `SANDBOX_SMOKE=SKIP` on a GPU-less guest | `tools/classify-sandbox-smoke.ps1` |

Labels expected: `self-hosted`, `Windows`, `X64`.

## How the workflow routes

- `runs-on: ${{ startsWith(vars.CI_RUNNER || '', '[') && fromJSON(vars.CI_RUNNER) || (vars.CI_RUNNER || 'windows-latest') }}`
  on all three jobs (house expression A). Unset variable → hosted `windows-latest` (R1-safe default).
- Concurrency: `${{ github.workflow }}-${{ github.ref }}`, `cancel-in-progress: false` (R2).
- On `runner.environment == 'self-hosted'` the winget step is skipped and
  `tools/verify-runner-toolchain.ps1` runs first; a missing tool fails fast with
  `RUNNER_TOOLCHAIN=MISSING` and the list, so the failure reads as "not provisioned".
- Check names are unchanged, so the ruleset keeps matching.

## Activation (runner manager)

1. Bake the toolchain above into the Windows guest image (capability-change; owner + runner-security
   acceptance because the repo is public).
2. Enroll `shapeslines/CHOONZ-MoBA` in the `grom-selfhosted` runner group.
3. Set the variable **only after** step 1 verifies, so `push: main` runs cannot go red for provisioning:

   ```bash
   gh variable set CI_RUNNER --repo shapeslines/CHOONZ-MoBA --body '["self-hosted","Windows","X64"]'
   ```

4. Acceptance: `gh workflow run ci.yml --repo shapeslines/CHOONZ-MoBA --ref main`; expect the three
   matrix jobs green with `RUNNER_TOOLCHAIN=OK`, CTest 48/48 (or the current count) per config,
   sandbox `SKIP`.
5. Return a `kind: report` on WID `wid-20260903-choonz-moba-f56aa7` with the run URL. Fleet side:
   flip `layout.toml` `ci = "hosted"` → `"self-hosted"` for CHOONZ-MoBA and add `CI-REGISTER` rows.

Rollback: unset `CI_RUNNER`.

## Interim merge path (until activation)

R1 forbids routing `pull_request` events to self-hosted runners, and hosted runs are dead, so:

- **Local green** is the evidence: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/local-ci.ps1 -Configuration Debug`
  (canonical; writes `out/local-ci-Debug.json`), or the fleet instrument
  `python <GromCodebase>/tools/local_green.py --repo . --tier full` with `tools/local-green.manifest.json`.
- **Owner action needed once:** add the *Repository admin* role as a bypass actor on the `main-gate`
  ruleset so a local-green PR can be merged with `gh pr merge --admin`. Today the ruleset has no bypass
  actors, so even admins cannot merge.
- After activation, a trusted `workflow_dispatch` on a PR branch (`gh workflow run ci.yml --ref <branch>`)
  produces check-runs on that head SHA under the required names, which satisfies the ruleset without
  routing `pull_request` events to the runner.

## Pointers

- Fleet law: GromCodebase `docs/fleet/runners/booklet-ci-runner-variable.md`, `RUNBOOK-ci-migration-executor.md`,
  `SERVICE-CATALOG.md`, `runner-request.schema.json`; GromAtlas `DESIGN-CI-ADMISSION-LAW.md` (R1–R4).
- Repo: `.github/workflows/ci.yml`, `.github/runner-ci-request.json`, `tools/verify-runner-toolchain.ps1`,
  `tools/local-green.manifest.json`, `docs/testing-hardware.md` (owner GPU gate).
