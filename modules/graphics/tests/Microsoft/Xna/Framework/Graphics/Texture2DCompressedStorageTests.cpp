// SPDX-License-Identifier: MS-PL
//
// A block-compressed Texture2D keeps no level-0 CPU shadow. Its blocks are held by the renderer --
// every renderer that transfers DXT keeps them in its own compressed store, answers GetData from
// it and re-uploads it after a context or device loss -- so the zero-filled buffer the format
// constructor used to keep as cpuPixels_ was never written or read again: 0.5 byte per texel for
// Dxt1 and 1 byte for Dxt3/Dxt5, for the whole life of the texture. These tests pin that no such
// buffer is kept, and that what it might have been expected to serve still works without it.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr std::array<SurfaceFormat, 3> kClassicDxtFormats{
        SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5};

    [[nodiscard]] int BlockBytes(SurfaceFormat format)
    {
        return format == SurfaceFormat::Dxt1 ? 8 : 16;
    }

    [[nodiscard]] std::vector<std::uint8_t> Blocks(int count, SurfaceFormat format,
                                                   std::uint8_t seed)
    {
        std::vector<std::uint8_t> result(
            static_cast<std::size_t>(count) * static_cast<std::size_t>(BlockBytes(format)));
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = static_cast<std::uint8_t>(seed + index * 29u);
        return result;
    }

    [[nodiscard]] bool TransfersBlocks(GraphicsDevice& device, SurfaceFormat format)
    {
        return device.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(format));
    }

    /// The texture, or nothing when the active renderer refuses to store the format at all.
    [[nodiscard]] std::optional<Texture2D> TryCreate(GraphicsDevice& device, int size, bool mipMap,
                                                     SurfaceFormat format)
    {
        try
        {
            return Texture2D(device, size, size, mipMap, format);
        }
        catch (const System::InvalidOperationException&)
        {
            return std::nullopt;
        }
    }

    [[nodiscard]] Color DrawWithSpriteBatch(GraphicsDevice& device, Texture2D& texture)
    {
        constexpr int size = 8;
        RenderTarget2D target(device, size, size, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        SpriteBatch batch(device);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        SamplerState point = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        batch.Draw(texture, Rectangle(0, 0, size, size), Color::White);
        batch.End();
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(size) * size);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2];
    }
}

class Texture2DCompressedStorageTest : public ::testing::Test
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters()};
};

// The control: context recovery is on by default, so an uncompressed texture does keep its
// level-0 shadow. Without it the assertions below could not fail.
TEST_F(Texture2DCompressedStorageTest, AnUncompressedTextureKeepsItsLevelZeroShadow)
{
    Texture2D texture(device, 8, 8, false, SurfaceFormat::Color);
    EXPECT_FALSE(texture.GetCpuPixelsWeak().expired());
}

// Every renderer that can create a DXT texture, whether or not it transfers blocks, creates it
// without a CPU shadow -- mipmapped or not.
TEST_F(Texture2DCompressedStorageTest, ConstructionKeepsNoLevelZeroShadow)
{
    int created = 0;
    for (const SurfaceFormat format : kClassicDxtFormats)
    {
        for (const bool mipMap : {false, true})
        {
            SCOPED_TRACE(::testing::Message() << "format " << static_cast<int>(format)
                                              << (mipMap ? " mipmapped" : " single level"));
            std::optional<Texture2D> texture = TryCreate(device, 8, mipMap, format);
            if (!texture.has_value())
                continue;
            ++created;
            EXPECT_TRUE(texture->GetCpuPixelsWeak().expired());
        }
    }
    if (created == 0)
        GTEST_SKIP() << "the active renderer creates no classic DXT Texture2D";
}

// The blocks are established and read back -- whole level, a block-aligned rectangle and a mip
// level -- from the renderer, through the texture and through a copy of it, and establishing
// them never brings a shadow back.
TEST_F(Texture2DCompressedStorageTest, EstablishedBlocksReadBackWithoutAShadow)
{
    int exercised = 0;
    for (const SurfaceFormat format : kClassicDxtFormats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        if (!TransfersBlocks(device, format))
            continue;
        ++exercised;

        Texture2D texture(device, 8, 8, true, format);
        const std::vector<std::uint8_t> base = Blocks(4, format, 0x11u);
        texture.SetData(base.data(), static_cast<int>(base.size()));

        // Level 0 is two by two blocks in row order; the right column is blocks 1 and 3.
        const Rectangle rightColumn(4, 0, 4, 8);
        const std::vector<std::uint8_t> patch = Blocks(2, format, 0xA3u);
        texture.SetData(0, &rightColumn, patch.data(), 0, static_cast<int>(patch.size()));
        const auto block = static_cast<std::size_t>(BlockBytes(format));
        std::vector<std::uint8_t> expected = base;
        std::copy(patch.begin(), patch.begin() + block, expected.begin() + block);
        std::copy(patch.begin() + block, patch.end(), expected.begin() + 3 * block);

        const std::vector<std::uint8_t> mip = Blocks(1, format, 0x47u);
        texture.SetData(1, nullptr, mip.data(), 0, static_cast<int>(mip.size()));

        std::vector<std::uint8_t> level0(expected.size(), 0xCDu);
        texture.GetData(level0.data(), static_cast<int>(level0.size()));
        EXPECT_EQ(level0, expected);
        std::vector<std::uint8_t> level1(mip.size(), 0xCDu);
        texture.GetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));
        EXPECT_EQ(level1, mip);
        EXPECT_TRUE(texture.GetCpuPixelsWeak().expired());

        const Texture2D copy = texture;
        EXPECT_TRUE(copy.GetCpuPixelsWeak().expired());
        EXPECT_EQ(copy.GetRendererWeak().lock(), texture.GetRendererWeak().lock());
        std::vector<std::uint8_t> copied(expected.size(), 0xCDu);
        copy.GetData(copied.data(), static_cast<int>(copied.size()));
        EXPECT_EQ(copied, expected);
    }
    if (exercised == 0)
        GTEST_SKIP() << "the active renderer does not transfer DXT blocks";
}

// A context or device loss restores a compressed texture from the renderer's own block store, so
// the texture still samples and reads back its blocks afterwards. On a renderer without a loss
// simulation both calls do nothing and the draw is an ordinary one.
TEST_F(Texture2DCompressedStorageTest, BlocksSurviveAContextLossWithoutAShadow)
{
    if (!TransfersBlocks(device, SurfaceFormat::Dxt1) ||
        !TransfersBlocks(device, SurfaceFormat::Dxt5))
        GTEST_SKIP() << "the active renderer does not transfer DXT blocks";

    // Dxt1: both endpoints red (0xF800), every index 0. Dxt5: alpha endpoints 200 and 40 with every
    // alpha index selecting the first, colour endpoints green (0x07E0), every colour index 0.
    const std::array<std::uint8_t, 8> red{0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00};
    const std::array<std::uint8_t, 16> green{200, 40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                             0xE0, 0x07, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};
    Texture2D dxt1(device, 4, 4, false, SurfaceFormat::Dxt1);
    dxt1.SetData(red.data(), static_cast<int>(red.size()));
    Texture2D dxt5(device, 4, 4, false, SurfaceFormat::Dxt5);
    dxt5.SetData(green.data(), static_cast<int>(green.size()));

    int lost = 0;
    int reset = 0;
    device.DeviceLost += [&lost](System::Object*, const System::EventArgs&) { ++lost; };
    device.DeviceReset += [&reset](System::Object*, const System::EventArgs&) { ++reset; };
    device.GetRenderer().DebugSimulateContextLoss();
    device.GetRenderer().DebugRestoreContext();
    // WebGPU reports its device loss and reset as XNA events; EasyGL recreates its context without
    // raising them, and the other renderers simulate nothing.
    RecordProperty("DeviceLostEvents", lost);
    RecordProperty("DeviceResetEvents", reset);
    EXPECT_EQ(lost, reset);

    EXPECT_TRUE(dxt1.GetCpuPixelsWeak().expired());
    EXPECT_TRUE(dxt5.GetCpuPixelsWeak().expired());

    std::array<std::uint8_t, 8> redBack{};
    dxt1.GetData(redBack.data(), static_cast<int>(redBack.size()));
    EXPECT_EQ(redBack, red);
    std::array<std::uint8_t, 16> greenBack{};
    dxt5.GetData(greenBack.data(), static_cast<int>(greenBack.size()));
    EXPECT_EQ(greenBack, green);

    const Color one = DrawWithSpriteBatch(device, dxt1);
    EXPECT_NEAR(one.getRProperty(), 255, 2);
    EXPECT_NEAR(one.getGProperty(), 0, 2);
    EXPECT_NEAR(one.getBProperty(), 0, 2);
    const Color five = DrawWithSpriteBatch(device, dxt5);
    EXPECT_NEAR(five.getRProperty(), 0, 2);
    EXPECT_NEAR(five.getGProperty(), 255, 2);
    EXPECT_NEAR(five.getBProperty(), 0, 2);
    EXPECT_NEAR(five.getAProperty(), 200, 2);
}
