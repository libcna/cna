// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include "CNA/Internal/PathUtf8.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include "CNA/Internal/Graphics/DdsSurfaceReader.hpp"

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

    /** @brief Walks up from the working directory, then from this file, to a repository path. */
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path();
             !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

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
    // Version 5 since TQ-038: the step order and the resize/mip kernel changed resized and
    // mipmapped bytes, which an incremental build has to see.
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.TextureProcessor", "5"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.Texture2DContentWriter", "2"}));
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

    const Cnb::CnbTextureData oldData = Cnb::ImportImageAsCnbTexture2D(CNA::Internal::PathToUtf8(source));
    const std::vector<std::uint8_t> oldLibraryBytes =
        Cnb::EncodeTexture2DToCnb(oldData, "Textures/wall");
    EXPECT_EQ(first.output.bytes, oldLibraryBytes);

#if !defined(_WIN32)
    const std::filesystem::path oldToolOutput = scratch.Path() / "old-tool.cnb";
    ASSERT_EQ(RunSourceTool({CNA::Internal::PathToUtf8(source), CNA::Internal::PathToUtf8(oldToolOutput), "--name", "Textures/wall"}),
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
    // differential corpus, XNAPP-251), and the pipeline follows
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
        Cnb::ImportImageAsCnbTexture2D(CNA::Internal::PathToUtf8(source), Cnb::CnbImageImportOptions{}),
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
    ContentManager content(nullptr, CNA::Internal::PathToUtf8(scratch.Path()));
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

// -- texture build policy (XNAP-54) -----------------------------------

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
    // XNAP-96. Four texels, chosen so that every rule the premultiply
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

    /**
     * @brief The exact rule XNA applies: `channel * alpha / 255`, truncated.
     *
     * TextureProcessorUtils.PremultiplyAlpha calls Color.FromNonPremultiplied(int,int,int,int)
     * (IL_0094), which divides in integers (IL_0000-000b). This helper used to round half up; every
     * texel the tests below quote happens to answer the same either way, which is why that never
     * showed (TQ-038; the discriminating texel is in PremultiplyAlphaTruncatesTheWayXnaDoes).
     */
    [[nodiscard]] std::uint8_t Premultiplied(std::uint32_t channel, std::uint32_t alpha)
    {
        return static_cast<std::uint8_t>((channel * alpha) / 255u);
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

TEST(Texture2DContentPipelineTest, PremultiplyAlphaKeepsWholeQuotientsAndAlpha)
{
    ScratchDirectory scratch("premultiply_rounding");
    WriteBytes(scratch.Path() / "wall.png", MakePng(kAlphaPolicyPixels, 4, 1));

    const std::vector<std::uint8_t> texels = BuildAlphaPolicyTexels(scratch.Path());
    ASSERT_GE(texels.size(), 16u);

    // The rule is channel * alpha / 255 in integers: 255*16 -> 16 exactly, 128*16 -> 8 (8.03),
    // 1*16 -> 0 (0.06). Alpha itself never changes. None of these separates truncation from
    // rounding; PremultiplyAlphaTruncatesTheWayXnaDoes does.
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
    // XNA's order is colour key -> premultiply -> resize -> mips -> block compression
    // (TextureProcessor.Process IL_0084-00be), so a keyed texel is transparent *before*
    // premultiplication runs and its colour is therefore zeroed. That is what XNA 4.0 produces for a
    // colour-keyed texel, and it is the observable consequence of the two policies composing in
    // that order.
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

// XNAPP-021: the four sources XNA's TextureImporter accepts and a
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
// (XNAPP-255).
TEST(Texture2DContentPipelineTest, ADdsCubeIsImportedAsACubeRatherThanRefused)
{
    ScratchDirectory scratch("dds_cube");
    WriteBytes(scratch.Path() / "wall.dds", MakeUncompressedDds(true));

    const Pipeline::ContentBuildResult result = BuildNamed(scratch.Path(), "wall.dds");
    EXPECT_EQ(result.importer.name, "CNA.ImageImporter");
    EXPECT_EQ(result.processor.name, "CNA.TextureCubeProcessor");
}

// XNASWEEP-109. XNA's `TextureImporter` loads an image through
// `System.Drawing`, and GDI+ honours a PNG's own `gAMA` chunk: a file that declares a gamma of
// 0.45 rather than the standard 0.45455 is corrected on load. The correction is one unit over most
// of the range and none at either end, but it reaches every texel, and 103 of the 2,267 distinct
// PNG sources in the public XNA sample corpus declare exactly 0.45000. CNA decoded through
// stb_image, which ignores the chunk, so those textures were wrong in every texel of the middle of
// their range: NetRumble's `barrierPurple.png` had 35,340 of 65,536 bytes differing from XNA's own
// build, all of them by one. An `sRGB` chunk declares the standard gamma and so corrects nothing,
// which is the other half of the rule.
TEST(Texture2DContentPipelineTest, APngsOwnGammaChunkIsAppliedTheWayGdiPlusAppliesIt)
{
    const std::filesystem::path corrected = Locate("tests/assets/xna40/texture/gamma_45000.png");
    const std::filesystem::path plain = Locate("tests/assets/xna40/texture/gamma_srgb.png");
    if (!std::filesystem::exists(corrected) || !std::filesystem::exists(plain))
    {
        GTEST_SKIP() << "the gamma fixtures are missing";
    }

    const Pipeline::ImportedImage withGamma = Pipeline::DecodeImportedImage(corrected);
    const Pipeline::ImportedImage withSrgb = Pipeline::DecodeImportedImage(plain);
    ASSERT_EQ(withGamma.width, 16u);
    ASSERT_EQ(withSrgb.width, 16u);

    // The ramp is 8, 24, 40, ... 248 in every channel, and `sRGB` leaves it exactly there.
    for (std::uint32_t step = 0; step < 16u; ++step)
    {
        const std::uint8_t authored = static_cast<std::uint8_t>(step * 16u + 8u);
        EXPECT_EQ(withSrgb.rgbaPixels[step * 4u], authored) << step;
        EXPECT_EQ(withSrgb.rgbaPixels[step * 4u + 3u], 255u) << step;
    }

    // `gAMA` 0.45000 corrects towards a display gamma of 2.2: `round(255 * (c/255)^(1/(0.45*2.2)))`.
    for (std::uint32_t step = 0; step < 16u; ++step)
    {
        const double authored = static_cast<double>(step * 16u + 8u);
        const auto expected = static_cast<std::uint8_t>(
            std::floor(255.0 * std::pow(authored / 255.0, 1.0 / (0.45 * 2.2)) + 0.5));
        EXPECT_EQ(withGamma.rgbaPixels[step * 4u], expected) << step;
        // Alpha is not a colour and is left alone.
        EXPECT_EQ(withGamma.rgbaPixels[step * 4u + 3u], 255u) << step;
    }
    // And the correction is not a no-op: the middle of the ramp moves.
    EXPECT_NE(withGamma.rgbaPixels[8u * 4u], withSrgb.rgbaPixels[8u * 4u]);
}

TEST(Texture2DContentPipelineTest, Safe002DdsChannelWidthsScaleWithoutOverflow)
{
    for (const unsigned bits : {1u, 8u, 16u, 24u, 31u, 32u})
    {
        SCOPED_TRACE(bits);
        auto bytes = MakeUncompressedDds();
        const auto put = [&](std::size_t at, std::uint32_t value)
        {
            for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8u * i));
        };
        const auto mask = static_cast<std::uint32_t>((std::uint64_t{1} << bits) - 1u);
        put(92, mask);
        put(96, 0); put(100, 0); put(104, 0);
        for (std::size_t pixel = 0; pixel < 4; ++pixel) put(128 + pixel * 4, mask);
        const auto decoded = CNA::Internal::Graphics::ReadDdsSurfaces(bytes, "Safe002 masks");
        ASSERT_EQ(decoded.surfaces.size(), 1u);
        ASSERT_EQ(decoded.surfaces[0].size(), 1u);
        EXPECT_EQ(decoded.surfaces[0][0], (std::vector<std::uint8_t>{
            255,0,0,255, 255,0,0,255, 255,0,0,255, 255,0,0,255}));
        const auto valid = CNA::Internal::Graphics::ReadDdsSurfaces(MakeUncompressedDds(), "recovery");
        ASSERT_EQ(valid.surfaces[0][0].size(), 16u);
        EXPECT_EQ(valid.surfaces[0][0][0], 255u);
    }
}

TEST(Texture2DContentPipelineTest, Safe002DdsSurfaceMutationsRefuseBeforeSizingAndRecover)
{
    const auto seed = MakeUncompressedDds();
    const auto recover = [&]
    {
        const auto image = CNA::Internal::Graphics::ReadDdsSurfaces(seed, "recovery");
        EXPECT_EQ(image.width, 2u); EXPECT_EQ(image.height, 2u);
        ASSERT_EQ(image.surfaces.size(), 1u); ASSERT_EQ(image.surfaces[0].size(), 1u);
        EXPECT_EQ(image.surfaces[0][0], (std::vector<std::uint8_t>{
            255,0,0,255, 0,255,0,255, 0,0,255,255, 128,128,128,128}));
    };
    for (const std::size_t length : {0u, 3u, 4u, 127u, 128u, 143u})
    {
        SCOPED_TRACE(length);
        const std::vector<std::uint8_t> bytes(seed.begin(), seed.begin() + length);
        EXPECT_THROW((void)CNA::Internal::Graphics::ReadDdsSurfaces(bytes, "Safe002 short"), std::runtime_error);
        recover();
    }
    for (const auto [offset, value] : std::vector<std::pair<std::size_t, std::uint32_t>>{
             {4,0xFFFFFFFFu}, {12,0}, {12,16385}, {16,0x7FFFFFFFu}, {16,0xFFFFFFFFu},
             {28,3}, {28,0xFFFFFFFFu}, {76,0}, {80,0}, {88,0}, {88,0xFFFFFFFFu},
             {112,0x200u}, {112,0x200000u}})
    {
        SCOPED_TRACE(offset);
            SCOPED_TRACE(value);
        auto bytes = seed;
        for (unsigned i = 0; i < 4; ++i) bytes[offset+i] = static_cast<std::uint8_t>(value >> (8u*i));
        // A volume declaration with zero depth is one slice, and remains a valid image.
        if (offset == 112 && value == 0x200000u)
        {
            bytes[24] = 2;
        }
        EXPECT_THROW((void)CNA::Internal::Graphics::ReadDdsSurfaces(bytes, "Safe002 field"), std::runtime_error);
        recover();
    }
}

// -- XNA's resize kernel, step order and dither on the canonical route (TQ-038) -------------------
//
// Every expected byte below is XNA's own: the committed graphics oracle
// (tests/reference/xna40/graphics/graphics-content-oracle.json, the genuine BitmapContent.Copy and
// TextureContent.GenerateMipmaps over Color bitmaps) or the genuine BuildContent of the sources in
// tests/assets/xna40/texture (TQ-038 lane, reproduced by the differential corpus rows of the same
// names). None is CNA's output.

namespace
{
    /** @brief Hex text (two digits a byte, RGBA order) as bytes. */
    [[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string_view hex)
    {
        const auto nibble = [](const char digit)
        {
            return static_cast<std::uint8_t>(digit <= '9' ? digit - '0' : digit - 'A' + 10);
        };
        std::vector<std::uint8_t> bytes;
        for (std::size_t at = 0u; at + 1u < hex.size(); at += 2u)
        {
            bytes.push_back(static_cast<std::uint8_t>((nibble(hex[at]) << 4) | nibble(hex[at + 1u])));
        }
        return bytes;
    }

    /** @brief GraphicsContentOracle.cs's Gradient(width, height), RGBA. */
    [[nodiscard]] std::vector<std::uint8_t> OracleGradient(const int width, const int height)
    {
        std::vector<std::uint8_t> pixels;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                pixels.push_back(static_cast<std::uint8_t>(x * 255 / std::max(1, width - 1)));
                pixels.push_back(static_cast<std::uint8_t>(y * 255 / std::max(1, height - 1)));
                pixels.push_back(static_cast<std::uint8_t>(37 + 11 * x + 7 * y));
                pixels.push_back(static_cast<std::uint8_t>(255 - 40 * y));
            }
        }
        return pixels;
    }

    /** @brief One destination texel's taps as `first:w,w,...` over the axis total. */
    [[nodiscard]] std::string Spell(const std::vector<Pipeline::XnaResizeTaps>& taps, const std::uint64_t total)
    {
        std::string text = "/" + std::to_string(total);
        for (const Pipeline::XnaResizeTaps& tap : taps)
        {
            text += " " + std::to_string(tap.first) + ":";
            for (std::size_t at = 0u; at < tap.weights.size(); ++at)
            {
                text += (at == 0u ? "" : ",") + std::to_string(tap.weights[at]);
            }
        }
        return text;
    }

    /** @brief Builds `wall.png` from @p pixels through the canonical route and answers every level. */
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> BuildLevels(
        const std::string& tag, const std::vector<std::uint8_t>& pixels, const int width, const int height,
        const Pipeline::ContentProcessorParameters& parameters)
    {
        ScratchDirectory scratch(tag);
        WriteBytes(scratch.Path() / "wall.png", MakePng(pixels, width, height));
        return Cnb::DecodeTexture2DFromCnb(
                   Cnb::CnbDocument::Parse(BuildTexture(scratch.Path(), parameters).output.bytes, tag + ".cnb"))
            .representations[0]
            .levels;
    }

    /** @brief What BuildContent hands the canonical processor for XNA's TextureProcessor defaults. */
    [[nodiscard]] Pipeline::ContentProcessorParameters XnaTextureProcessorDefaults()
    {
        Pipeline::ContentProcessorParameters parameters;
        parameters.Set(Pipeline::TextureColorKeyParameter, std::string("255,0,255"));
        return parameters;
    }
}

TEST(TextureXnaResizeKernel, AxisTapsAreXnasKernelExactly)
{
    // Destination texel i is the mean over [i*s/d, (i+1)*s/d] of the linear interpolation between
    // source centres, held at the end values (TQ-034's rule, measured to the float on
    // resize/v4_* and resize/4x4_to_2x2_vector4). Worked by hand:
    //   3 -> 4: texel 0 spans [0, 0.75]: 0.5 of p0, then p0..p1 over t in [0, 0.25], so p1 gets
    //           (0.25^2/2)/0.75 = 1/24 (TQ-034 measured 1/24 at that edge); texel 1 spans
    //           [0.75, 1.5], mean t = 0.625, so 3/8 and 5/8.
    //   4 -> 2: {1,7,7,1}/16 with the outer tap clamped onto texel 0: 8,7,1 / 16.
    //   3 -> 3: an unchanged length is still filtered: 7/8,1/8 then 1/8,3/4,1/8.
    //   7 -> 3: texel 0 spans [0, 7/3]: p0 1, p1 1/2 + 35/72, p2 25/72, over 7/3 -> 72,71,25 / 168.
    const auto taps = [](const std::uint32_t source, const std::uint32_t destination)
    {
        std::uint64_t total = 0u;
        const std::vector<Pipeline::XnaResizeTaps> axis = Pipeline::XnaResizeAxisTaps(source, destination, total);
        return Spell(axis, total);
    };
    EXPECT_EQ(taps(3u, 4u), "/24 0:23,1 0:9,15 1:15,9 1:1,23");
    EXPECT_EQ(taps(4u, 2u), "/16 0:8,7,1 1:1,7,8");
    EXPECT_EQ(taps(3u, 3u), "/8 0:7,1 0:1,6,1 1:1,7");
    EXPECT_EQ(taps(7u, 3u), "/168 0:72,71,25 1:1,47,72,47,1 4:25,71,72");
    EXPECT_EQ(taps(2u, 1u), "/2 0:1,1");
    EXPECT_EQ(taps(1u, 4u), "/1 0:1 0:1 0:1 0:1");
    std::uint64_t total = 0u;
    EXPECT_THROW((void)Pipeline::XnaResizeAxisTaps(0u, 4u, total), std::invalid_argument);
    EXPECT_THROW((void)Pipeline::XnaResizeAxisTaps(4u, 0u, total), std::invalid_argument);
}

TEST(TextureXnaResizeKernel, ResamplingAnswersXnasColorResizeByteForByte)
{
    // BitmapContent.Copy between Color bitmaps of different sizes, which is what ResizeToPowerOfTwo
    // calls (TextureProcessorUtils.ResizeToPowerOfTwo IL_005c). The answer is the kernel narrowed
    // through the ordered dither, not rounded: rounding misses 3, 12, 6, 6 and 7 of these channels.
    const std::vector<std::uint8_t> four = Bytes("00000000FF0000FF00FF00800A141E28");
    const std::vector<std::uint8_t> gradient4 = OracleGradient(4, 4);
    std::vector<std::uint8_t> corner;  // region/copy_subrect_resized reads the 2x2 at (0,0)
    for (const std::size_t texel : {0u, 1u, 4u, 5u})
    {
        for (std::size_t channel = 0u; channel < 4u; ++channel)
        {
            corner.push_back(gradient4[texel * 4u + channel]);
        }
    }
    // textureprop/resize_to_power_of_two: probe_3x2.png premultiplied, 3x2 -> 4x2 -- the height does
    // not change and is filtered all the same.
    const std::vector<std::uint8_t> probe3x2 = Bytes("FF0000FF00FF00FF0000FFFF80808080000000FF20202040");
    EXPECT_EQ(Pipeline::ResampleRgbaImage(gradient4, 4u, 4u, 2u, 2u),
              Bytes("303030E9CF3044E830CF3C9ECFCF519D"))
        << "resize/4x4_to_2x2";
    EXPECT_EQ(Pipeline::ResampleRgbaImage(four, 2u, 2u, 4u, 4u),
              Bytes("0000000040000040C00000C0FF0000FF004000203031014A9214069FC10507C9"
                    "00C000601293055F363B115F470F165E00FF008002C4076A084F173E0A141E28"))
        << "resize/2x2_to_4x4";
    EXPECT_EQ(Pipeline::ResampleRgbaImage(OracleGradient(3, 3), 3u, 3u, 2u, 2u),
              Bytes("2B2B2BF2D42A3AF22BD535BDD4D443BC"))
        << "resize/3x3_to_2x2";
    EXPECT_EQ(Pipeline::ResampleRgbaImage(corner, 2u, 2u, 4u, 4u),
              Bytes("000025FF150028FF40002EFF550030FF001527F5151529F540152FF5551531F5"
                    "00402BE115402DE1404033E1554035E100552CD715552ED7405534D7555537D7"))
        << "region/copy_subrect_resized";
    EXPECT_EQ(Pipeline::ResampleRgbaImage(probe3x2, 3u, 2u, 4u, 2u),
              Bytes("E61910F05A9106F9028D56F6040DDAE88A6D6B95363E2AD50B1F17C11B1C395F"))
        << "textureprop/resize_to_power_of_two";
    // Same size in and out is a copy: XNA's ResizeToPowerOfTwo converts nothing then (IL_003b-004d).
    EXPECT_EQ(Pipeline::ResampleRgbaImage(probe3x2, 3u, 2u, 3u, 2u), probe3x2);
}

TEST(TextureXnaResizeKernel, AMipChainIsXnasGenerateMipmapsByteForByte)
{
    // texture/generate_mipmaps_8x2: each level from the one above (GenerateMipmaps IL_008d-00aa).
    // The 4x1 -> 2x1 step halves an even axis beside a unit one, which the area average this used
    // to take there answers differently.
    const std::vector<std::vector<std::uint8_t>> chain =
        Pipeline::GenerateRgbaMipChain(OracleGradient(8, 2), 8u, 2u);
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(chain[0], Bytes("15802FEB5A7F44EBA4805AEBEA7F6FEB"));
    EXPECT_EQ(chain[1], Bytes("3D803BEBC27F63EB"));
    EXPECT_EQ(chain[2], Bytes("80804FEB"));
    // texture/generate_mipmaps_overwrite: the 4x4 gradient's first level.
    EXPECT_EQ(Pipeline::GenerateRgbaMipChain(OracleGradient(4, 4), 4u, 4u).front(),
              Bytes("303030E9CF3044E830CF3C9ECFCF519D"));
}

TEST(Texture2DContentPipelineTest, PremultiplyAlphaTruncatesTheWayXnaDoes)
{
    // 30 at alpha 200 is 23.53: XNA answers 23 (Color.FromNonPremultiplied divides in integers,
    // IL_0000-000b), rounding would answer 24; 90 at 200 is 70.59, 70 against 71.
    std::vector<std::uint8_t> pixels{30u, 60u, 90u, 200u};
    Pipeline::PremultiplyRgbaAlpha(pixels);
    EXPECT_EQ(pixels, (std::vector<std::uint8_t>{23u, 47u, 70u, 200u}));
    EXPECT_EQ(BuildLevels("premultiply_truncates", {30u, 60u, 90u, 200u}, 1, 1, {}).front(),
              (std::vector<std::uint8_t>{23u, 47u, 70u, 200u}));
}

TEST(Texture2DContentPipelineTest, PremultiplicationRunsBeforeTheResizeAsXnasBuildDoes)
{
    // texture/order_3x1_resize_premultiply and _resize_only (genuine BuildContent, TQ-034): an opaque
    // texel beside two transparent white ones, 3x1 -> 4x1. Premultiplied first, the white never
    // reaches the second texel: (75,37,19,96). Resized first it would be (88,74,67,96).
    const std::vector<std::uint8_t> source = Bytes("C86432FFFFFFFF00FFFFFF00");
    Pipeline::ContentProcessorParameters premultiplied;
    premultiplied.Set(Pipeline::TextureResizeToPowerOfTwoParameter, true);
    EXPECT_EQ(BuildLevels("order_premultiply", source, 3, 1, premultiplied).front(),
              Bytes("C06030F54B2513600000000000000000"));
    Pipeline::ContentProcessorParameters straight = premultiplied;
    straight.Set(Pipeline::TexturePremultiplyAlphaParameter, false);
    EXPECT_EQ(BuildLevels("order_straight", source, 3, 1, straight).front(),
              Bytes("CB6B3BF5EAC5B260FFFFFF00FFFFFF00"));
}

TEST(Texture2DContentPipelineTest, AResizedTextureIsXnasBuildByteForByte)
{
    // probe_resize_5x4.png (partial alpha and one magenta texel) and probe_resize_6x3.png, built by
    // the genuine BuildContent with ResizeToPowerOfTwo: 5x4 -> 8x4 keeps a height that is already a
    // power of two and still filters it; 6x3 -> 8x4 changes both.
    const std::vector<std::uint8_t> five = Bytes(
        "4CA0C7005154FE00E0226500686F2EFFD5DEF296865D90FF8E1D489CFF00FFFFCA5230DE71BEBDFF"
        "3A2B1312EAE5D000FE1384D94DC44DBE1018315EA5334C15583E5B3D22057000EE880FFFEB63B4B3");
    Pipeline::ContentProcessorParameters keyed = XnaTextureProcessorDefaults();
    keyed.Set(Pipeline::TextureResizeToPowerOfTwoParameter, true);
    EXPECT_EQ(BuildLevels("resize_5x4_default", five, 5, 4, keyed).front(),
              Bytes("110C12200E070D1A0A0306120301020624220F4F65602DDA777C67CA7B8992A4"
                    "65466BC1552D4BA03E0F236F26051437421C1758864F2EC87B7F6EDE659FA0DD"
                    "160F1430120A0E271F06122C73093C788D334AAF637436C23F51349F2727357E"
                    "0C0406150E070B23130B113118050F23562C1060BE7618DAAF5F48CB913E6FA9"))
        << "texture/resize_5x4_default";
    Pipeline::ContentProcessorParameters straight;
    straight.Set(Pipeline::TextureResizeToPowerOfTwoParameter, true);
    straight.Set(Pipeline::TexturePremultiplyAlphaParameter, false);
    EXPECT_EQ(BuildLevels("resize_5x4_straight", five, 5, 4, straight).front(),
              Bytes("5497C1205677D11A684CD916B82C9B1CC1366265856840DEA4AA99CAC7D8E9A4"
                    "765F87C181507DA09C397D84DA17BABAE324A8DCB35D4CDD8C8D7BDE72ACB0DD"
                    "53342C30876C6427C99EA72FDC439B8EBF4178C57A964BC5516B4C9F3838537E"
                    "9733451583405523684969314B1E6F236F325660C77D28DAD47166CBCF5AA1A9"))
        << "texture/resize_5x4_straight";
    EXPECT_EQ(BuildLevels("resize_6x3", Bytes("AB311DFFD7D910FF1C1FE3FFA9E483FF06CB4FFF2A65B2FF"
                                              "CE796FFF247E79FFE0BEABFF963D96FF1AADD3FFED86EEFF"
                                              "E4A846FFEC45F7FF34519AFF1D6580FFC0510CFFE80AEFFF"),
                          6, 3, keyed)
                  .front(),
              Bytes("AE3B20FFC29919FF909461FF414BC9FF8CBE92FF43D166FF17A579FF306AB0FF"
                    "BD6150FF888751FF79957BFF9582B3FF967F98FF46A19AFF49A1B6FF9E7CD5FF"
                    "D28963FF96758DFF827AA7FF9487A1FF715B92FF5E738AFF9078AFFFE55AEAFF"
                    "E3A24FFFE36BB2FFA54DD1FF3E579AFF2D6181FF805A3DFFCB3B66FFE612E5FF"))
        << "texture/resize_6x3";
}

TEST(Texture2DContentPipelineTest, GeneratedMipLevelsAreXnasBuildByteForByte)
{
    // probe_mip_8x2.png, probe_mip_14x5.png and probe_mip_9x9.png built by the genuine BuildContent
    // with GenerateMipmaps. 8x2 halves an even axis beside a unit one at 4x1 -> 2x1, 14x5 halves an
    // even axis beside an odd one and then reduces 7 -> 3, and 9x9 reduces 9 -> 4 after the default
    // premultiply; the area average this route used for every odd or unit dimension misses each of
    // them by 1 to 13 units.
    Pipeline::ContentProcessorParameters mips = XnaTextureProcessorDefaults();
    mips.Set(Pipeline::TextureGenerateMipmapsParameter, true);

    const std::vector<std::vector<std::uint8_t>> eight = BuildLevels(
        "mip_8x2",
        Bytes("AE1B12FFF5A04FFF186D17FF24AC74FF3F52BEFF8EC8E0FFEE58EDFF6F97E2FF"
              "E88BCAFF8EB999FF443D91FF0238E7FFD63F65FFBA99D5FF3F32EDFF38C1A1FF"),
        8, 2, mips);
    ASSERT_EQ(eight.size(), 4u);
    EXPECT_EQ(eight[1], Bytes("BE7B6FFF316681FF8F79B9FF767FD6FF"));
    EXPECT_EQ(eight[2], Bytes("7E727CFF7D7BC4FF"));
    EXPECT_EQ(eight[3], Bytes("7E77A0FF"));

    const std::vector<std::vector<std::uint8_t>> fourteen = BuildLevels(
        "mip_14x5",
        Bytes("CD1B9BFF0F7E87FF0492ECFFA53717FFEC5539FF96F306FF113873FFA5C3F7FFB79E3AFF8191A2FF"
              "782544FF82DFCCFF9B6C92FF5B4D6CFF176D62FFA9136BFF1EEF47FF08B0C3FF5A9158FF89FA5AFF"
              "0D4480FF6F6A4FFFCBA78EFF1E8E1BFFC378A6FFDEED18FFE92D93FFC7422AFF7CE013FF8F78E6FF"
              "9846D7FFEB9DDBFFB00EAAFF64BD38FF7AD69CFF2D4B38FFF5A9DFFFF99404FFFF6C90FFC4CEA3FF"
              "B7996DFFA2B6B4FF3B7E9FFF40F6DFFF9E9CD0FF9F020CFF920C2EFF076DF1FF6E5415FF487F52FF"
              "4DCB69FF525111FF066850FF21F0DDFFCA0515FF150573FF90C2F9FF16310BFF3C5C09FF00DB6DFF"
              "503AFDFFD58742FF1CAD68FF2F571BFF421566FF964D6CFFF471FCFF2823A1FF9BC590FF2931BAFF"),
        14, 5, mips);
    ASSERT_EQ(fourteen.size(), 4u);
    EXPECT_EQ(fourteen[1], Bytes("6A607EFF578B90FF8D9E4FFF597C81FF9E9569FFAF9679FFAA6575FF"
                                 "589B98FF707174FF725988FF497C4CFF796B5BFF747FA1FF6F5780FF"));
    EXPECT_EQ(fourteen[2], Bytes("677E83FF6F7D67FF8F767FFF"));
    EXPECT_EQ(fourteen[3], Bytes("777B79FF"));

    const std::vector<std::vector<std::uint8_t>> nine = BuildLevels(
        "mip_9x9",
        Bytes("BF0E6079A10703FF968339EE6183EFBD4AABFBFF2B0AC3FB1AD03CFF4246A9FF93D0FB35"
              "7A2F31DEFEA330007B36ADFFD842BE00D2B6666D7C9FD40067EA6E00F787688DCF832900"
              "E76C897BA3BA8BFFFC61FAA6BA6070FFE43F2AF31A8B2CD7C3D8DDCC1D330D8103B75C22"
              "600156CF446952FF0B9F3676D707EAB2360A91003CB7BE7898E6D3CC7498AA0027B70D4D"
              "948D22D624BF12FFD8251C2925C2250D5B5B19FF4F5A4986D2EAB5FF4EB2D2FF9D38D591"
              "65A5E2FF17BAAA0005B93BFF17E4371539DD3AFF50AF1BA7328DFCFF33B68E167B2E20FF"
              "EC7CEFF89C1A84601A0CFFFF018AB365744AE3FFC044A1FF328D2F00B84A98FFC3A221D7"
              "1CC2B40036998D00E8EBD7004E8311FFCFE5C0001D739CD2202B124D5B4C7196A5B5D900"
              "900AABC27E591CF98703CC007ECC9AFF4DE9741D2EEA0300AC526AFFC92C65002A427E36"),
        9, 9, mips);
    ASSERT_EQ(nine.size(), 4u);
    EXPECT_EQ(nine[1], Bytes("65222CA65B4E6DAB2B48579730343C6F5B5A47CC622B4A90516C56A7283E3968"
                             "4F5C67AA1949458F546B68C5634C55B53D19336D2D4A377E2D283970281B2152"));
    EXPECT_EQ(nine[2], Bytes("5B404CAB39484A883941468B3F3E438A"));
    EXPECT_EQ(nine[3], Bytes("43424892"));
}
