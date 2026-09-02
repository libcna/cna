// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-013: the first interoperability milestone, end to end.
//
//     source PNG -> ImageImporter -> TextureProcessor -> Texture2D .xnb -> ContentManager
//
// The same importer and the same processor also feed the CNB route, and both routes are built
// from the identical source in one test, so a divergence between the two output formats shows up
// as a failure here rather than as a surprise later.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutput.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Pipeline = CNA::Content::Pipeline;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    /** @brief A unique directory tree removed when the test finishes. */
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            static std::atomic_uint counter{0u};
            path_ = std::filesystem::temp_directory_path() /
                    ("cna_xnb_pipeline_" + std::to_string(counter.fetch_add(1u)) + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

    private:
        std::filesystem::path path_;
    };

    class XnbOutputPipelineTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();

            registry_ = std::make_shared<Pipeline::ContentPipelineRegistry>();
            Pipeline::RegisterTexture2DContentPipeline(*registry_);
            Pipeline::RegisterSoundEffectContentPipeline(*registry_);
            Pipeline::RegisterBuiltInXnbAssetWriters(*registry_);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        /** @brief Writes a 4x4 PNG whose pixels are a known, non-uniform pattern. */
        [[nodiscard]] std::filesystem::path WriteSourcePng(const std::filesystem::path& directory,
                                                           const std::string& name)
        {
            std::vector<std::uint8_t> pixels(kWidth * kHeight * 4u);
            for (unsigned y = 0u; y < kHeight; ++y)
            {
                for (unsigned x = 0u; x < kWidth; ++x)
                {
                    const std::size_t offset = (static_cast<std::size_t>(y) * kWidth + x) * 4u;
                    pixels[offset + 0u] = static_cast<std::uint8_t>(16u * x + 1u);
                    pixels[offset + 1u] = static_cast<std::uint8_t>(16u * y + 2u);
                    pixels[offset + 2u] = static_cast<std::uint8_t>(16u * (x + y) + 3u);
                    pixels[offset + 3u] = 255u;
                }
            }
            expectedPixels_ = pixels;

            const std::filesystem::path path = directory / (name + ".png");
            CNA::Internal::Graphics::ImageLoader::SavePng(
                pixels.data(), static_cast<int>(kWidth), static_cast<int>(kHeight),
                path.string());
            return path;
        }

        static constexpr unsigned kWidth = 4u;
        static constexpr unsigned kHeight = 4u;

        std::shared_ptr<Pipeline::ContentPipelineRegistry> registry_;
        std::vector<std::uint8_t> expectedPixels_;
    };
}

TEST_F(XnbOutputPipelineTest, APngBuildsToXnbAndLoadsBackThroughContentManager)
{
    const TemporaryDirectory sources;
    const TemporaryDirectory output;
    const std::filesystem::path png = WriteSourcePng(sources.Path(), "player");

    Pipeline::ContentPipeline pipeline(registry_);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = sources.Path();
    request.source = png;
    request.logicalName = "player";
    request.outputFormat = Pipeline::ContentOutputFormat::Xnb;

    const Pipeline::ContentBuildResult result = pipeline.Build(request);

    EXPECT_EQ(result.outputFormat, Pipeline::ContentOutputFormat::Xnb);
    ASSERT_NE(result.xnbOutput, nullptr);
    EXPECT_TRUE(result.output.bytes.empty()) << "the CNB output must stay untouched";
    // Real XNA content assembly-qualifies a reader that does not live in Microsoft.Xna.Framework,
    // because Type.GetType() would otherwise fail to find it; CNA writes the same spelling.
    EXPECT_EQ(result.xnbOutput->rootReaderName,
              "Microsoft.Xna.Framework.Content.Texture2DReader, "
              "Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=842cf8be1de50553");
    EXPECT_EQ(result.writer.name, "CNA.Xnb.Texture2DWriter");
    EXPECT_EQ(result.importer.name, "CNA.ImageImporter")
        << "the .xnb route must reuse the same importer as the .cnb route";
    ASSERT_GE(result.xnbOutput->bytes.size(), 10u);
    EXPECT_EQ(result.xnbOutput->bytes[0], 'X');

    // Publish it the way a build tool would, then load it exactly as a game does.
    const std::filesystem::path xnb = output.Path() / "player.xnb";
    {
        std::ofstream file(xnb, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(result.xnbOutput->bytes.data()),
                   static_cast<std::streamsize>(result.xnbOutput->bytes.size()));
    }

    GraphicsDevice device;
    ContentManager content(nullptr, output.Path().string());
    content.setGraphicsDevice(device);

    Texture2D texture = content.Load<Texture2D>("player");
    EXPECT_EQ(texture.getWidthProperty(), static_cast<int>(kWidth));
    EXPECT_EQ(texture.getHeightProperty(), static_cast<int>(kHeight));
    EXPECT_EQ(texture.getLevelCountProperty(), 1);
    EXPECT_EQ(texture.getFormatProperty(), SurfaceFormat::Color);

    std::vector<Color> loaded(static_cast<std::size_t>(kWidth) * kHeight);
    texture.GetData(loaded.data(), 0, static_cast<int>(loaded.size()));
    for (std::size_t index = 0u; index < loaded.size(); ++index)
    {
        EXPECT_EQ(loaded[index].getRProperty(), expectedPixels_[index * 4u + 0u]) << index;
        EXPECT_EQ(loaded[index].getGProperty(), expectedPixels_[index * 4u + 1u]) << index;
        EXPECT_EQ(loaded[index].getBProperty(), expectedPixels_[index * 4u + 2u]) << index;
        EXPECT_EQ(loaded[index].getAProperty(), expectedPixels_[index * 4u + 3u]) << index;
    }
}

TEST_F(XnbOutputPipelineTest, TheSameSourceAndProcessorFeedBothOutputFormats)
{
    const TemporaryDirectory sources;
    const std::filesystem::path png = WriteSourcePng(sources.Path(), "shared");

    Pipeline::ContentPipeline pipeline(registry_);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = sources.Path();
    request.source = png;
    request.logicalName = "shared";

    request.outputFormat = Pipeline::ContentOutputFormat::Cnb;
    const Pipeline::ContentBuildResult cnb = pipeline.Build(request);

    request.outputFormat = Pipeline::ContentOutputFormat::Xnb;
    const Pipeline::ContentBuildResult xnb = pipeline.Build(request);

    EXPECT_EQ(cnb.importer, xnb.importer);
    EXPECT_EQ(cnb.processor, xnb.processor);
    EXPECT_NE(cnb.writer, xnb.writer) << "only the serializer differs between formats";
    EXPECT_EQ(cnb.dependencies, xnb.dependencies);
    EXPECT_EQ(cnb.outputFormat, Pipeline::ContentOutputFormat::Cnb);
    EXPECT_FALSE(cnb.output.bytes.empty());
    EXPECT_EQ(cnb.xnbOutput, nullptr);
    EXPECT_EQ(xnb.outputFormat, Pipeline::ContentOutputFormat::Xnb);
    ASSERT_NE(xnb.xnbOutput, nullptr);
    EXPECT_FALSE(xnb.xnbOutput->bytes.empty());
}

TEST_F(XnbOutputPipelineTest, XnbOutputIsDeterministicAcrossBuilds)
{
    const TemporaryDirectory sources;
    const std::filesystem::path png = WriteSourcePng(sources.Path(), "deterministic");

    Pipeline::ContentPipeline pipeline(registry_);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = sources.Path();
    request.source = png;
    request.logicalName = "deterministic";
    request.outputFormat = Pipeline::ContentOutputFormat::Xnb;

    const Pipeline::ContentBuildResult first = pipeline.Build(request);
    const Pipeline::ContentBuildResult second = pipeline.Build(request);
    ASSERT_NE(first.xnbOutput, nullptr);
    ASSERT_NE(second.xnbOutput, nullptr);
    EXPECT_EQ(first.xnbOutput->bytes, second.xnbOutput->bytes);
}

TEST_F(XnbOutputPipelineTest, TheContainerDescriptionIsHonouredEndToEnd)
{
    const TemporaryDirectory sources;
    const std::filesystem::path png = WriteSourcePng(sources.Path(), "hidef");

    CNA::Content::Xnb::XnbFileOptions options;
    options.platform = CNA::Content::Xnb::XnbTargetPlatform::DesktopGL;
    options.profile = CNA::Content::Xnb::XnbGraphicsProfile::HiDef;
    options.version = 4;

    Pipeline::ContentPipeline pipeline(registry_);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = sources.Path();
    request.source = png;
    request.logicalName = "hidef";
    request.outputFormat = Pipeline::ContentOutputFormat::Xnb;
    request.xnbOptions = &options;

    const Pipeline::ContentBuildResult result = pipeline.Build(request);
    ASSERT_NE(result.xnbOutput, nullptr);
    ASSERT_GE(result.xnbOutput->bytes.size(), 10u);
    EXPECT_EQ(result.xnbOutput->bytes[3], 'd');
    EXPECT_EQ(result.xnbOutput->bytes[4], 4);
    EXPECT_EQ(result.xnbOutput->bytes[5] & 0x01u, 0x01u);
}

TEST_F(XnbOutputPipelineTest, AProcessedTypeWithNoXnbWriterFailsAtSelectionWithContext)
{
    // The model route has a CNB writer and, deliberately, no .xnb writer yet, so asking for .xnb
    // output must fail with a selection diagnostic rather than produce something unloadable.
    const TemporaryDirectory sources;
    const std::filesystem::path png = WriteSourcePng(sources.Path(), "unroutable");

    auto bareRegistry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    Pipeline::RegisterTexture2DContentPipeline(*bareRegistry);   // no XNB writers registered

    Pipeline::ContentPipeline pipeline(bareRegistry);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = sources.Path();
    request.source = png;
    request.logicalName = "unroutable";
    request.outputFormat = Pipeline::ContentOutputFormat::Xnb;

    try
    {
        (void)pipeline.Build(request);
        FAIL() << "expected a selection failure";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Selection);
        EXPECT_EQ(error.LogicalName(), "unroutable");
    }
}
