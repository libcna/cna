// SPDX-License-Identifier: MS-PL
// ISSUE-027: witnesses for target-dependent blend input ranges.
#include <array>
#include <string>
#include <gtest/gtest.h>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using PackedVector::HalfVector4;

class BlendSourceRangeTest : public testing::Test
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(800, 480)};
    void SetUp() override
    {
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
        if (!device.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets))
            GTEST_SKIP() << "Renderer does not expose half-float targets";
    }
    template<class Pixel>
    Pixel Render(const Vector4& source, SurfaceFormat format, const BlendState& blend)
    {
        RenderTarget2D target(device, 8, 8, false, format, DepthFormat::None,
                              0, RenderTargetUsage::PreserveContents);
        Texture2D texture(device, 1, 1, false, SurfaceFormat::HalfVector4);
        const HalfVector4 texel(source.X, source.Y, source.Z, source.W);
        texture.SetData(&texel, 1);
        device.SetRenderTarget(&target);
        // The XNA ClearOptions/Vector4 overload first quantizes through Color.
        // Use the raw-float extension to isolate blending with exact binary16 inputs.
        device.Clear(0.25f, 0.25f, 0.25f, 0.25f);
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &blend, &SamplerState::PointClamp,
                    nullptr, nullptr);
        batch.Draw(texture, Rectangle(0, 0, 8, 8), Color::White);
        batch.End();
        device.SetRenderTarget(nullptr);
        std::array<Pixel, 64> pixels;
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[4 * 8 + 4];
    }
    void ExpectColor(const Color& pixel, int r, int g, int b, int a)
    {
        // One UNORM8 step allows rounding of exact binary fractions to integer channels.
        EXPECT_NEAR(pixel.getRProperty(), r, 1);
        EXPECT_NEAR(pixel.getGProperty(), g, 1);
        EXPECT_NEAR(pixel.getBProperty(), b, 1);
        EXPECT_NEAR(pixel.getAProperty(), a, 1);
    }
    BlendState Difference(BlendFunction function)
    {
        BlendState blend;
        blend.setColorSourceBlendProperty(Blend::One);
        blend.setColorDestinationBlendProperty(Blend::One);
        blend.setAlphaSourceBlendProperty(Blend::One);
        blend.setAlphaDestinationBlendProperty(Blend::One);
        blend.setColorBlendFunctionProperty(function);
        blend.setAlphaBlendFunctionProperty(function);
        return blend;
    }
};

TEST_F(BlendSourceRangeTest, ColorClampsAlphaAboveOneBeforeDestinationFactor)
{
    ExpectColor(Render<Color>(Vector4(0.75f, 0.5f, 0.25f, 2.0f),
                              SurfaceFormat::Color, BlendState::AlphaBlend),
                191, 128, 64, 255);
}

TEST_F(BlendSourceRangeTest, ColorClampsNegativeAlphaBeforeDestinationFactor)
{
    ExpectColor(Render<Color>(Vector4(0.25f, 0.5f, 0.75f, -1.0f),
                              SurfaceFormat::Color, BlendState::AlphaBlend),
                128, 192, 255, 64);
}

TEST_F(BlendSourceRangeTest, ColorClampsRgbBeforeSubtraction)
{
    const BlendState blend = Difference(BlendFunction::Subtract);
    ExpectColor(Render<Color>(Vector4(2.0f, -1.0f, 0.5f, 0.5f),
                              SurfaceFormat::Color, blend), 191, 0, 64, 64);
}

TEST_F(BlendSourceRangeTest, ColorClampsRgbBeforeReverseSubtraction)
{
    const BlendState blend = Difference(BlendFunction::ReverseSubtract);
    ExpectColor(Render<Color>(Vector4(2.0f, -1.0f, 0.0f, 0.5f),
                              SurfaceFormat::Color, blend), 0, 64, 64, 0);
}

TEST_F(BlendSourceRangeTest, HdrPreservesUnclampedAlphaBlendInputs)
{
    const Vector4 pixel = Render<HalfVector4>(Vector4(0.75f, 0.5f, 0.25f, 2.0f),
                            SurfaceFormat::HdrBlendable, BlendState::AlphaBlend).ToVector4();
    // All values and arithmetic results are exactly representable in binary16.
    EXPECT_FLOAT_EQ(pixel.X, 0.5f);
    EXPECT_FLOAT_EQ(pixel.Y, 0.25f);
    EXPECT_FLOAT_EQ(pixel.Z, 0.0f);
    EXPECT_FLOAT_EQ(pixel.W, 1.75f);
}

TEST_F(BlendSourceRangeTest, HdrPreservesNegativeAndAboveOneDifference)
{
    const BlendState blend = Difference(BlendFunction::Subtract);
    const Vector4 pixel = Render<HalfVector4>(Vector4(2.0f, -1.0f, 0.5f, 0.5f),
                            SurfaceFormat::HdrBlendable, blend).ToVector4();
    EXPECT_FLOAT_EQ(pixel.X, 1.75f);
    EXPECT_FLOAT_EQ(pixel.Y, -1.25f);
    EXPECT_FLOAT_EQ(pixel.Z, 0.25f);
    EXPECT_FLOAT_EQ(pixel.W, 0.25f);
}
}
