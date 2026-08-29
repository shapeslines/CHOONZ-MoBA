# ADR 0012 — Hard minimum spec: Vulkan 1.3 with dynamicRendering + synchronization2

- **Status:** Accepted (2026-06-11)
- **See also:** ROADMAP M2.0/M2.1; [0004](0004-vulkan-loader.md); ARCHITECTURE §5

## Context

M2.0 deliberately left a gate open: either commit to Vulkan 1.3's dynamic rendering
(`vkCmdBeginRendering`, no `VkRenderPass`/`VkFramebuffer` objects) + synchronization2
(`vkQueueSubmit2`/`vkCmdPipelineBarrier2`, explicit per-barrier stage+access), or
carry a classic render-pass fallback behind the seam. M2.1's first pipeline forces
the choice — a triangle needs a real color attachment.

## Decision

**Vulkan 1.3 with the `dynamicRendering` and `synchronization2` features is the hard
minimum spec.** Device selection skips any physical device below it; if none
qualifies, the renderer reports the spec in one clear message and the app continues
renderer-less (same contract as no Vulkan at all). There is **no render-pass /
sync1-only fallback path**, and both features are enabled unconditionally at device
creation.

## Consequences

- One code path for attachments and barriers — render-pass objects, framebuffers, and
  the dual sync vocabulary never enter the codebase. Sync correctness is the #1 risk
  of this phase; not writing every barrier twice is worth real hardware reach.
- Requires drivers from ~2022 onward (1.3 shipped in drivers for GPUs back to ~2014
  hardware on NVIDIA/AMD/Intel). Pre-1.3-driver machines are out of spec — accepted
  for a from-scratch engine that ships years from now.
- If reach ever becomes a problem, the fallback is a *renderer backend* decision
  behind the existing seam (like the null backend), not a per-callsite `#if`.
