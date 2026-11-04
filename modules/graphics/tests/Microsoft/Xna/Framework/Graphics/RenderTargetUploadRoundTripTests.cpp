// SPDX-License-Identifier: MS-PL
//
// A RenderTarget2D is a Texture2D, so SetData on one is legal XNA: the texels must land in the
// surface a later GetData reads, or the renderer must refuse with NotSupportedException. The
// failure these tests exist to catch is the silent third outcome -- ITextureRenderer declares
// UpdatePixels/UpdatePixelsLevel with empty default bodies, so a render-target renderer that does
// not override them accepts the upload, drops it, and answers the next readback from the cleared
// surface. Every renderer that creates a real RenderTarget2D resource must take one of the two
// honest outcomes on both the whole-level and the partial-rectangle route.
//
// Microsoft XNA itself has only the first outcome: Texture2D.CopyData's pool-DEFAULT branch
// (IL_0187-0302) copies the target into a system-memory texture, patches the requested rectangle
// and loads it back with D3DXLoadSurfaceFromSurface, and the GetData branch reads the same way.
// SOFTWARE keeps every render-target texel in CPU storage, so on SOFTWARE a refusal of either
// route -- or of the readback -- is a defect, not an honest alternative.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace
{
    constexpr int kW = 4;
    constexpr int kH = 3;

    /// Every texel encodes its own index, so a write that landed at the wrong offset, rewrote the
    /// whole level or was dropped cannot look like the expected image.
    std::vector<Color> IndexedPattern(int base)
    {
        std::vector<Color> out;
        out.reserve(static_cast<std::size_t>(kW) * kH);
        for (int i = 0; i < kW * kH; ++i)
            out.emplace_back(base + i, 2 * i, 255 - i, 255);
        return out;
    }

    const Color kSentinel(1, 2, 3, 4);

    /// The renderers on which a refused render-target upload or readback contradicts XNA's
    /// CopyData contract outright, because they keep render-target texels in CPU storage.
    [[nodiscard]] bool RefusalIsADefect()
    {
        return CNA::Testing::ActiveRendererIs(CNA::GraphicsRendererType::Software);
    }
}

class RenderTargetUploadRoundTripTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;

    /// Reads the whole target back. Returns false when the renderer refuses the readback
    /// honestly, in which case the caller's buffer must be untouched.
    static bool ReadBack(RenderTarget2D& rt, std::vector<Color>& out)
    {
        out.assign(static_cast<std::size_t>(kW) * kH, kSentinel);
        try
        {
            rt.GetData(out.data(), static_cast<int>(out.size()));
        }
        catch (const System::NotSupportedException&)
        {
            for (const auto& pixel : out)
                EXPECT_EQ(pixel, kSentinel)
                    << "a refused readback must leave the caller's buffer untouched";
            return false;
        }
        return true;
    }
};

TEST_F(RenderTargetUploadRoundTripTest, WholeLevelUploadIsReadBackOrRefused)
{
    RenderTarget2D rt(gd, kW, kH);
    if (rt.GetRenderTargetRenderer() == nullptr)
        GTEST_SKIP() << "this renderer creates no RenderTarget2D resource";

    const auto in = IndexedPattern(40);
    try
    {
        rt.SetData(in.data(), static_cast<int>(in.size()));
    }
    catch (const System::NotSupportedException&)
    {
        EXPECT_FALSE(RefusalIsADefect()) << "XNA stores a resolved target's SetData";
        return;  // an explicit refusal is the other honest outcome
    }

    std::vector<Color> out;
    if (!ReadBack(rt, out))
    {
        EXPECT_FALSE(RefusalIsADefect()) << "XNA reads a resolved target back";
        return;
    }
    EXPECT_EQ(out, in) << "an accepted RenderTarget2D::SetData must reach the surface GetData reads";
}

TEST_F(RenderTargetUploadRoundTripTest, PartialRectangleUploadIsMergedOrRefused)
{
    RenderTarget2D rt(gd, kW, kH);
    if (rt.GetRenderTargetRenderer() == nullptr)
        GTEST_SKIP() << "this renderer creates no RenderTarget2D resource";

    auto expected = IndexedPattern(60);
    const Rectangle patchRect(1, 1, 2, 1);
    const std::vector<Color> patch{ Color(200, 10, 20, 255), Color(210, 30, 40, 255) };
    try
    {
        rt.SetData(expected.data(), static_cast<int>(expected.size()));
        rt.SetData(0, &patchRect, patch.data(), 0, static_cast<int>(patch.size()));
    }
    catch (const System::NotSupportedException&)
    {
        EXPECT_FALSE(RefusalIsADefect()) << "XNA patches a resolved target's rectangle";
        return;
    }
    for (int col = 0; col < patchRect.Width; ++col)
        expected[static_cast<std::size_t>(patchRect.Y * kW + patchRect.X + col)] =
            patch[static_cast<std::size_t>(col)];

    std::vector<Color> out;
    if (!ReadBack(rt, out))
    {
        EXPECT_FALSE(RefusalIsADefect()) << "XNA reads a resolved target back";
        return;
    }
    EXPECT_EQ(out, expected)
        << "a partial RenderTarget2D::SetData must replace exactly the rectangle it names";
}

// XNA Texture2D.CopyData IL_0423-043b: once a SetData has completed -- the refusals throw before
// that point, and GetData skips it because isSetting is false (IL_0423-0425) -- the resource's
// IDynamicGraphicsResource.SetContentLost(false) runs, and RenderTarget2D.get_IsContentLost
// (IL_0000-001f) reads that _contentLost field. Every write route ends the lost state.
TEST_F(RenderTargetUploadRoundTripTest, ACompletedSetDataClearsContentLostButARefusalOrReadDoesNot)
{
    RenderTarget2D rt(gd, kW, kH);
    if (rt.GetRenderTargetRenderer() == nullptr)
        GTEST_SKIP() << "this renderer creates no RenderTarget2D resource";

    const auto in = IndexedPattern(80);
    rt.NotifyContentLostEXT();
    ASSERT_TRUE(rt.getIsContentLostProperty());

    EXPECT_THROW(rt.SetData(in.data(), kW * kH - 1), System::ArgumentException);
    EXPECT_TRUE(rt.getIsContentLostProperty()) << "a refused SetData must not clear the flag";

    std::vector<Color> out;
    (void) ReadBack(rt, out);
    EXPECT_TRUE(rt.getIsContentLostProperty()) << "GetData must not clear the flag";

    const Microsoft::Xna::Framework::Rectangle one(1, 1, 1, 1);
    const Color patch(9, 8, 7, 6);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kW) * kH * 4u, 0x5Au);
    try
    {
        rt.SetData(in.data(), kW * kH);
        EXPECT_FALSE(rt.getIsContentLostProperty()) << "whole-level SetData";

        rt.NotifyContentLostEXT();
        rt.SetData(0, &one, &patch, 0, 1);
        EXPECT_FALSE(rt.getIsContentLostProperty()) << "rectangle SetData";

        rt.NotifyContentLostEXT();
        rt.SetData(bytes.data(), static_cast<int>(bytes.size()));
        EXPECT_FALSE(rt.getIsContentLostProperty()) << "byte SetData";
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "this renderer refuses RenderTarget2D uploads";
    }
}

// TextureCube.CopyData IL_042e-0446 is the same rule for a RenderTargetCube face.
TEST(RenderTargetCubeContentLostTest, ACompletedFaceSetDataClearsContentLost)
{
    GraphicsDevice gd;
    std::unique_ptr<RenderTargetCube> rt;
    try
    {
        rt = std::make_unique<RenderTargetCube>(gd, 2, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0,
                                                RenderTargetUsage::PreserveContents);
    }
    catch (const std::exception&)
    {
        GTEST_SKIP() << "this renderer creates no cube-map render target";
    }

    const std::vector<Color> face(4, Color(1, 2, 3, 4));
    rt->NotifyContentLostEXT();
    ASSERT_TRUE(rt->getIsContentLostProperty());
    EXPECT_THROW(rt->SetData(CubeMapFace::NegativeZ, face.data(), 3), System::ArgumentException);
    EXPECT_TRUE(rt->getIsContentLostProperty()) << "a refused SetData must not clear the flag";

    const std::vector<std::uint8_t> bytes(16, 0x44u);
    try
    {
        rt->SetData(CubeMapFace::NegativeZ, face.data(), 4);
        EXPECT_FALSE(rt->getIsContentLostProperty()) << "Color face SetData";

        rt->NotifyContentLostEXT();
        rt->SetData(CubeMapFace::PositiveY, 0, nullptr, bytes.data(), 0, 16);
        EXPECT_FALSE(rt->getIsContentLostProperty()) << "byte face SetData";
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "this renderer refuses RenderTargetCube uploads";
    }
}
