// SPDX-License-Identifier: MS-PL
// ISSUE-004: pixel evidence for live device sampler changes between Immediate draws.
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

class SpriteImmediateSamplerTest : public testing::Test
{
protected:
    // Backbuffer readback requires HiDef, as in the existing rasterization suite.
    static GraphicsDevice MakeDevice()
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(256);
        parameters.setBackBufferHeightProperty(256);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::HiDef, parameters);
    }
    GraphicsDevice device = MakeDevice();
    Texture2D texture{device, 2, 1};
    void SetUp() override
    {
        CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, Fna3d);
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
        const std::array<Color, 2> data{Color::Red, Color::Blue};
        texture.SetData(data.data(), 0, static_cast<int>(data.size()));
        device.Clear(Color::Black);
    }
    std::vector<Color> Row(int y)
    {
        std::vector<Color> row(64);
        const Rectangle area(16, y + 8, 64, 1);
        device.GetBackBufferData(&area, row.data(), 0, static_cast<int>(row.size()));
        return row;
    }
    static std::size_t Colors(const std::vector<Color>& row)
    {
        std::vector<Color> unique;
        for (const auto& color : row)
            if (std::find(unique.begin(), unique.end(), color) == unique.end())
                unique.push_back(color);
        return unique.size();
    }
};

TEST_F(SpriteImmediateSamplerTest, FilterChangesAffectOnlySubsequentDraws)
{
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                &SamplerState::PointClamp, nullptr, nullptr);
    batch.Draw(texture, Rectangle(16, 16, 64, 16), Color::White);
    device.getSamplerStatesProperty()(0, SamplerState::LinearClamp);
    batch.Draw(texture, Rectangle(16, 48, 64, 16), Color::White);
    batch.End();
    EXPECT_EQ(Colors(Row(16)), 2u);
    EXPECT_GT(Colors(Row(48)), 2u);
}

TEST_F(SpriteImmediateSamplerTest, ReturningToPointDoesNotRetroactivelyChangeLinearDraw)
{
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                &SamplerState::PointClamp, nullptr, nullptr);
    batch.Draw(texture, Rectangle(16, 16, 64, 16), Color::White);
    device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
    batch.Draw(texture, Rectangle(16, 48, 64, 16), Color::White);
    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    batch.Draw(texture, Rectangle(16, 80, 64, 16), Color::White);
    batch.End();
    EXPECT_EQ(Colors(Row(16)), 2u);
    EXPECT_GT(Colors(Row(48)), 2u);
    EXPECT_EQ(Row(16), Row(80));
}

TEST_F(SpriteImmediateSamplerTest, AddressChangeMatchesAnIndependentWrapBatchExactly)
{
    const Rectangle repeated(0, 0, 4, 1);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                &SamplerState::PointClamp, nullptr, nullptr);
    batch.Draw(texture, Rectangle(16, 16, 64, 16), repeated, Color::White);
    device.getSamplerStatesProperty()(0, SamplerState::PointWrap);
    batch.Draw(texture, Rectangle(16, 48, 64, 16), repeated, Color::White);
    batch.End();
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                &SamplerState::PointWrap, nullptr, nullptr);
    batch.Draw(texture, Rectangle(16, 80, 64, 16), repeated, Color::White);
    batch.End();
    EXPECT_NE(Row(16), Row(80));
    EXPECT_EQ(Row(48), Row(80));
}

TEST_F(SpriteImmediateSamplerTest, DeferredEndStillAppliesTheBatchSampler)
{
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                &SamplerState::PointClamp, nullptr, nullptr);
    batch.Draw(texture, Rectangle(16, 16, 64, 16), Color::White);
    device.getSamplerStatesProperty()(0, SamplerState::LinearClamp);
    batch.Draw(texture, Rectangle(16, 48, 64, 16), Color::White);
    batch.End();
    EXPECT_EQ(Colors(Row(16)), 2u);
    EXPECT_EQ(Row(16), Row(48));
}
} // namespace
