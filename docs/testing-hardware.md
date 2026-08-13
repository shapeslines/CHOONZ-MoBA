# Hardware validation protocol (G26)

The renderer's device-scoring path (ADR-0012 minimum spec: Vulkan 1.3 +
`dynamicRendering` + `synchronization2`) has only ever run on NVIDIA discrete GPUs
(GTX 1070, RTX 4070 Ti). This is the owner-run protocol that closes the gap when a
machine with other hardware is available. Each run is scripted + interactive, exactly
like the Phase 2 owner gates (JOURNAL Session 06), and its results are recorded here.

## When to run

- Any time a machine with a **non-NVIDIA discrete GPU** (AMD/Intel dGPU) or an
  **integrated GPU** (Intel iGPU / AMD iGPU / Apple-silicon-adjacent Windows boxes) is
  available — including laptops with hybrid graphics (run the iGPU explicitly).
- After any change to: device scoring, swapchain/format selection, depth-format
  preference, feature gates (ADR-0012), or the memory-type picker.

## Scripted portion (no display interaction)

```bat
cmake --preset ci
cmake --build build-ci --config Debug
ctest --test-dir build-ci -C Debug --output-on-failure
build-ci\tools\sandbox\Debug\sandbox.exe --frames 90 --screenshot out.bmp
```

Pass criteria:

- Build and ctest green (`/WX`, Debug + Release).
- Sandbox exits 0 and writes `out.bmp` (~2.7 MB for 1280x720).
- Log shows `renderer: Vulkan 1.3 up | validation=on | GPU: <name> (<type>)` with
  validation ON, and **zero** `[vk ERROR]` / `[vk WARN]` lines in the log.
- If the device is below the minimum spec the sandbox must print
  `no device meets the minimum spec ... ADR-0012` and exit nonzero (fail-closed, not
  a hang).

## Interactive portion (needs a real display)

Run `sandbox.exe` with validation on and exercise, in order:

1. Resize the window (drag) — no validation errors, clean swapchain recreation.
2. Minimize / restore — render skips while minimized, resumes cleanly.
3. Alt-tab out and back — focus loss/return; no stuck keys.
4. F1 overlay toggle on/off — stats render correctly (incl. `live_device_allocations`).
5. Close the window — clean exit, no leaked-handle warnings.

## Recording results

Append a dated entry below. One row per hardware run.

| Date | Hardware (GPU) | Driver | SDK | Scripted | Interactive | Notes |
|---|---|---|---|---|---|---|
| 2026-08-12 | NVIDIA GeForce GTX 1070 | — | 1.4.357.0 | pass | pass (2026-06/08 gates) | engine dev box |
| 2026-08-12 | NVIDIA RTX 4070 Ti | — | — | pass | pass (S6 gate) | owner validation run |

## Follow-ups implied

- PER_MONITOR_V2 DPI (needs WM_DPICHANGED in the platform — G28's documented next step).
- A dedicated transfer queue on hardware that advertises one (Phase 8 trigger).
