// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
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

// RV-TEX review #3: a render target's IsContentLost latches its device's loss when read, as XNA's
// getter does (RenderTarget2D/RenderTargetCube.get_IsContentLost IL_0008-001f), without raising
// ContentLost. GraphicsDevice.get_IsDeviceLost (IL_0000-000b) counts a released device as lost.
TEST(GraphicsDeviceLossRefreshTest, ARenderTargetReadsItsDisposedDeviceAsContentLost)
{
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    RenderTarget2D flat(d, 4, 4);
    RenderTargetCube cube(d, 4, false, SurfaceFormat::Color, DepthFormat::None);
    int raised = 0;
    flat.ContentLost += [&](System::Object*, const System::EventArgs&) { ++raised; };
    cube.ContentLost += [&](System::Object*, const System::EventArgs&) { ++raised; };
    EXPECT_FALSE(flat.getIsContentLostProperty());
    EXPECT_FALSE(cube.getIsContentLostProperty());
    d.Dispose();
    EXPECT_TRUE(flat.getIsContentLostProperty());
    EXPECT_TRUE(cube.getIsContentLostProperty());
    EXPECT_EQ(raised, 0);
}

// The live loss: inside DeviceLost the device is Lost, so a target read there is already lost --
// not only after the reset that raises ContentLost.
TEST(GraphicsDeviceLossRefreshTest, ARenderTargetReadDuringDeviceLostIsContentLost)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    RenderTarget2D target(d, 4, 4);
    std::optional<bool> lostInHandler;
    d.DeviceLost += [&](System::Object*, const System::EventArgs&) { lostInHandler = target.getIsContentLostProperty(); };
    d.GetRenderer().DebugSimulateContextLoss();
    ASSERT_TRUE(lostInHandler.has_value());
    EXPECT_TRUE(*lostInHandler);
    d.GetRenderer().DebugRestoreContext();
    EXPECT_TRUE(target.getIsContentLostProperty()) << "the latch holds until the content is written";
}

TEST(GraphicsDeviceLossRefreshTest, RecoveryReportsLostNotResetNormalWithExactEventPayloads)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    std::vector<std::string> order;
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    auto observe = [&](const char* name, GraphicsDeviceStatus status, System::Object* sender, const System::EventArgs& args) {
        order.push_back(name); EXPECT_EQ(sender,&d); EXPECT_EQ(&args,&System::EventArgs::Empty);
        EXPECT_EQ(d.getGraphicsDeviceStatusProperty(),status);
    };
    // Refresh get_GraphicsDeviceStatus IL_0029-0045: DEVICELOST -> Lost(1),
    // DEVICENOTRESET -> NotReset(2), success -> Normal(0). Refresh Present core
    // IL_00ae-00bd raises DeviceLost(this,Empty); Reset IL_003d-0055/0297-02ac wraps recovery.
    // CNA's documented WebGPU debug loss/restore seam supplies those three renderer notifications.
    d.DeviceLost += [&](System::Object* s,const System::EventArgs& e) { observe("lost",GraphicsDeviceStatus::Lost,s,e); };
    d.DeviceResetting += [&](System::Object* s,const System::EventArgs& e) { observe("resetting",GraphicsDeviceStatus::NotReset,s,e); };
    d.DeviceReset += [&](System::Object* s,const System::EventArgs& e) { observe("reset",GraphicsDeviceStatus::Normal,s,e); };
    d.Disposing += [&](System::Object* s,const System::EventArgs& e) {
        order.push_back("disposing"); EXPECT_EQ(s,&d); EXPECT_EQ(&e,&System::EventArgs::Empty);
        EXPECT_TRUE(d.getIsDisposedProperty());
    };
    EXPECT_EQ(d.getGraphicsDeviceStatusProperty(),GraphicsDeviceStatus::Normal);
    d.GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(d.getGraphicsDeviceStatusProperty(),GraphicsDeviceStatus::Lost);
    EXPECT_FALSE(d.GetRenderer().CanBeginDrawEXT());
    d.GetRenderer().DebugRestoreContext();
    EXPECT_EQ(d.getGraphicsDeviceStatusProperty(),GraphicsDeviceStatus::Normal);
    EXPECT_TRUE(d.GetRenderer().CanBeginDrawEXT());
    d.Dispose(); d.Dispose();
    EXPECT_EQ(order,(std::vector<std::string>{"lost","resetting","reset","disposing"}));
}
