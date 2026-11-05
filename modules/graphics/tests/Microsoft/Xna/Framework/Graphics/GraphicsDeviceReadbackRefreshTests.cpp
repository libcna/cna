// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    PresentationParameters Parameters(int width = 8, int height = 6)
    {
        auto p = PresentationParameters::CreateWindowedEXT(800, 480);
        p.setBackBufferWidthProperty(width);
        p.setBackBufferHeightProperty(height);
        p.setBackBufferFormatProperty(SurfaceFormat::Color);
        p.setMultiSampleCountProperty(0);
        return p;
    }
    template<class E, class F>
    void Argument(const char* parameter, const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "accepted invalid call"; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(e.getParamNameProperty(), parameter);
            std::string text = e.getMessageProperty();
            auto end = text.find(" (Parameter ");
            if (end == std::string::npos) end = text.find("\nParameter name:");
            EXPECT_EQ(text.substr(0, end), body);
        }
    }
    template<class E, class F>
    void Refusal(const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "accepted invalid call"; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(std::string(e.what()), body);
        }
    }
}

TEST(GraphicsDeviceReadbackRefreshTest, DisposalThenReachProfilePrecedeEveryOverloadsDataChecks)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, Parameters());
    // Refresh GetBackBufferData<T>(rect,...) IL_0000-0037; wrappers IL_0008-0012/0012-001c.
    for (int form = 0; form != 6; ++form)
    {
        auto call = [&] {
            switch (form) {
            case 0: device.GetBackBufferData(static_cast<Color*>(nullptr), 0); break;
            case 1: device.GetBackBufferData(static_cast<Color*>(nullptr), -1, 0); break;
            case 2: device.GetBackBufferData(nullptr, static_cast<Color*>(nullptr), -1, 0); break;
            case 3: device.GetBackBufferData(static_cast<std::uint8_t*>(nullptr), 0); break;
            case 4: device.GetBackBufferData(static_cast<std::uint8_t*>(nullptr), -1, 0); break;
            case 5: device.GetBackBufferData(nullptr, static_cast<std::uint8_t*>(nullptr), -1, 0); break;
            }
        };
        Refusal<System::NotSupportedException>("XNA Framework Reach profile does not support GetBackBufferData.", call);
    }
    device.Dispose();
    EXPECT_THROW(device.GetBackBufferData(static_cast<Color*>(nullptr), 0), System::ObjectDisposedException);
    EXPECT_THROW(device.GetBackBufferData(static_cast<Color*>(nullptr), -1, 0), System::ObjectDisposedException);
    EXPECT_THROW(device.GetBackBufferData(nullptr, static_cast<Color*>(nullptr), -1, 0), System::ObjectDisposedException);
    EXPECT_THROW(device.GetBackBufferData(static_cast<std::uint8_t*>(nullptr), 0), System::ObjectDisposedException);
    EXPECT_THROW(device.GetBackBufferData(static_cast<std::uint8_t*>(nullptr), -1, 0), System::ObjectDisposedException);
    EXPECT_THROW(device.GetBackBufferData(nullptr, static_cast<std::uint8_t*>(nullptr), -1, 0), System::ObjectDisposedException);
}

TEST(GraphicsDeviceReadbackRefreshTest, NullDataWinsAndNegativeIndexUsesHelpersDataIndexName)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    const Rectangle outside(-1, 0, 1, 1);
    // Refresh core IL_0037-0052/0253-0262; Framework Helpers.ValidateCopyParameters IL_0000-0041.
    auto nullError = [&](auto action) {
        Argument<System::ArgumentNullException>("data", "This method does not accept null for this parameter.", action);
    };
    nullError([&] { device.GetBackBufferData(static_cast<Color*>(nullptr), 1); });
    nullError([&] { device.GetBackBufferData(static_cast<Color*>(nullptr), -1, 0); });
    nullError([&] { device.GetBackBufferData(&outside, static_cast<Color*>(nullptr), -1, 0); });
    nullError([&] { device.GetBackBufferData(static_cast<std::uint8_t*>(nullptr), 4); });
    nullError([&] { device.GetBackBufferData(static_cast<std::uint8_t*>(nullptr), -1, 0); });
    nullError([&] { device.GetBackBufferData(&outside, static_cast<std::uint8_t*>(nullptr), -1, 0); });
    Color pixel = Color::Magenta;
    Argument<System::ArgumentOutOfRangeException>("dataIndex", "This parameter must be a valid index within the array.",
        [&] { device.GetBackBufferData(&outside, &pixel, -1, 0); });
    Argument<System::ArgumentOutOfRangeException>("elementCount", "This parameter must be a valid index within the array.",
        [&] { device.GetBackBufferData(&outside, &pixel, 0, 0); });
    Argument<System::ArgumentOutOfRangeException>("elementCount", "This parameter must be a valid index within the array.",
        [&] { device.GetBackBufferData(&outside, &pixel, 0, -1); });
    EXPECT_EQ(pixel, Color::Magenta);
}

TEST(GraphicsDeviceReadbackRefreshTest, ElementSizePrecedesRectangleAndRectanglePrecedesTotalBytes)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    struct Three { std::uint8_t bytes[3]; } three{};
    struct Eight { std::uint8_t bytes[8]; } eight{};
    const Rectangle outside(-1, 0, 1, 1);
    // Refresh core IL_00b7-00da; Texture.GetAndValidateSizes IL_0012-0041.
    // Color format=4 bytes: 4%3 != 0, and sizeof(Eight)=8 > 4, so neither type is legal.
    Argument<System::ArgumentException>("", "The type you are using for T in this method is an invalid size for this resource.",
        [&] { device.GetBackBufferData(&outside, &three, 0, 1); });
    Argument<System::ArgumentException>("", "The type you are using for T in this method is an invalid size for this resource.",
        [&] { device.GetBackBufferData(&outside, &eight, 0, 1); });
    Color pixel = Color::Magenta;
    // Texture.GetAndValidateRect IL_0035-0115 (ParamName rect), then ValidateTotalSize IL_005e-0076.
    for (const auto rect : {Rectangle(-1,0,1,1), Rectangle(0,-1,1,1), Rectangle(8,0,1,1),
         Rectangle(0,6,1,1), Rectangle(7,5,2,1), Rectangle(0,0,0,1), Rectangle(0,0,1,-1)})
        Argument<System::ArgumentException>("rect", "The rectangle is too large or too small for this resource.",
            [&] { device.GetBackBufferData(&rect, &pixel, 0, 2); });
    const Rectangle one(7,5,1,1);
    Argument<System::ArgumentException>("", "The size of the data passed in is too large or too small for this resource.",
        [&] { device.GetBackBufferData(&one, &pixel, 0, 2); });
    // Full Color buffer=8*6*4=192 bytes, 47 Colors supply 188; raw bytes supply 191 or 193.
    std::array<Color, 48> colors{};
    std::array<std::uint8_t, 193> bytes{};
    Argument<System::ArgumentException>("", "The size of the data passed in is too large or too small for this resource.",
        [&] { device.GetBackBufferData(colors.data(), 47); });
    for (const int count : {191, 193})
        Argument<System::ArgumentException>("", "The size of the data passed in is too large or too small for this resource.",
            [&] { device.GetBackBufferData(bytes.data(), 0, count); });
    EXPECT_EQ(pixel, Color::Magenta);
}

TEST(GraphicsDeviceReadbackRefreshTest, BoundTargetRefusalFollowsRangesAndPrecedesSizeAndRectangle)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software, CNA::GraphicsRendererType::OpenGL33, CNA::GraphicsRendererType::OpenGLES3);
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    RenderTarget2D target(device, 2, 2);
    device.SetRenderTarget(&target);
    struct Three { std::uint8_t bytes[3]; } bad{};
    const Rectangle outside(-1,0,1,1);
    // Refresh GetBackBufferData core IL_0048-0065: copy checks before target check; target before format/rect.
    Argument<System::ArgumentOutOfRangeException>("dataIndex", "This parameter must be a valid index within the array.",
        [&] { device.GetBackBufferData(&outside, &bad, -1, 1); });
    Refusal<System::InvalidOperationException>("Cannot use GetBackBufferData when a render target is active.",
        [&] { device.GetBackBufferData(&outside, &bad, 0, 1); });
    ASSERT_EQ(device.GetRenderTargets().size(), 1u);
    EXPECT_EQ(device.GetRenderTargets()[0].getRenderTargetProperty(), &target);
    EXPECT_EQ(bad.bytes[0], 0u);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
}
