# ADR 0004 — Vulkan loader: hand-loaded, own dispatch table

- **Status:** Accepted (2026-06-05)
- **See also:** ARCHITECTURE.md §5 (The Vulkan Boundary)

## Context

We use raw Vulkan in a pure-from-scratch engine. Either link the official Vulkan
loader (`vulkan-1.lib`) and call `vk*` directly, or hand-load `vulkan-1.dll` and
route every call through our own dispatch table. The official loader is the
sanctioned API (not a third-party helper), but hand-loading is the most literal
"own every byte" path — and is the user's explicit choice.

## Decision

**Hand-load `vulkan-1.dll` and own a two-tier dispatch table.**

1. The platform layer loads `vulkan-1.dll` from System32 first, then from an explicit canonical,
   existing, drive-absolute `VULKAN_SDK\Bin` fallback with restricted dependency search. It never
   uses the ambient DLL search order. The platform exposes
   `platform_vk_get_loader()` → `vkGetInstanceProcAddr` (resolved via
   `GetProcAddress`).
2. The renderer builds a dispatch table in tiers:
   - **Global** functions (`vkCreateInstance`, `vkEnumerateInstance*`) via
     `vkGetInstanceProcAddr(NULL, name)` — the NULL-instance rule (mandatory, easy
     to miss).
   - **Instance** functions via `vkGetInstanceProcAddr(instance, name)` after
     instance creation.
   - **Device** functions re-resolved via `vkGetDeviceProcAddr(device, name)` after
     device creation, to skip the loader trampoline's per-call indirection.
- **Build links Vulkan *include dirs* only** (`find_package(Vulkan)` for headers,
  validation layers, and `glslc`) — **never `vulkan-1.lib`.**
- **Every Vulkan call goes through the table** (`vk.CreateSwapchainKHR(...)`, not a
  presumed-linked `vkCreateSwapchainKHR`). Each pointer is null-checked at
  table-build time → `platform_fatal` on failure.
- Surface creation stays in the platform layer (`platform_vk_create_surface`); the
  renderer never sees the `HWND`.

## Consequences

- Matches the own-the-metal ethos; zero link-time Vulkan dependency; the per-device
  tier avoids the loader trampoline on hot device calls.
- The loader is ~the content of `volk` (more than a one-liner) — honest scope.
- A mis-resolved pointer fails at table-build with a clear fatal, not an opaque
  crash mid-frame.
- Loader replacement through the current working directory or `PATH` is rejected by construction;
  the explicit SDK fallback remains usable for development installs.
