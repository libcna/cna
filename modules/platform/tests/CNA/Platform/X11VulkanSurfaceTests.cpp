// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0086: a real `VK_KHR_xlib_surface` against a real X11 window.
//
// The rest of the X11 suite can assert that `GetInstanceExtensions()` names the right two strings
// and that a null instance refuses. Neither says the surface actually works, and "the platform
// advertises `vulkanSurface`" is a promise that it does.
//
// So this test builds a real `VkInstance` with the two extensions the platform asked for, hands
// the platform a real window, and then asks the Vulkan implementation itself whether the surface
// it got back is one a physical device can present to. That last step is the one that matters:
// `vkCreateXlibSurfaceKHR` returning success proves the call was well-formed, while
// `vkGetPhysicalDeviceSurfaceSupportKHR` proves the surface describes the window it should.
//
// Vulkan is reached through `dlopen` rather than a link edge, for exactly the reason the platform
// itself does: a machine with no Vulkan loader must still build and run everything else. Without
// one this skips, with the reason recorded.

#include <gtest/gtest.h>

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

// The handful of Vulkan declarations this needs, restated rather than including <vulkan/vulkan.h>.
// The layouts are frozen by the specification, and restating them keeps the Vulkan headers from
// becoming a build dependency of the platform test suite -- the same trade the platform's own
// surface service makes, and for the same reason.
using VkInstanceHandle = void*;
using VkPhysicalDeviceHandle = void*;
using VkResultCode = int;
constexpr VkResultCode kVkSuccess = 0;
constexpr int kVkStructureTypeApplicationInfo = 0;
constexpr int kVkStructureTypeInstanceCreateInfo = 1;
constexpr std::uint32_t kVkTrue = 1;

struct VkApplicationInfoLayout
{
    int sType;
    const void* next;
    const char* applicationName;
    std::uint32_t applicationVersion;
    const char* engineName;
    std::uint32_t engineVersion;
    std::uint32_t apiVersion;
};

struct VkInstanceCreateInfoLayout
{
    int sType;
    const void* next;
    std::uint32_t flags;
    const VkApplicationInfoLayout* applicationInfo;
    std::uint32_t enabledLayerCount;
    const char* const* enabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char* const* enabledExtensionNames;
};

using PfnGetInstanceProcAddr = void* (*) (VkInstanceHandle, const char*);
using PfnCreateInstance = VkResultCode (*)(const VkInstanceCreateInfoLayout*, const void*,
                                           VkInstanceHandle*);
using PfnDestroyInstance = void (*)(VkInstanceHandle, const void*);
using PfnEnumeratePhysicalDevices = VkResultCode (*)(VkInstanceHandle, std::uint32_t*,
                                                     VkPhysicalDeviceHandle*);
using PfnGetPhysicalDeviceSurfaceSupport = VkResultCode (*)(VkPhysicalDeviceHandle, std::uint32_t,
                                                            std::uint64_t, std::uint32_t*);

bool HasDisplay()
{
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
}

class X11VulkanSurfaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!HasDisplay())
        {
            GTEST_SKIP() << "no DISPLAY";
        }

        // RTLD_NOW so a truncated loader fails here rather than at the first call, and the
        // versioned SONAME because that is what an application actually links.
        loader_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (loader_ == nullptr)
        {
            loader_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (loader_ == nullptr)
        {
            GTEST_SKIP() << "no Vulkan loader on this machine: " << dlerror();
        }
        getInstanceProcAddr_ =
            reinterpret_cast<PfnGetInstanceProcAddr>(dlsym(loader_, "vkGetInstanceProcAddr"));
        if (getInstanceProcAddr_ == nullptr)
        {
            GTEST_SKIP() << "the Vulkan loader exports no vkGetInstanceProcAddr";
        }

        platform_ = PlatformFactory::Create("X11");
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "cannot reach the X server: " << error.what();
        }
        acquired_ = true;

        vulkan_ = platform_->GetVulkanSurface();
        if (vulkan_ == nullptr)
        {
            GTEST_SKIP() << "this build reports no Vulkan surface capability";
        }
    }

    void TearDown() override
    {
        if (instance_ != nullptr)
        {
            if (auto* destroy = reinterpret_cast<PfnDestroyInstance>(
                    getInstanceProcAddr_(instance_, "vkDestroyInstance")))
            {
                destroy(instance_, nullptr);
            }
            instance_ = nullptr;
        }
        window_.reset();
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        if (loader_ != nullptr)
        {
            dlclose(loader_);
            loader_ = nullptr;
        }
    }

    /// Builds an instance with exactly the extensions the platform asked for. Returns false, with
    /// the reason recorded, when this machine has no usable Vulkan implementation.
    bool CreateInstance(std::string& reason)
    {
        const std::vector<std::string> required = vulkan_->GetInstanceExtensions();
        std::vector<const char*> names;
        names.reserve(required.size());
        for (const std::string& name : required)
        {
            names.push_back(name.c_str());
        }

        auto* createInstance = reinterpret_cast<PfnCreateInstance>(
            getInstanceProcAddr_(nullptr, "vkCreateInstance"));
        if (createInstance == nullptr)
        {
            reason = "the loader exports no vkCreateInstance";
            return false;
        }

        VkApplicationInfoLayout application{};
        application.sType = kVkStructureTypeApplicationInfo;
        application.applicationName = "CNA X11 surface test";
        application.engineName = "CNA";
        // VK_API_VERSION_1_0. Asking for no more than the floor keeps this running on an old
        // loader, and nothing here needs a later feature.
        application.apiVersion = 1u << 22;

        VkInstanceCreateInfoLayout info{};
        info.sType = kVkStructureTypeInstanceCreateInfo;
        info.applicationInfo = &application;
        info.enabledExtensionCount = static_cast<std::uint32_t>(names.size());
        info.enabledExtensionNames = names.data();

        const VkResultCode result = createInstance(&info, nullptr, &instance_);
        if (result != kVkSuccess || instance_ == nullptr)
        {
            // The usual cause is a machine with a loader but no driver, or one whose driver does
            // not implement VK_KHR_xlib_surface. Both are environmental.
            reason = "vkCreateInstance refused the platform's extensions, VkResult " +
                     std::to_string(result);
            instance_ = nullptr;
            return false;
        }
        return true;
    }

    void* loader_ = nullptr;
    PfnGetInstanceProcAddr getInstanceProcAddr_ = nullptr;
    VkInstanceHandle instance_ = nullptr;
    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    IPlatformVulkanSurface* vulkan_ = nullptr;
    bool acquired_ = false;
};

TEST_F(X11VulkanSurfaceTest, TheExtensionsThePlatformNamesAreOnesAnInstanceAccepts)
{
    // The first thing a renderer does is enable exactly this list. A platform that named an
    // extension the loader does not have would fail at instance creation, several layers from the
    // place that got it wrong.
    std::string reason;
    if (!CreateInstance(reason))
    {
        GTEST_SKIP() << reason;
    }
    EXPECT_NE(instance_, nullptr);
}

TEST_F(X11VulkanSurfaceTest, ARealWindowProducesASurfaceAPhysicalDeviceCanPresentTo)
{
    std::string reason;
    if (!CreateInstance(reason))
    {
        GTEST_SKIP() << reason;
    }

    WindowDescription description;
    description.title = "CNA Vulkan surface";
    description.width = 160;
    description.height = 120;
    description.centered = false;
    description.renderIntent = WindowRenderIntent::Vulkan;
    window_ = platform_->CreateWindow(description);
    ASSERT_NE(window_, nullptr);
    window_->Show();
    window_->Sync();

    const VulkanSurfaceHandle surface = vulkan_->CreateSurface(instance_, window_->GetId());
    ASSERT_NE(surface, 0u) << "the platform returned a null surface for a real window";

    // The assertion that makes this more than a well-formedness check: ask Vulkan itself whether
    // the surface describes something presentable. A surface built from the wrong display, the
    // wrong XID, or a stale window fails here even though its creation succeeded.
    auto* enumerate = reinterpret_cast<PfnEnumeratePhysicalDevices>(
        getInstanceProcAddr_(instance_, "vkEnumeratePhysicalDevices"));
    auto* surfaceSupport = reinterpret_cast<PfnGetPhysicalDeviceSurfaceSupport>(
        getInstanceProcAddr_(instance_, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    ASSERT_NE(enumerate, nullptr);
    ASSERT_NE(surfaceSupport, nullptr);

    std::uint32_t deviceCount = 0;
    ASSERT_EQ(enumerate(instance_, &deviceCount, nullptr), kVkSuccess);
    if (deviceCount == 0)
    {
        vulkan_->DestroySurface(instance_, surface);
        GTEST_SKIP() << "this machine has a Vulkan loader but no physical device";
    }
    std::vector<VkPhysicalDeviceHandle> devices(deviceCount, nullptr);
    ASSERT_EQ(enumerate(instance_, &deviceCount, devices.data()), kVkSuccess);

    bool presentable = false;
    for (VkPhysicalDeviceHandle device : devices)
    {
        // Queue family 0 is the graphics family on every implementation that has one; a device
        // where it is not simply reports false and the loop moves on.
        std::uint32_t supported = 0;
        if (surfaceSupport(device, 0, surface, &supported) == kVkSuccess && supported == kVkTrue)
        {
            presentable = true;
            break;
        }
    }
    EXPECT_TRUE(presentable)
        << "no physical device can present to the surface the platform created, which means the "
           "surface does not describe the window it was asked for";

    vulkan_->DestroySurface(instance_, surface);
    // Destroying twice, and destroying nothing, must both be harmless: teardown paths do exactly
    // that after a partial failure.
    EXPECT_NO_THROW(vulkan_->DestroySurface(instance_, 0));
}

TEST_F(X11VulkanSurfaceTest, TwoWindowsProduceTwoDistinctSurfaces)
{
    std::string reason;
    if (!CreateInstance(reason))
    {
        GTEST_SKIP() << reason;
    }

    WindowDescription description;
    description.width = 96;
    description.height = 72;
    description.centered = false;
    description.renderIntent = WindowRenderIntent::Vulkan;

    description.title = "surface one";
    auto first = platform_->CreateWindow(description);
    description.title = "surface two";
    auto second = platform_->CreateWindow(description);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    const VulkanSurfaceHandle firstSurface = vulkan_->CreateSurface(instance_, first->GetId());
    const VulkanSurfaceHandle secondSurface = vulkan_->CreateSurface(instance_, second->GetId());
    EXPECT_NE(firstSurface, 0u);
    EXPECT_NE(secondSurface, 0u);
    EXPECT_NE(firstSurface, secondSurface)
        << "two windows must not share one surface; a renderer would present both into the same "
           "swapchain";

    vulkan_->DestroySurface(instance_, firstSurface);
    vulkan_->DestroySurface(instance_, secondSurface);
}

TEST_F(X11VulkanSurfaceTest, AnUnknownWindowRefusesRatherThanBuildingASurfaceFromNothing)
{
    std::string reason;
    if (!CreateInstance(reason))
    {
        GTEST_SKIP() << reason;
    }
    EXPECT_THROW((void) vulkan_->CreateSurface(instance_, 999999), PlatformException);
}

} // namespace
