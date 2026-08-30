// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/ContentPath.hpp"
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

    std::filesystem::path Utf8Path(std::string_view value)
    {
        return CNA::Internal::ContentPathFromUtf8(value);
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterModelContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentBuildResult BuildModel(const std::filesystem::path& fixture,
                                            const std::string& logicalName,
                                            std::optional<bool> generateChildAssets = std::nullopt)
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = fixture.parent_path();
        request.source = fixture;
        request.logicalName = logicalName;
        if (generateChildAssets.has_value())
        {
            request.parameters.Set(Pipeline::ModelGenerateChildAssetsParameter,
                                   *generateChildAssets);
        }
        return pipeline.Build(request);
    }
}

TEST(ModelContentPipelineTest, RunsDistinctHeadlessStagesAndReusesTheFrozenModelCodec)
{
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "glTF fixture not found"; }

    const Pipeline::ContentBuildResult result = BuildModel(fixture, "asset");
    EXPECT_EQ(result.importer, (Pipeline::ContentComponentIdentity{"CNA.GltfImporter", "2"}));
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.ModelProcessor", "3"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.ModelContentWriter", "3"}));
    EXPECT_EQ(result.output.assetSchemaVersion, Cnb::CnbModelSchemaVersion);
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

TEST(ModelContentPipelineTest, AnimatedSingleModelKeepsEmbeddedClipsWithoutChangingPrimaryBytes)
{
#if !defined(CNA_GLTF_TO_CNB_TOOL_PATH)
    GTEST_SKIP() << "the direct glTF producer was not built";
#else
    const std::filesystem::path fixture = FindFixture("anim-two-clips.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "animated glTF fixture not found"; }
    ScratchDirectory scratch("animated_oracle");

    const std::string command = std::string(CNA_GLTF_TO_CNB_TOOL_PATH) + " " +
                                fixture.string() + " " + scratch.Path().string() +
                                " actor --quiet >/dev/null 2>&1";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const Pipeline::ContentBuildResult result = BuildModel(fixture, "actor");
    EXPECT_TRUE(result.output.additionalOutputs.empty());
    EXPECT_EQ(result.output.bytes, ReadBytes(scratch.Path() / "actor.cnb"));
    const Cnb::CnbModelData model = Cnb::DecodeModelFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "animated model.cnb"));
    ASSERT_EQ(model.animations.size(), 2u);
    EXPECT_EQ(model.animations[0].name, "Walk");
    EXPECT_EQ(model.animations[1].name, "Clip1");
#endif
}

TEST(ModelContentPipelineTest, GeneratedChildModePublishesEquivalentStandaloneAnimationClips)
{
    const std::filesystem::path fixture = FindFixture("anim-two-clips.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "animated glTF fixture not found"; }

    const Pipeline::ContentBuildResult result =
        BuildModel(fixture, "Models/actor", true);
    const Cnb::CnbModelData model = Cnb::DecodeModelFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "animated model bundle.cnb"));
    ASSERT_EQ(model.animations.size(), 2u);
    ASSERT_EQ(result.output.additionalOutputs.size(), 2u);

    const std::map<std::string, const Cnb::CnbModelAnimation*> embedded = {
        {"Models/actor_Clip1", &model.animations[1]},
        {"Models/actor_Walk", &model.animations[0]}};
    for (const Pipeline::ContentAdditionalWriteOutput& child :
         result.output.additionalOutputs)
    {
        ASSERT_EQ(child.assetTypeId, Cnb::CnbAssetTypeId::AnimationClip);
        const auto expected = embedded.find(child.logicalName);
        ASSERT_NE(expected, embedded.end()) << child.logicalName;
        const auto decoded = Cnb::DecodeAnimationClipFromCnb(
            Cnb::CnbDocument::Parse(child.bytes, child.logicalName + ".cnb"));
        EXPECT_EQ(decoded.Duration.getTicksProperty(),
                  expected->second->clip.Duration.getTicksProperty());
        EXPECT_EQ(decoded.TargetSpace, expected->second->clip.TargetSpace);
        EXPECT_EQ(decoded.Tracks.size(), expected->second->clip.Tracks.size());
    }
}

TEST(ModelContentPipelineTest, MultiModelGltfRequiresOptInAndPublishesEveryGroupDeterministically)
{
    const std::filesystem::path fixture = FindFixture("skin-plus-static-mesh.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "multi-group glTF fixture not found"; }

    try
    {
        static_cast<void>(BuildModel(fixture, "Models/compound"));
        FAIL() << "multi-Model glTF built without explicit child-output policy";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
        EXPECT_NE(std::string(error.what()).find("produced 2 Model documents"),
                  std::string::npos) << error.what();
        EXPECT_NE(std::string(error.what()).find("generateChildAssets"),
                  std::string::npos) << error.what();
    }

    const Pipeline::ContentBuildResult first =
        BuildModel(fixture, "Models/compound", true);
    const Pipeline::ContentBuildResult second =
        BuildModel(fixture, "Models/compound", true);
    EXPECT_EQ(first.output.bytes, second.output.bytes);
    ASSERT_EQ(first.output.additionalOutputs.size(), 1u);
    EXPECT_EQ(first.output.additionalOutputs[0].logicalName, "Models/compound_static");
    EXPECT_EQ(first.output.additionalOutputs[0].assetTypeId, Cnb::CnbAssetTypeId::Model);
    EXPECT_EQ(first.output.additionalOutputs[0].bytes,
              second.output.additionalOutputs[0].bytes);

    const Cnb::CnbModelData primary = Cnb::DecodeModelFromCnb(
        Cnb::CnbDocument::Parse(first.output.bytes, "primary multi-Model output"));
    const Cnb::CnbModelData child = Cnb::DecodeModelFromCnb(
        Cnb::CnbDocument::Parse(first.output.additionalOutputs[0].bytes,
                                "static multi-Model child"));
    EXPECT_TRUE(primary.skeleton.has_value());
    EXPECT_FALSE(child.skeleton.has_value());
    EXPECT_FALSE(primary.meshes.empty());
    EXPECT_FALSE(child.meshes.empty());
}

TEST(ModelContentPipelineTest, CollidingSanitizedSkinNamesAreRejectedBeforeAnyModelIsSelected)
{
    const std::filesystem::path fixture = FindFixture("skin-plus-static-mesh.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "multi-group glTF fixture not found"; }
    ScratchDirectory scratch("colliding_skin_names");
    const std::filesystem::path source = scratch.Path() / "collision.gltf";

    const std::vector<std::uint8_t> original = ReadBytes(fixture);
    std::string json(original.begin(), original.end());
    const std::string authored = "\"name\": \"Skin\"";
    const std::size_t position = json.find(authored);
    ASSERT_NE(position, std::string::npos);
    json.replace(position, authored.size(), "\"name\": \"static\"");
    WriteBytes(source, std::vector<std::uint8_t>(json.begin(), json.end()));
    for (const char* sidecar : {"skin-plus-static-mesh.vb.bin",
                                "skin-plus-static-mesh.ib.bin",
                                "skin-plus-static-mesh.p1.vb.bin",
                                "skin-plus-static-mesh.p1.ib.bin"})
    {
        WriteBytes(scratch.Path() / sidecar, ReadBytes(fixture.parent_path() / sidecar));
    }

    try
    {
        static_cast<void>(BuildModel(source, "Models/collision", true));
        FAIL() << "colliding sanitized Model names were silently selected or overwritten";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Import);
        EXPECT_NE(std::string(error.what()).find("collide after skin-name filename sanitization"),
                  std::string::npos) << error.what();
    }
}

TEST(ModelContentPipelineTest, GeneratedTextureChildrenAreCompiledAndReferencesAreRemapped)
{
    for (const char* fixtureName : {"gltf-external-image.gltf", "gltf-data-uri-image.gltf"})
    {
        const std::filesystem::path fixture = FindFixture(fixtureName);
        if (fixture.empty()) { GTEST_SKIP() << fixtureName << " fixture not found"; }

        const Pipeline::ContentBuildResult baseline =
            BuildModel(fixture, "Models/car");
        const Pipeline::ContentBuildResult generated =
            BuildModel(fixture, "Models/car", true);
        ASSERT_FALSE(baseline.runtimeReferences.empty()) << fixtureName;
        ASSERT_FALSE(generated.runtimeReferences.empty()) << fixtureName;
        ASSERT_EQ(generated.output.additionalOutputs.size(), 1u) << fixtureName;
        const Pipeline::ContentAdditionalWriteOutput& texture =
            generated.output.additionalOutputs.front();
        EXPECT_EQ(texture.logicalName, "Models/car_tex0.png") << fixtureName;
        EXPECT_EQ(texture.assetTypeId, Cnb::CnbAssetTypeId::Texture2D) << fixtureName;
        EXPECT_EQ(generated.runtimeReferences.front().logicalName,
                  texture.logicalName) << fixtureName;
        EXPECT_EQ(generated.runtimeReferences.front().expectedAssetTypeId,
                  Cnb::CnbAssetTypeId::Texture2D) << fixtureName;
        EXPECT_NE(baseline.runtimeReferences.front().logicalName,
                  generated.runtimeReferences.front().logicalName) << fixtureName;

        const Cnb::CnbDocument textureDocument =
            Cnb::CnbDocument::Parse(texture.bytes, texture.logicalName + ".cnb");
        const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(textureDocument);
        EXPECT_EQ(decoded.width, 16u) << fixtureName;
        EXPECT_EQ(decoded.height, 16u) << fixtureName;

        const Cnb::CnbModelData model = Cnb::DecodeModelFromCnb(
            Cnb::CnbDocument::Parse(generated.output.bytes, "generated texture model"));
        ASSERT_FALSE(model.parts.empty()) << fixtureName;
        EXPECT_EQ(model.parts.front().material.baseColorTexture,
                  texture.logicalName) << fixtureName;
    }
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

TEST(ModelContentPipelineTest, PreservesUnicodeNativePathsThroughGltfAndItsSidecars)
{
    const std::filesystem::path fixture = FindFixture("gltf-external-image.gltf");
    const std::filesystem::path vertexFixture =
        FindFixture("gltf-external-image.vb.bin");
    const std::filesystem::path indexFixture =
        FindFixture("gltf-external-image.ib.bin");
    const std::filesystem::path imageFixture =
        FindFixture("gltf-external-image.texture.png");
    if (fixture.empty() || vertexFixture.empty() || indexFixture.empty() || imageFixture.empty())
    {
        GTEST_SKIP() << "external glTF fixtures not found";
    }

    ScratchDirectory scratch("unicode");
    const std::filesystem::path sourceRoot = scratch.Path() / Utf8Path("kořen_根");
    const std::filesystem::path sourceDirectory =
        sourceRoot / Utf8Path("vnoření_内") / Utf8Path("modely_模型");
    const std::filesystem::path gltfPath =
        sourceDirectory / Utf8Path("žluťoučký_模型.gltf");
    const std::string bufferUri = "geometrie_形/údaje_ß.bin";
    const std::string imageUri = "textury_图/obrázek_壁.png";

    const std::vector<std::uint8_t> sourceBytes = ReadBytes(fixture);
    std::string source(sourceBytes.begin(), sourceBytes.end());
    const std::string originalImageUri = "gltf-external-image.texture.png";
    const std::size_t imagePosition = source.find(originalImageUri);
    ASSERT_NE(imagePosition, std::string::npos);
    source.replace(imagePosition, originalImageUri.size(), imageUri);

    const std::size_t buffersPosition = source.find("\"buffers\": [");
    ASSERT_NE(buffersPosition, std::string::npos);
    const std::string uriPrefix = "\"uri\": \"";
    const std::size_t uriPosition = source.find(uriPrefix, buffersPosition);
    ASSERT_NE(uriPosition, std::string::npos);
    const std::size_t uriValuePosition = uriPosition + uriPrefix.size();
    const std::size_t uriEnd = source.find('"', uriValuePosition);
    ASSERT_NE(uriEnd, std::string::npos);
    source.replace(uriValuePosition, uriEnd - uriValuePosition, bufferUri);

    std::vector<std::uint8_t> geometry = ReadBytes(vertexFixture);
    const std::vector<std::uint8_t> indices = ReadBytes(indexFixture);
    geometry.insert(geometry.end(), indices.begin(), indices.end());
    ASSERT_EQ(geometry.size(), 150u);
    const std::vector<std::uint8_t> rewrittenSource(source.begin(), source.end());
    WriteBytes(gltfPath, rewrittenSource);
    WriteBytes(sourceDirectory / Utf8Path(bufferUri), geometry);
    WriteBytes(sourceDirectory / Utf8Path(imageUri), ReadBytes(imageFixture));

    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = sourceRoot;
    request.source = gltfPath;
    request.logicalName = "Models/unicode";
    const Pipeline::ContentBuildResult first = pipeline.Build(request);
    const Pipeline::ContentBuildResult second = pipeline.Build(request);

    EXPECT_EQ(first.output.bytes, second.output.bytes);
    ASSERT_EQ(first.dependencies.size(), 3u);
    std::vector<std::string> dependencyNames;
    for (const Pipeline::ContentDependency& dependency : first.dependencies)
    {
        if (dependency.kind == Pipeline::ContentDependencyKind::SourceFile)
        {
            dependencyNames.push_back(CNA::Internal::ContentPathToUtf8(
                CNA::Internal::ContentPathFromUtf8(dependency.identity).filename()));
        }
    }
    EXPECT_NE(std::find(dependencyNames.begin(), dependencyNames.end(), "údaje_ß.bin"),
              dependencyNames.end());
    EXPECT_NE(std::find(dependencyNames.begin(), dependencyNames.end(), "obrázek_壁.png"),
              dependencyNames.end());

    ASSERT_EQ(first.runtimeReferences.size(), 1u);
    EXPECT_EQ(first.runtimeReferences[0].logicalName, "žluťoučký_模型_tex0.png");
    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(first.output.bytes, "unicode model.cnb");
    const Cnb::CnbModelData decoded = Cnb::DecodeModelFromCnb(document);
    ASSERT_EQ(decoded.parts.size(), 1u);
    EXPECT_EQ(decoded.parts[0].vertexCount, 3u);
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

    request.parameters = {};
    request.parameters.Set(Pipeline::ModelGenerateChildAssetsParameter,
                           std::string("true"));
    try
    {
        static_cast<void>(pipeline.Build(request));
        FAIL() << "ModelProcessor accepted a mistyped child-output option";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
        EXPECT_NE(std::string(error.what()).find("must be a bool"), std::string::npos)
            << error.what();
    }
}
