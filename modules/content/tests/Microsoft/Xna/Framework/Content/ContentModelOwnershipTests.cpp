// SPDX-License-Identifier: MS-PL
// ISSUE-011: a non-disposable Model root still owns disposable GPU objects.
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include "System/Guid.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct ModelAsset
    {
        const char* label;
        const char* root;
        const char* name;
    };
    class ContentModelOwnershipTest : public ::testing::TestWithParam<ModelAsset> { };

    class CnjFixture
    {
    public:
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("cna-content-ownership-" + System::Guid::NewGuid().ToString());
        CnjFixture()
        {
            if (!std::filesystem::create_directory(root))
                throw std::runtime_error("Cannot create the ownership fixture directory");
            const std::array<float, 24> vertices{
                -0.5f, -0.5f, 0, 0, 0, 1, 0, 1,
                 0.5f, -0.5f, 0, 0, 0, 1, 1, 1,
                 0.0f,  0.5f, 0, 0, 0, 1, 0.5f, 0};
            const std::array<std::uint16_t, 3> indices{0, 1, 2};
            Write("vertices.bin", reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
            Write("indices.bin", reinterpret_cast<const char*>(indices.data()), sizeof(indices));
            const std::string descriptor = R"({"cnjVersion":1,"type":"Model","meshes":[{
                "name":"Triangle","vertices":"vertices.bin","indices":"indices.bin",
                "vertexStride":32,"effect":"BasicEffect"}]})";
            Write("triangle.cnj", descriptor.data(), descriptor.size());
        }
        ~CnjFixture()
        {
            std::error_code error;
            for (const char* name : {"triangle.cnj", "vertices.bin", "indices.bin"})
                std::filesystem::remove(root / name, error);
            std::filesystem::remove(root, error);
        }
    private:
        void Write(const char* name, const char* bytes, std::size_t count)
        {
            std::ofstream stream;
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            stream.open(root / name, std::ios::binary);
            stream.write(bytes, static_cast<std::streamsize>(count));
        }
    };
}

TEST_P(ContentModelOwnershipTest, UnloadDisposesEveryMaterializedBufferAndEffect)
{
    GraphicsDevice device;
    const auto fixture = std::string(GetParam().label) == "Cnj"
        ? std::make_unique<CnjFixture>() : nullptr;
    ContentManager manager(nullptr, fixture ? fixture->root.string() : GetParam().root);
    manager.setGraphicsDevice(device);
    auto model = manager.Load<Model>(GetParam().name);
    std::set<GraphicsResource*> resources;
    const auto& meshes = model.getMeshesProperty();
    ASSERT_GT(meshes.getCountProperty(), 0);
    for (int m = 0; m < meshes.getCountProperty(); ++m)
    {
        const auto& parts = meshes[m]->getMeshPartsProperty();
        for (int p = 0; p < parts.getCountProperty(); ++p)
        {
            auto* part = parts[p];
            if (auto* buffer = part->getVertexBufferProperty()) resources.insert(buffer);
            if (auto* buffer = part->getIndexBufferProperty()) resources.insert(buffer);
            if (auto* effect = part->getEffectProperty()) resources.insert(effect);
        }
    }
    ASSERT_GE(resources.size(), 3U);
    for (const auto* resource : resources)
        ASSERT_FALSE(resource->getIsDisposedProperty()) << resource->GetTypeName();
    manager.Unload();
    for (const auto* resource : resources)
        EXPECT_TRUE(resource->getIsDisposedProperty()) << resource->GetTypeName();
}

INSTANTIATE_TEST_SUITE_P(Formats, ContentModelOwnershipTest,
    ::testing::Values(
        ModelAsset{"Xnb", "tests/assets/xnb/monogame/windows/uncompressed", "BlenderDefaultCube"},
        ModelAsset{"Cnb", "tests/assets/cnb/skinned", "skinned_model"},
        ModelAsset{"Cnj", "", "triangle"},
        ModelAsset{"Gltf", "tests/assets/gltf", "skin-four-weighted"}),
    [](const ::testing::TestParamInfo<ModelAsset>& info) { return std::string(info.param.label); });
