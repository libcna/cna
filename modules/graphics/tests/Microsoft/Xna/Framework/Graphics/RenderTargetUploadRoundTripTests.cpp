// SPDX-License-Identifier: MS-PL
//
// A RenderTarget2D is a Texture2D, so SetData on one is legal XNA: the texels must land in the
// surface a later GetData reads, or the renderer must refuse with NotSupportedException. The
// failure these tests exist to catch is the silent third outcome -- ITextureRenderer declares
// UpdatePixels/UpdatePixelsLevel with empty default bodies, so a render-target renderer that does
// not override them accepts the upload, drops it, and answers the next readback from the cleared
// surface. Every renderer that creates a real RenderTarget2D resource must take one of the two
// honest outcomes on both the whole-level and the partial-rectangle route.
#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

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
        return;  // an explicit refusal is the other honest outcome
    }

    std::vector<Color> out;
    if (!ReadBack(rt, out))
        return;
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
        return;
    }
    for (int col = 0; col < patchRect.Width; ++col)
        expected[static_cast<std::size_t>(patchRect.Y * kW + patchRect.X + col)] =
            patch[static_cast<std::size_t>(col)];

    std::vector<Color> out;
    if (!ReadBack(rt, out))
        return;
    EXPECT_EQ(out, expected)
        << "a partial RenderTarget2D::SetData must replace exactly the rectangle it names";
}
