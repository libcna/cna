// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-021, Phases 15 and 16: the canonical `.x` and `.fbx`
// routes.
//
// Both readers and XNA's ModelProcessor were implemented and measured before this route existed,
// and none of them was reachable: `XnaComponentNames` mapped the two importers onto names no
// registry contained, so a project naming a model built nothing and reported nothing wrong. What
// these tests hold in place is the wiring -- that the components resolve, that the nested build a
// material starts resolves too, and that XNA's own processor properties still arrive.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/BuildTimeMediaDecoder.hpp"
#include "CNA/Content/Pipeline/TextureCompressionPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnaModelSourceContentPipeline.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    /**
     * @brief A registry holding the model routes, their writer, and the texture route their
     *        materials reach.
     *
     * The encoder is not optional here: XNA's `ModelProcessor` builds a model's textures with
     * `TextureFormat` defaulting to `DxtCompressed`, so a registry without one refuses the nested
     * build rather than the model.
     */
    std::shared_ptr<Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(*registry,
                                                   Pipeline::MakeBlockCompressionTextureEncoder());
        Pipeline::RegisterModelContentPipeline(*registry);
        Pipeline::RegisterXnaModelSourceContentPipeline(*registry);
        return registry;
    }

    /** @brief A source root holding one model fixture and, when it names one, its texture. */
    class ModelSource
    {
    public:
        explicit ModelSource(const std::string& fixture, const std::string& companion = {})
            : root_(std::filesystem::temp_directory_path() / ("cna_xnapp_model_" + fixture))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(root_);
            const std::filesystem::path models = Locate("tests/assets/xna40/model");
            std::filesystem::copy_file(models / fixture, root_ / fixture,
                                       std::filesystem::copy_options::overwrite_existing);
            if (!companion.empty())
            {
                std::filesystem::copy_file(models / companion, root_ / companion,
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
        ~ModelSource()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        ModelSource(const ModelSource&) = delete;
        ModelSource& operator=(const ModelSource&) = delete;

        [[nodiscard]] const std::filesystem::path& Root() const { return root_; }

    private:
        std::filesystem::path root_;
    };

    Pipeline::ContentBuildResult BuildModel(const std::filesystem::path& root,
                                            const std::string& source,
                                            const Pipeline::ContentProcessorParameters& parameters = {})
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = source;
        request.logicalName = "Models/thing";
        request.parameters = parameters;
        request.environment.outputDirectory = root / "out";
        return pipeline.Build(request);
    }
}

// Both source extensions resolve, and to the components the XNA name mapping points at.
TEST(XnaModelSourceContentPipelineTest, BothModelExtensionsResolveToTheirOwnImporter)
{
    const std::shared_ptr<Pipeline::ContentPipelineRegistry> registry = MakeRegistry();
    EXPECT_EQ(registry->ResolveImporter("thing.x")->Identity().name, "CNA.XImporter");
    EXPECT_EQ(registry->ResolveImporter("thing.fbx")->Identity().name, "CNA.FbxImporter");
    EXPECT_EQ(registry->ResolveImporter("thing.x")->DefaultProcessor(), "CNA.XnaModelProcessor");
    EXPECT_EQ(registry->ResolveImporter("thing.fbx")->DefaultProcessor(), "CNA.XnaModelProcessor");
}

TEST(XnaModelSourceContentPipelineTest, AnXFileBecomesAProcessedModel)
{
    const ModelSource source("bare_mesh.x");
    const Pipeline::ContentBuildResult result = BuildModel(source.Root(), "bare_mesh.x");

    EXPECT_EQ(result.importer.name, "CNA.XImporter");
    EXPECT_EQ(result.processor.name, "CNA.XnaModelProcessor");
    EXPECT_EQ(result.output.assetTypeName, "Microsoft.Xna.Framework.Graphics.Model");
    EXPECT_FALSE(result.output.bytes.empty());
}

TEST(XnaModelSourceContentPipelineTest, AnFbxFileBecomesAProcessedModel)
{
    const ModelSource source("fbx_bare_mesh.fbx");
    const Pipeline::ContentBuildResult result = BuildModel(source.Root(), "fbx_bare_mesh.fbx");

    EXPECT_EQ(result.importer.name, "CNA.FbxImporter");
    EXPECT_EQ(result.processor.name, "CNA.XnaModelProcessor");
    EXPECT_EQ(result.output.assetTypeName, "Microsoft.Xna.Framework.Graphics.Model");
}

// The texture a material names is built as its own asset, which is what XNA does, and the model
// refers to it by the name a ContentManager loads rather than by a path on this machine.
TEST(XnaModelSourceContentPipelineTest, AMaterialsTextureIsBuiltAsItsOwnAsset)
{
    const ModelSource source("quad_textured.x", "surface.png");
    const Pipeline::ContentBuildResult result = BuildModel(source.Root(), "quad_textured.x");

    ASSERT_EQ(result.output.additionalOutputs.size(), 1u);
    // XNA's generated nested-asset names carry an index, so a model's texture is `surface_0`
    // rather than `surface` (measured through the genuine BuildContent,
    // tests/reference/xna40/differential/differential-oracle.json case model/x_textured).
    EXPECT_EQ(result.output.additionalOutputs[0].logicalName, "surface_0");
    EXPECT_FALSE(result.output.additionalOutputs[0].bytes.empty());
    bool referred = false;
    for (const Pipeline::RuntimeContentReference& reference : result.runtimeReferences)
    {
        if (reference.logicalName == "surface_0") { referred = true; }
    }
    EXPECT_TRUE(referred) << "the model must name the texture it was built with";
}

// A model whose material names a texture that is not there is a build failure, not a model with a
// missing texture: the nested build is the thing that fails, and its message says which file.
TEST(XnaModelSourceContentPipelineTest, AMissingTextureIsReportedAgainstTheFileThatNamesIt)
{
    const ModelSource source("quad_textured.x");
    try
    {
        (void)BuildModel(source.Root(), "quad_textured.x");
        FAIL() << "a missing texture cannot build";
    }
    catch (const std::exception& error)
    {
        EXPECT_NE(std::string(error.what()).find("surface.png"), std::string::npos) << error.what();
    }
}

// XNA's own processor properties reach the processor, by XNA's own spellings. Scale is the one
// that shows in the output without decoding it: every position is multiplied by it, so a model
// built at a different scale is a different payload.
TEST(XnaModelSourceContentPipelineTest, XnaProcessorPropertiesAreAcceptedByTheirXnaNames)
{
    const ModelSource source("bare_mesh.x");
    Pipeline::ContentProcessorParameters scaled;
    scaled.Set("Scale", std::string("2.0"));
    const Pipeline::ContentBuildResult plain = BuildModel(source.Root(), "bare_mesh.x");
    const Pipeline::ContentBuildResult big = BuildModel(source.Root(), "bare_mesh.x", scaled);

    EXPECT_NE(plain.output.bytes, big.output.bytes);

    Pipeline::ContentProcessorParameters unknown;
    unknown.Set("NoSuchProperty", std::string("1"));
    EXPECT_THROW((void)BuildModel(source.Root(), "bare_mesh.x", unknown), std::exception);
}

// Every malformed fixture is refused. The importer differentials measure what XNA says about each
// of them; what matters here is that a coordinator build fails rather than quietly writing nothing.
TEST(XnaModelSourceContentPipelineTest, EveryMalformedModelSourceIsRefused)
{
    for (const char* fixture : {"empty.x", "not_x.x", "truncated.x", "bad_version.x",
                                "index_out_of_range.x", "fbx_empty.fbx", "fbx_not_fbx.fbx",
                                "fbx_truncated.fbx"})
    {
        const ModelSource source(fixture);
        EXPECT_THROW((void)BuildModel(source.Root(), fixture), std::exception) << fixture;
    }
}

// plans/plan_xnapipeline_parity.md XNAPP-021: the video route's frame metadata.
//
// XNA's VideoProcessor reads a video's size, rate and length from the file. `cna_content` has no
// decoder and must not grow one, so the canonical importer takes a probe from whoever registers
// it. Before this the three were required parameters, which meant a `.wmv` -- the one video format
// XNA itself accepts -- could not be built by naming it and nothing else.
TEST(XnaVideoProbeTest, TheBuildTimeProbeSuppliesWhatTheProcessorWouldOtherwiseRequire)
{
    if (!CNA::Content::Pipeline::BuildTimeMedia::IsAvailable())
    {
        GTEST_SKIP() << "this build has no media decoder";
    }
    const Pipeline::VideoMetadataProbe probe =
        CNA::Content::Pipeline::BuildTimeMedia::MakeVideoMetadataProbe();
    ASSERT_TRUE(static_cast<bool>(probe));

    const std::filesystem::path media = Locate("tests/assets/xna40/media");
    const std::optional<Pipeline::ProbedVideoMetadata> read =
        probe(media / "wmv_64x48_15fps_silent.wmv");
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->width, 64u);
    EXPECT_EQ(read->height, 48u);
    EXPECT_NEAR(read->framesPerSecond, 15.0f, 0.01f);
    EXPECT_GT(read->durationMs, 0u);

    // A file the decoder cannot open answers nothing rather than throwing, so a project that knows
    // what the file contains can still describe it with parameters.
    EXPECT_FALSE(probe(media / "truncated.wmv").has_value());
    EXPECT_FALSE(probe(media / "empty.wmv").has_value());
}
