// SPDX-License-Identifier: MS-PL
//
// TQ-034: ModelProcessor's per-material rules -- the channels the effect a material will be drawn
// with needs, the material every material-less batch shares, VertexColorEnabled -- and the
// identities its refusals carry. Expectations are the genuine processor's answers over the same
// hand-built scenes (TQ-034 lane oracle, cases model/*, XNA 4.0 Refresh under Wine), read against
// its IL (ModelProcessor.ProcessGeometryUsingMaterial, SetVertexColorEnabled, CloneMaterial,
// ProcessWeightsChannel, ConvertVertexWeights).
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "XnaProcessorTestContext.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexCollections.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelProcessor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace
{
    namespace Pipeline = Microsoft::Xna::Framework::Content::Pipeline;
    namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
    namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;
    using Pipeline::ContentIdentity;
    using Processors::MaterialProcessorDefaultEffect;
    using CNA::Tests::XnaProcessors::RecordingContext;
    using CNA::Tests::XnaProcessors::RefusalOf;
    using CNA::Tests::XnaProcessors::Spell;

    std::shared_ptr<Graphics::GeometryContent> Batch(int uvSets, bool colours)
    {
        auto geometry = std::make_shared<Graphics::GeometryContent>();
        geometry->getVerticesProperty().AddRange({0, 1, 2});
        geometry->getIndicesProperty().AddRange({0, 1, 2});
        auto& channels = geometry->getVerticesProperty().getChannelsProperty();
        channels.Add<Vector3>(Graphics::VertexChannelNames::Normal(),
                              {Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1)});
        for (int set = 0; set < uvSets; ++set)
        {
            channels.Add<Vector2>(Graphics::VertexChannelNames::TextureCoordinate(set),
                                  {Vector2(0, 0), Vector2(1, 0), Vector2(0, 1)});
        }
        if (colours)
        {
            channels.Add<Color>(Graphics::VertexChannelNames::Color(0),
                                {Color(255, 0, 0, 255), Color(0, 255, 0, 255), Color(0, 0, 255, 255)});
        }
        return geometry;
    }

    /** @brief The lane oracle's triangle: three positions, a normal, uvSets UV sets, colours. */
    std::shared_ptr<Graphics::MeshContent> Triangle(const std::string& name, int uvSets, bool colours)
    {
        auto mesh = std::make_shared<Graphics::MeshContent>();
        mesh->setNameProperty(name);
        mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
        mesh->getGeometryProperty().Add(Batch(uvSets, colours));
        return mesh;
    }

    std::shared_ptr<Graphics::GeometryContent> GeometryAt(const Graphics::MeshContent& mesh, int index)
    {
        return static_cast<const System::Collections::ObjectModel::Collection<std::shared_ptr<Graphics::GeometryContent>>&>(
            mesh.getGeometryProperty())[index];
    }

    std::shared_ptr<Graphics::NodeContent> Scene(const std::vector<std::shared_ptr<Graphics::NodeContent>>& children)
    {
        auto root = std::make_shared<Graphics::NodeContent>();
        root->setNameProperty("Root");
        for (const auto& child : children)
        {
            root->getChildrenProperty().Add(child);
        }
        return root;
    }

    /** @brief The material's VertexColorEnabled opaque entry: True, False or unset. */
    std::string Vce(const Graphics::MaterialContent& material)
    {
        Pipeline::ContentObject value;
        if (!material.getOpaqueDataProperty().TryGetValue("VertexColorEnabled", value))
        {
            return "unset";
        }
        return Pipeline::Unbox<bool>(value) ? "True" : "False";
    }

    std::vector<std::shared_ptr<Graphics::MaterialContent>> PartMaterials(const Processors::ModelContent& model)
    {
        std::vector<std::shared_ptr<Graphics::MaterialContent>> materials;
        for (const auto& mesh : model.getMeshesProperty())
        {
            for (const auto& part : mesh->getMeshPartsProperty())
            {
                materials.push_back(part->getMaterialProperty());
            }
        }
        return materials;
    }

    void SetTexture(Graphics::BasicMaterialContent& material)
    {
        material.setTextureProperty(std::make_shared<Pipeline::ExternalReference<Graphics::TextureContent>>("tex.png"));
    }

    Graphics::BoneWeightCollection BoundTo(const std::string& bone)
    {
        Graphics::BoneWeightCollection weights;
        weights.Add(Graphics::BoneWeight(bone, 1.0f));
        return weights;
    }
}

// ProcessGeometryUsingMaterial IL_00b9-0107 and IL_012a-0189: the effect decides how many UV sets
// every batch must carry -- BasicEffect one if the material has a Texture and none otherwise,
// DualTextureEffect two, SkinnedEffect, EnvironmentMapEffect and AlphaTestEffect one -- and a
// missing one is refused under the batch's identity. CNA checked none of them.
TEST(XnaModelProcessorMaterialRules, TheEffectDecidesWhichTextureCoordinatesEveryBatchNeeds)
{
    RecordingContext context;
    {
        auto mesh = Triangle("Mesh", 0, false);
        mesh->setIdentityProperty(ContentIdentity("mesh.x", "XImporter", "mesh"));
        auto geometry = GeometryAt(*mesh, 0);
        geometry->setIdentityProperty(ContentIdentity("geometry.x", "XImporter", "geometry"));
        auto material = std::make_shared<Graphics::BasicMaterialContent>();
        SetTexture(*material);
        geometry->setMaterialProperty(material);
        const auto refusal = RefusalOf([&] { Processors::ModelProcessor processor; (void)processor.Process(Scene({mesh}), context); });
        EXPECT_EQ(refusal.message, "The mesh \"Mesh\", using BasicEffect, contains geometry that is missing texture "
                                   "coordinates for channel 0.");  // model/basic_texture_without_uv
        EXPECT_EQ(Spell(refusal.identity), "{geometry.x|XImporter|geometry}");
    }
    {
        auto mesh = Triangle("Mesh", 0, false);
        GeometryAt(*mesh, 0)->setMaterialProperty(std::make_shared<Graphics::BasicMaterialContent>());
        const auto refusal = RefusalOf([&] { Processors::ModelProcessor processor; (void)processor.Process(Scene({mesh}), context); });
        EXPECT_EQ(refusal.message, "built");  // model/basic_no_texture_without_uv
    }
    const auto withEffect = [&](MaterialProcessorDefaultEffect effect, int uvSets)
    {
        auto mesh = Triangle("Mesh", uvSets, false);
        GeometryAt(*mesh, 0)->setIdentityProperty(ContentIdentity("geometry.x", "XImporter", "geometry"));
        return RefusalOf([&]
        {
            Processors::ModelProcessor processor;
            processor.setDefaultEffectProperty(effect);
            (void)processor.Process(Scene({mesh}), context);
        });
    };
    const auto dualOne = withEffect(MaterialProcessorDefaultEffect::DualTextureEffect, 1);
    EXPECT_EQ(dualOne.message, "The mesh \"Mesh\", using DualTextureEffect, contains geometry that is missing texture "
                               "coordinates for channel 1.");  // model/dualtexture_default_effect_one_uv
    EXPECT_EQ(Spell(dualOne.identity), "{geometry.x|XImporter|geometry}");
    EXPECT_EQ(withEffect(MaterialProcessorDefaultEffect::DualTextureEffect, 2).message, "built");
    EXPECT_EQ(withEffect(MaterialProcessorDefaultEffect::EnvironmentMapEffect, 0).message,
              "The mesh \"Mesh\", using EnvironmentMapEffect, contains geometry that is missing texture coordinates "
              "for channel 0.");  // model/envmap_default_effect_no_uv
    EXPECT_EQ(withEffect(MaterialProcessorDefaultEffect::AlphaTestEffect, 0).message,
              "The mesh \"Mesh\", using AlphaTestEffect, contains geometry that is missing texture coordinates for "
              "channel 0.");  // model/alphatest_default_effect_no_uv
}

// The missing UV set is found before a tangent frame is asked for: XNA processes every material
// (Process IL_0101-0103) before any mesh (IL_0134). Lane oracle
// model/texture_and_tangent_frames_both_missing.
TEST(XnaModelProcessorMaterialRules, MaterialsAreCheckedBeforeAnyMeshIsProcessed)
{
    RecordingContext context;
    auto mesh = Triangle("Mesh", 0, false);
    auto material = std::make_shared<Graphics::BasicMaterialContent>();
    SetTexture(*material);
    GeometryAt(*mesh, 0)->setMaterialProperty(material);
    const auto refusal = RefusalOf([&]
    {
        Processors::ModelProcessor processor;
        processor.setGenerateTangentFramesProperty(true);
        (void)processor.Process(Scene({mesh}), context);
    });
    EXPECT_EQ(refusal.message, "The mesh \"Mesh\", using BasicEffect, contains geometry that is missing texture "
                               "coordinates for channel 0.");
}

// The skinning rule is the effect's, not the DefaultEffect's alone: a SkinnedMaterialContent needs
// weights whatever DefaultEffect says, an EffectMaterialContent needs nothing whatever it says, and
// a BlendIndices0/BlendWeight0 pair is as good as a Weights channel (IL_0192-01cd). A refusal names
// the batch (IL_01fb-0201). Lane oracle model/skinned_default_no_weights_identity,
// skinned_material_basic_default_no_weights, skinned_default_effect_material_no_weights,
// skinned_default_blend_channels.
TEST(XnaModelProcessorMaterialRules, SkinningNeedsWeightsOnlyWhereTheEffectIsSkinned)
{
    RecordingContext context;
    {
        auto mesh = Triangle("Mesh", 1, false);
        GeometryAt(*mesh, 0)->setIdentityProperty(ContentIdentity("geometry.x", "XImporter", "geometry"));
        const auto refusal = RefusalOf([&]
        {
            Processors::ModelProcessor processor;
            processor.setDefaultEffectProperty(MaterialProcessorDefaultEffect::SkinnedEffect);
            (void)processor.Process(Scene({mesh}), context);
        });
        EXPECT_EQ(refusal.message, "The skinned mesh \"Mesh\" contains geometry without any vertex weights.");
        EXPECT_EQ(Spell(refusal.identity), "{geometry.x|XImporter|geometry}");
    }
    {
        auto mesh = Triangle("Mesh", 1, false);
        GeometryAt(*mesh, 0)->setIdentityProperty(ContentIdentity("geometry.x", "XImporter", "geometry"));
        GeometryAt(*mesh, 0)->setMaterialProperty(std::make_shared<Graphics::SkinnedMaterialContent>());
        const auto refusal = RefusalOf([&] { Processors::ModelProcessor processor; (void)processor.Process(Scene({mesh}), context); });
        EXPECT_EQ(refusal.message, "The skinned mesh \"Mesh\" contains geometry without any vertex weights.");
        EXPECT_EQ(Spell(refusal.identity), "{geometry.x|XImporter|geometry}");
    }
    {
        auto mesh = Triangle("Mesh", 1, false);
        auto material = std::make_shared<Graphics::EffectMaterialContent>();
        material->setEffectProperty(std::make_shared<Pipeline::ExternalReference<Graphics::EffectContent>>("custom.fx"));
        GeometryAt(*mesh, 0)->setMaterialProperty(material);
        const auto refusal = RefusalOf([&]
        {
            Processors::ModelProcessor processor;
            processor.setDefaultEffectProperty(MaterialProcessorDefaultEffect::SkinnedEffect);
            (void)processor.Process(Scene({mesh}), context);
        });
        EXPECT_EQ(refusal.message, "built");
    }
    {
        auto mesh = Triangle("Mesh", 1, false);
        auto& channels = GeometryAt(*mesh, 0)->getVerticesProperty().getChannelsProperty();
        using Microsoft::Xna::Framework::Graphics::PackedVector::Byte4;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        channels.Add<Byte4>(Graphics::VertexChannelNames::EncodeName(VertexElementUsage::BlendIndices, 0),
                            {Byte4(0, 0, 0, 0), Byte4(0, 0, 0, 0), Byte4(0, 0, 0, 0)});
        channels.Add<Vector4>(Graphics::VertexChannelNames::EncodeName(VertexElementUsage::BlendWeight, 0),
                              {Vector4(1, 0, 0, 0), Vector4(1, 0, 0, 0), Vector4(1, 0, 0, 0)});
        const auto refusal = RefusalOf([&]
        {
            Processors::ModelProcessor processor;
            processor.setDefaultEffectProperty(MaterialProcessorDefaultEffect::SkinnedEffect);
            (void)processor.Process(Scene({mesh}), context);
        });
        EXPECT_EQ(refusal.message, "built");
    }
}

// ProcessWeightsChannel and ConvertVertexWeights refuse under the mesh's identity, in XNA's words
// (IL_0011-0021, IL_002d-005a). Lane oracle model/weights_without_skeleton and
// model/unknown_bone_identity.
TEST(XnaModelProcessorMaterialRules, WeightRefusalsNameTheMeshInXnasWords)
{
    RecordingContext context;
    const auto weighted = [](const std::string& bone)
    {
        auto mesh = Triangle("Mesh", 1, false);
        mesh->setIdentityProperty(ContentIdentity("mesh.x", "XImporter", "mesh"));
        auto geometry = GeometryAt(*mesh, 0);
        geometry->setIdentityProperty(ContentIdentity("geometry.x", "XImporter", "geometry"));
        geometry->getVerticesProperty().getChannelsProperty().Add<Graphics::BoneWeightCollection>(
            std::string(Graphics::VertexChannelNames::Weights()), {BoundTo(bone), BoundTo(bone), BoundTo(bone)});
        return mesh;
    };
    const auto lonely = RefusalOf([&] { Processors::ModelProcessor processor; (void)processor.Process(Scene({weighted("Bone0")}), context); });
    EXPECT_EQ(lonely.message, "Skeleton not found. Meshes that contain a Weights vertex channel cannot be processed "
                              "without access to the skeleton data.");
    EXPECT_EQ(Spell(lonely.identity), "{mesh.x|XImporter|mesh}");

    auto bone = std::make_shared<Graphics::BoneContent>();
    bone->setNameProperty("Bone0");
    const auto ghost = RefusalOf([&] { Processors::ModelProcessor processor; (void)processor.Process(Scene({bone, weighted("Ghost")}), context); });
    EXPECT_EQ(ghost.message, "Vertex is bound to bone \"Ghost\", but this bone is not present in the skeleton.");
    EXPECT_EQ(Spell(ghost.identity), "{mesh.x|XImporter|mesh}");
}

// Every material-less batch of the model shares one BasicMaterialContent, converted once
// (ProcessGeometryUsingMaterials IL_0048-0057 collects them, ProcessGeometryUsingMaterial
// IL_0000-003c gives them one). Lane oracle model/null_material_two_meshes and
// model/null_material_two_batches: same=True, one convert->MaterialProcessor. CNA gave each batch
// its own, so a model of N material-less meshes carried N effects.
TEST(XnaModelProcessorMaterialRules, MaterialLessBatchesShareOneMaterial)
{
    RecordingContext context;
    auto first = Triangle("First", 1, false);
    auto second = Triangle("Second", 1, false);
    second->getGeometryProperty().Add(Batch(1, false));
    Processors::ModelProcessor processor;
    const auto model = processor.Process(Scene({first, second}), context);
    const auto materials = PartMaterials(*model);
    ASSERT_EQ(materials.size(), 3u);
    ASSERT_NE(materials[0], nullptr);
    EXPECT_EQ(materials[0], materials[1]);
    EXPECT_EQ(materials[1], materials[2]);
    EXPECT_NE(std::dynamic_pointer_cast<Graphics::BasicMaterialContent>(materials[0]), nullptr);
    EXPECT_EQ(context.requests, std::vector<std::string>{"convert->MaterialProcessor"});
}

// SetVertexColorEnabled (IL_0000-00e6): Basic, DualTexture and AlphaTest materials say whether their
// batches carry Color0 -- False included -- and Skinned, EnvironmentMap and effect materials are not
// touched; batches that disagree get a clone saying False, made without the original's name or
// identity (CloneMaterial IL_0000-0097), and across meshes too. Lane oracle model/vce_*.
TEST(XnaModelProcessorMaterialRules, VertexColorEnabledFollowsTheBatchesAndClonesWhenTheyDisagree)
{
    RecordingContext context;
    const auto single = [&](std::shared_ptr<Graphics::MaterialContent> material, int uvSets, bool colours)
    {
        auto mesh = Triangle("Mesh", uvSets, colours);
        if (material != nullptr)
        {
            GeometryAt(*mesh, 0)->setMaterialProperty(material);
        }
        Processors::ModelProcessor processor;
        const auto materials = PartMaterials(*processor.Process(Scene({mesh}), context));
        return materials.empty() || materials[0] == nullptr ? std::string("none") : Vce(*materials[0]);
    };
    EXPECT_EQ(single(nullptr, 1, false), "False");                                                // vce_none
    EXPECT_EQ(single(nullptr, 1, true), "True");                                                  // vce_all
    EXPECT_EQ(single(std::make_shared<Graphics::DualTextureMaterialContent>(), 2, true), "True");  // vce_dualtexture_material
    EXPECT_EQ(single(std::make_shared<Graphics::AlphaTestMaterialContent>(), 1, true), "True");    // vce_alphatest_material
    EXPECT_EQ(single(std::make_shared<Graphics::EnvironmentMapMaterialContent>(), 1, true), "unset");  // vce_envmap_material
    auto effect = std::make_shared<Graphics::EffectMaterialContent>();
    effect->setEffectProperty(std::make_shared<Pipeline::ExternalReference<Graphics::EffectContent>>("custom.fx"));
    EXPECT_EQ(single(effect, 1, true), "unset");                                                  // vce_effect_material

    // One material, a coloured batch and a plain one: model/vce_mixed_clone_identity.
    auto mesh = Triangle("Mesh", 1, true);
    mesh->getGeometryProperty().Add(Batch(1, false));
    auto shared = std::make_shared<Graphics::BasicMaterialContent>();
    shared->setNameProperty("Shared");
    shared->setIdentityProperty(ContentIdentity("model.x", "XImporter", "Shared"));
    SetTexture(*shared);
    shared->getOpaqueDataProperty().SetValue<SharpRuntime::intcs>("Extra", 3);
    GeometryAt(*mesh, 0)->setMaterialProperty(shared);
    GeometryAt(*mesh, 1)->setMaterialProperty(shared);
    RecordingContext mixedContext;
    Processors::ModelProcessor processor;
    const auto materials = PartMaterials(*processor.Process(Scene({mesh}), mixedContext));
    ASSERT_EQ(materials.size(), 2u);
    EXPECT_EQ(materials[0], shared);
    ASSERT_NE(materials[1], nullptr);
    EXPECT_NE(materials[1], shared);
    EXPECT_EQ(Vce(*materials[0]), "True");
    EXPECT_EQ(Vce(*materials[1]), "False");
    EXPECT_EQ(materials[1]->getNameProperty(), "");
    EXPECT_EQ(Spell(materials[1]->getIdentityProperty()), "{||}");
    EXPECT_EQ(materials[1]->getTexturesProperty().getCountProperty(), 1);
    EXPECT_EQ(materials[1]->getOpaqueDataProperty().GetValue<SharpRuntime::intcs>("Extra", 0), 3);
    EXPECT_EQ(mixedContext.requests, (std::vector<std::string>{"convert->MaterialProcessor", "convert->MaterialProcessor"}));

    // The same disagreement across two meshes: model/vce_mixed_across_meshes.
    auto coloured = Triangle("Coloured", 1, true);
    auto plain = Triangle("Plain", 1, false);
    auto across = std::make_shared<Graphics::BasicMaterialContent>();
    GeometryAt(*coloured, 0)->setMaterialProperty(across);
    GeometryAt(*plain, 0)->setMaterialProperty(across);
    Processors::ModelProcessor acrossProcessor;
    const auto acrossMaterials = PartMaterials(*acrossProcessor.Process(Scene({coloured, plain}), context));
    ASSERT_EQ(acrossMaterials.size(), 2u);
    EXPECT_EQ(Vce(*acrossMaterials[0]), "True");
    EXPECT_EQ(Vce(*acrossMaterials[1]), "False");
    EXPECT_NE(acrossMaterials[0], acrossMaterials[1]);
}
