---
paths:
  - "engine/rhi/**"
---

# rhi module (Vulkan)

Source: ADR 0006, ADR 0012, ADR 0020, [rhi spec](../../docs/specs/rhi.md).

- Vulkan, volk and VMA are used **only** here, and only in `src/` and `tests/`. Public headers
  expose ezMU handles, value types and opaque raw handles (`void*`, `std::uint64_t`); a Vulkan,
  volk or VMA include in `include/` fails the `architecture.rules` test.
- Include Vulkan through `src/vulkan.hpp` only (`VK_NO_PROTOTYPES`, function pointers from volk).
- Vulkan 1.3 idioms only: dynamic rendering (no `VkRenderPass`/`VkFramebuffer`),
  synchronization2 barriers, timeline semaphores for frame pacing.
- Check every `VkResult`; convert failures to `foundation::Result` with `detail::vkError`.
- All GPU memory goes through VMA. Give every Vulkan object a debug name.
- Destroy resources through deferred destruction; never `vkDeviceWaitIdle` in per-frame paths.
- Threading: a `Device` is externally synchronized (one thread at a time; the render thread
  after startup). Document the thread of every new public function.
- A validation message is a bug. Never suppress one without a documented reason.
- GPU tests: tag `[gpu]`, use `test::makeTestDevice` with a `ValidationGuard` declared first;
  compare images with `foundation::compareImages` against PNGs in `tests/reference/`, writing
  `actual.png`/`diff.png` under `test-output/` on mismatch (ADR 0020).
