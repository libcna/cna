// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-53/XNAP-54: the compressed texture route, end to end.
//
// These live in the build-time module rather than beside the processor's own tests because the
// encoder does: a registry assembled from cna_content alone has no block encoder, and proving
// that route works means assembling the one a content compiler actually uses.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/TextureCompressionPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
namespace Xnb = CNA::Internal::Xnb;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_texture_compression_" + tag + "_" +
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

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> MakePng(const std::vector<std::uint8_t>& pixels, const int width,
                                      const int height)
    {
        return CNA::Internal::Graphics::ImageLoader::EncodePng(pixels.data(), width, height,
                                                               width, height);
    }

    /** @brief A deterministic image with two strongly separated colours and a chosen alpha. */
    std::vector<std::uint8_t> MakeStripes(const std::uint32_t width, const std::uint32_t height,
                                          const std::uint8_t alphaA, const std::uint8_t alphaB)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
                const bool first = ((x / 2u) + (y / 2u)) % 2u == 0u;
                pixels[offset] = first ? 248u : 8u;
                pixels[offset + 1u] = first ? 12u : 200u;
                pixels[offset + 2u] = first ? 24u : 16u;
                pixels[offset + 3u] = first ? alphaA : alphaB;
            }
        }
        return pixels;
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(
            *registry, Pipeline::MakeBlockCompressionTextureEncoder());
        Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
        return registry;
    }

    Pipeline::ContentBuildResult Build(const ScratchDirectory& scratch,
                                       const Pipeline::ContentOutputFormat format,
                                       const std::string& textureFormat,
                                       const bool generateMipmaps = false)
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = scratch.Path();
        request.source = "wall.png";
        request.logicalName = "Textures/wall";
        request.outputFormat = format;
        request.parameters.Set(Pipeline::TextureFormatParameter, textureFormat);
        if (generateMipmaps)
        {
            request.parameters.Set(Pipeline::TextureGenerateMipmapsParameter, true);
        }
        return pipeline.Build(request);
    }

    Xnb::XnbTextureData DecodeXnbTexture(const ScratchDirectory& scratch,
                                         const Pipeline::ContentBuildResult& result)
    {
        const std::filesystem::path path = scratch.Path() / "wall.xnb";
        WriteBytes(path, result.output.bytes);
        return std::get<Xnb::XnbTextureData>(Xnb::DecodeXnbCanonicalAsset(path).value);
    }
}

TEST(TextureCompressionRouteTest, AnImageSourceBuildsToADxt1Texture2DXnb)
{
    ScratchDirectory scratch("dxt1");
    const std::vector<std::uint8_t> pixels = MakeStripes(8u, 8u, 255u, 255u);
    WriteBytes(scratch.Path() / "wall.png", MakePng(pixels, 8, 8));

    const Pipeline::ContentBuildResult result =
        Build(scratch, Pipeline::ContentOutputFormat::Xnb, "Dxt1");
    const Xnb::XnbTextureData texture = DecodeXnbTexture(scratch, result);

    EXPECT_EQ(texture.surfaceFormat, SurfaceFormat::Dxt1);
    EXPECT_EQ(texture.width, 8u);
    EXPECT_EQ(texture.height, 8u);
    ASSERT_EQ(texture.levels.size(), 1u);
    // Four 4x4 blocks at eight bytes each.
    EXPECT_EQ(texture.levels[0].size(), 32u);

    const std::vector<std::uint8_t> decoded = CNA::Internal::Graphics::DxtUtil::DecompressDxt1(
        texture.levels[0].data(), texture.levels[0].size(), 8, 8);
    ASSERT_EQ(decoded.size(), pixels.size());
    long long worst = 0;
    for (std::size_t texel = 0; texel + 3u < decoded.size(); texel += 4u)
    {
        for (std::size_t channel = 0; channel < 3u; ++channel)
        {
            worst = std::max<long long>(worst, std::abs(static_cast<int>(decoded[texel + channel]) -
                                                        static_cast<int>(pixels[texel + channel])));
        }
        EXPECT_EQ(decoded[texel + 3u], 255u);
    }
    // Two colours in a four-entry palette leaves only 565 quantization error behind.
    EXPECT_LE(worst, 4) << "worst channel error " << worst;
}

TEST(TextureCompressionRouteTest, DxtCompressedPicksTheFormatFromThePixels)
{
    {
        ScratchDirectory scratch("auto_opaque");
        WriteBytes(scratch.Path() / "wall.png", MakePng(MakeStripes(8u, 8u, 255u, 0u), 8, 8));
        // Alpha is only ever 0 or 255, which BC1's single alpha bit carries exactly.
        EXPECT_EQ(DecodeXnbTexture(scratch,
                                   Build(scratch, Pipeline::ContentOutputFormat::Xnb,
                                         "DxtCompressed"))
                      .surfaceFormat,
                  SurfaceFormat::Dxt1);
    }
    {
        ScratchDirectory scratch("auto_faded");
        WriteBytes(scratch.Path() / "wall.png", MakePng(MakeStripes(8u, 8u, 255u, 128u), 8, 8));
        EXPECT_EQ(DecodeXnbTexture(scratch,
                                   Build(scratch, Pipeline::ContentOutputFormat::Xnb,
                                         "DxtCompressed"))
                      .surfaceFormat,
                  SurfaceFormat::Dxt5);
    }
}

TEST(TextureCompressionRouteTest, EveryMipLevelIsCompressedAtItsOwnSize)
{
    ScratchDirectory scratch("mips");
    WriteBytes(scratch.Path() / "wall.png", MakePng(MakeStripes(16u, 16u, 255u, 200u), 16, 16));

    const Xnb::XnbTextureData texture = DecodeXnbTexture(
        scratch, Build(scratch, Pipeline::ContentOutputFormat::Xnb, "Dxt5", true));
    EXPECT_EQ(texture.surfaceFormat, SurfaceFormat::Dxt5);
    // 16x16 -> 8x8 -> 4x4 -> 2x2 -> 1x1, and a block never shrinks below one 16-byte block.
    ASSERT_EQ(texture.levels.size(), 5u);
    EXPECT_EQ(texture.levels[0].size(), 16u * 16u);
    EXPECT_EQ(texture.levels[1].size(), 4u * 16u);
    EXPECT_EQ(texture.levels[2].size(), 16u);
    EXPECT_EQ(texture.levels[3].size(), 16u);
    EXPECT_EQ(texture.levels[4].size(), 16u);
}

TEST(TextureCompressionRouteTest, ACnbBuildKeepsRgba8AndSaysWhy)
{
    ScratchDirectory scratch("cnb");
    WriteBytes(scratch.Path() / "wall.png", MakePng(MakeStripes(8u, 8u, 255u, 255u), 8, 8));

    const Pipeline::ContentBuildResult result =
        Build(scratch, Pipeline::ContentOutputFormat::Cnb, "Dxt1");
    const Cnb::CnbTextureData texture = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "wall.cnb"));
    ASSERT_EQ(texture.representations.size(), 1u);
    EXPECT_EQ(texture.representations[0].format, Cnb::CnbTextureFormat::Rgba8);

    bool warned = false;
    for (const Pipeline::ContentLogMessage& message : result.messages)
    {
        if (message.level == Pipeline::ContentLogLevel::Warning &&
            message.text.find("CNB texture schema 1") != std::string::npos)
        {
            warned = true;
        }
    }
    EXPECT_TRUE(warned) << "a documented loss has to be reported, not applied silently";
}

TEST(TextureCompressionRouteTest, CompressionIsDeterministicAcrossBuilds)
{
    ScratchDirectory scratch("deterministic");
    WriteBytes(scratch.Path() / "wall.png", MakePng(MakeStripes(16u, 12u, 255u, 90u), 16, 12));
    EXPECT_EQ(Build(scratch, Pipeline::ContentOutputFormat::Xnb, "Dxt5").output.bytes,
              Build(scratch, Pipeline::ContentOutputFormat::Xnb, "Dxt5").output.bytes);
}
