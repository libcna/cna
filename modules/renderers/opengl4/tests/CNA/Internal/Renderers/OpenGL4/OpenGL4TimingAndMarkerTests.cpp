// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0027: GPU timers and debug markers.
//
// A timer is two GL_TIMESTAMP queries rather than one GL_TIME_ELAPSED query, because a GL context
// allows only one elapsed-time query to be active at a time: two CNA timers whose ranges overlap --
// a whole-frame timer around a per-pass one -- would make the inner Begin a GL_INVALID_OPERATION.
// The first case here is exactly that overlap. The second shows a SetStringMarkerEXT marker
// entering the GL debug stream, where capture tools and the renderer's own callback see it.

#if defined(CNA_RENDERER_OPENGL4)

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using Microsoft::Xna::Framework::Color;

    /// Collects every renderer log line for the lifetime of the object.
    class LogCapture
    {
    public:
        LogCapture() : previousLevel_(CNA::Logger::GetMinimumLevel())
        {
            // Markers are echoed at debug level; a Release build's default floor is INFO.
            CNA::Logger::SetMinimumLevel(CNA::LogLevel::DEBUG);
            CNA::Logger::SetSink([this](CNA::LogLevel, CNA::LogCategory, std::string_view line) {
                lines_.emplace_back(line);
            });
        }
        ~LogCapture()
        {
            CNA::Logger::ResetSink();
            CNA::Logger::SetMinimumLevel(previousLevel_);
        }
        LogCapture(const LogCapture&) = delete;
        LogCapture& operator=(const LogCapture&) = delete;

        [[nodiscard]] int Count(std::string_view needle) const
        {
            int count = 0;
            for (const std::string& line : lines_)
                if (line.find(needle) != std::string::npos) ++count;
            return count;
        }

    private:
        CNA::LogLevel previousLevel_;
        std::vector<std::string> lines_;
    };

    /// Polls @p timer until its result arrives, for at most two seconds.
    bool WaitFor(const CNA::Internal::Renderers::IGpuTimerRenderer& timer)
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            if (timer.IsResultAvailable()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
}

TEST(OpenGL4Timing, OverlappingTimersBothMeasureTheirOwnRanges)
{
    LogCapture log;
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto& renderer = device.GetRenderer();
    ASSERT_TRUE(renderer.SupportsGpuTimerEXT());
    EXPECT_EQ(1000u, renderer.GetTimestampPeriodPicosecondsEXT());

    RenderTarget2D target(device, 512, 512);
    device.SetRenderTarget(&target);
    auto outer = renderer.CreateGpuTimerEXT();
    auto inner = renderer.CreateGpuTimerEXT();
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);

    outer->Begin();
    for (int i = 0; i < 20; ++i) device.Clear(Color::Red);
    inner->Begin();   // one GL_TIME_ELAPSED query would already be active here
    for (int i = 0; i < 20; ++i) device.Clear(Color::Lime);
    inner->End();
    for (int i = 0; i < 20; ++i) device.Clear(Color::Blue);
    outer->End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ASSERT_TRUE(WaitFor(*inner)) << "the inner range never completed";
    ASSERT_TRUE(WaitFor(*outer)) << "the outer range never completed";
    const std::uint64_t innerNs = inner->ElapsedNanoseconds();
    const std::uint64_t outerNs = outer->ElapsedNanoseconds();
    EXPECT_GT(innerNs, 0u);
    EXPECT_GE(outerNs, innerNs) << "the enclosing range measured less than the range inside it";
    EXPECT_EQ(0, log.Count("[OpenGL4 GL Error]"));
}

TEST(OpenGL4Timing, AnUnclosedOrUnopenedTimerReportsNothing)
{
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto timer = device.GetRenderer().CreateGpuTimerEXT();
    ASSERT_NE(timer, nullptr);
    EXPECT_FALSE(timer->IsResultAvailable());
    EXPECT_EQ(0u, timer->ElapsedNanoseconds());
    timer->Begin();
    EXPECT_FALSE(timer->IsResultAvailable()) << "an open range has no result";
    timer->End();
    ASSERT_TRUE(WaitFor(*timer));
}

TEST(OpenGL4DebugMarker, AMarkerEntersTheGlDebugStream)
{
    LogCapture log;
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    const auto& renderer =
        static_cast<const CNA::Internal::Renderers::OpenGL4::OpenGL4Renderer&>(device.GetRenderer());
    if (!renderer.IsDebugOutputEnabledEXT())
        GTEST_SKIP() << "GL debug output is off in this build (enable CNA_OPENGL4_DEBUG_OUTPUT)";

    device.SetStringMarkerEXT("cna-opengl4-marker-probe");
    EXPECT_EQ(1, log.Count("[OpenGL4 Marker] cna-opengl4-marker-probe"));
    EXPECT_EQ(0, log.Count("[OpenGL4 GL Error]"));
}

#endif // CNA_RENDERER_OPENGL4
