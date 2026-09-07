// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_texture_" + tag + "_" +
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
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::vector<std::uint8_t> DistinctPixels(int width, int height)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                pixels[offset] = static_cast<std::uint8_t>(x * 11 + 1);
                pixels[offset + 1u] = static_cast<std::uint8_t>(y * 13 + 2);
                pixels[offset + 2u] = static_cast<std::uint8_t>((x + y) * 7 + 3);
                pixels[offset + 3u] = 255u;
            }
        }
        return pixels;
    }

    std::vector<std::uint8_t> MakePng(const std::vector<std::uint8_t>& pixels,
                                      int width, int height)
    {
        return CNA::Internal::Graphics::ImageLoader::EncodePng(
            pixels.data(), width, height, width, height);
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(*registry);
        // The image importer answers a cube or a volume for a DDS that declares one
        // (XNAPP-255), so a registry that can route only the 2D third of it is not a registry
        // this importer can be measured in.
        Pipeline::RegisterCnjContentPipeline(*registry);
        return registry;
    }

    /** @brief Builds one named source, for the routes whose fixture is not a `.png`. */
    Pipeline::ContentBuildResult BuildNamed(const std::filesystem::path& root,
                                            const std::string& source)
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = source;
        request.logicalName = "Textures/wall";
        return pipeline.Build(request);
    }

    /** @brief Little-endian dword, appended. */
    void Word32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    /**
     * @brief A 2x2 uncompressed A8R8G8B8 DDS holding red, green, blue and half-transparent grey.
     *
     * @param cube When true, declares a cube map and writes the six faces one such file carries.
     */
    std::vector<std::uint8_t> MakeUncompressedDds(const bool cube = false)
    {
        std::vector<std::uint8_t> bytes{'D', 'D', 'S', ' '};
        Word32(bytes, 124u);      // dwSize
        Word32(bytes, 0x100Fu);   // caps | height | width | pitch | pixelformat
        Word32(bytes, 2u);        // height
        Word32(bytes, 2u);        // width
        Word32(bytes, 8u);        // pitch
        Word32(bytes, 0u);        // depth
        Word32(bytes, 1u);        // mip count
        for (int reserved = 0; reserved < 11; ++reserved) { Word32(bytes, 0u); }
        Word32(bytes, 32u);       // ddspf.dwSize
        Word32(bytes, 0x41u);     // DDPF_RGB | DDPF_ALPHAPIXELS
        Word32(bytes, 0u);        // fourCC
        Word32(bytes, 32u);       // bits per pixel
        Word32(bytes, 0x00FF0000u);
        Word32(bytes, 0x0000FF00u);
        Word32(bytes, 0x000000FFu);
        Word32(bytes, 0xFF000000u);
        Word32(bytes, cube ? 0x1008u : 0x1000u);  // DDSCAPS_TEXTURE, plus COMPLEX for a cube
        Word32(bytes, cube ? 0xFE00u : 0u);       // DDSCAPS2_CUBEMAP and its six face bits
        Word32(bytes, 0u);
        Word32(bytes, 0u);
        Word32(bytes, 0u);
        // BGRA texels, top-down, as a DDS stores them.
        const std::array<std::uint8_t, 16> texels{0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                                                  0xFF, 0x00, 0x00, 0xFF, 0x80, 0x80, 0x80, 0x80};
        for (int face = 0; face < (cube ? 6 : 1); ++face)
        {
            bytes.insert(bytes.end(), texels.begin(), texels.end());
        }
        return bytes;
    }

    Pipeline::ContentBuildResult BuildTexture(
        const std::filesystem::path& root, const Pipeline::ContentProcessorParameters& parameters = {})
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "wall.png";
        request.logicalName = "Textures/wall";
        request.parameters = parameters;
        return pipeline.Build(request);
    }

#if !defined(_WIN32)
    int RunSourceTool(const std::vector<std::string>& arguments)
    {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_SOURCE_TO_CNB_TOOL_PATH));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int spawnResult =
            posix_spawn(&pid, CNA_SOURCE_TO_CNB_TOOL_PATH, nullptr, nullptr, argv.data(), environ);
        if (spawnResult != 0) { return -1; }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { return -1; }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif
}

TEST(Texture2DContentPipelineTest, BuildsHeadlesslyThroughDistinctImporterProcessorAndWriter)
{
    ScratchDirectory scratch("stages");
    const std::vector<std::uint8_t> sourcePixels = DistinctPixels(7, 5);
    WriteBytes(scratch.Path() / "wall.png", MakePng(sourcePixels, 7, 5));

    const Pipeline::ContentBuildResult result = BuildTexture(scratch.Path());
    EXPECT_EQ(result.importer, (Pipeline::ContentComponentIdentity{"CNA.ImageImporter", "1"}));
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.TextureProcessor", "3"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.Texture2DContentWriter", "1"}));
    EXPECT_EQ(result.output.assetTypeId, Cnb::CnbAssetTypeId::Texture2D);
    EXPECT_EQ(result.output.assetTypeName, "Microsoft.Xna.Framework.Graphics.Texture2D");
    ASSERT_EQ(result.dependencies.size(), 1u);
    EXPECT_EQ(result.dependencies[0].kind, Pipeline::ContentDependencyKind::PrimarySource);
    EXPECT_TRUE(result.runtimeReferences.empty());
    ASSERT_EQ(result.messages.size(), 2u);
    EXPECT_EQ(result.messages[0].stage, Pipeline::ContentPipelineStage::Import);
    EXPECT_EQ(result.messages[0].component, "CNA.ImageImporter");
    EXPECT_EQ(result.messages[1].stage, Pipeline::ContentPipelineStage::Process);
    EXPECT_EQ(result.messages[1].component, "CNA.TextureProcessor");

    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(result.output.bytes, "pipeline wall.cnb");
    EXPECT_EQ(document.Metadata().contentName, "Textures/wall");
    const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(document);
    EXPECT_EQ(decoded.width, 7u);
    EXPECT_EQ(decoded.height, 5u);
    ASSERT_EQ(decoded.representations.size(), 1u);
    ASSERT_EQ(decoded.representations[0].levels.size(), 1u);
    EXPECT_EQ(decoded.representations[0].levels[0], sourcePixels);
}

TEST(Texture2DContentPipelineTest, ReadsANativeNonAsciiFilesystemPathWithoutNarrowing)
{
    ScratchDirectory scratch("unicode");
    const std::filesystem::path relative =
        std::filesystem::path(u8"Textury") / std::filesystem::path(u8"žluťoučký_壁.png");
    const std::filesystem::path source = scratch.Path() / relative;
    const std::vector<std::uint8_t> pixels = DistinctPixels(3, 2);
    WriteBytes(source, MakePng(pixels, 3, 2));

    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = source;
    request.logicalName = "Textury/žluťoučký_壁";
    const Pipeline::ContentBuildResult result = pipeline.Build(request);

    ASSERT_EQ(result.dependencies.size(), 1u);
    EXPECT_EQ(result.dependencies.front().identity,
              CNA::Internal::ContentPathToUtf8(std::filesystem::weakly_canonical(source)));
    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(result.output.bytes, "unicode path pipeline output");
    EXPECT_EQ(document.Metadata().contentName, "Textury/žluťoučký_壁");
    EXPECT_EQ(Cnb::DecodeTexture2DFromCnb(document).representations[0].levels[0], pixels);

    const Cnb::CnbTextureData compatibility = Cnb::ImportImageAsCnbTexture2D(source);
    EXPECT_EQ(Cnb::EncodeTexture2DToCnb(compatibility, request.logicalName), result.output.bytes);
}

TEST(Texture2DContentPipelineTest, IsByteIdenticalToTheUnchangedSourceProducer)
{
    ScratchDirectory scratch("oracle");
    const std::vector<std::uint8_t> sourcePixels = DistinctPixels(4, 3);
    const std::filesystem::path source = scratch.Path() / "wall.png";
    WriteBytes(source, MakePng(sourcePixels, 4, 3));

    const Pipeline::ContentBuildResult first = BuildTexture(scratch.Path());
    const Pipeline::ContentBuildResult second = BuildTexture(scratch.Path());
    EXPECT_EQ(first.output.bytes, second.output.bytes);

    const Cnb::CnbTextureData oldData = Cnb::ImportImageAsCnbTexture2D(source.string());
    const std::vector<std::uint8_t> oldLibraryBytes =
        Cnb::EncodeTexture2DToCnb(oldData, "Textures/wall");
    EXPECT_EQ(first.output.bytes, oldLibraryBytes);

#if !defined(_WIN32)
    const std::filesystem::path oldToolOutput = scratch.Path() / "old-tool.cnb";
    ASSERT_EQ(RunSourceTool({source.string(), oldToolOutput.string(), "--name", "Textures/wall"}),
              0);
    EXPECT_EQ(first.output.bytes, ReadBytes(oldToolOutput));
#endif
}

TEST(Texture2DContentPipelineTest, ColorKeyPolicyMatchesTheUnchangedProducerExactly)
{
    ScratchDirectory scratch("color_key");
    std::vector<std::uint8_t> sourcePixels = DistinctPixels(4, 4);
    sourcePixels[0] = 200u;
    sourcePixels[1] = 100u;
    sourcePixels[2] = 50u;
    sourcePixels[3] = 255u;
    const std::filesystem::path source = scratch.Path() / "wall.png";
    WriteBytes(source, MakePng(sourcePixels, 4, 4));

    // premultiplyAlpha is pinned off here on purpose: this contract is about the colour-key
    // policy converging with the unchanged producer, and the unchanged producer has no
    // premultiplication step at all (XNAP-96).
    //
    // It converges on *which* texels are keyed and no longer on what is left in them. XNA writes
    // transparent black -- the colour goes with the alpha -- and the pre-pipeline producer keeps
    // the colour, which is invisible while premultiplication is on and shows the moment it is
    // turned off. Measured on the genuine build (`texture/png4x4_no_premultiply` in the
    // differential corpus, plans/plan_xnapipeline_parity.md XNAPP-251), and the pipeline follows
    // XNA. So the byte-for-byte comparison below is against a producer told not to key at all,
    // and the keyed texel is asserted directly.
    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureColorKeyParameter, std::string("200,100,50"));
    parameters.Set(Pipeline::TexturePremultiplyAlphaParameter, false);
    const Pipeline::ContentBuildResult result = BuildTexture(scratch.Path(), parameters);

    const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "keyed pipeline wall.cnb"));
    ASSERT_FALSE(decoded.representations.empty());
    ASSERT_FALSE(decoded.representations[0].levels.empty());
    EXPECT_EQ(decoded.representations[0].levels[0][0], 0u);
    EXPECT_EQ(decoded.representations[0].levels[0][1], 0u);
    EXPECT_EQ(decoded.representations[0].levels[0][2], 0u);
    EXPECT_EQ(decoded.representations[0].levels[0][3], 0u);

    // Every other texel is the unchanged producer's, byte for byte: the divergence is the keyed
    // texel and nothing else. Compared against a producer told not to key, since the two disagree
    // about exactly the texels a key names.
    Pipeline::ContentProcessorParameters unkeyed;
    unkeyed.Set(Pipeline::TexturePremultiplyAlphaParameter, false);
    const Pipeline::ContentBuildResult plain = BuildTexture(scratch.Path(), unkeyed);
    const std::vector<std::uint8_t> oldLibraryBytes = Cnb::EncodeTexture2DToCnb(
        Cnb::ImportImageAsCnbTexture2D(source.string(), Cnb::CnbImageImportOptions{}),
        "Textures/wall");
    EXPECT_EQ(plain.output.bytes, oldLibraryBytes);

    const Cnb::CnbTextureData plainDecoded = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(plain.output.bytes, "unkeyed pipeline wall.cnb"));
    ASSERT_FALSE(plainDecoded.representations.empty());
    for (std::size_t texel = 4u; texel < plainDecoded.representations[0].levels[0].size(); ++texel)
    {
        EXPECT_EQ(plainDecoded.representations[0].levels[0][texel],
                  decoded.representations[0].levels[0][texel])
            << "texel byte " << texel << " changed, and only the keyed texel should have";
    }
}

TEST(Texture2DContentPipelineTest, ResultLoadsThroughTheExistingContentManagerRuntimePath)
{
    ScratchDirectory scratch("runtime");
    const std::vector<std::uint8_t> sourcePixels = DistinctPixels(3, 2);
    WriteBytes(scratch.Path() / "wall.png", MakePng(sourcePixels, 3, 2));
    const Pipeline::ContentBuildResult result = BuildTexture(scratch.Path());
    WriteBytes(scratch.Path() / "Textures" / "wall.cnb", result.output.bytes);

    GraphicsDevice device;
    ContentManager content(nullptr, scratch.Path().string());
    content.setGraphicsDevice(device);
    auto loaded = content.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("Textures/wall");
    EXPECT_EQ(loaded.getWidthProperty(), 3);
    EXPECT_EQ(loaded.getHeightProperty(), 2);

    std::vector<Microsoft::Xna::Framework::Color> loadedPixels(6u);
    loaded.GetData(loadedPixels.data(), static_cast<int>(loadedPixels.size()));
    for (std::size_t index = 0u; index < loadedPixels.size(); ++index)
    {
        EXPECT_EQ(loadedPixels[index].getRProperty(), sourcePixels[index * 4u]);
        EXPECT_EQ(loadedPixels[index].getGProperty(), sourcePixels[index * 4u + 1u]);
        EXPECT_EQ(loadedPixels[index].getBProperty(), sourcePixels[index * 4u + 2u]);
        EXPECT_EQ(loadedPixels[index].getAProperty(), sourcePixels[index * 4u + 3u]);
    }
}

TEST(Texture2DContentPipelineTest, RejectsUnknownMistypedAndMalformedProcessorParameters)
{
    ScratchDirectory scratch("parameters");
    WriteBytes(scratch.Path() / "wall.png", MakePng(DistinctPixels(2, 2), 2, 2));

    for (const auto& [name, value] :
         std::vector<std::pair<std::string, Pipeline::ContentProcessorParameterValue>>{
             {"mystery", true},
             {Pipeline::TextureColorKeyParameter, std::uint64_t{1u}},
             {Pipeline::TextureColorKeyParameter, std::string("1,2")},
             {Pipeline::TextureColorKeyParameter, std::string("1,2,256")},
             {Pipeline::TextureColorKeyParameter, std::string("1,2,3,4,5")}})
    {
        Pipeline::ContentProcessorParameters parameters;
        parameters.Set(name, value);
        try
        {
            static_cast<void>(BuildTexture(scratch.Path(), parameters));
            ADD_FAILURE() << "parameter should have failed: " << name;
        }
        catch (const Pipeline::ContentPipelineError& error)
        {
            EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
            EXPECT_EQ(error.Component(), "CNA.TextureProcessor");
        }
    }
}

// -- texture build policy (plans/plan_xnapipeline.md XNAP-54) -----------------------------------

TEST(TextureImageOperationsTest, NextPowerOfTwoRoundsUpAndLeavesExactPowersAlone)
{
    EXPECT_EQ(Pipeline::NextPowerOfTwoDimension(0u), 1u);
    EXPECT_EQ(Pipeline::NextPowerOfTwoDimension(1u), 1u);
    EXPECT_EQ(Pipeline::NextPowerOfTwoDimension(2u), 2u);
    EXPECT_EQ(Pipeline::NextPowerOfTwoDimension(3u), 4u);
    EXPECT_EQ(Pipeline::NextPowerOfTwoDimension(100u), 128u);
    EXPECT_EQ(Pipeline::NextPowerOfTwoDimension(1024u), 1024u);
}

TEST(TextureImageOperationsTest, HalvingAnImageAveragesEachTwoByTwoGroup)
{
    // Four texels of 0, 10, 20 and 30 average to 15 exactly, so an off-by-one in the weighting
    // or a dropped column is visible rather than merely plausible.
    std::vector<std::uint8_t> source(2u * 2u * 4u);
    for (std::size_t texel = 0; texel < 4u; ++texel)
    {
        for (std::size_t channel = 0; channel < 4u; ++channel)
        {
            source[texel * 4u + channel] = static_cast<std::uint8_t>(texel * 10u);
        }
    }
    const std::vector<std::uint8_t> halved =
        Pipeline::ResampleRgbaImage(source, 2u, 2u, 1u, 1u);
    ASSERT_EQ(halved.size(), 4u);
    for (const std::uint8_t channel : halved) { EXPECT_EQ(channel, 15u); }
}

TEST(TextureImageOperationsTest, ShrinkingByAnAwkwardRatioAreaAveragesRatherThanDroppingTexels)
{
    // 3 -> 2 gives each destination texel one and a half source texels, so neither result can be
    // a source value copied through.
    std::vector<std::uint8_t> source(3u * 1u * 4u);
    for (std::size_t texel = 0; texel < 3u; ++texel)
    {
        for (std::size_t channel = 0; channel < 4u; ++channel)
        {
            source[texel * 4u + channel] = static_cast<std::uint8_t>(texel * 60u);
        }
    }
    const std::vector<std::uint8_t> shrunk =
        Pipeline::ResampleRgbaImage(source, 3u, 1u, 2u, 1u);
    ASSERT_EQ(shrunk.size(), 8u);
    // (0*2 + 60*1) / 3 = 20 and (60*1 + 120*2) / 3 = 100.
    EXPECT_EQ(shrunk[0], 20u);
    EXPECT_EQ(shrunk[4], 100u);
}

TEST(TextureImageOperationsTest, EnlargingInterpolatesAndKeepsTheCornersInPlace)
{
    std::vector<std::uint8_t> source(2u * 1u * 4u);
    for (std::size_t channel = 0; channel < 4u; ++channel)
    {
        source[channel] = 0u;
        source[4u + channel] = 200u;
    }
    const std::vector<std::uint8_t> grown = Pipeline::ResampleRgbaImage(source, 2u, 1u, 4u, 1u);
    ASSERT_EQ(grown.size(), 16u);
    EXPECT_EQ(grown[0], 0u);
    EXPECT_EQ(grown[12], 200u);
    EXPECT_GT(grown[4], grown[0]);
    EXPECT_LT(grown[8], grown[12]);
}

TEST(TextureImageOperationsTest, PremultiplyScalesEachChannelByItsOwnAlpha)
{
    std::vector<std::uint8_t> pixels{200u, 100u, 50u, 128u, 10u, 20u, 30u, 255u,
                                     90u,  90u,  90u, 0u};
    Pipeline::PremultiplyRgbaAlpha(pixels);
    // (200 * 128 + 127) / 255 == 100, and the fully opaque texel is untouched.
    EXPECT_EQ(pixels[0], 100u);
    EXPECT_EQ(pixels[1], 50u);
    EXPECT_EQ(pixels[2], 25u);
    EXPECT_EQ(pixels[3], 128u);
    EXPECT_EQ(pixels[4], 10u);
    EXPECT_EQ(pixels[7], 255u);
    EXPECT_EQ(pixels[8], 0u);
    EXPECT_EQ(pixels[11], 0u);
}

TEST(TextureImageOperationsTest, AMipChainHalvesUntilOneByOne)
{
    const std::vector<std::uint8_t> level0(8u * 4u * 4u, 64u);
    const std::vector<std::vector<std::uint8_t>> chain =
        Pipeline::GenerateRgbaMipChain(level0, 8u, 4u);
    // 8x4 -> 4x2 -> 2x1 -> 1x1.
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(chain[0].size(), 4u * 2u * 4u);
    EXPECT_EQ(chain[1].size(), 2u * 1u * 4u);
    EXPECT_EQ(chain[2].size(), 4u);
    for (const std::vector<std::uint8_t>& level : chain)
    {
        for (const std::uint8_t channel : level) { EXPECT_EQ(channel, 64u); }
    }
    EXPECT_TRUE(Pipeline::GenerateRgbaMipChain(std::vector<std::uint8_t>(4u, 0u), 1u, 1u).empty());
}

namespace
{
    // plans/plan_xnapipeline.md XNAP-96. Four texels, chosen so that every rule the premultiply
    // policy has is visible in one image:
    //   0: partial alpha 128 -- the rounding case
    //   1: opaque         255 -- must be untouched
    //   2: transparent      0 -- must become zero RGB
    //   3: partial alpha  16  -- a second rounding case, with a value that rounds down
    const std::vector<std::uint8_t> kAlphaPolicyPixels{
        200u, 100u, 50u, 128u,
        10u, 20u, 30u, 255u,
        90u, 180u, 240u, 0u,
        255u, 128u, 1u, 16u,
    };

    std::vector<std::uint8_t> BuildAlphaPolicyTexels(
        const std::filesystem::path& root,
        const Pipeline::ContentProcessorParameters& parameters = {})
    {
        const Pipeline::ContentBuildResult result = BuildTexture(root, parameters);
        return Cnb::DecodeTexture2DFromCnb(
                   Cnb::CnbDocument::Parse(result.output.bytes, "premultiply.cnb"))
            .representations[0]
            .levels[0];
    }

    /** @brief The exact rule the processor applies: round-half-up on `channel * alpha / 255`. */
    [[nodiscard]] std::uint8_t Premultiplied(std::uint32_t channel, std::uint32_t alpha)
    {
        return static_cast<std::uint8_t>((channel * alpha + 127u) / 255u);
    }
}

TEST(Texture2DContentPipelineTest, PremultiplyAlphaDefaultsToTrueLikeXna40)
{
    ScratchDirectory scratch("premultiply_default");
    WriteBytes(scratch.Path() / "wall.png", MakePng(kAlphaPolicyPixels, 4, 1));

    // The parameter is omitted entirely: XNA 4.0's TextureProcessor.PremultiplyAlpha defaults to
    // true, and so does CNA's.
    const std::vector<std::uint8_t> texels = BuildAlphaPolicyTexels(scratch.Path());
    ASSERT_GE(texels.size(), 16u);

    EXPECT_EQ(texels[0], Premultiplied(200u, 128u));
    EXPECT_EQ(texels[1], Premultiplied(100u, 128u));
    EXPECT_EQ(texels[2], Premultiplied(50u, 128u));
    EXPECT_EQ(texels[3], 128u);
}

TEST(Texture2DContentPipelineTest, PremultiplyAlphaExplicitTrueMatchesTheDefault)
{
    ScratchDirectory scratch("premultiply_explicit_true");
    WriteBytes(scratch.Path() / "wall.png", MakePng(kAlphaPolicyPixels, 4, 1));

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TexturePremultiplyAlphaParameter, true);
    EXPECT_EQ(BuildAlphaPolicyTexels(scratch.Path(), parameters),
              BuildAlphaPolicyTexels(scratch.Path()));
}

TEST(Texture2DContentPipelineTest, PremultiplyAlphaExplicitFalseKeepsStraightAlpha)
{
    ScratchDirectory scratch("premultiply_explicit_false");
    WriteBytes(scratch.Path() / "wall.png", MakePng(kAlphaPolicyPixels, 4, 1));

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TexturePremultiplyAlphaParameter, false);
    EXPECT_EQ(BuildAlphaPolicyTexels(scratch.Path(), parameters), kAlphaPolicyPixels);
}

TEST(Texture2DContentPipelineTest, PremultiplyAlphaLeavesOpaqueTexelsExactlyAsAuthored)
{
    ScratchDirectory scratch("premultiply_opaque");
    WriteBytes(scratch.Path() / "wall.png", MakePng(kAlphaPolicyPixels, 4, 1));

    const std::vector<std::uint8_t> texels = BuildAlphaPolicyTexels(scratch.Path());
    ASSERT_GE(texels.size(), 16u);
    EXPECT_EQ(texels[4], 10u);
    EXPECT_EQ(texels[5], 20u);
    EXPECT_EQ(texels[6], 30u);
    EXPECT_EQ(texels[7], 255u);
}

TEST(Texture2DContentPipelineTest, PremultiplyAlphaZeroesTheColourOfAFullyTransparentTexel)
{
    ScratchDirectory scratch("premultiply_transparent");
    WriteBytes(scratch.Path() / "wall.png", MakePng(kAlphaPolicyPixels, 4, 1));

    const std::vector<std::uint8_t> texels = BuildAlphaPolicyTexels(scratch.Path());
    ASSERT_GE(texels.size(), 16u);
    EXPECT_EQ(texels[8], 0u);
    EXPECT_EQ(texels[9], 0u);
    EXPECT_EQ(texels[10], 0u);
    EXPECT_EQ(texels[11], 0u);
}

TEST(Texture2DContentPipelineTest, PremultiplyAlphaRoundsPartialAlphaHalfUp)
{
    ScratchDirectory scratch("premultiply_rounding");
    WriteBytes(scratch.Path() / "wall.png", MakePng(kAlphaPolicyPixels, 4, 1));

    const std::vector<std::uint8_t> texels = BuildAlphaPolicyTexels(scratch.Path());
    ASSERT_GE(texels.size(), 16u);

    // The rule is (channel * alpha + 127) / 255 in integers: 255*16 -> 16 exactly,
    // 128*16 -> 8 (8.03 rounds to 8), 1*16 -> 0 (0.06 rounds to 0). Alpha itself never changes.
    EXPECT_EQ(texels[12], 16u);
    EXPECT_EQ(texels[13], 8u);
    EXPECT_EQ(texels[14], 0u);
    EXPECT_EQ(texels[15], 16u);
    EXPECT_EQ(texels[12], Premultiplied(255u, 16u));
    EXPECT_EQ(texels[13], Premultiplied(128u, 16u));
    EXPECT_EQ(texels[14], Premultiplied(1u, 16u));
}

TEST(Texture2DContentPipelineTest, ColourKeyedTexelsBecomeTransparentBlackUnderTheDefault)
{
    // The documented order is colour key -> resize -> premultiply -> mips -> block compression,
    // so a keyed texel is transparent *before* premultiplication runs and its colour is therefore
    // zeroed. That is what XNA 4.0 produces for a colour-keyed texel, and it is the observable
    // consequence of the two policies composing in that order.
    ScratchDirectory scratch("premultiply_color_key");
    std::vector<std::uint8_t> pixels{200u, 100u, 50u, 255u, 10u, 20u, 30u, 255u};
    WriteBytes(scratch.Path() / "wall.png", MakePng(pixels, 2, 1));

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureColorKeyParameter, std::string("200,100,50"));
    const std::vector<std::uint8_t> texels = BuildAlphaPolicyTexels(scratch.Path(), parameters);
    ASSERT_GE(texels.size(), 8u);
    EXPECT_EQ(texels[0], 0u);
    EXPECT_EQ(texels[1], 0u);
    EXPECT_EQ(texels[2], 0u);
    EXPECT_EQ(texels[3], 0u);
    EXPECT_EQ(texels[4], 10u);
    EXPECT_EQ(texels[7], 255u);
}

TEST(Texture2DContentPipelineTest, PremultiplicationRunsBeforeMipGeneration)
{
    // A 2x2 source with one opaque white texel and three transparent white ones. Averaging
    // straight alpha would give the 1x1 mip white at quarter alpha -- a bright halo. Premultiplied
    // first, the average is (64,64,64,64), which is the same colour the top level shows.
    ScratchDirectory scratch("premultiply_mip_order");
    const std::vector<std::uint8_t> pixels{
        255u, 255u, 255u, 255u, 255u, 255u, 255u, 0u,
        255u, 255u, 255u, 0u, 255u, 255u, 255u, 0u,
    };
    WriteBytes(scratch.Path() / "wall.png", MakePng(pixels, 2, 2));

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureGenerateMipmapsParameter, true);
    const Pipeline::ContentBuildResult result = BuildTexture(scratch.Path(), parameters);
    const Cnb::CnbTextureData texture = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "premultiply_mip_order.cnb"));
    ASSERT_EQ(texture.representations[0].levels.size(), 2u);
    const std::vector<std::uint8_t>& smallest = texture.representations[0].levels[1];
    ASSERT_EQ(smallest.size(), 4u);
    EXPECT_EQ(smallest[0], 64u);
    EXPECT_EQ(smallest[1], 64u);
    EXPECT_EQ(smallest[2], 64u);
    EXPECT_EQ(smallest[3], 64u);
}

TEST(Texture2DContentPipelineTest, GenerateMipmapsProducesEveryLevelDownToOneByOne)
{
    ScratchDirectory scratch("mips");
    WriteBytes(scratch.Path() / "wall.png", MakePng(DistinctPixels(8, 8), 8, 8));

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureGenerateMipmapsParameter, true);
    const Cnb::CnbTextureData texture = Cnb::DecodeTexture2DFromCnb(Cnb::CnbDocument::Parse(
        BuildTexture(scratch.Path(), parameters).output.bytes, "mips.cnb"));
    EXPECT_EQ(texture.mipCount, 4u);
    ASSERT_EQ(texture.representations.size(), 1u);
    ASSERT_EQ(texture.representations[0].levels.size(), 4u);
    EXPECT_EQ(texture.representations[0].levels[3].size(), 4u);

    // Without the parameter the texture keeps exactly the level the image carried.
    EXPECT_EQ(Cnb::DecodeTexture2DFromCnb(
                  Cnb::CnbDocument::Parse(BuildTexture(scratch.Path()).output.bytes, "flat.cnb"))
                  .mipCount,
              1u);
}

TEST(Texture2DContentPipelineTest, ResizeToPowerOfTwoRoundsBothDimensionsUp)
{
    ScratchDirectory scratch("resize");
    WriteBytes(scratch.Path() / "wall.png", MakePng(DistinctPixels(5, 3), 5, 3));

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureResizeToPowerOfTwoParameter, true);
    const Cnb::CnbTextureData texture = Cnb::DecodeTexture2DFromCnb(Cnb::CnbDocument::Parse(
        BuildTexture(scratch.Path(), parameters).output.bytes, "resize.cnb"));
    EXPECT_EQ(texture.width, 8u);
    EXPECT_EQ(texture.height, 4u);
    EXPECT_EQ(texture.representations[0].levels[0].size(), 8u * 4u * 4u);
}

TEST(Texture2DContentPipelineTest, TextureFormatValuesAreParsedAndUnknownOnesNamed)
{
    EXPECT_EQ(Pipeline::TryParseTextureBuildFormat("DxtCompressed"),
              Pipeline::TextureBuildFormat::DxtCompressed);
    EXPECT_EQ(Pipeline::TryParseTextureBuildFormat("dxt5"), Pipeline::TextureBuildFormat::Dxt5);
    EXPECT_EQ(Pipeline::TryParseTextureBuildFormat("COLOR"),
              Pipeline::TextureBuildFormat::Color);
    EXPECT_FALSE(Pipeline::TryParseTextureBuildFormat("Bc7").has_value());
    EXPECT_EQ(Pipeline::TextureBuildFormatName(Pipeline::TextureBuildFormat::Dxt1), "Dxt1");

    ScratchDirectory scratch("format");
    WriteBytes(scratch.Path() / "wall.png", MakePng(DistinctPixels(4, 4), 4, 4));
    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureFormatParameter, std::string("Bc7"));
    try
    {
        (void)BuildTexture(scratch.Path(), parameters);
        FAIL() << "an unknown textureFormat must be refused";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_NE(std::string(error.what()).find("NoChange, Color, DxtCompressed"),
                  std::string::npos)
            << error.what();
    }
}

TEST(Texture2DContentPipelineTest, CompressionWithoutAnEncoderSaysWhichBuildProvidesOne)
{
    // This registry is the runtime module's own, with no build-time encoder attached. Refusing
    // here rather than quietly writing uncompressed pixels is the point.
    ScratchDirectory scratch("no_encoder");
    WriteBytes(scratch.Path() / "wall.png", MakePng(DistinctPixels(4, 4), 4, 4));
    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureFormatParameter, std::string("Dxt1"));
    try
    {
        (void)BuildTexture(scratch.Path(), parameters);
        FAIL() << "a compressed format must be refused when no encoder is registered";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("cna_content_pipeline"), std::string::npos) << message;
    }
}

// plans/plan_xnapipeline_parity.md XNAPP-021: the four sources XNA's TextureImporter accepts and a
// plain stb decode does not. Before this the canonical graph had no route for them at all, so a
// `.dds`, `.dib`, `.pfm` or `.ppm` the XNA façade imported perfectly never reached any container.
TEST(Texture2DContentPipelineTest, RoutesEveryTextureSourceXnaItselfAccepts)
{
    const std::vector<std::string> routed = Pipeline::ImageImporter().SourceExtensions();
    for (const char* extension :
         {".bmp", ".dds", ".dib", ".hdr", ".jpg", ".pfm", ".png", ".ppm", ".tga"})
    {
        EXPECT_NE(std::find(routed.begin(), routed.end(), extension), routed.end())
            << "XNA's TextureImporter accepts " << extension;
    }
}

TEST(Texture2DContentPipelineTest, DecodesAnUncompressedDdsSurface)
{
    ScratchDirectory scratch("dds");
    WriteBytes(scratch.Path() / "wall.dds", MakeUncompressedDds());

    const Pipeline::ContentBuildResult result = BuildNamed(scratch.Path(), "wall.dds");
    const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "pipeline wall.cnb"));

    EXPECT_EQ(decoded.width, 2u);
    EXPECT_EQ(decoded.height, 2u);
    ASSERT_EQ(decoded.representations.size(), 1u);
    ASSERT_EQ(decoded.representations[0].levels.size(), 1u);
    // RGBA, and the half-transparent grey premultiplied by its own alpha, which is this
    // processor's documented default.
    EXPECT_EQ(decoded.representations[0].levels[0],
              (std::vector<std::uint8_t>{0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                                         0x00, 0x00, 0xFF, 0xFF, 0x40, 0x40, 0x40, 0x80}));
}

TEST(Texture2DContentPipelineTest, DecodesAPortableFloatMapWithTheColorPackingRule)
{
    ScratchDirectory scratch("pfm");
    // "PF" is colour, and a negative scale means little-endian floats stored bottom-up.
    std::vector<std::uint8_t> pfm{'P', 'F', '\n', '2', ' ', '1', '\n', '-', '1', '.', '0', '\n'};
    for (const float channel : {0.0f, 0.5f, 1.0f, 2.0f, -1.0f, 0.25f})
    {
        std::array<std::uint8_t, 4> raw{};
        std::memcpy(raw.data(), &channel, sizeof(channel));
        pfm.insert(pfm.end(), raw.begin(), raw.end());
    }
    WriteBytes(scratch.Path() / "wall.pfm", pfm);

    const Pipeline::ContentBuildResult result = BuildNamed(scratch.Path(), "wall.pfm");
    const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "pipeline wall.cnb"));

    EXPECT_EQ(decoded.width, 2u);
    EXPECT_EQ(decoded.height, 1u);
    // Out-of-range floats clamp rather than wrap, and alpha is opaque: a float map carries none.
    EXPECT_EQ(decoded.representations[0].levels[0],
              (std::vector<std::uint8_t>{0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x00, 0x40, 0xFF}));
}

TEST(Texture2DContentPipelineTest, DecodesAPortablePixmapAndAHeaderlessBitmap)
{
    ScratchDirectory scratch("ppm_dib");
    const std::vector<std::uint8_t> ppm{'P', '6', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n',
                                        0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00};
    WriteBytes(scratch.Path() / "wall.ppm", ppm);

    std::vector<std::uint8_t> dib(40u, 0u);
    dib[0] = 40u;   // BITMAPINFOHEADER
    dib[4] = 2u;    // width
    dib[8] = 1u;    // height
    dib[12] = 1u;   // planes
    dib[14] = 32u;  // bits per pixel
    const std::array<std::uint8_t, 8> bgra{0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
    dib.insert(dib.end(), bgra.begin(), bgra.end());
    WriteBytes(scratch.Path() / "wall.dib", dib);

    for (const char* source : {"wall.ppm", "wall.dib"})
    {
        const Pipeline::ContentBuildResult result = BuildNamed(scratch.Path(), source);
        const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(
            Cnb::CnbDocument::Parse(result.output.bytes, "pipeline wall.cnb"));
        EXPECT_EQ(decoded.width, 2u) << source;
        EXPECT_EQ(decoded.height, 1u) << source;
        EXPECT_EQ(decoded.representations[0].levels[0],
                  (std::vector<std::uint8_t>{0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF}))
            << source;
    }
}

// A `.dds` is three formats wearing one extension, and this importer answers whichever one the
// header describes -- the same thing XNA's own TextureImporter does. It used to refuse a cube map
// and name the route that would build it; there was no such route
// (plans/plan_xnapipeline_parity.md XNAPP-255).
TEST(Texture2DContentPipelineTest, ADdsCubeIsImportedAsACubeRatherThanRefused)
{
    ScratchDirectory scratch("dds_cube");
    WriteBytes(scratch.Path() / "wall.dds", MakeUncompressedDds(true));

    const Pipeline::ContentBuildResult result = BuildNamed(scratch.Path(), "wall.dds");
    EXPECT_EQ(result.importer.name, "CNA.ImageImporter");
    EXPECT_EQ(result.processor.name, "CNA.TextureCubeProcessor");
}
