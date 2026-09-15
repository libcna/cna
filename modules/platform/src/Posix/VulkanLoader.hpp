// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Platform::Posix {

    /** @brief `PFN_vkGetInstanceProcAddr`, restated so that no Vulkan header is needed. */
    using VkGetInstanceProcAddrFn = void* (*)(void* instance, const char* name);

    /**
     * @brief Finds the process's `vkGetInstanceProcAddr`, without the platform linking a Vulkan
     * loader (shared by the X11 and Wayland surface services, plans/plan_wayland.md WAYLAND-0014).
     *
     * `dlsym(RTLD_DEFAULT, ...)` first, and that is the case that matters: an application whose
     * renderer links `libvulkan` -- which is what CNA's own Vulkan renderer does -- has the symbol
     * in the global scope already, and using the caller's own loader is the only way the
     * `VkInstance` it hands over is one a surface-creation entry point can act on.
     *
     * Failing that, the loader is opened here. An application that `dlopen`s Vulkan with
     * `RTLD_LOCAL` (engines that pin a loader version do) leaves nothing in the global scope. There
     * is one system Vulkan loader, so the handle opened here resolves to the same implementation
     * the caller's instance came from. The handle is never closed: function pointers resolved
     * through it stay live for as long as any surface exists.
     *
     * @return The entry point, or null where no Vulkan loader is reachable.
     */
    [[nodiscard]] VkGetInstanceProcAddrFn ResolveVulkanProcAddr();

} // namespace CNA::Platform::Posix
