// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1309/MOD-1310: an imported glTF material as a PbrMaterial.
//
// The bridge is a template over a concept rather than a function of the importer's own struct, so
// the engine layer does not have to link the content module. That decoupling is the thing worth
// testing here: this file drives the bridge with a stand-in source it declares itself, which
// compiles only if the concept really names everything the bridge reads. The companion test in
// modules/content/tests drives it with the importer's actual record, which is what proves the two
// still agree.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/GltfMaterialBridge.hpp"
#include "CNA/Graphics/MaterialBinding.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"

#include <array>
#include <cstdint>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;
using CNA::Graphics::GltfMaterialTexturesEXT;
using CNA::Graphics::PbrMaterial;
using CNA::Graphics::PbrTextureSlot;
using CNA::Graphics::applyMaterial;
using CNA::Graphics::extractMaterial;
using CNA::Graphics::materialFromGltfEXT;

namespace {

    /// The same fields, and only the fields, that CNA::Internal::GltfImport::MaterialOut carries
    /// and the bridge reads.
    struct StandInMaterialOut
    {
        Vector4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        Vector3 emissiveFactor{0.0f, 0.0f, 0.0f};
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        float iorEXT = 1.5f;
        float specularFactorEXT = 1.0f;
        Vector3 specularColorFactorEXT{1.0f, 1.0f, 1.0f};
        AlphaModeEXT alphaMode = AlphaModeEXT::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        std::array<std::uint8_t, 7> textureCoordinateSetsEXT{};
        std::array<TextureTransformEXT, 7> textureTransformsEXT{};
    };

    static_assert(CNA::Graphics::GltfMaterialSourceEXT<StandInMaterialOut>,
                  "the stand-in must satisfy the concept the bridge is written against");

} // namespace

TEST(GltfMaterialBridgeTest, AnUnconfiguredImportGivesTheDefaultMaterial)
{
    // glTF's default material and PbrMaterial's defaults are the same material; if they ever
    // stopped being, importing a file that declares nothing would silently change its look.
    EXPECT_EQ(materialFromGltfEXT(StandInMaterialOut{}, GltfMaterialTexturesEXT{}), PbrMaterial{});
}

TEST(GltfMaterialBridgeTest, EveryDecodedValueReachesTheMaterial)
{
    GraphicsDevice gd;
    Texture2D baseColor(gd, 2, 2);
    Texture2D normal(gd, 2, 2);
    Texture2D specularColor(gd, 2, 2);

    StandInMaterialOut source;
    source.baseColorFactor = Vector4(0.2f, 0.4f, 0.6f, 0.8f);
    source.metallicFactor = 0.3f;
    source.roughnessFactor = 0.7f;
    // Above 1, which is exactly what KHR_materials_emissive_strength produces and what an 8-bit
    // emissive colour could not have carried.
    source.emissiveFactor = Vector3(4.0f, 0.0f, 2.0f);
    source.normalScale = 0.5f;
    source.occlusionStrength = 0.25f;
    source.iorEXT = 1.45f;
    source.specularFactorEXT = 0.9f;
    source.specularColorFactorEXT = Vector3(0.1f, 0.5f, 1.0f);
    source.alphaMode = AlphaModeEXT::Mask;
    source.alphaCutoff = 0.75f;
    source.doubleSided = true;
    source.textureCoordinateSetsEXT = {0, 1, 0, 1, 0, 1, 0};
    TextureTransformEXT transform;
    transform.Offset = Vector2(0.25f, 0.5f);
    transform.Scale = Vector2(2.0f, 3.0f);
    transform.Rotation = 0.5f;
    source.textureTransformsEXT[3] = transform;

    GltfMaterialTexturesEXT textures;
    textures.Slots[0] = &baseColor;
    textures.Slots[1] = &normal;
    textures.Slots[6] = &specularColor;

    const PbrMaterial material = materialFromGltfEXT(source, textures);

    EXPECT_EQ(material.getAlbedoTexture(), &baseColor);
    EXPECT_EQ(material.getNormalTexture(), &normal);
    EXPECT_EQ(material.getSpecularColorTexture(), &specularColor);
    EXPECT_EQ(material.getMetallicRoughnessTexture(), nullptr);
    // 0.2 * 255 = 51 exactly; 0.4/0.6/0.8 round to 102/153/204.
    EXPECT_EQ(material.getAlbedoColor(), Color(51, 102, 153, 204));
    EXPECT_FLOAT_EQ(material.getMetallicFactor(), 0.3f);
    EXPECT_FLOAT_EQ(material.getRoughnessFactor(), 0.7f);
    EXPECT_EQ(material.getEmissiveFactor(), Vector3(4.0f, 0.0f, 2.0f));
    EXPECT_FLOAT_EQ(material.getNormalScale(), 0.5f);
    EXPECT_FLOAT_EQ(material.getOcclusionStrength(), 0.25f);
    EXPECT_FLOAT_EQ(material.getIor(), 1.45f);
    EXPECT_FLOAT_EQ(material.getSpecularFactor(), 0.9f);
    EXPECT_EQ(material.getSpecularColorFactor(), Vector3(0.1f, 0.5f, 1.0f));
    EXPECT_EQ(material.getAlphaMode(), AlphaModeEXT::Mask);
    EXPECT_FLOAT_EQ(material.getAlphaCutoff(), 0.75f);
    EXPECT_TRUE(material.isDoubleSided());
    EXPECT_EQ(material.getTextureCoordinateSet(PbrTextureSlot::Normal), 1);
    EXPECT_EQ(material.getTextureCoordinateSet(PbrTextureSlot::Occlusion), 0);
    EXPECT_EQ(material.getTextureTransform(PbrTextureSlot::Emissive), transform);
    EXPECT_EQ(material.getTextureTransform(PbrTextureSlot::BaseColor), TextureTransformEXT{});
}

TEST(GltfMaterialBridgeTest, TheImportedMaterialSurvivesTheEffectRoundTrip)
{
    // The two halves of Phase 13 composed: import to a material, apply it, read it back.
    GraphicsDevice gd;
    StandInMaterialOut source;
    source.baseColorFactor = Vector4(0.2f, 0.4f, 0.6f, 0.8f);
    source.emissiveFactor = Vector3(3.0f, 1.0f, 0.5f);
    source.alphaMode = AlphaModeEXT::Blend;
    source.doubleSided = true;
    source.textureCoordinateSetsEXT = {1, 1, 1, 1, 1, 1, 1};

    const PbrMaterial imported = materialFromGltfEXT(source, GltfMaterialTexturesEXT{});
    PbrEffect effect(gd);
    applyMaterial(imported, effect);
    EXPECT_EQ(extractMaterial(effect), imported);
}

#endif // CNA_CNAEXT
