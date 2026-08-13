// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief An opaque `VkInstance`. Kept untyped so this header never includes Vulkan. */
    using VulkanInstanceHandle = void*;

    /** @brief An opaque `VkSurfaceKHR`. Null means "no surface". */
    using VulkanSurfaceHandle = std::uint64_t;

    /**
     * @brief Creates Vulkan surfaces for windows.
     *
     * Deliberately untyped: `VkInstance` is a pointer handle while `VkSurfaceKHR` is a 64-bit
     * non-dispatchable handle that is *not* pointer-sized on 32-bit targets. Using `void*` for
     * the surface would truncate it there, which is the same class of bug as carrying an X11 XID
     * in a pointer (see NativeWindowHandle). The widths here are chosen to match Vulkan's, so a
     * consumer casts rather than converts.
     *
     * Consumers: the `VULKAN` renderer, plus `DILIGENT`, `LLGL` and `WICKED` when they select a
     * Vulkan device.
     *
     * Gated by the `VulkanSurface` capability.
     */
    class IPlatformVulkanSurface
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformVulkanSurface() = default;

        /**
         * @brief Gets the instance extensions required to present to this platform's windows.
         *
         * Passed to `VkInstanceCreateInfo` before any surface can be created, so this is called
         * before the instance exists and takes no instance argument.
         *
         * @return Extension names, e.g. `VK_KHR_surface` plus the platform-specific one.
         * @throws PlatformNotSupportedException If the platform reports no `VulkanSurface` capability.
         */
        [[nodiscard]] virtual std::vector<std::string> GetInstanceExtensions() const = 0;

        /**
         * @brief Creates a Vulkan surface for a window.
         *
         * The window must have been created with `WindowRenderIntent::Vulkan`.
         *
         * @param instance The `VkInstance` to create the surface against.
         * @param window The stable id of the platform window to present to.
         * @return A non-zero surface handle.
         * @throws PlatformNotSupportedException If the platform reports no `VulkanSurface` capability.
         * @throws PlatformException If surface creation failed.
         */
        [[nodiscard]] virtual VulkanSurfaceHandle CreateSurface(VulkanInstanceHandle instance,
                                                                WindowId window) = 0;

        /**
         * @brief Destroys a surface previously created by this service.
         *
         * @param instance The instance the surface was created against.
         * @param surface The surface to destroy. Passing zero is a no-op.
         */
        virtual void DestroySurface(VulkanInstanceHandle instance, VulkanSurfaceHandle surface) = 0;
    };

} // namespace CNA::Platform
