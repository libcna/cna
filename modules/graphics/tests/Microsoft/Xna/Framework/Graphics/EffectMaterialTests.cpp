// SPDX-License-Identifier: MS-PL
// Task 883: EffectMaterial::Clone() — the last of the 8 concrete Effect
// subclasses (6 XNA stock effects + EffectMaterial + the CNAEXT ShaderEffect
// extension) to gain a Clone() override once Effect::Clone() became a pure
// virtual base-class contract.

#include <gtest/gtest.h>

#include <memory>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectMaterial;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(EffectMaterialTest, CloneReturnsIndependentEffectMaterial)
{
    GraphicsDevice gd;
    BasicEffect source(gd);
    EffectMaterial material(source);

    std::unique_ptr<Effect> cloned(material.Clone());
    auto* clone = dynamic_cast<EffectMaterial*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&material));
    EXPECT_EQ(clone->GetTypeName(), "Microsoft.Xna.Framework.Graphics.EffectMaterial");
}
