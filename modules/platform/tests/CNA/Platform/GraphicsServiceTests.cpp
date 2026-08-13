// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <type_traits>

namespace {

using namespace CNA::Platform;

TEST(GlContextTests, DefaultDescriptionRequestsAUsableModernCoreContext)
{
    // Defaults must be something a renderer can actually run on; 3.3 core with depth+stencil is
    // the baseline EasyGL's desktop profile targets.
    const GlContextDescription description;
    EXPECT_EQ(description.majorVersion, 3);
    EXPECT_EQ(description.minorVersion, 3);
    EXPECT_EQ(description.profile, GlProfile::Core);
    EXPECT_GT(description.depthBits, 0);
    EXPECT_GT(description.stencilBits, 0);
    EXPECT_TRUE(description.doubleBuffer);
}

TEST(GlContextTests, MultisamplingIsOffByDefault)
{
    // Requesting MSAA a caller did not ask for changes both memory use and rasterisation.
    const GlContextDescription description;
    EXPECT_EQ(description.multisampleBuffers, 0);
    EXPECT_EQ(description.multisampleSamples, 0);
}

TEST(GlContextTests, ProfileNamesAreDistinctAndStable)
{
    std::set<std::string> names;
    for (const GlProfile profile : {GlProfile::Core, GlProfile::Compatibility, GlProfile::Es})
    {
        EXPECT_FALSE(ToString(profile).empty());
        EXPECT_EQ(&ToString(profile), &ToString(profile));
        names.insert(ToString(profile));
    }
    EXPECT_EQ(names.size(), 3u);
}

TEST(GlContextTests, InterfaceHasAVirtualDestructor)
{
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformGlContext>);
}

TEST(VulkanSurfaceTests, SurfaceHandleIs64BitRegardlessOfPointerWidth)
{
    // VkSurfaceKHR is a non-dispatchable 64-bit handle and is NOT pointer-sized on 32-bit
    // targets. Declaring it void* would truncate it there -- the same class of bug as carrying
    // an X11 XID in a pointer.
    EXPECT_EQ(sizeof(VulkanSurfaceHandle), 8u);
    EXPECT_TRUE(std::is_unsigned_v<VulkanSurfaceHandle>);
}

TEST(VulkanSurfaceTests, InstanceHandleIsPointerSized)
{
    // VkInstance is a dispatchable handle -- a real pointer.
    EXPECT_EQ(sizeof(VulkanInstanceHandle), sizeof(void*));
}

TEST(VulkanSurfaceTests, InterfaceHasAVirtualDestructor)
{
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformVulkanSurface>);
}

TEST(VulkanSurfaceTests, WindowIdentityIsAStablePlatformId)
{
    using CreateSurfaceSignature = VulkanSurfaceHandle (IPlatformVulkanSurface::*)(
        VulkanInstanceHandle, WindowId);
    EXPECT_TRUE((std::is_same_v<decltype(&IPlatformVulkanSurface::CreateSurface),
                                CreateSurfaceSignature>));
}

} // namespace
