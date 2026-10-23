// SPDX-License-Identifier: MS-PL
// ISSUE-011: child acquisition, partial reads, shared references and generated textures.
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/Guid.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;
namespace Cnb = CNA::Content::Cnb;

namespace
{
    class ContentRoot
    {
        std::vector<std::string> files;
    public:
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("cna-content-child-ownership-" + System::Guid::NewGuid().ToString());
        ContentRoot()
        {
            if (!std::filesystem::create_directory(path))
                throw std::runtime_error("Cannot create ownership content root");
        }
        ~ContentRoot()
        {
            std::error_code error;
            for (const auto& name : files) std::filesystem::remove(path / name, error);
            std::filesystem::remove(path, error);
        }
        void Write(const std::string& name, const char* data, std::size_t size)
        {
            files.push_back(name);
            std::ofstream file;
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.open(path / name, std::ios::binary);
            file.write(data, static_cast<std::streamsize>(size));
        }
        void WriteText(const std::string& name, const std::string& text)
        {
            Write(name, text.data(), text.size());
        }
        void Triangle(const std::string& meshes)
        {
            const std::array<float, 24> vertices{
                -0.5f, -0.5f, 0, 0, 0, 1, 0, 1,
                 0.5f, -0.5f, 0, 0, 0, 1, 1, 1,
                 0.0f,  0.5f, 0, 0, 0, 1, 0.5f, 0};
            const std::array<std::uint16_t, 3> indices{0, 1, 2};
            Write("vertices.bin", reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
            Write("indices.bin", reinterpret_cast<const char*>(indices.data()), sizeof(indices));
            WriteText("model.cnj", R"({"cnjVersion":1,"type":"Model","meshes":[)" + meshes + "]}");
        }
    };

    std::string Mesh(const std::string& effect)
    {
        return R"({"name":"Triangle","vertices":"vertices.bin","indices":"indices.bin",
                  "vertexStride":32,"effect":")" + effect + "\"}";
    }

    class CountingEffect final : public BasicEffect
    {
    public:
        int disposeCalls = 0;
        explicit CountingEffect(GraphicsDevice& device) : BasicEffect(device) { }
    protected:
        void Dispose(bool disposing) override
        {
            if (disposing) ++disposeCalls;
            BasicEffect::Dispose(disposing);
        }
    };

    class SharedEffectReader final : public LooseFileContentTypeReader<std::shared_ptr<Effect>>
    {
        std::shared_ptr<Effect> effect;
    public:
        explicit SharedEffectReader(std::shared_ptr<Effect> value) : effect(std::move(value)) { }
        std::vector<std::string> GetExtensions() const override { return {".effect"}; }
        std::shared_ptr<Effect> Read(const std::string&, ContentManager&) override { return effect; }
    };

    template<class TEffect>
    void CheckModelTexture(const char* asset, bool white)
    {
        GraphicsDevice device;
        ContentManager manager(nullptr, "tests/assets/gltf");
        manager.setGraphicsDevice(device);
        auto model = manager.Load<Model>(asset);
        ASSERT_GT(model.getMeshesProperty().getCountProperty(), 0);
        auto* mesh = model.getMeshesProperty()[0];
        ASSERT_GT(mesh->getMeshPartsProperty().getCountProperty(), 0);
        auto* effect = dynamic_cast<TEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
        ASSERT_NE(effect, nullptr);
        const auto* texture = effect->getTextureProperty();
        ASSERT_NE(texture, nullptr);
        Texture2D copied(*texture);
        std::vector<Color> pixels(texture->getWidthProperty() * texture->getHeightProperty());
        ASSERT_FALSE(pixels.empty());
        ASSERT_NO_THROW(copied.GetData(pixels.data(), static_cast<int>(pixels.size())));
        if (white)
        {
            EXPECT_EQ(pixels[0], Color::White);
        }
        const auto renderer = texture->GetRendererWeak();
        manager.Unload();
        EXPECT_TRUE(texture->getIsDisposedProperty());
        EXPECT_TRUE(copied.getIsDisposedProperty());
        EXPECT_TRUE(renderer.expired());
        EXPECT_THROW(copied.GetData(pixels.data(), static_cast<int>(pixels.size())), System::ObjectDisposedException);
    }
}

TEST(ContentModelChildOwnershipTest, CnbV2DisposesSharedBuffersAndAllFiveStockEffects)
{
    ContentRoot root;
    Cnb::CnbModelV2Data data;
    data.bones.emplace_back();
    data.bones[0].name = "Root";
    Cnb::CnbModelV2VertexDeclaration declaration;
    declaration.vertexStride = 12;
    declaration.elements = {{0, Cnb::CnbModelV2VertexFormat::Vector3,
                             Cnb::CnbModelV2VertexUsage::Position, 0}};
    data.vertexDeclarations = {declaration};
    data.vertexBuffers = {{0, 3, std::vector<std::uint8_t>(36)}};
    data.indexBuffers = {{2, 3, {0, 0, 1, 0, 2, 0}}};
    Cnb::CnbModelV2Mesh mesh;
    mesh.name = "Shared";
    mesh.parentBone = 0;
    for (const auto kind : {Cnb::CnbModelV2EffectKind::BasicEffect,
         Cnb::CnbModelV2EffectKind::SkinnedEffect, Cnb::CnbModelV2EffectKind::DualTextureEffect,
         Cnb::CnbModelV2EffectKind::AlphaTestEffect, Cnb::CnbModelV2EffectKind::EnvironmentMapEffect})
    {
        Cnb::CnbModelV2Effect effect;
        effect.kind = kind;
        if (kind == Cnb::CnbModelV2EffectKind::SkinnedEffect) effect.weightsPerVertex = 4;
        mesh.parts.push_back({0, 3, 0, 1, 0, 0, static_cast<std::uint32_t>(data.effects.size())});
        data.effects.push_back(effect);
    }
    data.meshes = {mesh};
    const auto bytes = Cnb::EncodeModelV2ToCnb(data, "model");
    root.Write("model.cnb", reinterpret_cast<const char*>(bytes.data()), bytes.size());
    GraphicsDevice device;
    ContentManager manager(nullptr, root.path.string());
    manager.setGraphicsDevice(device);
    auto model = manager.Load<Model>("model");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    const auto& parts = model.getMeshesProperty()[0]->getMeshPartsProperty();
    ASSERT_EQ(parts.getCountProperty(), 5);
    std::set<GraphicsResource*> resources;
    for (int i = 0; i < parts.getCountProperty(); ++i)
    {
        resources.insert(parts[i]->getVertexBufferProperty());
        resources.insert(parts[i]->getIndexBufferProperty());
        resources.insert(parts[i]->getEffectProperty());
    }
    ASSERT_EQ(resources.size(), 7U);
    for (auto* resource : resources)
    {
        ASSERT_NE(resource, nullptr);
        ASSERT_FALSE(resource->getIsDisposedProperty());
    }
    manager.Unload();
    for (auto* resource : resources) EXPECT_TRUE(resource->getIsDisposedProperty());
}

TEST(ContentModelChildOwnershipTest, FailedModelReadRetainsCompletedChildrenUntilUnload)
{
    ContentRoot root;
    root.Triangle(Mesh("BasicEffect") +
        R"(,{"name":"Missing","vertices":"missing.bin","indices":"indices.bin",
             "vertexStride":32,"effect":"BasicEffect"})");
    GraphicsDevice device;
    ContentManager manager(nullptr, root.path.string());
    manager.setGraphicsDevice(device);
    const auto before = device.GetTrackedResourceCount();
    EXPECT_THROW((void)manager.Load<Model>("model"), ContentLoadException);
    EXPECT_EQ(device.GetTrackedResourceCount(), before + 3);
    manager.Unload();
    EXPECT_EQ(device.GetTrackedResourceCount(), before);
}

TEST(ContentModelChildOwnershipTest, ReusedExternalEffectIsNotRecordedAgainByEachMesh)
{
    ContentRoot root;
    root.Triangle(Mesh("shared.effect") + "," + Mesh("shared.effect"));
    root.WriteText("shared.effect", "custom reader fixture");
    GraphicsDevice device;
    auto effect = std::make_shared<CountingEffect>(device);
    ContentManager manager(nullptr, root.path.string());
    manager.setGraphicsDevice(device);
    manager.RegisterTypeReader<std::shared_ptr<Effect>>(std::make_unique<SharedEffectReader>(effect));
    auto model = manager.Load<Model>("model");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 2);
    for (int i = 0; i < 2; ++i)
        EXPECT_EQ(model.getMeshesProperty()[i]->getMeshPartsProperty()[0]->getEffectProperty(), effect.get());
    EXPECT_EQ(effect->disposeCalls, 0);
    manager.Unload();
    EXPECT_EQ(effect->disposeCalls, 1);
    EXPECT_TRUE(effect->getIsDisposedProperty());
}

TEST(ContentModelChildOwnershipTest, EmbeddedGltfTextureAndValueCopyAreDisposed)
{
    CheckModelTexture<PbrEffect>("mat-basecolor-factor-times-texture", false);
}

TEST(ContentModelChildOwnershipTest, GeneratedWhiteSkinnedTextureAndValueCopyAreDisposed)
{
    CheckModelTexture<SkinnedEffect>("skin-unlit", true);
}
