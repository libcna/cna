// SPDX-License-Identifier: MS-PL
//
// plan_modern.md MOD-1309/MOD-1310: the importer's own MaterialOut through the engine layer's
// glTF material bridge.
//
// The bridge itself is tested in modules/graphics-ext against a stand-in source, which is what
// proves it needs nothing from the content module. This file is the other half: it drives the same
// bridge with the record the importer actually produces, so a field renamed or retyped on
// MaterialOut fails here rather than at some consumer months later. It lives on this side of the
// boundary because MaterialOut is internal import data and stays there.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/GltfMaterialBridge.hpp"
#include "CNA/Graphics/MaterialBinding.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "GltfFixtureCorpus.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;
using CNA::Graphics::GltfMaterialTexturesEXT;
using CNA::Graphics::PbrMaterial;
using CNA::Graphics::PbrTextureSlot;
using CNA::Graphics::materialFromGltfEXT;
using CNA::Internal::GltfImport::MaterialOut;

static_assert(CNA::Graphics::GltfMaterialSourceEXT<MaterialOut>,
              "the importer's material record must keep satisfying the bridge's concept");

TEST(GltfMaterialToPbrMaterialTest, ADefaultImportedMaterialIsTheDefaultPbrMaterial)
{
    // A primitive with no material owns a MaterialOut holding glTF's defaults; those are also
    // PbrMaterial's defaults, and the two have to stay that way.
    EXPECT_EQ(materialFromGltfEXT(MaterialOut{}, GltfMaterialTexturesEXT{}), PbrMaterial{});
}

TEST(GltfMaterialToPbrMaterialTest, TheImporterRecordCarriesEveryFieldTheBridgeReads)
{
    GraphicsDevice gd;
    Texture2D baseColor(gd, 2, 2);
    Texture2D occlusion(gd, 2, 2);

    MaterialOut imported;
    imported.baseColorFactor = Vector4(1.0f, 0.5f, 0.0f, 1.0f);
    imported.metallicFactor = 0.125f;
    imported.roughnessFactor = 0.875f;
    imported.emissiveFactor = Vector3(6.0f, 6.0f, 6.0f);
    imported.normalScale = 2.0f;
    imported.occlusionStrength = 0.5f;
    imported.iorEXT = 1.6f;
    imported.specularFactorEXT = 0.25f;
    imported.specularColorFactorEXT = Vector3(0.5f, 0.5f, 0.5f);
    imported.alphaMode = AlphaModeEXT::Blend;
    imported.alphaCutoff = 0.4f;
    imported.doubleSided = true;
    imported.textureCoordinateSetsEXT[static_cast<std::size_t>(
        CNA::Internal::GltfImport::TextureSlotEXT::Occlusion)] = 1;
    TextureTransformEXT transform;
    transform.Offset = Vector2(0.125f, 0.25f);
    transform.Scale = Vector2(4.0f, 4.0f);
    imported.textureTransformsEXT[static_cast<std::size_t>(
        CNA::Internal::GltfImport::TextureSlotEXT::BaseColor)] = transform;

    GltfMaterialTexturesEXT textures;
    textures.Slots[static_cast<std::size_t>(PbrTextureSlot::BaseColor)] = &baseColor;
    textures.Slots[static_cast<std::size_t>(PbrTextureSlot::Occlusion)] = &occlusion;

    const PbrMaterial material = materialFromGltfEXT(imported, textures);

    EXPECT_EQ(material.getAlbedoTexture(), &baseColor);
    EXPECT_EQ(material.getAmbientOcclusionTexture(), &occlusion);
    // The importer's factor is four floats and the material's is a Color, so 0.5 quantises to 128.
    EXPECT_EQ(material.getAlbedoColor(), Color(255, 128, 0, 255));
    EXPECT_FLOAT_EQ(material.getMetallicFactor(), 0.125f);
    EXPECT_FLOAT_EQ(material.getRoughnessFactor(), 0.875f);
    EXPECT_EQ(material.getEmissiveFactor(), Vector3(6.0f, 6.0f, 6.0f));
    EXPECT_FLOAT_EQ(material.getNormalScale(), 2.0f);
    EXPECT_FLOAT_EQ(material.getOcclusionStrength(), 0.5f);
    EXPECT_FLOAT_EQ(material.getIor(), 1.6f);
    EXPECT_FLOAT_EQ(material.getSpecularFactor(), 0.25f);
    EXPECT_EQ(material.getSpecularColorFactor(), Vector3(0.5f, 0.5f, 0.5f));
    EXPECT_EQ(material.getAlphaMode(), AlphaModeEXT::Blend);
    EXPECT_FLOAT_EQ(material.getAlphaCutoff(), 0.4f);
    EXPECT_TRUE(material.isDoubleSided());
    EXPECT_EQ(material.getTextureCoordinateSet(PbrTextureSlot::Occlusion), 1);
    EXPECT_EQ(material.getTextureTransform(PbrTextureSlot::BaseColor), transform);
}

TEST(GltfMaterialToPbrMaterialTest, TheTwoSlotOrdersAreTheSameOrder)
{
    // The bridge indexes the importer's per-slot arrays with PbrTextureSlot, which is only correct
    // because the two enumerations agree value for value. They are declared in different modules,
    // so nothing but this test stops one of them from being reordered.
    using Slot = CNA::Internal::GltfImport::TextureSlotEXT;
    EXPECT_EQ(static_cast<int>(Slot::BaseColor), static_cast<int>(PbrTextureSlot::BaseColor));
    EXPECT_EQ(static_cast<int>(Slot::Normal), static_cast<int>(PbrTextureSlot::Normal));
    EXPECT_EQ(static_cast<int>(Slot::MetallicRoughness),
              static_cast<int>(PbrTextureSlot::MetallicRoughness));
    EXPECT_EQ(static_cast<int>(Slot::Emissive), static_cast<int>(PbrTextureSlot::Emissive));
    EXPECT_EQ(static_cast<int>(Slot::Occlusion), static_cast<int>(PbrTextureSlot::Occlusion));
    EXPECT_EQ(static_cast<int>(Slot::Specular), static_cast<int>(PbrTextureSlot::Specular));
    EXPECT_EQ(static_cast<int>(Slot::SpecularColor),
              static_cast<int>(PbrTextureSlot::SpecularColor));
}

TEST(GltfMaterialToPbrMaterialTest, TheMaterialDetourReproducesTheImporterDrawParameters)
{
    // MOD-1313 asked for a golden image of one model rendered both ways. This asserts the same
    // property one level lower and exactly: the two paths must hand the renderer the same
    // GpuDrawParams, which is what "the same picture" actually means -- and unlike a committed
    // image it also says WHICH field differs when one does.
    using Microsoft::Xna::Framework::Content::ContentManager;
    using Microsoft::Xna::Framework::Graphics::Model;
    using Microsoft::Xna::Framework::Graphics::PbrEffect;
    using CNA::Internal::Renderers::GpuDrawParams;

    GraphicsDevice gd;
    ContentManager cm(nullptr, CnaTest::GltfOracle::CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-factor-only-gold");
    auto* imported = dynamic_cast<PbrEffect*>(
        model.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(imported, nullptr);

    PbrEffect detoured(gd);
    CNA::Graphics::applyMaterial(CNA::Graphics::extractMaterial(*imported), detoured);

    GpuDrawParams direct{};
    GpuDrawParams viaMaterial{};
    imported->FillGpuDrawParams(direct);
    detoured.FillGpuDrawParams(viaMaterial);

    // Everything except the base-colour factor is carried exactly.
    EXPECT_FLOAT_EQ(direct.pbrMetallicFactor, viaMaterial.pbrMetallicFactor);
    EXPECT_FLOAT_EQ(direct.pbrRoughnessFactor, viaMaterial.pbrRoughnessFactor);
    EXPECT_FLOAT_EQ(direct.pbrNormalScale, viaMaterial.pbrNormalScale);
    EXPECT_FLOAT_EQ(direct.pbrOcclusionStrength, viaMaterial.pbrOcclusionStrength);
    EXPECT_FLOAT_EQ(direct.pbrSpecularFactor, viaMaterial.pbrSpecularFactor);
    EXPECT_EQ(direct.pbrTextureCoordinateSetMask, viaMaterial.pbrTextureCoordinateSetMask);
    EXPECT_EQ(direct.pbrBaseColorTextureIsSrgb, viaMaterial.pbrBaseColorTextureIsSrgb);
    EXPECT_EQ(direct.pbrEncodeOutputToSrgb, viaMaterial.pbrEncodeOutputToSrgb);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(direct.emissiveColor[i], viaMaterial.emissiveColor[i]) << "channel " << i;
        EXPECT_FLOAT_EQ(direct.pbrDielectricF0[i], viaMaterial.pbrDielectricF0[i]) << "channel " << i;
    }
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(direct.alphaTest[i], viaMaterial.alphaTest[i]) << "component " << i;

    // The base colour is the documented exception: a material stores it as a Color, so a float
    // factor comes back quantised to the nearest 1/255. Nothing else in the material rounds.
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(direct.diffuseColor[i], viaMaterial.diffuseColor[i], 1.0f / 255.0f)
            << "base colour component " << i;
}

#endif // CNA_CNAEXT
