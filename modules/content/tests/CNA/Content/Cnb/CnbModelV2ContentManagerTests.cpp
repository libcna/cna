// SPDX-License-Identifier: MS-PL

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

namespace Cnb = CNA::Content::Cnb;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

namespace
{
    class ScratchRoot
    {
    public:
        ScratchRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_model_v2_runtime_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchRoot()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        /** @brief Returns the temporary content root. */
        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path,
                    const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    Cnb::CnbModelV2Data RuntimeModel()
    {
        Cnb::CnbModelV2Data model;
        Cnb::CnbModelV2Bone detached;
        detached.name = "Detached";
        Cnb::CnbModelV2Bone selectedRoot;
        selectedRoot.name = "SelectedRoot";
        selectedRoot.transform[12] = 4.0f;
        model.bones = {detached, selectedRoot};
        model.rootBone = 1u;

        Cnb::CnbModelV2VertexDeclaration declaration;
        declaration.vertexStride = 24u;
        declaration.elements = {
            {0u, Cnb::CnbModelV2VertexFormat::Vector3,
             Cnb::CnbModelV2VertexUsage::Position, 0u},
            {12u, Cnb::CnbModelV2VertexFormat::Vector3,
             Cnb::CnbModelV2VertexUsage::Normal, 0u}};
        model.vertexDeclarations = {declaration};

        Cnb::CnbModelV2VertexBuffer vertices;
        vertices.declaration = 0u;
        vertices.vertexCount = 5u;
        vertices.bytes.resize(120u);
        for (std::size_t byte = 0u; byte < vertices.bytes.size(); ++byte)
        {
            vertices.bytes[byte] = static_cast<std::uint8_t>(byte);
        }
        model.vertexBuffers = {vertices};
        model.indexBuffers = {{2u, 6u,
                               {0u, 0u, 1u, 0u, 2u, 0u,
                                1u, 0u, 3u, 0u, 2u, 0u}}};

        Cnb::CnbModelV2Effect effect;
        effect.kind = Cnb::CnbModelV2EffectKind::BasicEffect;
        effect.diffuse = {{0.25f, 0.5f, 0.75f}};
        effect.emissive = {{0.1f, 0.2f, 0.3f}};
        effect.specular = {{0.8f, 0.7f, 0.6f}};
        effect.specularPower = 9.5f;
        effect.alpha = 0.75f;
        effect.vertexColorEnabled = true;
        model.effects = {effect};

        Cnb::CnbModelV2Mesh mesh;
        mesh.name = "SharedGeometry";
        mesh.parentBone = 1;
        mesh.boundingSphere = {{1.0f, 2.0f, 3.0f, 4.0f}};
        mesh.parts = {{1u, 4u, 0u, 1u, 0u, 0u, 0u},
                      {1u, 4u, 3u, 1u, 0u, 0u, 0u}};
        model.meshes = {mesh};
        return model;
    }

    Cnb::CnbModelV2Data RuntimeStockEffectsModel()
    {
        Cnb::CnbModelV2Data model;
        Cnb::CnbModelV2Bone root;
        root.name = "Root";
        model.bones = {root};

        Cnb::CnbModelV2VertexDeclaration declaration;
        declaration.vertexStride = 12u;
        declaration.elements = {{0u, Cnb::CnbModelV2VertexFormat::Vector3,
                                 Cnb::CnbModelV2VertexUsage::Position, 0u}};
        model.vertexDeclarations = {declaration};
        model.vertexBuffers = {{0u, 3u, std::vector<std::uint8_t>(36u)}};
        model.indexBuffers = {{2u, 3u, {0u, 0u, 1u, 0u, 2u, 0u}}};

        Cnb::CnbModelV2Effect basic;
        basic.kind = Cnb::CnbModelV2EffectKind::BasicEffect;
        basic.diffuse = {{0.1f, 0.2f, 0.3f}};
        basic.emissive = {{0.4f, 0.5f, 0.6f}};
        basic.specular = {{0.7f, 0.8f, 0.9f}};
        basic.specularPower = 13.0f;
        basic.alpha = 0.91f;
        basic.vertexColorEnabled = true;

        Cnb::CnbModelV2Effect skinned;
        skinned.kind = Cnb::CnbModelV2EffectKind::SkinnedEffect;
        skinned.diffuse = {{0.11f, 0.21f, 0.31f}};
        skinned.emissive = {{0.41f, 0.51f, 0.61f}};
        skinned.specular = {{0.71f, 0.81f, 0.91f}};
        skinned.specularPower = 17.0f;
        skinned.alpha = 0.82f;
        skinned.weightsPerVertex = 4u;

        Cnb::CnbModelV2Effect dual;
        dual.kind = Cnb::CnbModelV2EffectKind::DualTextureEffect;
        dual.diffuse = {{0.12f, 0.22f, 0.32f}};
        dual.alpha = 0.73f;
        dual.vertexColorEnabled = true;

        Cnb::CnbModelV2Effect alpha;
        alpha.kind = Cnb::CnbModelV2EffectKind::AlphaTestEffect;
        alpha.diffuse = {{0.13f, 0.23f, 0.33f}};
        alpha.alpha = 0.64f;
        alpha.alphaFunction = 6u;
        alpha.referenceAlpha = 0xF0000040u;
        alpha.vertexColorEnabled = true;

        Cnb::CnbModelV2Effect environment;
        environment.kind = Cnb::CnbModelV2EffectKind::EnvironmentMapEffect;
        environment.diffuse = {{0.14f, 0.24f, 0.34f}};
        environment.emissive = {{0.44f, 0.54f, 0.64f}};
        environment.specular = {{0.74f, 0.84f, 0.94f}};
        environment.environmentMapAmount = 0.55f;
        environment.fresnelFactor = 0.45f;
        environment.alpha = 0.35f;
        model.effects = {basic, skinned, dual, alpha, environment};

        Cnb::CnbModelV2Mesh mesh;
        mesh.name = "Effects";
        mesh.parentBone = 0;
        for (std::uint32_t effect = 0u; effect < 5u; ++effect)
        {
            mesh.parts.push_back({0u, 3u, 0u, 1u, 0u, 0u, effect});
        }
        model.meshes = {mesh};
        return model;
    }
}

TEST(CnbModelV2ContentManagerTest, LoadsExactRootDeclarationWindowsBoundsAndSharing)
{
    ScratchRoot root;
    const Cnb::CnbModelV2Data source = RuntimeModel();
    WriteBytes(root.Path() / "shared.cnb",
               Cnb::EncodeModelV2ToCnb(source, "shared"));

    GraphicsDevice device;
    ContentManager manager(nullptr, root.Path().string());
    manager.setGraphicsDevice(device);
    Model model = manager.Load<Model>("shared");

    ASSERT_EQ(model.getBonesProperty().getCountProperty(), 2);
    ASSERT_NE(model.getRootProperty(), nullptr);
    EXPECT_EQ(model.getRootProperty()->getIndexProperty(), 1);
    EXPECT_EQ(model.getRootProperty()->getNameProperty(), "SelectedRoot");
    EXPECT_FLOAT_EQ(model.getRootProperty()->getTransformProperty().M41, 4.0f);
    EXPECT_EQ(model.getTagProperty(), nullptr);

    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    const auto* mesh = model.getMeshesProperty()[0];
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->getParentBoneProperty(), model.getRootProperty());
    EXPECT_EQ(mesh->getTagProperty(), nullptr);
    EXPECT_FLOAT_EQ(mesh->getBoundingSphereProperty().Center.X, 1.0f);
    EXPECT_FLOAT_EQ(mesh->getBoundingSphereProperty().Center.Y, 2.0f);
    EXPECT_FLOAT_EQ(mesh->getBoundingSphereProperty().Center.Z, 3.0f);
    EXPECT_FLOAT_EQ(mesh->getBoundingSphereProperty().Radius, 4.0f);

    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 2);
    const auto* first = mesh->getMeshPartsProperty()[0];
    const auto* second = mesh->getMeshPartsProperty()[1];
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->getVertexBufferProperty(), second->getVertexBufferProperty());
    EXPECT_EQ(first->getIndexBufferProperty(), second->getIndexBufferProperty());
    EXPECT_EQ(first->getEffectProperty(), second->getEffectProperty());
    EXPECT_EQ(first->getTagProperty(), nullptr);
    EXPECT_EQ(first->getVertexOffsetProperty(), 1);
    EXPECT_EQ(first->getNumVerticesProperty(), 4);
    EXPECT_EQ(first->getStartIndexProperty(), 0);
    EXPECT_EQ(second->getStartIndexProperty(), 3);

    const auto& declaration =
        first->getVertexBufferProperty()->getVertexDeclarationProperty();
    EXPECT_EQ(declaration.getVertexStrideProperty(), 24);
    ASSERT_EQ(declaration.GetVertexElements().size(), 2u);
    EXPECT_EQ(declaration.GetVertexElements()[1].getOffsetProperty(), 12);
    EXPECT_EQ(declaration.GetVertexElements()[1].getVertexElementUsageProperty(),
              Microsoft::Xna::Framework::Graphics::VertexElementUsage::Normal);

    std::vector<std::uint8_t> bytes(120u);
    first->getVertexBufferProperty()->GetDataRawEXT(0, bytes.data(), 5, 24);
    EXPECT_EQ(bytes, source.vertexBuffers[0].bytes);

    const auto* effect = dynamic_cast<const BasicEffect*>(first->getEffectProperty());
    ASSERT_NE(effect, nullptr);
    EXPECT_FLOAT_EQ(effect->getSpecularPowerProperty(), 9.5f);
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 0.75f);
    EXPECT_TRUE(effect->getVertexColorEnabledProperty());
}

TEST(CnbModelV2ContentManagerTest, ConstructsAllFiveStockEffectKindsWithExactParameters)
{
    ScratchRoot root;
    WriteBytes(root.Path() / "effects.cnb",
               Cnb::EncodeModelV2ToCnb(RuntimeStockEffectsModel(), "effects"));

    GraphicsDevice device;
    ContentManager manager(nullptr, root.Path().string());
    manager.setGraphicsDevice(device);
    const Model model = manager.Load<Model>("effects");
    const auto* mesh = model.getMeshesProperty()[0];
    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 5);

    const auto* basic = dynamic_cast<const BasicEffect*>(
        mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(basic, nullptr);
    EXPECT_FLOAT_EQ(basic->getDiffuseColorProperty().Y, 0.2f);
    EXPECT_FLOAT_EQ(basic->getEmissiveColorProperty().Z, 0.6f);
    EXPECT_FLOAT_EQ(basic->getSpecularColorProperty().X, 0.7f);
    EXPECT_FLOAT_EQ(basic->getSpecularPowerProperty(), 13.0f);
    EXPECT_FLOAT_EQ(basic->getAlphaProperty(), 0.91f);
    EXPECT_TRUE(basic->getVertexColorEnabledProperty());

    const auto* skinned = dynamic_cast<const SkinnedEffect*>(
        mesh->getMeshPartsProperty()[1]->getEffectProperty());
    ASSERT_NE(skinned, nullptr);
    EXPECT_EQ(skinned->getWeightsPerVertexProperty(), 4);
    EXPECT_FLOAT_EQ(skinned->getDiffuseColorProperty().X, 0.11f);
    EXPECT_FLOAT_EQ(skinned->getSpecularPowerProperty(), 17.0f);
    EXPECT_FLOAT_EQ(skinned->getAlphaProperty(), 0.82f);

    const auto* dual = dynamic_cast<const DualTextureEffect*>(
        mesh->getMeshPartsProperty()[2]->getEffectProperty());
    ASSERT_NE(dual, nullptr);
    EXPECT_FLOAT_EQ(dual->getDiffuseColorProperty().Z, 0.32f);
    EXPECT_FLOAT_EQ(dual->getAlphaProperty(), 0.73f);
    EXPECT_TRUE(dual->getVertexColorEnabledProperty());

    const auto* alpha = dynamic_cast<const AlphaTestEffect*>(
        mesh->getMeshPartsProperty()[3]->getEffectProperty());
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(static_cast<std::uint32_t>(alpha->getAlphaFunctionProperty()), 6u);
    EXPECT_EQ(alpha->getReferenceAlphaProperty(),
              std::bit_cast<std::int32_t>(0xF0000040u));
    EXPECT_FLOAT_EQ(alpha->getAlphaProperty(), 0.64f);
    EXPECT_TRUE(alpha->getVertexColorEnabledProperty());

    const auto* environment = dynamic_cast<const EnvironmentMapEffect*>(
        mesh->getMeshPartsProperty()[4]->getEffectProperty());
    ASSERT_NE(environment, nullptr);
    EXPECT_FLOAT_EQ(environment->getDiffuseColorProperty().Y, 0.24f);
    EXPECT_FLOAT_EQ(environment->getEmissiveColorProperty().Z, 0.64f);
    EXPECT_FLOAT_EQ(environment->getEnvironmentMapSpecularProperty().X, 0.74f);
    EXPECT_FLOAT_EQ(environment->getEnvironmentMapAmountProperty(), 0.55f);
    EXPECT_FLOAT_EQ(environment->getFresnelFactorProperty(), 0.45f);
    EXPECT_FLOAT_EQ(environment->getAlphaProperty(), 0.35f);
}
