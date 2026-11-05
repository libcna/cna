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

namespace
{
    void Geometry(const GraphicsDevice& d, int x, int y, int w, int h, float lo, float hi, Rectangle scissor)
    {
        const auto v = d.getViewportProperty();
        EXPECT_EQ(v.getXProperty(), x); EXPECT_EQ(v.getYProperty(), y);
        EXPECT_EQ(v.getWidthProperty(), w); EXPECT_EQ(v.getHeightProperty(), h);
        // 0,1,1/4,3/4 are exactly representable binary32; no tolerance is needed.
        EXPECT_EQ(v.getMinDepthProperty(), lo); EXPECT_EQ(v.getMaxDepthProperty(), hi);
        EXPECT_EQ(d.getScissorRectangleProperty(), scissor);
    }
}
TEST(GraphicsDeviceResetGeometryRefreshTest, SameSizePreservesEveryViewportAndScissorFieldAcrossAllOverloads)
{
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    Viewport sub(1,2,3,2); sub.setMinDepthProperty(0.25f); sub.setMaxDepthProperty(0.75f);
    const Rectangle clip(2,1,4,3);
    d.setViewportProperty(sub); d.setScissorRectangleProperty(clip);
    // Refresh Reset core IL_005a-0098: save geometry only with no target and unchanged W/H.
    // SavedDeviceState::.ctor IL_0125-014d, Restore IL_0187-01d0.
    for (int form = 0; form != 4; ++form)
    {
        const auto p = Parameters();
        switch (form) {
        case 0: d.Reset(); break;
        case 1: d.Reset(p); break;
        case 2: d.Reset(p, GraphicsAdapter::getDefaultAdapterProperty()); break;
        case 3: d.Reset(p, &GraphicsAdapter::getDefaultAdapterProperty()); break;
        }
        Geometry(d,1,2,3,2,0.25f,0.75f,clip);
    }
}
TEST(GraphicsDeviceResetGeometryRefreshTest, ResizeAndBoundTargetResetUseWholeNewBufferAtResetNotification)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software, CNA::GraphicsRendererType::OpenGL33, CNA::GraphicsRendererType::OpenGLES3);
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    RenderTarget2D target(d,2,2);
    int notifications = 0;
    bool boundCase = false;
    d.DeviceResetting += [&](System::Object* sender, const System::EventArgs& e) {
        EXPECT_EQ(sender, &d); EXPECT_EQ(&e, &System::EventArgs::Empty);
        if (boundCase) {
            ASSERT_EQ(d.GetRenderTargets().size(), 1u);
            EXPECT_EQ(d.GetRenderTargets()[0].getRenderTargetProperty(), &target);
        }
    };
    d.DeviceReset += [&](System::Object* sender, const System::EventArgs& e) {
        ++notifications;
        EXPECT_EQ(sender, &d); EXPECT_EQ(&e, &System::EventArgs::Empty);
        // Refresh Reset IL_0102-0105 unbinds; IL_028c-02ac initializes/restores before notification.
        EXPECT_TRUE(d.GetRenderTargets().empty());
        Geometry(d,0,0,12,10,0.0f,1.0f,Rectangle(0,0,12,10));
    };
    Viewport sub(1,1,3,2); sub.setMinDepthProperty(0.25f); sub.setMaxDepthProperty(0.75f);
    d.setViewportProperty(sub); d.setScissorRectangleProperty(Rectangle(1,1,2,2));
    d.Reset(Parameters(12,10));
    boundCase = true;
    d.SetRenderTarget(&target);
    d.setScissorRectangleProperty(Rectangle(1,1,1,1));
    d.Reset();
    EXPECT_EQ(notifications, 2);
    EXPECT_FALSE(target.getIsDisposedProperty());
}
