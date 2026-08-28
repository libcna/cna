// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_model_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    std::filesystem::path FindFixture(const std::string& name)
    {
        for (const char* prefix : {"tests/assets/gltf/", "../tests/assets/gltf/",
                                   "../../tests/assets/gltf/"})
        {
            const std::filesystem::path candidate = std::string(prefix) + name;
            if (std::filesystem::exists(candidate))
            {
                return std::filesystem::weakly_canonical(candidate);
            }
        }
        return {};
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterModelContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentBuildResult BuildModel(const std::filesystem::path& fixture,
                                            const std::string& logicalName)
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = fixture.parent_path();
        request.source = fixture;
        request.logicalName = logicalName;
        return pipeline.Build(request);
    }
}

TEST(ModelContentPipelineTest, RunsDistinctHeadlessStagesAndReusesTheFrozenModelCodec)
{
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "glTF fixture not found"; }

    const Pipeline::ContentBuildResult result = BuildModel(fixture, "asset");
    EXPECT_EQ(result.importer, (Pipeline::ContentComponentIdentity{"CNA.GltfImporter", "1"}));
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.ModelProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.ModelContentWriter", "1"}));
    EXPECT_EQ(result.output.assetTypeId, Cnb::CnbAssetTypeId::Model);
    EXPECT_EQ(result.output.assetTypeName, "Microsoft.Xna.Framework.Graphics.Model");
    ASSERT_EQ(result.dependencies.size(), 1u);
    EXPECT_EQ(result.dependencies[0].kind, Pipeline::ContentDependencyKind::PrimarySource);
    EXPECT_TRUE(result.runtimeReferences.empty());

    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(result.output.bytes, "pipeline asset.cnb");
    EXPECT_EQ(document.Metadata().contentName, "asset");
    const Cnb::CnbModelData decoded = Cnb::DecodeModelFromCnb(document);
    ASSERT_FALSE(decoded.parts.empty());
    ASSERT_FALSE(decoded.meshes.empty());
    ASSERT_TRUE(decoded.skeleton.has_value());
    EXPECT_EQ(decoded.parts[0].vertexCount, 3u);

    EXPECT_EQ(Pipeline::ContentSha256(result.output.bytes),
              "9f432dff5a02ee2092ffc4c04e72e91505d1365ebec6274b15cf6dbba7d0276b")
        << "the pipeline changed the Model bytes pinned before the shared glTF refactor";
}

TEST(ModelContentPipelineTest, IsByteIdenticalToTheExistingDirectGltfProducer)
{
#if !defined(CNA_GLTF_TO_CNB_TOOL_PATH)
    GTEST_SKIP() << "the direct glTF producer was not built";
#else
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "glTF fixture not found"; }
    ScratchDirectory scratch("oracle");

    const std::string command = std::string(CNA_GLTF_TO_CNB_TOOL_PATH) + " " +
                                fixture.string() + " " + scratch.Path().string() +
                                " asset --quiet >/dev/null 2>&1";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const Pipeline::ContentBuildResult first = BuildModel(fixture, "asset");
    const Pipeline::ContentBuildResult second = BuildModel(fixture, "asset");
    EXPECT_EQ(first.output.bytes, second.output.bytes);
    EXPECT_EQ(first.output.bytes, ReadBytes(scratch.Path() / "asset.cnb"));
#endif
}

TEST(ModelContentPipelineTest, ReportsSourceDependenciesSeparatelyFromRuntimeXrefs)
{
    const std::filesystem::path fixture = FindFixture("gltf-external-image.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "glTF fixture not found"; }

    const Pipeline::ContentBuildResult result =
        BuildModel(fixture, "Models/gltf-external-image");
    ASSERT_EQ(result.dependencies.size(), 2u);
    EXPECT_EQ(result.dependencies[0].kind, Pipeline::ContentDependencyKind::PrimarySource);
    EXPECT_EQ(result.dependencies[1].kind, Pipeline::ContentDependencyKind::SourceFile);
    EXPECT_EQ(std::filesystem::path(result.dependencies[1].identity).filename(),
              "gltf-external-image.texture.png");

    ASSERT_EQ(result.runtimeReferences.size(), 1u);
    EXPECT_EQ(result.runtimeReferences[0].logicalName,
              "gltf-external-image_tex0.png");
    EXPECT_EQ(result.runtimeReferences[0].expectedAssetTypeId, 0u);
    EXPECT_NE(result.dependencies[1].identity, result.runtimeReferences[0].logicalName);
}

TEST(ModelContentPipelineTest, ResultLoadsThroughContentManagerWhenTheRendererSupports3D)
{
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "glTF fixture not found"; }
    const Pipeline::ContentBuildResult result = BuildModel(fixture, "Models/asset");

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
    {
        GTEST_SKIP() << "configured renderer has no 3D pipeline";
    }

    ScratchDirectory scratch("runtime");
    WriteBytes(scratch.Path() / "Models" / "asset.cnb", result.output.bytes);
    ContentManager content(nullptr, scratch.Path().string());
    content.setGraphicsDevice(device);
    const Model model = content.Load<Model>("Models/asset");
    EXPECT_GT(model.getMeshesProperty().getCountProperty(), 0);
}

TEST(ModelContentPipelineTest, RejectsParametersAtTheProcessorBoundary)
{
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "glTF fixture not found"; }

    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = fixture.parent_path();
    request.source = fixture;
    request.logicalName = "asset";
    request.parameters.Set("unitScale", 0.01);
    try
    {
        static_cast<void>(pipeline.Build(request));
        FAIL() << "ModelProcessor accepted an unsupported parameter";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
        EXPECT_EQ(error.Component(), "CNA.ModelProcessor");
    }
}
