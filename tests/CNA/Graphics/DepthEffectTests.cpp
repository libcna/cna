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
#include "CNA/Graphics/DitherMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using CNA::Graphics::DepthEffect;
using CNA::Graphics::DepthEffectMode;
using CNA::Graphics::DitherMode;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(DepthEffectTest, DefaultModeIsColor16Bit)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.getMode(), DepthEffectMode::Color16Bit);
}

TEST(DepthEffectTest, DefaultDitherModeIsNone)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.getDitherMode(), DitherMode::None);
}

TEST(DepthEffectTest, SetDitherModeRoundTripsForEveryMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    const DitherMode ditherModes[] = {
        DitherMode::None,
        DitherMode::Bayer4x4,
        DitherMode::Bayer8x8,
    };

    for (const auto ditherMode : ditherModes)
    {
        fx.setDitherMode(ditherMode);
        EXPECT_EQ(fx.getDitherMode(), ditherMode);
    }
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
    fx.setDitherMode(DitherMode::Bayer8x8);
    EXPECT_NO_THROW(fx.Apply());
}

TEST(DepthEffectTest, GetTypeNameReturnsCnaGraphicsDepthEffect)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.GetTypeName(), "CNA.Graphics.DepthEffect");
}

TEST(DepthEffectTest, CloneReturnsIndependentDepthEffectWithSameModeAndDitherMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    fx.setMode(DepthEffectMode::Grayscale2Bit);
    fx.setDitherMode(DitherMode::Bayer4x4);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<DepthEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));
    EXPECT_EQ(clone->getMode(), DepthEffectMode::Grayscale2Bit);
    EXPECT_EQ(clone->getDitherMode(), DitherMode::Bayer4x4);

    // Mutating the clone must not affect the original.
    clone->setMode(DepthEffectMode::Color8Bit);
    clone->setDitherMode(DitherMode::Bayer8x8);
    EXPECT_EQ(fx.getMode(), DepthEffectMode::Grayscale2Bit);
    EXPECT_EQ(fx.getDitherMode(), DitherMode::Bayer4x4);
}

#endif // CNA_NOXNA
