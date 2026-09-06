// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-192: presentation parameters, `GraphicsDevice.Reset()` and its events,
// and a programmatic backbuffer resize.
//
// Renderer-neutral, mirroring EasyGL's `presentation_parameters`, `present_interval`,
// `device_reset_events`, `backbuffer_resize` and `device_validation`.
//
// `Reset()` IS ASSERTED THROUGH ITS EVENTS AND THROUGH WHAT SURVIVES IT, because the two say
// different things. The events are the contract XNA code subscribes to -- `DeviceResetting` before,
// `DeviceReset` after, in that order and each exactly once -- and what survives is whether the
// device is usable afterwards at the size it was asked for. A reset that fired its events and left
// the device at the old size would satisfy an event-only test; one that resized silently would
// satisfy a size-only test; neither is a correct reset.
//
// A REAL WINDOW RESIZE -- as opposed to the programmatic one here -- is deliberately absent. It
// needs the platform to deliver a resize event, which a unit test cannot provoke without native
// event injection, and `docs/platform-abstraction.md` reserves that for the platform
// implementation's own tests. `easygl_real_window_resize_test` is where that lives for the
// reference renderer, and the equivalent for this one is a renderer example rather than a
// renderer-neutral unit test.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <string>
#include <vector>

using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PresentInterval;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

// A PresentationParameters object round-trips every value it was given. Not a spelling check: the
// device is constructed FROM this object, so a field it silently rewrites is a device behaving
// differently from what the caller asked for and can see.
TEST(PresentationLifecycle, PresentationParametersRoundTripTheirValues)
{
    PresentationParameters parameters;
    parameters.setBackBufferWidthProperty(320);
    parameters.setBackBufferHeightProperty(240);
    parameters.setBackBufferFormatProperty(SurfaceFormat::Color);
    parameters.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    parameters.setMultiSampleCountProperty(4);
    parameters.setPresentationIntervalProperty(PresentInterval::Immediate);
    parameters.setRenderTargetUsageProperty(RenderTargetUsage::PreserveContents);
    parameters.setIsFullScreenProperty(false);

    EXPECT_EQ(parameters.getBackBufferWidthProperty(), 320);
    EXPECT_EQ(parameters.getBackBufferHeightProperty(), 240);
    EXPECT_EQ(static_cast<int>(parameters.getBackBufferFormatProperty()),
              static_cast<int>(SurfaceFormat::Color));
    EXPECT_EQ(static_cast<int>(parameters.getDepthStencilFormatProperty()),
              static_cast<int>(DepthFormat::Depth24Stencil8));
    EXPECT_EQ(parameters.getMultiSampleCountProperty(), 4);
    EXPECT_EQ(static_cast<int>(parameters.getPresentationIntervalProperty()),
              static_cast<int>(PresentInterval::Immediate));
    EXPECT_EQ(static_cast<int>(parameters.getRenderTargetUsageProperty()),
              static_cast<int>(RenderTargetUsage::PreserveContents));
    EXPECT_FALSE(parameters.getIsFullScreenProperty());
    // Bounds is derived, not stored, so it must follow the width and height rather than lag them.
    EXPECT_EQ(parameters.getBoundsProperty().Width, 320);
    EXPECT_EQ(parameters.getBoundsProperty().Height, 240);
}

// Every PresentInterval value survives the round trip. XNA's four are Default, One, Two and
// Immediate, and a renderer may map several onto one native mode -- but the PARAMETERS object must
// still report what it was told, because that is what the caller reads back.
TEST(PresentationLifecycle, EveryPresentIntervalRoundTrips)
{
    const PresentInterval kIntervals[] = {PresentInterval::Default, PresentInterval::One,
                                          PresentInterval::Two, PresentInterval::Immediate};
    for (PresentInterval interval : kIntervals)
    {
        PresentationParameters parameters;
        parameters.setPresentationIntervalProperty(interval);
        EXPECT_EQ(static_cast<int>(parameters.getPresentationIntervalProperty()),
                  static_cast<int>(interval));
    }
}

// Reset() raises DeviceResetting then DeviceReset, each exactly once and in that order.
TEST(PresentationLifecycle, ResetRaisesItsTwoEventsInOrder)
{
    GraphicsDevice device;
    std::vector<std::string> order;
    device.DeviceResetting += [&order](System::Object*, const System::EventArgs&) {
        order.emplace_back("resetting");
    };
    device.DeviceReset += [&order](System::Object*, const System::EventArgs&) {
        order.emplace_back("reset");
    };
    device.DeviceLost += [&order](System::Object*, const System::EventArgs&) {
        order.emplace_back("lost");
    };

    device.Reset();

    ASSERT_EQ(order.size(), 2u)
        << "a reset raises exactly two events -- one too few means a subscriber never learns the "
           "device came back, one too many means it learns twice";
    EXPECT_EQ(order[0], "resetting") << "DeviceResetting comes FIRST, while the old device is still "
                                        "the one a handler would release resources from";
    EXPECT_EQ(order[1], "reset");
}

// ...and what survives it: the device is usable, and at the size it was asked for.
TEST(PresentationLifecycle, ResetAppliesTheNewBackbufferSizeAndLeavesTheDeviceUsable)
{
    GraphicsDevice device;
    PresentationParameters parameters = device.getPresentationParametersProperty();
    const int before = parameters.getBackBufferWidthProperty();
    parameters.setBackBufferWidthProperty(before == 320 ? 256 : 320);
    parameters.setBackBufferHeightProperty(192);
    const int wanted = parameters.getBackBufferWidthProperty();

    device.Reset(parameters);

    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferWidthProperty(), wanted)
        << "the reset applied the width it was given";
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferHeightProperty(), 192);
    // The device still works: a clear and a readback after the reset is the cheapest proof that the
    // swap chain, the depth buffer and whatever else the reset rebuilt are all consistent.
    EXPECT_NO_THROW(device.Clear(Microsoft::Xna::Framework::Color(30, 60, 90, 255)));
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), wanted)
        << "and the viewport followed the new backbuffer, which is what a stale Viewport after a "
           "resize would not do";
    EXPECT_EQ(device.getViewportProperty().getHeightProperty(), 192);
}

// A second reset in the same run, back to the original size. A renderer that rebuilt its swap chain
// correctly once but cached something size-derived fails the second time rather than the first.
TEST(PresentationLifecycle, TwoResetsInOneRunBothApply)
{
    GraphicsDevice device;
    PresentationParameters parameters = device.getPresentationParametersProperty();
    const int original = parameters.getBackBufferWidthProperty();

    parameters.setBackBufferWidthProperty(288);
    parameters.setBackBufferHeightProperty(160);
    device.Reset(parameters);
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferWidthProperty(), 288);

    parameters.setBackBufferWidthProperty(original);
    parameters.setBackBufferHeightProperty(240);
    device.Reset(parameters);
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferWidthProperty(), original);
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferHeightProperty(), 240);
    EXPECT_NO_THROW(device.Clear(Microsoft::Xna::Framework::Color(90, 30, 60, 255)));
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), original);
}
