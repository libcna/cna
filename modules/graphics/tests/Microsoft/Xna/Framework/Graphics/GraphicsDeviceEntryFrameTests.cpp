// SPDX-License-Identifier: MS-PL
// TQ-048: independent Refresh entry-point contracts.
#include <gtest/gtest.h>
#include <typeinfo>
#include <string>
#include <vector>
#include <limits>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "CNA/RendererTestGate.hpp"
#include <array>
#include <optional>

using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
    // Only the diagnostic body is XNA-owned. sharp-runtime selects the Framework/Core
    // parameter suffix; remove that suffix, without manufacturing an expected CNA message.
    [[maybe_unused]] std::string DiagnosticBody(const System::Exception& error)
    {
        std::string message = error.getMessageProperty();
        auto pos = message.find("\nParameter name:");
        if (pos == std::string::npos) pos = message.find(" (Parameter '");
        return message.substr(0, pos);
    }
    template<class E, class F>
    void ExpectArgument(const char* parameter, const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "call accepted: " << parameter; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(e.getParamNameProperty(), parameter);
            EXPECT_EQ(DiagnosticBody(e), body);
        }
    }
}

TEST(GraphicsDeviceEntryFrameTest, ViewportAndScissorRefuseWithoutClampingAndKeepAllFields)
{
    GraphicsDevice device;
    const Viewport original = device.getViewportProperty();
    Viewport valid(1, 2, 3, 4);
    valid.setMinDepthProperty(0.25f);
    valid.setMaxDepthProperty(0.75f);
    device.setViewportProperty(valid);
    // Refresh set_Viewport IL_0015-0113: ArgumentException(value), ViewportInvalid.
    Viewport bad(0, 0, original.getWidthProperty() + 1, 1);
    ExpectArgument<System::ArgumentException>("value",
        "The viewport is invalid. The viewport cannot be larger than or outside of the current render target bounds. The MinDepth and MaxDepth must be between 0 and 1.", [&] {
        device.setViewportProperty(bad);
    });
    const auto actual = device.getViewportProperty();
    EXPECT_EQ(actual.getXProperty(), 1); EXPECT_EQ(actual.getYProperty(), 2);
    EXPECT_EQ(actual.getWidthProperty(), 3); EXPECT_EQ(actual.getHeightProperty(), 4);
    // 1/4 and 3/4 are exact IEEE binary32 values; no tolerance is needed.
    EXPECT_EQ(actual.getMinDepthProperty(), 0.25f);
    EXPECT_EQ(actual.getMaxDepthProperty(), 0.75f);
    const Microsoft::Xna::Framework::Rectangle scissor(1, 2, 3, 4);
    device.setScissorRectangleProperty(scissor);
    // Refresh set_ScissorRectangle IL_0015-012a; both endpoints may equal the surface bound.
    ExpectArgument<System::ArgumentException>("value",
        "The scissor rectangle is invalid. The scissor rectangle cannot be larger than or outside of the current render target bounds.", [&] {
        device.setScissorRectangleProperty(Microsoft::Xna::Framework::Rectangle(0, 0, original.getWidthProperty() + 1, 1));
    });
    EXPECT_EQ(device.getScissorRectangleProperty(), scissor);
    const Microsoft::Xna::Framework::Rectangle empty(original.getWidthProperty(), original.getHeightProperty(), 0, 0);
    device.setScissorRectangleProperty(empty);
    EXPECT_EQ(device.getScissorRectangleProperty(), empty);
}
TEST(GraphicsDeviceEntryFrameTest, ClearIgnoresClippingButPreservesViewportScissorAndState)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
    GraphicsDevice device;
    RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None);
    device.SetRenderTarget(&target);
    const Viewport viewport(2, 2, 2, 2);
    const Microsoft::Xna::Framework::Rectangle scissor(3, 3, 1, 1);
    device.setViewportProperty(viewport);
    device.setScissorRectangleProperty(scissor);
    RasterizerState rasterizer;
    rasterizer.setScissorTestEnableProperty(true);
    device.setRasterizerStateProperty(rasterizer);
    // Clear(Color) -> DefaultClearOptions, depth=1, stencil=0 (Refresh IL_0000-001d).
    // Clear core disables scissor IL_0036-0056 and expands/restores viewport IL_0096-0119/0183-01bd.
    const Microsoft::Xna::Framework::Color color(10, 20, 30, 40);
    device.Clear(color);
    EXPECT_EQ(device.getViewportProperty().getXProperty(), 2);
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), 2);
    EXPECT_EQ(device.getScissorRectangleProperty(), scissor);
    EXPECT_TRUE(device.getRasterizerStateProperty().getScissorTestEnableProperty());
    device.SetRenderTarget(nullptr);
    std::array<Microsoft::Xna::Framework::Color, 64> actual{};
    target.GetData(actual.data(), 64);
    for (const auto& pixel : actual) EXPECT_EQ(pixel, color);
}
TEST(GraphicsDeviceEntryFrameTest, RefusedClearAndPresentPreserveTargetAndPixels)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
    GraphicsDevice device;
    RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    device.SetRenderTarget(&target);
    device.Clear(Microsoft::Xna::Framework::Color::Red);
    // Clear core IL_01c6-01db: missing depth/stencil; Present core IL_0013-0026:
    // target refusal precedes native Present and region processing.
    try { device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Microsoft::Xna::Framework::Color::Blue, 1.0f, 0); ADD_FAILURE(); }
    catch (const System::InvalidOperationException& e)
    {
        EXPECT_EQ(typeid(e), typeid(System::InvalidOperationException));
        EXPECT_EQ(e.getMessageProperty(), "Cannot clear depth or stencil because the device does not have an active depth or stencil buffer.");
    }
    for (bool region : {false, true})
    {
        try
        {
            if (region) device.Present(Microsoft::Xna::Framework::Rectangle(0, 0, 0, 0), std::nullopt, 0);
            else device.Present();
            ADD_FAILURE();
        }
        catch (const System::InvalidOperationException& e)
        {
            EXPECT_EQ(typeid(e), typeid(System::InvalidOperationException));
            EXPECT_EQ(e.getMessageProperty(), "Cannot call Present when a render target is active.");
        }
        ASSERT_EQ(device.GetRenderTargets().size(), 1u);
        EXPECT_EQ(device.GetRenderTargets()[0].getRenderTargetProperty(), &target);
    }
    device.SetRenderTarget(nullptr);
    std::array<Microsoft::Xna::Framework::Color, 16> actual{};
    target.GetData(actual.data(), 16);
    for (const auto& pixel : actual) EXPECT_EQ(pixel, Microsoft::Xna::Framework::Color::Red);
}
