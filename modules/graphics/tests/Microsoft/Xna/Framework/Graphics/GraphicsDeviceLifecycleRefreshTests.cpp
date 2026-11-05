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

TEST(GraphicsDeviceLifecycleRefreshTest, ResetAndDisposalHaveExactSenderArgsOrderAndNoLoss)
{
    std::vector<std::string> events; // Outlives d, including explicit disposal or a fatal assertion.
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    auto& adapter = GraphicsAdapter::getDefaultAdapterProperty();
    auto check = [&](System::Object* sender, const System::EventArgs& args) {
        EXPECT_EQ(sender, &d); EXPECT_EQ(&args, &System::EventArgs::Empty);
    };
    d.DeviceResetting += [&](System::Object* s, const System::EventArgs& a) {
        check(s,a); events.push_back("resetting");
        EXPECT_EQ(d.getPresentationParametersProperty().getBackBufferWidthProperty(), 8);
        EXPECT_EQ(d.getGraphicsDeviceStatusProperty(), GraphicsDeviceStatus::Normal);
    };
    d.DeviceReset += [&](System::Object* s, const System::EventArgs& a) {
        check(s,a); events.push_back("reset");
        EXPECT_EQ(d.getPresentationParametersProperty().getBackBufferWidthProperty(), 12);
        EXPECT_EQ(&d.getAdapterProperty(), &adapter);
        EXPECT_EQ(d.getGraphicsDeviceStatusProperty(), GraphicsDeviceStatus::Normal);
    };
    d.DeviceLost += [&](System::Object* s, const System::EventArgs& a) { check(s,a); events.push_back("lost"); };
    d.Disposing += [&](System::Object* s, const System::EventArgs& a) {
        EXPECT_EQ(s, &d); EXPECT_EQ(&a, &System::EventArgs::Empty);
        events.push_back("disposing"); EXPECT_TRUE(d.getIsDisposedProperty());
        // Refresh metadata getters IL_0000-0006 have no disposed check.
        EXPECT_EQ(&d.getAdapterProperty(), &adapter);
        EXPECT_EQ(d.getPresentationParametersProperty().getBackBufferWidthProperty(), 12);
        EXPECT_THROW(static_cast<void>(d.getGraphicsDeviceStatusProperty()), System::ObjectDisposedException);
        EXPECT_THROW(static_cast<void>(d.getDisplayModeProperty()), System::ObjectDisposedException);
        d.Dispose(); // reentrant notification must not recurse.
    };
    // Refresh Reset IL_003d-0055 and IL_0297-02ac; ~GraphicsDevice IL_0000-0026.
    d.Reset(Parameters(12,10));
    d.Dispose(); d.Dispose();
    EXPECT_EQ(events, (std::vector<std::string>{"resetting","reset","disposing"}));
}

TEST(GraphicsDeviceLifecycleRefreshTest, WindowedDisplayUsesAdapterModeWhileNormalStatusSurvivesPresent)
{
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    const auto mode = d.getAdapterProperty().getCurrentDisplayModeProperty();
    // Refresh get_DisplayMode IL_0013-0024 queries swap-chain display, not backbuffer 8x6.
    const auto actual = d.getDisplayModeProperty();
    EXPECT_EQ(actual.getWidthProperty(), mode.getWidthProperty());
    EXPECT_EQ(actual.getHeightProperty(), mode.getHeightProperty());
    EXPECT_EQ(actual.getFormatProperty(), mode.getFormatProperty());
    // get_GraphicsDeviceStatus IL_0025-0045: successful cooperative-level query -> Normal.
    EXPECT_EQ(d.getGraphicsDeviceStatusProperty(), GraphicsDeviceStatus::Normal);
    Viewport sub(1,2,3,2); sub.setMinDepthProperty(0.25f); sub.setMaxDepthProperty(0.75f);
    const Rectangle clip(2,1,4,3);
    d.setViewportProperty(sub); d.setScissorRectangleProperty(clip);
    for (int form = 0; form != 2; ++form)
    {
        if (form == 0) d.Present(); else d.Present(std::nullopt,std::nullopt,0);
        // Refresh Present core IL_0000-00ce never assigns viewport/scissor.
        EXPECT_EQ(d.getViewportProperty().getXProperty(), 1);
        EXPECT_EQ(d.getViewportProperty().getYProperty(), 2);
        EXPECT_EQ(d.getViewportProperty().getWidthProperty(), 3);
        EXPECT_EQ(d.getViewportProperty().getHeightProperty(), 2);
        EXPECT_EQ(d.getViewportProperty().getMinDepthProperty(), 0.25f);
        EXPECT_EQ(d.getViewportProperty().getMaxDepthProperty(), 0.75f);
        EXPECT_EQ(d.getScissorRectangleProperty(), clip);
        EXPECT_EQ(d.getGraphicsDeviceStatusProperty(), GraphicsDeviceStatus::Normal);
    }
}

TEST(GraphicsDevicePresentRefreshTest, BothOverloadsRefuseActiveTargetBeforeInvalidRectangleAndPreserveBinding)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software, CNA::GraphicsRendererType::OpenGL33, CNA::GraphicsRendererType::OpenGLES3);
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    RenderTarget2D target(d,2,2);
    d.SetRenderTarget(&target);
    const auto viewport = d.getViewportProperty();
    // Refresh Present core IL_0000-0026: disposed -> active target -> native rectangle handling.
    Refusal<System::InvalidOperationException>("Cannot call Present when a render target is active.", [&] { d.Present(); });
    Refusal<System::InvalidOperationException>("Cannot call Present when a render target is active.", [&] {
        d.Present(Rectangle(0,0,0,1), Rectangle(0,0,1,-1), 0);
    });
    ASSERT_EQ(d.GetRenderTargets().size(),1u);
    EXPECT_EQ(d.GetRenderTargets()[0].getRenderTargetProperty(), &target);
    EXPECT_EQ(d.getViewportProperty().getWidthProperty(), viewport.getWidthProperty());
    d.Dispose();
    EXPECT_THROW(d.Present(), System::ObjectDisposedException);
    EXPECT_THROW(d.Present(Rectangle(0,0,0,1),std::nullopt,0), System::ObjectDisposedException);
}
