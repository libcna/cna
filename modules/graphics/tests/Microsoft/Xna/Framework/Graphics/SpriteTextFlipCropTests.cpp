// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include "RecordingSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/Text/StringBuilder.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DummyTextureRenderer;
using CNA::Internal::Renderers::RecordingSpriteBatchRenderer;

TEST(SpriteTextFlipCropTest, AsymmetricCroppingMatchesXnaForEveryFlipAndBothTextTypes)
{
    // XNA SpriteFont.InternalDraw IL_0054-0098, 0141-0199, 01ba-024c.
    // InternalMeasure gives (18,25). Pen X for A,B,B,A is 0,11,3,10.
    // XNA horizontal crop is crop.X-crop.Width (NOT -crop.X-glyph.Width).
    // Normal local XY: (2,3),(10,1),(2,13),(12,15).
    // Flipped X: 18-penX+cropX-cropWidth -> 10,-1,7,0.
    // Flipped Y: 25-lineY-cropY-glyphHeight -> 14,19,7,2.
    const float localX[2][4] = {{2,10,2,12}, {10,-1,7,0}};
    const float localY[2][4] = {{3,1,13,15}, {14,19,7,2}};
    const Rectangle sources[] = {{2,4,6,8}, {12,3,4,5}, {12,3,4,5}, {2,4,6,8}};
    auto atlas = Texture2D::CreateWithRendererForTests(
        32,32,std::make_shared<DummyTextureRenderer>(32,32));
    const SpriteFont font(atlas, {{2,4,6,8},{12,3,4,5}},
                          {{2,3,10,11},{-1,1,7,13}}, {u'A',u'B'},12,2.0f,
                          {Vector3(-2,7,-1),Vector3(3,5,2)},std::nullopt);
    for (int flip = 0; flip < 4; ++flip)
    for (bool rotated : {false,true})
    for (bool builder : {false,true})
    {
        SCOPED_TRACE(flip);
        SCOPED_TRACE(rotated);
        SCOPED_TRACE(builder);
        auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
        auto* rec = renderer.get();
        SpriteBatch batch(std::move(renderer));
        const auto effects = static_cast<SpriteEffects>(flip);
        const float rotation = rotated ? 1.5707963267948966f : 0.0f;
        const Color tint(17,31,47,127);
        batch.Begin();
        if (builder)
            batch.DrawString(font, System::Text::StringBuilder("A\rB\nBA"), Vector2(10,20),
                             tint, rotation, Vector2(1,2), Vector2(2,3), effects,0.375f);
        else
            batch.DrawString(font, std::string("A\rB\nBA"), Vector2(10,20),
                             tint, rotation, Vector2(1,2), Vector2(2,3), effects,0.375f);
        batch.End();
        ASSERT_EQ(rec->drawCalls.size(),4u);
        for (int i=0; i<4; ++i)
        {
            SCOPED_TRACE(i);
            // Hand rotation of the literal coordinates: R90(x,y)=(-y,x).
            const float dx = (localX[flip & 1][i]-1)*2;
            const float dy = (localY[(flip >> 1) & 1][i]-2)*3;
            const auto& call = rec->drawCalls[i];
            EXPECT_NEAR(call.destinationX,10+(rotated ? -dy : dx),0.0001f);
            EXPECT_NEAR(call.destinationY,20+(rotated ? dx : dy),0.0001f);
            EXPECT_FLOAT_EQ(call.destinationWidth,sources[i].Width*2.0f);
            EXPECT_FLOAT_EQ(call.destinationHeight,sources[i].Height*3.0f);
            EXPECT_EQ(call.sourceRectangle,sources[i]);
            EXPECT_EQ(call.texture,atlas.GetRendererWeak().lock().get());
            EXPECT_EQ(call.color,tint);
            EXPECT_EQ(call.origin,Vector2::Zero);
            EXPECT_FLOAT_EQ(call.rotation,rotation);
            EXPECT_EQ(call.effects,effects);
            EXPECT_FLOAT_EQ(call.layerDepth,0.375f);
        }
    }
}
