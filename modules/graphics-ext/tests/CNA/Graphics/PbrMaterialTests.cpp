// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1300..MOD-1314: PbrMaterial as a lossless description of PbrEffect.
//
// The interesting property is not that a setter round-trips -- it is that nothing falls off
// between the material and the effect. So the centre of this file is a material with EVERY field
// set to a non-default value, pushed onto an effect and read back: any field applyMaterial or
// extractMaterial forgets shows up as an inequality, whichever one it is.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/MaterialBinding.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"

#include <string>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;
using CNA::Graphics::PbrMaterial;
using CNA::Graphics::PbrTextureSlot;
using CNA::Graphics::applyMaterial;
using CNA::Graphics::applyMaterialState;
using CNA::Graphics::extractMaterial;

namespace {

    class PbrMaterialTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        Texture2D albedo{gd, 2, 2};
        Texture2D normal{gd, 2, 2};
        Texture2D metallicRoughness{gd, 2, 2};
        Texture2D occlusion{gd, 2, 2};
        Texture2D emissive{gd, 2, 2};
        Texture2D specular{gd, 2, 2};
        Texture2D specularColor{gd, 2, 2};

        /// A material with nothing left at its default, so a dropped field cannot hide.
        PbrMaterial fullyPopulated()
        {
            PbrMaterial material;
            material.setAlbedoTexture(&albedo);
            material.setNormalTexture(&normal);
            material.setMetallicRoughnessTexture(&metallicRoughness);
            material.setAmbientOcclusionTexture(&occlusion);
            material.setEmissiveTexture(&emissive);
            material.setSpecularTexture(&specular);
            material.setSpecularColorTexture(&specularColor);
            material.setAlbedoColor(Color(12, 34, 56, 78));
            material.setMetallicFactor(0.25f);
            material.setRoughnessFactor(0.75f);
            material.setEmissiveFactor(Vector3(2.0f, 3.0f, 4.0f));
            material.setNormalScale(1.5f);
            material.setOcclusionStrength(0.4f);
            material.setIor(1.33f);
            material.setSpecularFactor(0.6f);
            material.setSpecularColorFactor(Vector3(0.1f, 0.2f, 0.3f));
            material.setAlphaMode(AlphaModeEXT::Mask);
            material.setAlphaCutoff(0.25f);
            material.setDoubleSided(true);
            material.setBaseColorTextureSrgb(false);
            material.setEmissiveTextureSrgb(false);
            material.setSpecularColorTextureSrgb(false);
            material.setOutputEncodedToSrgb(false);
            for (int slot = 0; slot < CNA::Graphics::kPbrTextureSlotCount; ++slot)
            {
                const auto named = static_cast<PbrTextureSlot>(slot);
                material.setTextureCoordinateSet(named, slot % 2);
                TextureTransformEXT transform;
                transform.Offset = Vector2(0.1f * static_cast<float>(slot), 0.2f);
                transform.Scale  = Vector2(2.0f, 1.0f + static_cast<float>(slot));
                transform.Rotation = 0.05f * static_cast<float>(slot);
                material.setTextureTransform(named, transform);
            }
            return material;
        }
    };

} // namespace

// ---------------------------------------------------------------------------
// Defaults (MOD-1300: the material's defaults are the effect's defaults)

TEST_F(PbrMaterialTest, DefaultsAreTheGltfDefaultMaterial)
{
    const PbrMaterial material;
    EXPECT_EQ(material.getAlbedoTexture(), nullptr);
    EXPECT_EQ(material.getSpecularColorTexture(), nullptr);
    EXPECT_EQ(material.getAlbedoColor(), Color(255, 255, 255, 255));
    EXPECT_FLOAT_EQ(material.getMetallicFactor(), 1.0f);
    EXPECT_FLOAT_EQ(material.getRoughnessFactor(), 1.0f);
    EXPECT_EQ(material.getEmissiveFactor(), Vector3::Zero);
    EXPECT_FLOAT_EQ(material.getNormalScale(), 1.0f);
    EXPECT_FLOAT_EQ(material.getOcclusionStrength(), 1.0f);
    EXPECT_FLOAT_EQ(material.getIor(), 1.5f);
    EXPECT_FLOAT_EQ(material.getSpecularFactor(), 1.0f);
    EXPECT_EQ(material.getSpecularColorFactor(), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(material.getAlphaMode(), AlphaModeEXT::Opaque);
    EXPECT_FLOAT_EQ(material.getAlphaCutoff(), 0.5f);
    EXPECT_FALSE(material.isDoubleSided());
    EXPECT_TRUE(material.isBaseColorTextureSrgb());
    EXPECT_TRUE(material.isOutputEncodedToSrgb());
    for (int slot = 0; slot < CNA::Graphics::kPbrTextureSlotCount; ++slot)
    {
        const auto named = static_cast<PbrTextureSlot>(slot);
        EXPECT_EQ(material.getTextureCoordinateSet(named), 0);
        EXPECT_EQ(material.getTextureTransform(named), TextureTransformEXT{});
    }
}

TEST_F(PbrMaterialTest, ADefaultMaterialLeavesADefaultEffectUnchanged)
{
    // The two default states have to agree, or "apply the default material" would silently be a
    // change. This is the assertion that pins MOD-1300's mapping table to the code.
    PbrEffect effect(gd);
    const PbrMaterial before = extractMaterial(effect);
    EXPECT_EQ(before, PbrMaterial{});

    applyMaterial(PbrMaterial{}, effect);
    EXPECT_EQ(extractMaterial(effect), PbrMaterial{});
}

// ---------------------------------------------------------------------------
// Round trip (MOD-1303/1304/1305)

TEST_F(PbrMaterialTest, EveryFieldSurvivesTheRoundTripThroughPbrEffect)
{
    const PbrMaterial original = fullyPopulated();
    PbrEffect effect(gd);
    applyMaterial(original, effect);

    // Spot checks on the effect itself, so a failure says which side is wrong rather than only
    // that the two disagree.
    EXPECT_EQ(effect.getTextureProperty(), &albedo);
    EXPECT_EQ(effect.getSpecularColorMapEXTProperty(), &specularColor);
    EXPECT_FLOAT_EQ(effect.getMetallicFactorProperty(), 0.25f);
    EXPECT_EQ(effect.getEmissiveFactorProperty(), Vector3(2.0f, 3.0f, 4.0f));
    EXPECT_EQ(effect.getAlphaModeEXTProperty(), AlphaModeEXT::Mask);
    EXPECT_TRUE(effect.getDoubleSidedEXTProperty());
    EXPECT_FALSE(effect.getEncodeOutputToSrgbEXTProperty());
    EXPECT_NEAR(effect.getDiffuseColorProperty().X, 12.0f / 255.0f, 1e-6f);
    EXPECT_NEAR(effect.getAlphaProperty(), 78.0f / 255.0f, 1e-6f);

    EXPECT_EQ(extractMaterial(effect), original);
}

TEST_F(PbrMaterialTest, EveryFieldSurvivesTheRoundTripThroughSkinnedPbrEffect)
{
    const PbrMaterial original = fullyPopulated();
    SkinnedPbrEffect effect(gd);
    applyMaterial(original, effect);
    EXPECT_EQ(extractMaterial(effect), original);
}

TEST_F(PbrMaterialTest, TheRoundTripIsExactForEveryEightBitAlbedoValue)
{
    // The albedo factor is the one field that changes representation (Color to Vector3 and back),
    // so it is the one that could lose a step. Truncation instead of rounding fails this at 1.
    PbrEffect effect(gd);
    PbrMaterial material;
    for (int value = 0; value <= 255; ++value)
    {
        material.setAlbedoColor(Color(value, 255 - value, value, 255 - value));
        applyMaterial(material, effect);
        EXPECT_EQ(extractMaterial(effect).getAlbedoColor(), material.getAlbedoColor())
            << "value " << value;
    }
}

TEST_F(PbrMaterialTest, ApplyingAMaterialLeavesSceneStateAlone)
{
    // A material describes a surface, not the frame it is drawn in.
    PbrEffect effect(gd);
    effect.setAmbientLightColorProperty(Vector3(0.3f, 0.3f, 0.3f));
    effect.DirectionalLight0.setEnabledProperty(true);
    effect.setFogEnabledProperty(true);

    applyMaterial(fullyPopulated(), effect);

    EXPECT_EQ(effect.getAmbientLightColorProperty(), Vector3(0.3f, 0.3f, 0.3f));
    EXPECT_TRUE(effect.DirectionalLight0.getEnabledProperty());
    EXPECT_TRUE(effect.getFogEnabledProperty());
}

// ---------------------------------------------------------------------------
// Device state (MOD-1306/1307)

TEST_F(PbrMaterialTest, AlphaModeAndSidednessSelectDeviceState)
{
    // Compared by the state's own distinguishing values rather than by identity: the device
    // stores a copy, so a pointer comparison would test the wrong thing.
    PbrMaterial material;
    applyMaterialState(material, gd);
    EXPECT_EQ(gd.getBlendStateProperty().getColorSourceBlendProperty(),
              BlendState::Opaque.getColorSourceBlendProperty());
    EXPECT_EQ(gd.getRasterizerStateProperty().getCullModeProperty(),
              CullMode::CullCounterClockwiseFace);

    // Mask is a discard in the shader, so it stays opaque: blending a cutout would be wrong.
    material.setAlphaMode(AlphaModeEXT::Mask);
    applyMaterialState(material, gd);
    EXPECT_EQ(gd.getBlendStateProperty().getColorSourceBlendProperty(),
              BlendState::Opaque.getColorSourceBlendProperty());

    material.setAlphaMode(AlphaModeEXT::Blend);
    material.setDoubleSided(true);
    applyMaterialState(material, gd);
    EXPECT_EQ(gd.getBlendStateProperty().getColorSourceBlendProperty(),
              BlendState::NonPremultiplied.getColorSourceBlendProperty());
    EXPECT_EQ(gd.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
}

// ---------------------------------------------------------------------------
// Value semantics (MOD-1311)

TEST_F(PbrMaterialTest, EqualityComparesEveryField)
{
    const PbrMaterial left = fullyPopulated();
    EXPECT_EQ(left, fullyPopulated());
    EXPECT_FALSE(left != fullyPopulated());

    const auto differsBy = [&](auto&& mutate) {
        PbrMaterial other = fullyPopulated();
        mutate(other);
        EXPECT_NE(left, other);
        EXPECT_TRUE(left != other);
    };

    differsBy([](PbrMaterial& m) { m.setAlbedoTexture(nullptr); });
    differsBy([](PbrMaterial& m) { m.setSpecularColorTexture(nullptr); });
    differsBy([](PbrMaterial& m) { m.setAlbedoColor(Color(1, 2, 3, 4)); });
    differsBy([](PbrMaterial& m) { m.setMetallicFactor(0.26f); });
    differsBy([](PbrMaterial& m) { m.setRoughnessFactor(0.76f); });
    differsBy([](PbrMaterial& m) { m.setEmissiveFactor(Vector3(2.0f, 3.0f, 4.5f)); });
    differsBy([](PbrMaterial& m) { m.setNormalScale(1.6f); });
    differsBy([](PbrMaterial& m) { m.setOcclusionStrength(0.5f); });
    differsBy([](PbrMaterial& m) { m.setIor(1.4f); });
    differsBy([](PbrMaterial& m) { m.setSpecularFactor(0.7f); });
    differsBy([](PbrMaterial& m) { m.setSpecularColorFactor(Vector3::Zero); });
    differsBy([](PbrMaterial& m) { m.setAlphaMode(AlphaModeEXT::Blend); });
    differsBy([](PbrMaterial& m) { m.setAlphaCutoff(0.3f); });
    differsBy([](PbrMaterial& m) { m.setDoubleSided(false); });
    differsBy([](PbrMaterial& m) { m.setBaseColorTextureSrgb(true); });
    differsBy([](PbrMaterial& m) { m.setEmissiveTextureSrgb(true); });
    differsBy([](PbrMaterial& m) { m.setSpecularColorTextureSrgb(true); });
    differsBy([](PbrMaterial& m) { m.setOutputEncodedToSrgb(true); });
    differsBy([](PbrMaterial& m) { m.setTextureCoordinateSet(PbrTextureSlot::Emissive, 7); });
    differsBy([](PbrMaterial& m) {
        TextureTransformEXT transform;
        transform.Rotation = 3.0f;
        m.setTextureTransform(PbrTextureSlot::Occlusion, transform);
    });
}

TEST_F(PbrMaterialTest, EqualMaterialsHashEqually)
{
    EXPECT_EQ(PbrMaterial{}.GetHashCode(), PbrMaterial{}.GetHashCode());
    EXPECT_EQ(fullyPopulated().GetHashCode(), fullyPopulated().GetHashCode());

    PbrMaterial changed = fullyPopulated();
    changed.setRoughnessFactor(0.7501f);
    EXPECT_NE(fullyPopulated().GetHashCode(), changed.GetHashCode());
}

TEST_F(PbrMaterialTest, ToStringSummarisesTheDistinguishingValues)
{
    EXPECT_EQ(PbrMaterial{}.ToString(),
              "{Albedo:{R:255 G:255 B:255 A:255} Metallic:1 Roughness:1 Emissive:{X:0 Y:0 Z:0} "
              "AlphaMode:Opaque DoubleSided:False Textures:0}");

    PbrMaterial material;
    material.setAlbedoTexture(&albedo);
    material.setNormalTexture(&normal);
    material.setAlphaMode(AlphaModeEXT::Blend);
    material.setDoubleSided(true);
    const std::string text = material.ToString();
    EXPECT_NE(text.find("AlphaMode:Blend"), std::string::npos) << text;
    EXPECT_NE(text.find("DoubleSided:True"), std::string::npos) << text;
    EXPECT_NE(text.find("Textures:2"), std::string::npos) << text;
}

TEST_F(PbrMaterialTest, OutOfRangeSlotsAreDefinedRatherThanUndefined)
{
    // The enum is the only legal input, but a caller can cast an integer into it, and reading
    // past the end of the arrays would be a memory bug rather than a wrong answer.
    PbrMaterial material;
    material.setTextureCoordinateSet(static_cast<PbrTextureSlot>(99), 1);
    EXPECT_EQ(material.getTextureCoordinateSet(static_cast<PbrTextureSlot>(99)), 1);
    EXPECT_EQ(material.getTextureCoordinateSet(PbrTextureSlot::BaseColor), 1);
}

#endif // CNA_CNAEXT
