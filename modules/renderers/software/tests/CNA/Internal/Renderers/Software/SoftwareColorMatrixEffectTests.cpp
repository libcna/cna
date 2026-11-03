// SPDX-License-Identifier: MS-PL
//
// ColorMatrixEffect end to end on the CPU SpriteBatch path: the fixed colour transform is applied to
// each sprite's sampled and tinted colour before blending, and its inputs are validated.

#if defined(CNA_RENDERER_SOFTWARE)

#include "System/ArgumentException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    /// A 4x4 HiDef device: GetBackBufferData, which observes the effect's output, is refused under
    /// Reach.
    PresentationParameters SmallBackbuffer()
    {
        PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
        parameters.setBackBufferWidthProperty(4);
        parameters.setBackBufferHeightProperty(4);
        return parameters;
    }

    Color DrawAndRead(GraphicsDevice& device, SpriteBatch& sprites, Texture2D& texture,
                      ColorMatrixEffect& effect)
    {
        device.Clear(Color::Black);
        sprites.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr,
                      &effect);
        sprites.Draw(texture, Rectangle(1, 1, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        sprites.End();

        Color result(0, 0, 0, 0);
        const Rectangle sample(1, 1, 1, 1);
        device.GetBackBufferData(&sample, &result, 0, 1);
        return result;
    }

    void ExpectColorExact(const Color& actual, std::array<int, 4> expected)
    {
        EXPECT_EQ(actual.getRProperty(), expected[0]) << "red";
        EXPECT_EQ(actual.getGProperty(), expected[1]) << "green";
        EXPECT_EQ(actual.getBProperty(), expected[2]) << "blue";
        EXPECT_EQ(actual.getAProperty(), expected[3]) << "alpha";
    }

    class SoftwareColorMatrixEffect : public ::testing::Test
    {
    protected:
        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              SmallBackbuffer()};
        Texture2D texture{device, 1, 1};
        SpriteBatch sprites{device};
        ColorMatrixEffect effect{device};

        void SetUp() override
        {
            const Color source(100, 200, 50, 128);
            texture.SetData(&source, 1);
        }
    };
}

TEST_F(SoftwareColorMatrixEffect, NonFiniteMatrixCoefficientsAreRefused)
{
    std::array<float, 16> matrix = effect.GetColorMatrix();
    matrix[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(effect.SetColorMatrix(matrix), System::ArgumentException);
}

TEST_F(SoftwareColorMatrixEffect, NonFiniteOffsetComponentsAreRefused)
{
    EXPECT_THROW(
        effect.SetColorOffset(Vector4(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f)),
        System::ArgumentException);
}

TEST_F(SoftwareColorMatrixEffect, GrayscaleUsesRec709WeightsAndKeepsAlpha)
{
    effect.SetGrayscale();
    // Rec.709: 100*.2126 + 200*.7152 + 50*.0722 = 167.91, rounds to 168.
    ExpectColorExact(DrawAndRead(device, sprites, texture, effect), {168, 168, 168, 128});
}

TEST_F(SoftwareColorMatrixEffect, TheMatrixAndOffsetTransformTheSampledColour)
{
    effect.SetColorMatrix({
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    });
    effect.SetColorOffset(Vector4(0.125f, 0.0f, 0.0f, 0.0f));
    // Matrix rows yield (50,50,200,128), then red adds 255/8: 81.875 -> 82.
    ExpectColorExact(DrawAndRead(device, sprites, texture, effect), {82, 50, 200, 128});
}

#endif // CNA_RENDERER_SOFTWARE
