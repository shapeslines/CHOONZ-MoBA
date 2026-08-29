# ADR 0005 — The platform seam

- **Status:** Accepted (2026-06-05)
- **See also:** ARCHITECTURE.md §4, §5; [0006](0006-module-layout.md)

## Context

Windows-first, portable later. The engine core must never touch Win32 directly, so
a single platform seam isolates all OS-specific code. The dedicated headless server
has **no window**, so the seam must support a windowless run path. Earlier drafts
gave `platform.h` incompatible definitions across subsystems (file I/O ownership,
page allocation, surface creation).

## Decision

One canonical platform seam under `engine/platform/include/platform/`, with all
Win32-specific code confined to `engine/platform/src/win32_*.cpp`:

- **Windowing / input / timing** — window create + message pump, Raw Input, and
  `platform_time_ticks` (QueryPerformanceCounter). Windowed builds only.
- **Page allocator** — `plat_mem_reserve / commit / release / page_size`. This is
  the *only* thing `eng_core`'s memory layer depends on (it builds all higher
  allocators on top, ADR-driven by ARCHITECTURE §6).
- **File I/O takes caller-supplied storage** — `platform_file_read(vpath, Allocator,
  PlatformFile* out)` — so "the arena owns the bytes" holds; the platform never hides
  allocations. `platform_file_write` publishes through a predictable `.tmp`, but creates that
  temporary with create-only semantics: a pre-existing temporary is never overwritten and a
  collision leaves both it and the destination unchanged.
- **DLL load** — `platform_lib_*` (used by the Vulkan loader and future hot-reload).
- **UDP sockets** — `platform_net.h` is the OS-free seam; `platform_net_win32.cpp`
  is the Winsock2 impl (see [0006] for the headless-test split).
- **Vulkan** — `platform_vk_create_surface` (renderer never sees `HWND`) and
  `platform_vk_get_loader` (see [0004]).
- **Headless run loop** — `platform_run_headless(tick_fn)` drives the accumulator
  from `platform_time_ticks` + sockets with no window/pump, for the dedicated
  server. Both loop owners read `SIM_HZ` ([0001]); neither hardcodes a rate.

Asset-id resolution sits **above** the platform seam (string/handle based, see
[0010]); the asset layer maps ids → paths, then calls platform file I/O.

## Consequences

- A Linux/macOS port reimplements only the platform module; engine core is
  untouched. (Not built until the Windows engine is real — premature portability
  is an over-engineering trap.)
- Two loop owners (windowed game/listen-server; headless dedicated server) share
  one accumulator contract.
