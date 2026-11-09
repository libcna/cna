// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"
#include <optional>
#include <string>
#include <algorithm>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#if defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_PRESENT_HEADLESS)
#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"
namespace {
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Headless;
std::string LastRegionArgs(const HeadlessRenderer& renderer)
{
    const auto& trace = renderer.TraceLog();
    const auto found = std::find_if(trace.rbegin(), trace.rend(), [](const auto& e) {
        return e.method == "PresentRegion";
    });
    return found == trace.rend() ? "<missing PresentRegion dispatch>" : found->argsSummary;
}
PresentationParameters PresentParameters()
{
    auto p = PresentationParameters::CreateWindowedEXT(8, 6);
    p.setHeadlessEXTProperty(true);
    return p;
}
}

TEST(Gap08PresentDispatch, DefaultOverloadDispatchesOrdinaryPresentExactlyOnce)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Headless);
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentParameters());
    auto& renderer = dynamic_cast<HeadlessRenderer&>(device.GetRenderer());
    renderer.SetMode(HeadlessMode::Trace);
    const auto count = renderer.GetStatistics().presentCount;
    // HND-TQ-47 R1: Present IL_003e-0047 first performs a pending lazy clear (a native Clear) and
    // IL_009e-00a5 arm it again, so clear the frame before each measured Present to leave nothing
    // pending and see the one native present the call itself dispatches.
    device.Clear(Microsoft::Xna::Framework::Color::Black);
    auto traceSize = renderer.TraceLog().size();
    // Refresh GraphicsDevice.Present() IL_0000-0009 passes three nulls to the same core.
    device.Present(std::nullopt, std::nullopt, 0);
    ASSERT_EQ(renderer.TraceLog().size(), traceSize + 1);
    EXPECT_EQ(renderer.TraceLog().back().method, "Present");
    EXPECT_EQ(renderer.GetStatistics().presentCount, count + 1);
    device.Clear(Microsoft::Xna::Framework::Color::Black);
    traceSize = renderer.TraceLog().size();
    device.Present();
    ASSERT_EQ(renderer.TraceLog().size(), traceSize + 1);
    EXPECT_EQ(renderer.TraceLog().back().method, "Present");
    EXPECT_EQ(renderer.GetStatistics().presentCount, count + 2);
}

TEST(Gap08PresentDispatch, SourceClippingUsesTheLiveLogicalBackbuffer)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Headless);
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentParameters());
    auto& renderer = dynamic_cast<HeadlessRenderer&>(device.GetRenderer());
    renderer.SetMode(HeadlessMode::Trace);
    renderer.SetVirtualResolution(12, 6);
    // CNA logical-surface contract: [10,14) intersect [0,12) = [10,12), width 2.
    // No claim that XNA has CNA's dynamic virtual-resolution extension.
    device.Present(Rectangle(10, 1, 4, 3), std::nullopt, 0);
    EXPECT_EQ(LastRegionArgs(renderer),
              "frameIndex=1 source=10,1,2x3");
}

TEST(Gap08PresentDispatch, ClippingDoesNotEraseTheOriginalDestinationMapping)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Headless);
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentParameters());
    auto& renderer = dynamic_cast<HeadlessRenderer&>(device.GetRenderer());
    renderer.SetMode(HeadlessMode::Trace);
    // Proposed CNA adaptation: retain [-4,4), clip only destination writes. If replaced by [0,4),
    // a native blitter stretches the entire source into half the requested width.
    // Refresh wrapper IL_005e-0077 forwards original destination RECT; native pixels pending.
    device.Present(Rectangle(-2, 1, 6, 3), Rectangle(-4, 0, 8, 6), 0);
    EXPECT_EQ(LastRegionArgs(renderer),
              "frameIndex=1 source=0,1,4x3 destination=-4,0,8x6");
}
#endif
