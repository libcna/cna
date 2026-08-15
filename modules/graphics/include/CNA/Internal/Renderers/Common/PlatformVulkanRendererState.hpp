// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformVulkanSurface.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <string>

namespace CNA::Internal::Renderers
{
    /** @brief Requires the narrow Vulkan surface service used by Vulkan-family renderers. */
    inline CNA::Platform::IPlatformVulkanSurface& RequirePlatformVulkanSurface(
        CNA::Platform::IPlatformVulkanSurface* service, const char* rendererName)
    {
        if (service == nullptr)
        {
            throw CNA::Platform::PlatformNotSupportedException(
                CNA::Platform::PlatformCapability::VulkanSurface,
                rendererName != nullptr ? rendererName : "Vulkan renderer");
        }
        return *service;
    }

    /**
     * @brief Owns one platform-created Vulkan presentation surface.
     *
     * The Vulkan instance remains renderer-owned and must outlive this object. Keeping this owner
     * as a member makes surface construction transactional: a later renderer-construction failure
     * still returns the surface through the same service that created it.
     */
    class PlatformVulkanSurfaceOwner final
    {
    public:
        PlatformVulkanSurfaceOwner(CNA::Platform::IPlatformVulkanSurface& service,
                                   const CNA::Platform::VulkanInstanceHandle instance,
                                   const CNA::Platform::WindowId window)
            : service_(service), instance_(instance)
        {
            if (instance_ == nullptr)
                throw CNA::Platform::PlatformException(
                    "PlatformVulkanSurfaceOwner", "null Vulkan instance");
            if (window == 0)
                throw CNA::Platform::PlatformException(
                    "PlatformVulkanSurfaceOwner", "missing platform window id");
            surface_ = service_.CreateSurface(instance_, window);
            if (surface_ == 0)
                throw CNA::Platform::PlatformException(
                    "PlatformVulkanSurfaceOwner", "platform returned a null Vulkan surface");
        }

        ~PlatformVulkanSurfaceOwner()
        {
            if (surface_ != 0) service_.DestroySurface(instance_, surface_);
        }

        PlatformVulkanSurfaceOwner(const PlatformVulkanSurfaceOwner&) = delete;
        PlatformVulkanSurfaceOwner& operator=(const PlatformVulkanSurfaceOwner&) = delete;
        PlatformVulkanSurfaceOwner(PlatformVulkanSurfaceOwner&&) = delete;
        PlatformVulkanSurfaceOwner& operator=(PlatformVulkanSurfaceOwner&&) = delete;

        /** @brief Returns the opaque platform surface handle. */
        [[nodiscard]] CNA::Platform::VulkanSurfaceHandle Get() const noexcept { return surface_; }

    private:
        CNA::Platform::IPlatformVulkanSurface& service_;
        CNA::Platform::VulkanInstanceHandle instance_ = nullptr;
        CNA::Platform::VulkanSurfaceHandle surface_ = 0;
    };
}
