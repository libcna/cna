// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Common/PlatformVulkanRendererState.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    using namespace CNA::Internal::Renderers;
    using namespace CNA::Platform;

    class FakeVulkanSurface final : public IPlatformVulkanSurface
    {
    public:
        [[nodiscard]] std::vector<std::string> GetInstanceExtensions() const override
        {
            return {"VK_KHR_surface", "VK_FAKE_window_surface"};
        }

        [[nodiscard]] VulkanSurfaceHandle CreateSurface(
            const VulkanInstanceHandle instance, const WindowId window) override
        {
            trace.emplace_back("create");
            createdInstance = instance;
            createdWindow = window;
            return returnedSurface;
        }

        void DestroySurface(const VulkanInstanceHandle instance,
                            const VulkanSurfaceHandle surface) override
        {
            trace.emplace_back("destroy");
            destroyedInstance = instance;
            destroyedSurface = surface;
        }

        VulkanSurfaceHandle returnedSurface = 0x123456789abcdef0ULL;
        VulkanInstanceHandle createdInstance = nullptr;
        VulkanInstanceHandle destroyedInstance = nullptr;
        WindowId createdWindow = 0;
        VulkanSurfaceHandle destroyedSurface = 0;
        std::vector<std::string> trace;
    };

    TEST(PlatformVulkanSurfaceOwnerTests, MissingServiceIsAPlatformCapabilityRefusal)
    {
        try
        {
            (void)RequirePlatformVulkanSurface(nullptr, "TEST_VULKAN");
            FAIL() << "missing service was accepted";
        }
        catch (const PlatformNotSupportedException& error)
        {
            EXPECT_EQ(error.GetCapability(), PlatformCapability::VulkanSurface);
        }
    }

    TEST(PlatformVulkanSurfaceOwnerTests, OwnsSurfaceForTheStableWindowId)
    {
        FakeVulkanSurface service;
        auto* instance = reinterpret_cast<VulkanInstanceHandle>(0x1111);
        {
            PlatformVulkanSurfaceOwner owner(service, instance, 77);
            EXPECT_EQ(owner.Get(), service.returnedSurface);
            EXPECT_EQ(service.createdInstance, instance);
            EXPECT_EQ(service.createdWindow, 77u);
        }
        EXPECT_EQ(service.destroyedInstance, instance);
        EXPECT_EQ(service.destroyedSurface, service.returnedSurface);
        EXPECT_EQ(service.trace, (std::vector<std::string>{"create", "destroy"}));
    }

    TEST(PlatformVulkanSurfaceOwnerTests, RefusesNullHandlesWithoutInventingOwnership)
    {
        FakeVulkanSurface service;
        EXPECT_THROW((PlatformVulkanSurfaceOwner(service, nullptr, 1)), PlatformException);
        EXPECT_THROW((PlatformVulkanSurfaceOwner(
            service, reinterpret_cast<VulkanInstanceHandle>(0x1111), 0)), PlatformException);
        service.returnedSurface = 0;
        EXPECT_THROW((PlatformVulkanSurfaceOwner(
            service, reinterpret_cast<VulkanInstanceHandle>(0x1111), 1)), PlatformException);
        EXPECT_EQ(service.trace, (std::vector<std::string>{"create"}));
    }
}
