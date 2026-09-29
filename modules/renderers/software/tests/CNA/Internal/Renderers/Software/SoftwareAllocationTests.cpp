// SPDX-License-Identifier: MS-PL
//
// The CPU allocation contract of Software's framebuffers and textures: every layout is planned,
// overflow-checked and held to one per-resource byte budget before a byte is committed, and a
// request that fails the plan is refused without disturbing what already exists.
//
// The planners also get a genuine 32-bit run (tools/media/arithmetic32bit); these are the
// host-width layouts and the renderer entry points that consume them.

#if defined(CNA_RENDERER_SOFTWARE)

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareFramebufferAllocation.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareTextureAllocation.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace CNA::Internal::Renderers;
    using namespace CNA::Internal::Renderers::Software;
    using CNA::Internal::Graphics::ImageData;

    /// Runs @p callback and returns the message of the @p ExceptionT it throws, or a marker naming
    /// what happened instead, so a failed expectation says which exception (if any) arrived.
    template<typename ExceptionT, typename Callback>
    std::string MessageOf(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const ExceptionT& error)
        {
            return error.what();
        }
        catch (const std::exception& error)
        {
            return std::string("<unexpected exception> ") + error.what();
        }
        return "<no exception>";
    }

    ImageData SolidImage(int width, int height, std::size_t bytes, std::uint8_t value = 0)
    {
        ImageData image;
        image.width = width;
        image.height = height;
        image.pixels.assign(bytes, value);
        return image;
    }
}

TEST(SoftwareFramebufferAllocation, ASingleSampleSurfaceWithoutDepthCommitsFiveBytesPerPixel)
{
    const SoftwareFramebufferAllocationLayout layout =
        PlanSoftwareFramebufferAllocation({800, 600, false, true, 0});
    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.pixelCount, 480000u);
    EXPECT_EQ(layout.colorBytes, 1920000u);
    EXPECT_EQ(layout.depthBytes, 0u);
    EXPECT_EQ(layout.stencilBytes, 480000u);
    EXPECT_EQ(layout.multiSampleBytes, 0u);
    EXPECT_EQ(layout.multiSampleDepthBytes, 0u);
    EXPECT_EQ(layout.multiSampleStencilBytes, 0u);
    EXPECT_EQ(layout.totalBytes, 2400000u);
}

TEST(SoftwareFramebufferAllocation, AFourSampleSurfaceWithoutDepthCommitsTwentyFiveBytesPerPixel)
{
    const SoftwareFramebufferAllocationLayout layout =
        PlanSoftwareFramebufferAllocation({800, 600, false, true, 4});
    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.depthBytes, 0u);
    EXPECT_EQ(layout.multiSampleBytes, 7680000u);
    EXPECT_EQ(layout.multiSampleDepthBytes, 0u);
    EXPECT_EQ(layout.multiSampleStencilBytes, 1920000u);
    EXPECT_EQ(layout.totalBytes, 12000000u);
}

TEST(SoftwareFramebufferAllocation, AFullFourSampleSurfaceAccountsForEveryPerSamplePlane)
{
    const SoftwareFramebufferAllocationLayout layout =
        PlanSoftwareFramebufferAllocation({800, 600, true, true, 4});
    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.depthBytes, 1920000u);
    EXPECT_EQ(layout.multiSampleDepthBytes, 7680000u);
    EXPECT_EQ(layout.multiSampleStencilBytes, 1920000u);
    EXPECT_EQ(layout.totalBytes, 21600000u);
}

TEST(SoftwareFramebufferAllocation, MipStorageCountsEveryGeneratedLowerLevel)
{
    const SoftwareFramebufferAllocationLayout layout =
        PlanSoftwareFramebufferAllocation({800, 600, false, true, 0, true});
    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.mipBytes, 639756u);
    EXPECT_EQ(layout.totalBytes, 3039756u);
}

TEST(SoftwareFramebufferAllocation, NonPositiveDimensionsAreRefusedBeforeUnsignedConversion)
{
    EXPECT_EQ(PlanSoftwareFramebufferAllocation({0, 1, false, true, 0}).error,
              SoftwareFramebufferAllocationError::NonPositiveDimension);
    EXPECT_EQ(PlanSoftwareFramebufferAllocation({1, -1, false, true, 0}).error,
              SoftwareFramebufferAllocationError::NonPositiveDimension);
}

TEST(SoftwareFramebufferAllocation, TheSingleAxisCeilingIsEnforced)
{
    EXPECT_EQ(PlanSoftwareFramebufferAllocation(
                  {SoftwareFramebufferMaxDimension + 1, 1, false, true, 0}).error,
              SoftwareFramebufferAllocationError::DimensionLimitExceeded);
}

TEST(SoftwareFramebufferAllocation, OnlyTheImplementedZeroAndFourSampleCountsAreAccepted)
{
    EXPECT_EQ(PlanSoftwareFramebufferAllocation({1, 1, false, true, 2}).error,
              SoftwareFramebufferAllocationError::UnsupportedSampleCount);
}

TEST(SoftwareFramebufferAllocation, AMultiplicationSafeSurfaceAboveTheBudgetIsRefused)
{
    EXPECT_EQ(PlanSoftwareFramebufferAllocation({11000, 11000, false, true, 0}).error,
              SoftwareFramebufferAllocationError::ByteBudgetExceeded);
}

TEST(SoftwareFramebufferAllocation, MipStorageParticipatesInTheSameBudget)
{
    EXPECT_TRUE(PlanSoftwareFramebufferAllocation({10000, 10000, false, true, 0}).IsValid());
    EXPECT_EQ(PlanSoftwareFramebufferAllocation({10000, 10000, false, true, 0, true}).error,
              SoftwareFramebufferAllocationError::ByteBudgetExceeded);
}

TEST(SoftwareFramebufferAllocation, TheLargestFourSampleSurfaceFailsSafelyAtAnyWordSize)
{
    const SoftwareFramebufferAllocationError error = PlanSoftwareFramebufferAllocation(
        {SoftwareFramebufferMaxDimension, SoftwareFramebufferMaxDimension, false, true, 4}).error;
    EXPECT_TRUE(error == SoftwareFramebufferAllocationError::ArithmeticOverflow ||
                error == SoftwareFramebufferAllocationError::ByteBudgetExceeded);
    EXPECT_LE(SoftwareFramebufferMaxBytes, std::numeric_limits<std::uint32_t>::max());
}

TEST(SoftwareTextureAllocation, ASingleLevelCommitsFourBytesPerPixel)
{
    const SoftwareTextureAllocationLayout layout = PlanSoftwareTextureAllocation({800, 600, 1});
    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.pixelCount, 480000u);
    EXPECT_EQ(layout.baseColorBytes, 1920000u);
    EXPECT_EQ(layout.mipBytes, 0u);
    EXPECT_EQ(layout.totalBytes, 1920000u);
}

TEST(SoftwareTextureAllocation, ADeclaredMipChainCountsEveryLowerLevel)
{
    const SoftwareTextureAllocationLayout layout = PlanSoftwareTextureAllocation({4, 4, 3});
    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.baseColorBytes, 64u);
    EXPECT_EQ(layout.mipBytes, 20u);
    EXPECT_EQ(layout.totalBytes, 84u);
}

TEST(SoftwareTextureAllocation, NonPositiveDimensionsAreRefusedBeforeUnsignedConversion)
{
    EXPECT_EQ(PlanSoftwareTextureAllocation({0, 1, 1}).error,
              SoftwareTextureAllocationError::NonPositiveDimension);
    EXPECT_EQ(PlanSoftwareTextureAllocation({1, -1, 1}).error,
              SoftwareTextureAllocationError::NonPositiveDimension);
}

TEST(SoftwareTextureAllocation, TheSingleAxisCeilingIsEnforced)
{
    EXPECT_EQ(PlanSoftwareTextureAllocation({SoftwareFramebufferMaxDimension + 1, 1, 1}).error,
              SoftwareTextureAllocationError::DimensionLimitExceeded);
}

TEST(SoftwareTextureAllocation, AMultiplicationSafeLevelAboveTheBudgetIsRefused)
{
    EXPECT_EQ(PlanSoftwareTextureAllocation({11586, 11586, 1}).error,
              SoftwareTextureAllocationError::ByteBudgetExceeded);
}

TEST(SoftwareTextureAllocation, DeclaredMipStorageParticipatesInTheSameBudget)
{
    EXPECT_TRUE(PlanSoftwareTextureAllocation({11500, 11500, 1}).IsValid());
    EXPECT_EQ(PlanSoftwareTextureAllocation({11500, 11500, 2}).error,
              SoftwareTextureAllocationError::ByteBudgetExceeded);
}

TEST(SoftwareRendererAllocation, RenderTargetsFailClearlyBeforeAnythingIsAllocated)
{
    SoftwareRenderer renderer(8, 6);
    EXPECT_NE(MessageOf<System::ArgumentOutOfRangeException>(
                  [&] { (void)renderer.CreateRenderTarget2D(0, 5, 0, true, false, 0); })
                  .find("dimensions must be positive"),
              std::string::npos);
    // A target without a depth format is colour only, four bytes a pixel: 11586 squared is the
    // first square size whose colour plane alone is over the budget.
    EXPECT_NE(MessageOf<System::ArgumentOutOfRangeException>(
                  [&] { (void)renderer.CreateRenderTarget2D(11586, 11586, 0, true, false, 0); })
                  .find("framebuffer byte budget exceeded"),
              std::string::npos);
}

TEST(SoftwareRendererAllocation, ARejectedResizeLeavesTheBackbufferAsItWas)
{
    SoftwareRenderer renderer(8, 6);
    renderer.Clear(0.25f, 0.5f, 0.75f, 1.0f);
    std::array<std::uint8_t, 4> before{};
    renderer.ReadBackbuffer(0, 0, 1, 1, before.data());

    EXPECT_NE(MessageOf<System::ArgumentOutOfRangeException>(
                  [&] { renderer.SetVirtualResolution(11000, 11000); })
                  .find("framebuffer byte budget exceeded"),
              std::string::npos);
    EXPECT_NE(MessageOf<System::ArgumentOutOfRangeException>(
                  [&] { renderer.SetVirtualResolution(-1, 6); })
                  .find("must be positive"),
              std::string::npos);

    int width = 0;
    int height = 0;
    renderer.GetViewportSize(width, height);
    EXPECT_EQ(width, 8);
    EXPECT_EQ(height, 6);
    std::array<std::uint8_t, 4> after{};
    renderer.ReadBackbuffer(0, 0, 1, 1, after.data());
    EXPECT_EQ(after, before);
}

TEST(SoftwareRendererAllocation, ATextureUploadHonoursTheCallersRowPitch)
{
    SoftwareRenderer renderer(8, 6);
    std::unique_ptr<ITextureRenderer> texture =
        renderer.CreateTexture(SolidImage(3, 2, 3u * 2u * 4u));
    auto* software = dynamic_cast<SoftwareTextureRenderer*>(texture.get());
    ASSERT_NE(software, nullptr);

    // An odd width and deliberately asymmetric channels, so a tight-row assumption, a padding
    // copy, a channel swap or a row overlap cannot pass by accident.
    const std::array<std::uint8_t, 32> padded{
        1, 17, 33, 49,  65, 81, 97, 113,  129, 145, 161, 177,
        0xDE, 0xAD, 0xBE, 0xEF,
        2, 19, 37, 53,  71, 89, 107, 127,  149, 167, 191, 211,
        0xCA, 0xFE, 0xBA, 0xBE,
    };
    const std::vector<std::uint8_t> expected{
        1, 17, 33, 49,  65, 81, 97, 113,  129, 145, 161, 177,
        2, 19, 37, 53,  71, 89, 107, 127,  149, 167, 191, 211,
    };
    texture->UpdatePixels(padded.data(), 16);
    EXPECT_EQ(software->ColorPixels(), expected);

    EXPECT_NE(MessageOf<System::ArgumentOutOfRangeException>(
                  [&] { texture->UpdatePixels(padded.data(), 11); })
                  .find("at least 12"),
              std::string::npos);
    EXPECT_EQ(software->ColorPixels(), expected)
        << "a refused short row pitch must leave the previous pixels in place";
}

TEST(SoftwareRendererAllocation, ASourceBufferIsHeldToExactlyItsLevel)
{
    SoftwareRenderer renderer(8, 6);

    std::unique_ptr<ITextureRenderer> truncated = renderer.CreateTexture(SolidImage(2, 2, 64, 7));
    auto* software = dynamic_cast<SoftwareTextureRenderer*>(truncated.get());
    ASSERT_NE(software, nullptr);
    EXPECT_EQ(software->ColorPixels().size(), 16u);

    EXPECT_NE(MessageOf<System::ArgumentException>(
                  [&] { (void)renderer.CreateTexture(SolidImage(4, 4, 8)); })
                  .find("requires at least"),
              std::string::npos);
}

TEST(SoftwareRendererAllocation, TexturesOutsideTheLayoutContractAreRefused)
{
    SoftwareRenderer renderer(8, 6);
    // Refused on the layout, before the (deliberately short) pixel buffer is even measured.
    EXPECT_NE(MessageOf<System::ArgumentOutOfRangeException>(
                  [&] { (void)renderer.CreateTexture(SolidImage(11586, 11586, 4)); })
                  .find("texture byte budget exceeded"),
              std::string::npos);
    EXPECT_NE(MessageOf<System::ArgumentOutOfRangeException>(
                  [&] { (void)renderer.CreateTexture(SolidImage(0, 4, 16)); })
                  .find("dimensions must be positive"),
              std::string::npos);
}

#endif // CNA_RENDERER_SOFTWARE
