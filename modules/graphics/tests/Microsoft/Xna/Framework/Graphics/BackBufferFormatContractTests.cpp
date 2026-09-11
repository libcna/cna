// SPDX-License-Identifier: MS-PL
// SOFTWARE-180: fixed-format renderers must report the backbuffer they actually allocate.

#include <array>
#include <cstdint>
#include <limits>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace
{
    struct FourBytePod
    {
        std::uint8_t bytes[4];
    };

    struct ThreeBytePod
    {
        std::uint8_t bytes[3];
    };

    PresentationParameters RequestedPackedBackbuffer(SurfaceFormat format)
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(8);
        parameters.setBackBufferHeightProperty(8);
        parameters.setBackBufferFormatProperty(format);
        return parameters;
    }

    void ExpectColorReadback(GraphicsDevice& device, const Color& expected)
    {
        device.Clear(expected);
        std::array<Color, 64> pixels{};
        ASSERT_NO_THROW(device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())));
        for (const Color& pixel : pixels) EXPECT_EQ(pixel, expected);
    }
}

TEST(BackBufferFormatContractTest, GenericReadbackPreservesRawBytesAndElementWindows)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    PresentationParameters parameters = RequestedPackedBackbuffer(SurfaceFormat::Color);
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    const Color clear(19, 73, 141, 211);
    device.Clear(clear);

    constexpr int byteCount = 8 * 8 * 4;
    std::array<std::uint8_t, byteCount + 4> bytes{};
    bytes.fill(0xCDu);
    device.GetBackBufferData(bytes.data(), 2, byteCount);
    EXPECT_EQ(bytes[0], 0xCDu);
    EXPECT_EQ(bytes[1], 0xCDu);
    EXPECT_EQ(bytes[byteCount + 2], 0xCDu);
    EXPECT_EQ(bytes[byteCount + 3], 0xCDu);
    for (int i = 0; i < 8 * 8; ++i)
    {
        const std::size_t offset = 2u + static_cast<std::size_t>(i) * 4u;
        EXPECT_EQ(bytes[offset + 0], clear.getRProperty());
        EXPECT_EQ(bytes[offset + 1], clear.getGProperty());
        EXPECT_EQ(bytes[offset + 2], clear.getBProperty());
        EXPECT_EQ(bytes[offset + 3], clear.getAProperty());
    }

    std::array<FourBytePod, 8 * 8> completePods{};
    device.GetBackBufferData(completePods.data(), static_cast<int>(completePods.size()));
    for (const FourBytePod& value : completePods)
    {
        EXPECT_EQ(value.bytes[0], clear.getRProperty());
        EXPECT_EQ(value.bytes[1], clear.getGProperty());
        EXPECT_EQ(value.bytes[2], clear.getBProperty());
        EXPECT_EQ(value.bytes[3], clear.getAProperty());
    }

    std::array<std::uint16_t, byteCount / 2 + 2> words{};
    words.fill(0xCDCDu);
    device.GetBackBufferData(words.data(), 1, byteCount / 2);
    const auto* wordBytes = reinterpret_cast<const std::uint8_t*>(words.data());
    EXPECT_EQ(wordBytes[0], 0xCDu);
    EXPECT_EQ(wordBytes[1], 0xCDu);
    for (int i = 0; i < 8 * 8; ++i)
    {
        const std::size_t offset = 2u + static_cast<std::size_t>(i) * 4u;
        EXPECT_EQ(wordBytes[offset + 0], clear.getRProperty());
        EXPECT_EQ(wordBytes[offset + 1], clear.getGProperty());
        EXPECT_EQ(wordBytes[offset + 2], clear.getBProperty());
        EXPECT_EQ(wordBytes[offset + 3], clear.getAProperty());
    }

    const Rectangle onePixel(3, 5, 1, 1);
    FourBytePod pod{{0, 0, 0, 0}};
    device.GetBackBufferData(&onePixel, &pod, 0, 1);
    EXPECT_EQ(pod.bytes[0], clear.getRProperty());
    EXPECT_EQ(pod.bytes[1], clear.getGProperty());
    EXPECT_EQ(pod.bytes[2], clear.getBProperty());
    EXPECT_EQ(pod.bytes[3], clear.getAProperty());

    EXPECT_THROW(device.GetBackBufferData(
                     &onePixel, static_cast<std::uint8_t*>(nullptr), 0, 4),
                 System::ArgumentNullException);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, &pod, 0, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, &pod, 0, 2),
                 System::ArgumentException);
    ThreeBytePod invalidWidth{{0, 0, 0}};
    EXPECT_THROW(device.GetBackBufferData(&onePixel, &invalidWidth, 0, 1),
                 System::ArgumentException);
}

TEST(BackBufferFormatContractTest, ConstructionReportsAndReadsTheActuallyAppliedFormat)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    PresentationParameters parameters = RequestedPackedBackbuffer(SurfaceFormat::Bgr565);
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);

    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferFormatProperty(),
              SurfaceFormat::Color);
    ExpectColorReadback(device, Color(19, 73, 141, 255));
}

TEST(BackBufferFormatContractTest, ResetAlsoReportsAndReadsTheActuallyAppliedFormat)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    PresentationParameters parameters = RequestedPackedBackbuffer(SurfaceFormat::Bgra4444);
    device.Reset(parameters);

    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferFormatProperty(),
              SurfaceFormat::Color);
    ExpectColorReadback(device, Color(211, 37, 89, 255));
}

TEST(BackBufferFormatContractTest, ReadbackRejectsMalformedDestinationAndRectangleRanges)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    PresentationParameters parameters = RequestedPackedBackbuffer(SurfaceFormat::Color);
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    std::array<Color, 2> pixels{Color::Red, Color::Blue};
    const Rectangle onePixel(0, 0, 1, 1);
    const Rectangle overflowingX(std::numeric_limits<int>::max(), 0, 1, 1);
    const Rectangle overflowingY(0, std::numeric_limits<int>::max(), 1, 1);

    EXPECT_THROW(device.GetBackBufferData(
                     &onePixel, static_cast<Color*>(nullptr), 0, 1),
                 System::ArgumentNullException);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, pixels.data() + 1, -1, 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, pixels.data(), 0, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, pixels.data(),
                                          std::numeric_limits<int>::max(), 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, pixels.data(), 0, 2),
                 System::ArgumentException);
    EXPECT_THROW(device.GetBackBufferData(&overflowingX, pixels.data(), 0, 1),
                 System::ArgumentException);
    EXPECT_THROW(device.GetBackBufferData(&overflowingY, pixels.data(), 0, 1),
                 System::ArgumentException);
}

TEST(BackBufferFormatContractTest, ReadbackRejectsAnActiveRenderTarget)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    PresentationParameters parameters = RequestedPackedBackbuffer(SurfaceFormat::Color);
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    RenderTarget2D target(device, 1, 1);
    Color pixel = Color::Magenta;
    const Rectangle onePixel(0, 0, 1, 1);

    device.SetRenderTarget(&target);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, &pixel, 0, 1),
                 System::InvalidOperationException);
    EXPECT_EQ(pixel, Color::Magenta);
    ASSERT_EQ(device.GetRenderTargets().size(), 1u);
    EXPECT_EQ(device.GetRenderTargets()[0].getRenderTargetProperty(), &target);

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
}
