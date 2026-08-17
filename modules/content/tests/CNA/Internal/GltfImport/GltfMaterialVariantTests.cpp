// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-341 / GLTF-342 -- KHR_materials_variants import and selection.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

using namespace CNA::Internal::GltfImport;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::Strings;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::PbrEffect;

namespace
{
    constexpr float kTolerance = 1e-6f;

    Model LoadVariantModel(GraphicsDevice& device)
    {
        ContentManager content(nullptr, CorpusDirectory().string());
        content.setGraphicsDevice(device);
        return content.Load<Model>("mat-material-variants");
    }

    ModelMeshPart* OnlyPart(Model& model)
    {
        if (model.getMeshesProperty().getCountProperty() != 1) { return nullptr; }
        auto* mesh = model.getMeshesProperty()[0];
        if (mesh->getMeshPartsProperty().getCountProperty() != 1) { return nullptr; }
        return mesh->getMeshPartsProperty()[0];
    }
}

TEST(GltfMaterialVariants, CoreExtractionKeepsTheDefaultAndDecodesEveryMappedMaterialFully)
{
    const LoadedFixture fixture("mat-material-variants");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(3u, static_cast<std::size_t>(fixture.Data().variants_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().meshes_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().meshes[0].primitives_count));

    const cgltf_primitive& primitive = fixture.Data().meshes[0].primitives[0];
    const cgltf_material* defaultMaterial = primitive.material;
    const MeshOut defaultMesh =
        ExtractMesh(&fixture.Data(), primitive, "VariantTri", nullptr, 1.0f);
    const std::vector<MaterialVariantOutEXT> variants = ExtractMaterialVariantsEXT(
        &fixture.Data(), primitive, "VariantTri", nullptr, 1.0f);

    ASSERT_EQ(defaultMaterial, primitive.material)
        << "extracting alternatives mutated the primitive's core/default material mapping";
    ASSERT_EQ(2u, variants.size());
    EXPECT_EQ(0u, variants[0].variantIndex);
    EXPECT_EQ(1u, variants[1].variantIndex);

    EXPECT_TRUE(defaultMesh.usePbr);
    EXPECT_EQ(48, defaultMesh.stride);
    EXPECT_TRUE(variants[0].mesh.usePbr);
    EXPECT_EQ(48, variants[0].mesh.stride);
    EXPECT_TRUE(variants[0].mesh.transmissionApproximatedEXT);
    EXPECT_NEAR(0.5f, variants[0].mesh.transmissionFactorEXT, kTolerance);
    EXPECT_EQ(AlphaModeEXT::Blend, variants[0].mesh.material.alphaMode);
    EXPECT_NEAR(0.5f, variants[0].mesh.material.baseColorFactor.W, kTolerance);
    EXPECT_FALSE(variants[1].mesh.usePbr);
    EXPECT_TRUE(variants[1].mesh.unlitEXT);
    EXPECT_EQ(32, variants[1].mesh.stride)
        << "the unlit alternative was treated as effect-only state instead of getting its own "
           "compatible vertex layout";
}

TEST(GltfMaterialVariants, MalformedSparseMappingsAreRejectedWithTheirExactCause)
{
    const LoadedFixture fixture("mat-material-variants");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const cgltf_primitive& source = fixture.Data().meshes[0].primitives[0];
    ASSERT_GE(source.mappings_count, 1u);

    const auto errorFor = [&](std::vector<cgltf_material_mapping> mappings) {
        cgltf_primitive primitive = source;
        primitive.mappings = mappings.data();
        primitive.mappings_count = mappings.size();
        try
        {
            (void)ExtractMaterialVariantsEXT(
                &fixture.Data(), primitive, "MalformedVariantTri", nullptr, 1.0f);
        }
        catch (const std::runtime_error& ex)
        {
            return std::string(ex.what());
        }
        return std::string{};
    };

    cgltf_material_mapping outOfRange = source.mappings[0];
    outOfRange.variant = fixture.Data().variants_count;
    EXPECT_NE(std::string::npos, errorFor({outOfRange}).find("declares only 3 variant"));

    cgltf_material_mapping missingMaterial = source.mappings[0];
    missingMaterial.material = nullptr;
    EXPECT_NE(std::string::npos, errorFor({missingMaterial}).find("no material"));

    EXPECT_NE(std::string::npos,
              errorFor({source.mappings[0], source.mappings[0]}).find("more than once"));
}

TEST(GltfMaterialVariants, FreshLoadUsesTheCoreMaterialAndExposesSourceOrderNames)
{
    const LoadedFixture fixture("mat-material-variants");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const std::vector<std::string> expectedNames =
        Strings(Path(fixture.Expected(), "l4.materialVariants.names"));

    GraphicsDevice device;
    Model model = LoadVariantModel(device);
    ModelMeshPart* part = OnlyPart(model);
    ASSERT_NE(nullptr, part);

    EXPECT_EQ(expectedNames, model.getMaterialVariantNamesEXTProperty());
    EXPECT_EQ(-1, model.getMaterialVariantEXTProperty());
    auto* effect = dynamic_cast<PbrEffect*>(part->getEffectProperty());
    ASSERT_NE(nullptr, effect) << "declaring variants changed the freshly loaded default effect";
    const auto color = effect->getDiffuseColorProperty();
    EXPECT_NEAR(0.6f, color.X, kTolerance);
    EXPECT_NEAR(0.1f, color.Y, kTolerance);
    EXPECT_NEAR(0.1f, color.Z, kTolerance);
    EXPECT_EQ(3, part->getNumVerticesProperty());
}

TEST(GltfMaterialVariants, SelectionSwapsCompleteStateAndSparseSelectionRestoresTheDefault)
{
    GraphicsDevice device;
    Model model = LoadVariantModel(device);
    ModelMeshPart* part = OnlyPart(model);
    ASSERT_NE(nullptr, part);

    auto* defaultEffect = part->getEffectProperty();
    auto* defaultVertexBuffer = part->getVertexBufferProperty();

    model.setMaterialVariantEXTProperty(0);
    EXPECT_EQ(0, model.getMaterialVariantEXTProperty());
    auto* blue = dynamic_cast<PbrEffect*>(part->getEffectProperty());
    ASSERT_NE(nullptr, blue);
    EXPECT_NE(defaultEffect, blue);
    const auto blueColor = blue->getDiffuseColorProperty();
    EXPECT_NEAR(0.05f, blueColor.X, kTolerance);
    EXPECT_NEAR(0.2f, blueColor.Y, kTolerance);
    EXPECT_NEAR(0.9f, blueColor.Z, kTolerance);
    EXPECT_EQ(AlphaModeEXT::Blend, blue->getAlphaModeEXTProperty());
    EXPECT_NEAR(0.5f, blue->getAlphaProperty(), kTolerance);

    model.setMaterialVariantEXTProperty(1);
    EXPECT_EQ(1, model.getMaterialVariantEXTProperty());
    auto* green = dynamic_cast<BasicEffect*>(part->getEffectProperty());
    ASSERT_NE(nullptr, green);
    EXPECT_FALSE(green->getLightingEnabledProperty());
    const auto greenColor = green->getDiffuseColorProperty();
    EXPECT_NEAR(0.1f, greenColor.X, kTolerance);
    EXPECT_NEAR(0.8f, greenColor.Y, kTolerance);
    EXPECT_NEAR(0.2f, greenColor.Z, kTolerance);
    EXPECT_NEAR(0.75f, green->getAlphaProperty(), kTolerance);
    EXPECT_NE(defaultVertexBuffer, part->getVertexBufferProperty());
    EXPECT_EQ(3, part->getNumVerticesProperty());

    // Variant 2 has no mapping on this primitive. The extension requires the primitive's default,
    // not whichever variant happened to be selected immediately before it.
    model.setMaterialVariantEXTProperty(2);
    EXPECT_EQ(2, model.getMaterialVariantEXTProperty());
    EXPECT_EQ(defaultEffect, part->getEffectProperty());
    EXPECT_EQ(defaultVertexBuffer, part->getVertexBufferProperty());
    EXPECT_EQ(1, model.getMeshesProperty()[0]->getEffectsProperty().getCountProperty())
        << "switching effects left stale alternatives in ModelMesh::Effects";

    model.setMaterialVariantEXTProperty(-1);
    EXPECT_EQ(-1, model.getMaterialVariantEXTProperty());
    EXPECT_EQ(defaultEffect, part->getEffectProperty());
    EXPECT_EQ(defaultVertexBuffer, part->getVertexBufferProperty());
}

TEST(GltfMaterialVariants, InvalidIndicesThrowWithoutChangingTheCurrentSelection)
{
    GraphicsDevice device;
    Model model = LoadVariantModel(device);
    model.setMaterialVariantEXTProperty(0);
    ModelMeshPart* part = OnlyPart(model);
    ASSERT_NE(nullptr, part);
    auto* selectedEffect = part->getEffectProperty();

    EXPECT_THROW(model.setMaterialVariantEXTProperty(-2), std::out_of_range);
    EXPECT_THROW(model.setMaterialVariantEXTProperty(3), std::out_of_range);
    EXPECT_EQ(0, model.getMaterialVariantEXTProperty());
    EXPECT_EQ(selectedEffect, part->getEffectProperty());
}

TEST(GltfMaterialVariants, ModelCopiesShareSelectionBecauseTheyShareTheUnderlyingParts)
{
    GraphicsDevice device;
    Model model = LoadVariantModel(device);
    Model copy = model;

    copy.setMaterialVariantEXTProperty(1);
    EXPECT_EQ(1, model.getMaterialVariantEXTProperty());
    EXPECT_EQ(1, copy.getMaterialVariantEXTProperty());
    ASSERT_NE(nullptr, dynamic_cast<BasicEffect*>(OnlyPart(model)->getEffectProperty()));

    model.setMaterialVariantEXTProperty(-1);
    EXPECT_EQ(-1, copy.getMaterialVariantEXTProperty());
    ASSERT_NE(nullptr, dynamic_cast<PbrEffect*>(OnlyPart(copy)->getEffectProperty()));
}

TEST(GltfMaterialVariants, TheGlbTwinPreservesTheSameSelectableState)
{
    GraphicsDevice device;
    ContentManager content(nullptr, CorpusDirectory().string());
    content.setGraphicsDevice(device);
    Model model = content.Load<Model>("mat-material-variants.glb");

    EXPECT_EQ((std::vector<std::string>{"Ocean blue", "Unlit green", "No mapping"}),
              model.getMaterialVariantNamesEXTProperty());
    model.setMaterialVariantEXTProperty(1);
    ModelMeshPart* part = OnlyPart(model);
    ASSERT_NE(nullptr, part);
    auto* effect = dynamic_cast<BasicEffect*>(part->getEffectProperty());
    ASSERT_NE(nullptr, effect);
    EXPECT_FALSE(effect->getLightingEnabledProperty());
    EXPECT_NEAR(0.75f, effect->getAlphaProperty(), kTolerance);
}
