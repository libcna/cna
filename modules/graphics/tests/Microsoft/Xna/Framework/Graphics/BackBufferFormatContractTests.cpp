// SPDX-License-Identifier: MS-PL
// SOFTWARE-180: fixed-format renderers must report the backbuffer they actually allocate.

#include <array>
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
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace
{
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

    EXPECT_THROW(device.GetBackBufferData(&onePixel, pixels.data() + 1, -1, 1),
                 std::out_of_range);
    EXPECT_THROW(device.GetBackBufferData(&onePixel, pixels.data(),
                                          std::numeric_limits<int>::max(), 1),
                 std::out_of_range);
    EXPECT_THROW(device.GetBackBufferData(&overflowingX, pixels.data(), 0, 1),
                 std::out_of_range);
    EXPECT_THROW(device.GetBackBufferData(&overflowingY, pixels.data(), 0, 1),
                 std::out_of_range);
}
