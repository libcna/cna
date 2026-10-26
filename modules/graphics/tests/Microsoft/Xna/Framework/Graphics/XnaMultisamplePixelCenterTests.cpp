// SPDX-License-Identifier: MS-PL
// 2026-10-26 dependency audit, FNA3D B2-04: XNA 4.0 keeps Direct3D 9's integer pixel centres on a
// multisampled render target, so the XNA pixel-centre correction applies there too.
//
// Measured on the real runtime (spikes/xna-msaa-pixel-center-spike/): a BasicEffect quad covering
// exactly an 8x8 target, [0,8] x [0,8] in window pixels, comes back from a 4x (and an 8x)
// multisampled target with its first column and row at 128 and the corner at 64 -- half and a
// quarter of their samples covered -- and every other pixel at 255; a quad covering [0,4] x [0,8]
// leaves its interior seam, column 4, half covered. Every CNA renderer and FNA3D's drivers used to
// skip the correction for a multisampled destination, which drew both quads whole: the
// half-integer convention, half a pixel away from XNA's picture.
//
// The single-sampled draw is the control: both conventions cover every pixel of the full quad and
// exactly the first four columns of the half one. A renderer that grants no multisampling, or
// cannot read a target back, has nothing to answer and skips.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;
    // A resolve averages the samples; 2 of 4 covered is 127.5, rounded either way.
    constexpr int kTolerance = 2;

    /// The red channel of an 8x8 target after a white quad [0,right] x [0,8] on black, or nothing
    /// when this renderer cannot draw into a target and read it back.
    std::optional<std::vector<int>> DrawQuad(GraphicsDevice& device, int samples, float right,
                                             int& granted)
    {
        try
        {
            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None,
                                  samples, RenderTargetUsage::DiscardContents);
            granted = target.getMultiSampleCountProperty();
            device.SetRenderTarget(&target);
            device.Clear(Color(0, 0, 0, 255));
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setRasterizerStateProperty(RasterizerState::CullNone);

            BasicEffect effect(device);
            effect.VertexColorEnabled = true;
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
                0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 0.0f, 1.0f));
            effect.Apply();

            const Color white(255, 255, 255, 255);
            const float n = static_cast<float>(kSize);
            const VertexPositionColor quad[] = {
                {Vector3(0.0f, 0.0f, 0.0f), white},  {Vector3(right, 0.0f, 0.0f), white},
                {Vector3(0.0f, n, 0.0f), white},     {Vector3(right, 0.0f, 0.0f), white},
                {Vector3(right, n, 0.0f), white},    {Vector3(0.0f, n, 0.0f), white},
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            device.SetRenderTarget(nullptr);

            std::vector<Color> texels(static_cast<std::size_t>(kSize * kSize));
            target.GetData(texels.data(), static_cast<int>(texels.size()));
            std::vector<int> red;
            red.reserve(texels.size());
            for (const Color& texel : texels) red.push_back(texel.getRProperty());
            return red;
        }
        catch (const std::exception&)
        {
            device.SetRenderTarget(nullptr);
            return std::nullopt;
        }
    }

    std::string Picture(const std::vector<int>& red)
    {
        std::ostringstream out;
        for (int y = 0; y < kSize; ++y)
        {
            out << "\n   ";
            for (int x = 0; x < kSize; ++x) out << ' ' << red[static_cast<std::size_t>(y * kSize + x)];
        }
        return out.str();
    }

    /// XNA's measured picture: @p expected(x, y) for every pixel, within the resolve's rounding.
    template <typename Expected>
    void ExpectPicture(const std::vector<int>& red, Expected expected, const std::string& what)
    {
        int wrong = 0;
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                const int got = red[static_cast<std::size_t>(y * kSize + x)];
                if (std::abs(got - expected(x, y)) > kTolerance) ++wrong;
            }
        }
        EXPECT_EQ(wrong, 0) << what << ": " << wrong << " pixel(s) differ from XNA's picture"
                            << Picture(red);
    }

    GraphicsDevice& HiDef(GraphicsDevice& device)
    {
        device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
        return device;
    }
}

TEST(XnaMultisamplePixelCenterTest, ASingleSampledQuadCoversExactlyItsPixels)
{
    GraphicsDevice device;
    int granted = 0;
    const auto full = DrawQuad(HiDef(device), 0, 8.0f, granted);
    if (!full) GTEST_SKIP() << "this renderer cannot draw into a render target and read it back";
    ExpectPicture(*full, [](int, int) { return 255; }, "full quad, single-sampled");

    const auto half = DrawQuad(device, 0, 4.0f, granted);
    ASSERT_TRUE(half.has_value());
    ExpectPicture(*half, [](int x, int) { return x < 4 ? 255 : 0; }, "half quad, single-sampled");
}

TEST(XnaMultisamplePixelCenterTest, AMultisampledQuadKeepsXnasIntegerPixelCentres)
{
    GraphicsDevice device;
    int checked = 0;
    for (const int samples : {4, 8})
    {
        int granted = 0;
        const auto full = DrawQuad(HiDef(device), samples, 8.0f, granted);
        if (!full) GTEST_SKIP() << "this renderer cannot draw into a render target and read it back";
        if (granted != samples) continue;
        const std::string where = std::to_string(samples) + " samples";

        // Pixel i's samples sit around x = i: the first column and row are half covered, the
        // corner a quarter.
        ExpectPicture(*full, [](int x, int y) {
            if (x == 0 && y == 0) return 64;
            return (x == 0 || y == 0) ? 128 : 255;
        }, "full quad, " + where);

        const auto half = DrawQuad(device, samples, 4.0f, granted);
        ASSERT_TRUE(half.has_value());
        ExpectPicture(*half, [](int x, int y) {
            if (x > 4) return 0;
            const bool edgeX = (x == 0 || x == 4);
            if (edgeX && y == 0) return 64;
            return (edgeX || y == 0) ? 128 : 255;
        }, "half quad, " + where);
        ++checked;
    }
    if (checked == 0) GTEST_SKIP() << "this renderer grants neither 4 nor 8 samples to a target";
}
