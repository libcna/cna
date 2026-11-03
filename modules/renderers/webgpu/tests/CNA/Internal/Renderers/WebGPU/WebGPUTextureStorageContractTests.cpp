// SPDX-License-Identifier: MS-PL
//
// TQ-012: WebGPU texture storage measured on a device -- which SurfaceFormats a Texture2D and a
// TextureCube store (docs/webgpu-renderer.md, "Textures, formats and render targets"), and the
// block-compressed copy layout: block rows, sub-rectangle offsets and the block-aligned extent of a
// mip level smaller than one 4x4 block.
//
// Needs a WebGPU device, so a real window: run it only through the private runner,
//   tools/platform/run_gpu_tests_private.sh --exec cmake-build-webgpu/CnaRendererTests \
//     --gtest_filter='WebGPUCompressedTransferContract.*:WebGPUTextureFormatContract.*'
//
// Oracles: BC1 (DXT1) is 8 bytes per 4x4 block and BC3 (DXT5) 16; XNA transfers a compressed
// rectangle as tightly packed block rows in row-major block order; a BC1 block whose two 565
// endpoints are equal and whose indices are all 0 decodes every texel to that endpoint
// (0xF800 -> (255, 0, 0), 0x07E0 -> (0, 255, 0), 0x001F -> (0, 0, 255); the 5- and 6-bit maxima
// expand to 255).

#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)

#include <array>
#include <functional>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    GraphicsDevice MakeDevice()
    {
        PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
        parameters.setBackBufferWidthProperty(16);
        parameters.setBackBufferHeightProperty(16);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              parameters);
    }

    WebGPURenderer& RendererOf(GraphicsDevice& device)
    {
        auto* renderer = dynamic_cast<WebGPURenderer*>(&device.GetRenderer());
        if (renderer == nullptr)
            throw std::runtime_error("the active renderer is not WebGPURenderer");
        return *renderer;
    }

    bool StoresBlocks(GraphicsDevice& device, SurfaceFormat format)
    {
        return device.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(format));
    }

    /// @p count blocks of @p blockBytes each; byte b of block k is (k * 16 + b) & 0xFF, so every
    /// block and every byte inside it is distinguishable.
    std::vector<std::uint8_t> NumberedBlocks(int count, int blockBytes, int firstBlock = 0)
    {
        std::vector<std::uint8_t> bytes;
        for (int k = firstBlock; k < firstBlock + count; ++k)
            for (int b = 0; b < blockBytes; ++b)
                bytes.push_back(static_cast<std::uint8_t>((k * 16 + b) & 0xFF));
        return bytes;
    }

    std::vector<std::uint8_t> Concat(std::initializer_list<std::vector<std::uint8_t>> parts)
    {
        std::vector<std::uint8_t> out;
        for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
        return out;
    }

    /// One BC1 block of a single colour: both endpoints @p rgb565 (little-endian), all indices 0.
    std::vector<std::uint8_t> SolidDxt1Block(std::uint16_t rgb565)
    {
        const auto lo = static_cast<std::uint8_t>(rgb565 & 0xFF);
        const auto hi = static_cast<std::uint8_t>(rgb565 >> 8);
        return {lo, hi, lo, hi, 0, 0, 0, 0};
    }
}

TEST(WebGPUCompressedTransferContract, SubRectanglesReadBackExactlyTheBlocksTheyCover)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    int exercised = 0;
    for (const auto& [format, blockBytes] : std::array<std::pair<SurfaceFormat, int>, 2>{{
             {SurfaceFormat::Dxt1, 8}, {SurfaceFormat::Dxt5, 16}}})
    {
        SCOPED_TRACE(static_cast<int>(format));
        if (!StoresBlocks(device, format)) continue;
        ++exercised;
        // 12 x 8 texels = 3 x 2 blocks, numbered 0 1 2 / 3 4 5 in row-major order.
        Texture2D texture(device, 12, 8, false, format);
        const std::vector<std::uint8_t> all = NumberedBlocks(6, blockBytes);
        texture.SetData(all.data(), static_cast<int>(all.size()));

        const auto read = [&](const Rectangle& rect, int blocks) {
            std::vector<std::uint8_t> out(static_cast<std::size_t>(blocks * blockBytes), 0xCD);
            texture.GetData(0, &rect, out.data(), 0, static_cast<int>(out.size()));
            return out;
        };
        // (4, 4, 8, 4): block columns 1-2 of block row 1 -> blocks 4, 5.
        EXPECT_EQ(read(Rectangle(4, 4, 8, 4), 2),
                  Concat({NumberedBlocks(1, blockBytes, 4), NumberedBlocks(1, blockBytes, 5)}));
        // (8, 0, 4, 8): block column 2 of both rows -> blocks 2, 5, one per packed row.
        EXPECT_EQ(read(Rectangle(8, 0, 4, 8), 2),
                  Concat({NumberedBlocks(1, blockBytes, 2), NumberedBlocks(1, blockBytes, 5)}));
        // (0, 0, 12, 8): everything, in order.
        EXPECT_EQ(read(Rectangle(0, 0, 12, 8), 6), all);

        // A sub-rectangle write lands on exactly its blocks: (4, 0, 4, 8) is block column 1 -> 1, 4.
        const std::vector<std::uint8_t> patch = NumberedBlocks(2, blockBytes, 9);
        const Rectangle column(4, 0, 4, 8);
        texture.SetData(0, &column, patch.data(), 0, static_cast<int>(patch.size()));
        const std::vector<std::uint8_t> expected = Concat({
            NumberedBlocks(1, blockBytes, 0), NumberedBlocks(1, blockBytes, 9), NumberedBlocks(1, blockBytes, 2),
            NumberedBlocks(1, blockBytes, 3), NumberedBlocks(1, blockBytes, 10), NumberedBlocks(1, blockBytes, 5)});
        EXPECT_EQ(read(Rectangle(0, 0, 12, 8), 6), expected);
    }
    if (exercised == 0)
        GTEST_SKIP() << "this device stores no BC blocks natively";
}

TEST(WebGPUCompressedTransferContract, MipLevelsSmallerThanABlockUploadAndSampleTheirOwnBlock)
{
    // An 8x8 DXT1 chain: level 0 is 2x2 blocks, levels 1 (4x4), 2 (2x2) and 3 (1x1) one block
    // each. Levels 2 and 3 are smaller than a block, so the GPU copy must use the block-aligned
    // 4x4 extent (WebGPU rejects a partial-block copy width as a validation error).
    //
    // Each level is one solid colour (level 0 RED, 1 BLUE, 2 GREEN, 3 BLUE). Sampling with
    // SamplerState.MaxMipLevel = 2 -- XNA's "most detailed level to use" -- must read level 2 even
    // though the 8x8 texture is drawn 1:1 onto an 8x8 target (LOD 0), so the centre pixel is GREEN:
    // that proves level 2's partial-block upload reached the GPU and MaxMipLevel reached the sampler.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    if (!StoresBlocks(device, SurfaceFormat::Dxt1))
        GTEST_SKIP() << "this device stores no BC blocks natively";
    WebGPURenderer& renderer = RendererOf(device);
    const std::size_t errorsBefore = renderer.GetUncapturedErrorCountEXT();

    Texture2D texture(device, 8, 8, true, SurfaceFormat::Dxt1);
    ASSERT_EQ(texture.getLevelCountProperty(), 4);
    const std::vector<std::uint8_t> red = SolidDxt1Block(0xF800);
    const std::vector<std::uint8_t> green = SolidDxt1Block(0x07E0);
    const std::vector<std::uint8_t> blue = SolidDxt1Block(0x001F);
    const std::vector<std::uint8_t> level0 = Concat({red, red, red, red});
    texture.SetData(0, nullptr, level0.data(), 0, static_cast<int>(level0.size()));
    texture.SetData(1, nullptr, blue.data(), 0, static_cast<int>(blue.size()));
    texture.SetData(2, nullptr, green.data(), 0, static_cast<int>(green.size()));
    texture.SetData(3, nullptr, blue.data(), 0, static_cast<int>(blue.size()));

    // Every level reads back as exactly the blocks written to it.
    for (const auto& [level, bytes] : std::array<std::pair<int, const std::vector<std::uint8_t>*>, 4>{{
             {0, &level0}, {1, &blue}, {2, &green}, {3, &blue}}})
    {
        std::vector<std::uint8_t> out(bytes->size(), 0xCD);
        texture.GetData(level, nullptr, out.data(), 0, static_cast<int>(out.size()));
        EXPECT_EQ(out, *bytes) << "level " << level;
    }

    RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::PreserveContents);
    SpriteBatch batch(device);
    const auto drawWithMaxMipLevel = [&](int maxMipLevel) {
        // A fresh state per draw: XNA refuses to modify a state already bound to the device.
        SamplerState sampler;
        sampler.setFilterProperty(TextureFilter::Point);
        sampler.setAddressUProperty(TextureAddressMode::Clamp);
        sampler.setAddressVProperty(TextureAddressMode::Clamp);
        sampler.setMaxMipLevelProperty(maxMipLevel);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &sampler, nullptr, nullptr, nullptr);
        batch.Draw(texture, Rectangle(0, 0, 8, 8), Color::White);
        batch.End();
        device.SetRenderTarget(nullptr);
        Color pixel = Color::Transparent;
        const Rectangle centre(4, 4, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    };
    const auto expectPixel = [](const Color& actual, const Color& expected, const char* what) {
        EXPECT_EQ(actual, expected) << what << ": got (" << int(actual.getRProperty()) << ","
                                    << int(actual.getGProperty()) << "," << int(actual.getBProperty()) << ","
                                    << int(actual.getAProperty()) << ")";
    };
    expectPixel(drawWithMaxMipLevel(0), Color(255, 0, 0, 255), "level 0 (LOD 0, no clamp)");
    expectPixel(drawWithMaxMipLevel(1), Color(0, 0, 255, 255), "MaxMipLevel 1 -> level 1");
    expectPixel(drawWithMaxMipLevel(2), Color(0, 255, 0, 255), "MaxMipLevel 2 -> level 2 (2x2, partial block)");
    EXPECT_EQ(renderer.GetUncapturedErrorCountEXT(), errorsBefore) << "a compressed upload failed validation";
}

namespace
{
    /// Constructs with @p construct; returns the reported format, or nullopt for a NAMED refusal
    /// (System::NotSupportedException or System::InvalidOperationException). Anything else fails.
    std::optional<SurfaceFormat> ConstructOrRefuse(const std::function<SurfaceFormat()>& construct,
                                                   const std::string& what)
    {
        try
        {
            return construct();
        }
        catch (const System::NotSupportedException&) { return std::nullopt; }
        catch (const System::InvalidOperationException&) { return std::nullopt; }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << what << " threw an unnamed exception: " << e.what();
            return std::nullopt;
        }
    }
}

TEST(WebGPUTextureFormatContract, PackedSixteenBitColourIsRefusedAndNativeStorageKeepsTheRequestedFormat)
{
    // docs/webgpu-renderer.md: Bgr565, Bgra5551 and Bgra4444 are refused for Texture2D and
    // TextureCube -- WebGPU has no packed 16-bit colour texture, and CNA must not report a widened
    // RGBA8 allocation as the requested format. The formats the page lists as stored natively
    // (Color; NormalizedByte2/4; HalfVector4, HdrBlendable, Single and Vector4 as Texture2D; the BC
    // formats where the device stores blocks) are accepted and report exactly what was asked for.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    for (const SurfaceFormat packed : {SurfaceFormat::Bgr565, SurfaceFormat::Bgra5551, SurfaceFormat::Bgra4444})
    {
        const int ordinal = static_cast<int>(packed);
        EXPECT_FALSE(ConstructOrRefuse([&] { Texture2D t(device, 4, 4, false, packed); return t.getFormatProperty(); },
                                       "Texture2D " + std::to_string(ordinal)).has_value())
            << "Texture2D accepted packed 16-bit SurfaceFormat " << ordinal;
        EXPECT_FALSE(ConstructOrRefuse([&] { TextureCube t(device, 4, false, packed); return t.getFormatProperty(); },
                                       "TextureCube " + std::to_string(ordinal)).has_value())
            << "TextureCube accepted packed 16-bit SurfaceFormat " << ordinal;
    }

    std::vector<SurfaceFormat> native{SurfaceFormat::Color, SurfaceFormat::NormalizedByte2,
                                      SurfaceFormat::NormalizedByte4, SurfaceFormat::HalfVector4,
                                      SurfaceFormat::HdrBlendable, SurfaceFormat::Single, SurfaceFormat::Vector4};
    for (const SurfaceFormat bc : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
                                   SurfaceFormat::Bc7EXT})
        if (StoresBlocks(device, bc)) native.push_back(bc);
    for (const SurfaceFormat format : native)
    {
        const int ordinal = static_cast<int>(format);
        const std::optional<SurfaceFormat> reported = ConstructOrRefuse(
            [&] { Texture2D t(device, 4, 4, false, format); return t.getFormatProperty(); },
            "Texture2D " + std::to_string(ordinal));
        ASSERT_TRUE(reported.has_value()) << "Texture2D refused natively stored SurfaceFormat " << ordinal;
        EXPECT_EQ(static_cast<int>(*reported), ordinal) << "Texture2D reports a different format";
    }
}

#endif
