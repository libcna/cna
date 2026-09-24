// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0021: two GraphicsDevices are two GL contexts, and a GL
// object name means something only in the context that created it. Before GL4-0021 OpenGL4 made its
// context current only when creating a resource, so work for the first device issued after the
// second was created -- a render-target bind, a clear, an upload, a delete -- landed in the second
// device's context: on a name that did not exist there (GL_INVALID_VALUE in glDeleteProgram, 105
// times in CnaGraphicsExtTests' MultiDeviceTest) or, worse, on an unrelated object that happened to
// carry the same name. Each device's work must stay in its own context, and a resource that outlives
// its device must issue no GL at all.

#if defined(CNA_RENDERER_OPENGL4)

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using Microsoft::Xna::Framework::Color;

    /// Counts the renderer's "[OpenGL4 GL Error]" lines for the lifetime of the object.
    class GlErrorCapture
    {
    public:
        GlErrorCapture()
        {
            CNA::Logger::SetSink([this](CNA::LogLevel, CNA::LogCategory, std::string_view line) {
                if (line.find("[OpenGL4 GL Error]") != std::string_view::npos)
                    errors_.emplace_back(line);
            });
        }
        ~GlErrorCapture() { CNA::Logger::ResetSink(); }
        GlErrorCapture(const GlErrorCapture&) = delete;
        GlErrorCapture& operator=(const GlErrorCapture&) = delete;

        [[nodiscard]] std::string Describe() const
        {
            std::string text;
            for (const std::string& line : errors_) text += "\n  " + line;
            return text;
        }
        [[nodiscard]] std::size_t Count() const { return errors_.size(); }

    private:
        std::vector<std::string> errors_;
    };

    bool IsUniform(const RenderTarget2D& target, const Color& expected)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(target.getWidthProperty()) *
                                  static_cast<std::size_t>(target.getHeightProperty()));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        for (const Color& pixel : pixels)
            if (pixel != expected) return false;
        return true;
    }
}

TEST(OpenGL4MultiDevice, InterleavedWorkStaysInEachDevicesOwnContext)
{
    GlErrorCapture capture;
    GraphicsDevice first;
    if (first.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    RenderTarget2D firstTarget(first, 4, 4);

    // Creating the second device leaves ITS context current, which is the state every later call
    // for the first device used to inherit.
    GraphicsDevice second;
    RenderTarget2D secondTarget(second, 4, 4);

    first.SetRenderTarget(&firstTarget);
    second.SetRenderTarget(&secondTarget);
    first.Clear(Color::Red);
    second.Clear(Color::Lime);
    first.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    second.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    EXPECT_TRUE(IsUniform(firstTarget, Color::Red))
        << "the first device's clear did not reach its own render target";
    EXPECT_TRUE(IsUniform(secondTarget, Color::Lime))
        << "the second device's clear did not reach its own render target";
    EXPECT_EQ(0u, capture.Count()) << capture.Describe();
}

TEST(OpenGL4MultiDevice, DestroyingOneDevicesResourcesLeavesTheOtherIntact)
{
    GlErrorCapture capture;
    auto first = std::make_unique<GraphicsDevice>();
    if (first->GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto firstTarget = std::make_unique<RenderTarget2D>(*first, 4, 4);
    first->SetRenderTarget(firstTarget.get());
    first->Clear(Color::Red);
    first->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    GraphicsDevice second;
    RenderTarget2D secondTarget(second, 4, 4);
    second.SetRenderTarget(&secondTarget);
    second.Clear(Color::Lime);
    second.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    // The second device's context is current here; the first's names must be deleted in the
    // first's own context, not as whatever carries the same names in the second's.
    firstTarget.reset();
    first.reset();

    EXPECT_TRUE(IsUniform(secondTarget, Color::Lime))
        << "releasing the first device's resources disturbed the second device's target";
    second.SetRenderTarget(&secondTarget);
    second.Clear(Color::Blue);
    second.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    EXPECT_TRUE(IsUniform(secondTarget, Color::Blue));
    EXPECT_EQ(0u, capture.Count()) << capture.Describe();
}

TEST(OpenGL4MultiDevice, ARendererResourceOutlivingItsDeviceIssuesNoGl)
{
    GlErrorCapture capture;
    auto first = std::make_unique<GraphicsDevice>();
    if (first->GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    GraphicsDevice second;

    // Renderer-level objects, so their lifetime is independent of any framework bookkeeping.
    auto buffer = first->GetRenderer().CreateVertexBuffer(3);
    const float vertices[12] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    buffer->SetData(vertices, 3, 16);
    auto query = first->GetRenderer().CreateOcclusionQuery();

    first.reset();   // the first context -- and every name in it -- is gone

    // Deleting these now would reach the SECOND context, which is current.
    EXPECT_NO_THROW(buffer.reset());
    EXPECT_NO_THROW(query.reset());

    RenderTarget2D target(second, 4, 4);
    second.SetRenderTarget(&target);
    second.Clear(Color::Lime);
    second.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    EXPECT_TRUE(IsUniform(target, Color::Lime));
    EXPECT_EQ(0u, capture.Count()) << capture.Describe();
}

#endif // CNA_RENDERER_OPENGL4
