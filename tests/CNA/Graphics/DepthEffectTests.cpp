// SPDX-License-Identifier: MS-PL
// DepthEffect is a NOXNA CNA extension (no XNA/FNA precedent) — like ShaderEffectTests.cpp,
// these tests exercise the structural contract (mode round-trip, Clone() independence,
// GetTypeName()) against a default-constructed GraphicsDevice with no real backend, not GLSL
// compile/render correctness (that needs a live EasyGL context — see
// examples/easygl_postprocesseffect_shader_test.cpp for the runtime-verified pattern).

#ifdef CNA_NOXNA

#include <gtest/gtest.h>

#include <memory>

#include "CNA/Graphics/DepthEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using CNA::Graphics::DepthEffect;
using CNA::Graphics::DepthEffectMode;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(DepthEffectTest, DefaultModeIsColor16Bit)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.getMode(), DepthEffectMode::Color16Bit);
}

TEST(DepthEffectTest, SetModeRoundTripsForEveryMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    const DepthEffectMode modes[] = {
        DepthEffectMode::Color16Bit,
        DepthEffectMode::Color8Bit,
        DepthEffectMode::Grayscale4Bit,
        DepthEffectMode::Grayscale2Bit,
        DepthEffectMode::Grayscale1Bit,
    };

    for (const auto mode : modes)
    {
        fx.setMode(mode);
        EXPECT_EQ(fx.getMode(), mode);
    }
}

TEST(DepthEffectTest, ApplyDoesNotCrashWithoutABackend)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    fx.setMode(DepthEffectMode::Grayscale1Bit);
    EXPECT_NO_THROW(fx.Apply());
}

TEST(DepthEffectTest, GetTypeNameReturnsCnaGraphicsDepthEffect)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.GetTypeName(), "CNA.Graphics.DepthEffect");
}

TEST(DepthEffectTest, CloneReturnsIndependentDepthEffectWithSameMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    fx.setMode(DepthEffectMode::Grayscale2Bit);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<DepthEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));
    EXPECT_EQ(clone->getMode(), DepthEffectMode::Grayscale2Bit);

    // Mutating the clone must not affect the original.
    clone->setMode(DepthEffectMode::Color8Bit);
    EXPECT_EQ(fx.getMode(), DepthEffectMode::Grayscale2Bit);
}

#endif // CNA_NOXNA
