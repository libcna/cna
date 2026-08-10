// SPDX-License-Identifier: MS-PL
// Task 883: SpriteEffect::Clone() had no dedicated unit test file before this
// task's Effect::Clone() base-class contract work (only exercised implicitly
// via SpriteBatch's internal use of SpriteEffect, never Clone() itself).

#include <gtest/gtest.h>

#include <memory>

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SpriteEffect;

TEST(SpriteEffectTest, CloneReturnsIndependentSpriteEffect)
{
    GraphicsDevice gd;
    SpriteEffect fx(gd);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<SpriteEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));
}

TEST(SpriteEffectTest, CloneAndOriginalApplyIndependently)
{
    GraphicsDevice gd;
    SpriteEffect fx(gd);
    std::unique_ptr<Effect> cloned(fx.Clone());

    EXPECT_NO_THROW(fx.Apply());
    EXPECT_NO_THROW(cloned->Apply());
}
