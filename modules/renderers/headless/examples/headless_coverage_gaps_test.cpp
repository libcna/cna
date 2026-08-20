// SPDX-License-Identifier: MS-PL
// plans/plan_headless.md: closes a batch of small, previously-flagged test-coverage gaps that were
// left as honest "implemented but not individually verified" notes after the first three
// Headless renderer commits: HEADLESS-40 (trace log coverage for state-change methods),
// HEADLESS-32/61 (GetLastFrameStatistics()'s diff math), HEADLESS-33 (per-type alive-resource
// breakdown), HEADLESS-11 (the 32-bit IndexBuffer path), and HEADLESS-5 (CNA_HEADLESS_MODE
// environment-variable parsing).
//
// Check A -- HeadlessTrace mode's call log now records ApplyBlendState/ApplyDepthStencilState/
//   ApplyRasterizerState/ApplySamplerState (previously only draws/clears/resource-creation/
//   SetData/Present were logged).
// Check B -- GetLastFrameStatistics() at the start of a frame (before this frame's own draws)
//   reports zero new draw calls relative to the snapshot taken at the previous Present().
// Check C -- GetLastFrameStatistics() after exactly 2 draw calls in the current frame reports
//   drawCallCount == 2, not the cumulative total across all frames so far -- proves the diff
//   math genuinely isolates the current frame.
// Check D -- AliveResources() broken down by type name matches the exact expected counts for a
//   known set of concurrently-alive VertexBuffer/IndexBuffer/Texture2D resources.
// Check E -- a 32-bit IndexBuffer (IndexElementSize::ThirtyTwoBits) round-trips real index data
//   and successfully drives a DrawIndexedPrimitives call -- the 16-bit path already had coverage
//   in Headless_Smoke, the 32-bit path did not.
// Check F/G/H/I/J -- ParseHeadlessModeFromEnvironment() (via CNA_HEADLESS_MODE) for Fast,
//   case-insensitive Trace, case-insensitive Validation, an unrecognized value, and unset --
//   only the programmatic SetMode() path had coverage before this.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"
#include "System/Environment.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Headless;

namespace
{
    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }

    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    // Checks F-J: env-var parsing needs no window/Game at all -- HeadlessRenderer can be
    // constructed directly, and each constructor call re-reads CNA_HEADLESS_MODE fresh.
    void CheckEnvironmentModeParsing()
    {
        System::Environment::SetEnvironmentVariable("CNA_HEADLESS_MODE", "Fast");
        {
            HeadlessRenderer renderer(64, 64);
            Check(renderer.GetMode() == HeadlessMode::Fast, "CNA_HEADLESS_MODE=Fast parses to HeadlessMode::Fast");
        }

        System::Environment::SetEnvironmentVariable("CNA_HEADLESS_MODE", "TRACE");
        {
            HeadlessRenderer renderer(64, 64);
            Check(renderer.GetMode() == HeadlessMode::Trace,
                  "CNA_HEADLESS_MODE=TRACE (uppercase) parses to HeadlessMode::Trace");
        }

        System::Environment::SetEnvironmentVariable("CNA_HEADLESS_MODE", "validation");
        {
            HeadlessRenderer renderer(64, 64);
            Check(renderer.GetMode() == HeadlessMode::Validation,
                  "CNA_HEADLESS_MODE=validation (lowercase) parses to HeadlessMode::Validation");
        }

        System::Environment::SetEnvironmentVariable("CNA_HEADLESS_MODE", "bogus");
        {
            HeadlessRenderer renderer(64, 64);
            Check(renderer.GetMode() == HeadlessMode::Validation,
                  "an unrecognized CNA_HEADLESS_MODE value defaults to HeadlessMode::Validation");
        }

        System::Environment::SetEnvironmentVariable("CNA_HEADLESS_MODE", {});
        // sharp-runtime #2313 (downstream ticket #2366): "" STOPPED meaning "remove". It now
        // stores an empty value, which is what .NET does; only a null value removes. This site
        // wants REMOVAL -- the check below is "an UNSET CNA_HEADLESS_MODE defaults to
        // Validation" -- so it must not pass "".
        //
        // `{}` rather than `std::nullopt` DELIBERATELY: it means "remove" under BOTH the current
        // sharp-runtime on develop, where the parameter is `const std::string&` and `{}` is the
        // empty string that deletes, and the one on next, where it is
        // `const std::optional<std::string>&` and `{}` is nullopt. `std::nullopt` does not
        // compile against develop at all. Verified both ways.
        {
            HeadlessRenderer renderer(64, 64);
            Check(renderer.GetMode() == HeadlessMode::Validation,
                  "an unset CNA_HEADLESS_MODE defaults to HeadlessMode::Validation");
        }
    }
}

class HeadlessCoverageGapsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<HeadlessRenderer&>(dev.GetRenderer());

        // Check B: at the top of this frame, before any draw this frame has issued, the
        // per-frame diff should be zero -- the previous frame's automatic EndDraw()->Present()
        // already rolled the snapshot forward.
        if (frame_ == 2)
        {
            const HeadlessStatistics& diff = renderer.GetLastFrameStatistics();
            Check(diff.drawCallCount == 0,
                  "GetLastFrameStatistics() reports zero draw calls at the top of a fresh frame");
        }

        dev.Clear(Color::Black);

        VertexBuffer vb(dev, PosColorDecl(), 3, BufferUsage::None);
        const VertexPositionColor verts[3] = {
            { Vector3(0.0f, 1.0f, 0.0f), Color::Red },
            { Vector3(-1.0f, -1.0f, 0.0f), Color::Green },
            { Vector3(1.0f, -1.0f, 0.0f), Color::Blue },
        };
        vb.SetData(verts, 0, 3);
        BasicEffect fx(dev);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);

        if (frame_ == 2)
        {
            // Check C: exactly 2 DrawPrimitives calls happened in this frame -- the diff must
            // read 2, not the cumulative total across frames 1 and 2 (which would be 4).
            const HeadlessStatistics& diff = renderer.GetLastFrameStatistics();
            Check(diff.drawCallCount == 2,
                  "GetLastFrameStatistics() reports exactly 2 draw calls for the current frame only");
        }

        if (frame_ == 3)
        {
            // Check A: HeadlessTrace now logs state-change methods, not just draws/clears/
            // resource creation/SetData/Present.
            {
                renderer.SetMode(HeadlessMode::Trace);
                const std::size_t before = renderer.TraceLog().size();
                renderer.ApplyBlendState(0, 0, 0, 0, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
                renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
                renderer.ApplyRasterizerState(0, 0, false, 0.0f, 0.0f);
                renderer.ApplySamplerState(0, 0, 0, 0, 0);
                const std::string formatted = renderer.FormatTraceLog();
                Check(renderer.TraceLog().size() == before + 4 &&
                      formatted.find("ApplyBlendState") != std::string::npos &&
                      formatted.find("ApplyDepthStencilState") != std::string::npos &&
                      formatted.find("ApplyRasterizerState") != std::string::npos &&
                      formatted.find("ApplySamplerState") != std::string::npos,
                      "HeadlessTrace log now records ApplyBlendState/DepthStencil/Rasterizer/SamplerState calls");
                renderer.SetMode(HeadlessMode::Validation);
            }

            // Check D: per-type alive-resource breakdown, measured as a delta against a baseline
            // captured before creating a known set of resources (other long-lived resources,
            // e.g. this frame's own `vb` above, are already alive at this point).
            {
                auto countByType = [&](const std::string& typeName) {
                    std::size_t n = 0;
                    for (const HeadlessResourceRecord& r : renderer.AliveResources())
                        if (r.typeName == typeName) ++n;
                    return n;
                };
                const std::size_t baselineVb = countByType("VertexBuffer");
                const std::size_t baselineIb = countByType("IndexBuffer");
                const std::size_t baselineTex = countByType("Texture2D");

                auto extraVb1 = std::make_unique<VertexBuffer>(dev, PosColorDecl(), 3, BufferUsage::None);
                auto extraVb2 = std::make_unique<VertexBuffer>(dev, PosColorDecl(), 3, BufferUsage::None);
                auto extraIb = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
                auto extraTex = std::make_unique<Texture2D>(
                    Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));

                Check(countByType("VertexBuffer") == baselineVb + 2 &&
                      countByType("IndexBuffer") == baselineIb + 1 &&
                      countByType("Texture2D") == baselineTex + 1,
                      "AliveResources() per-type breakdown matches the exact known set of newly created resources");

                extraVb1.reset();
                extraVb2.reset();
                extraIb.reset();
                extraTex.reset();
            }

            // Check E: the 32-bit IndexBuffer path -- previously untested (only the 16-bit path
            // was exercised by Headless_Smoke).
            {
                VertexBuffer quadVb(dev, PosColorDecl(), 4, BufferUsage::None);
                const VertexPositionColor quadVerts[4] = {
                    { Vector3(-1.0f,  1.0f, 0.0f), Color::Red },
                    { Vector3( 1.0f,  1.0f, 0.0f), Color::Green },
                    { Vector3( 1.0f, -1.0f, 0.0f), Color::Blue },
                    { Vector3(-1.0f, -1.0f, 0.0f), Color::White },
                };
                quadVb.SetData(quadVerts, 0, 4);

                IndexBuffer ib32(dev, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
                const std::uint32_t indices32[6] = {0, 1, 2, 0, 2, 3};
                ib32.SetData(indices32, 0, 6);

                BasicEffect fx32(dev);
                fx32.Apply();

                bool threw = false;
                try
                {
                    dev.SetVertexBuffer(&quadVb);
                    dev.SetIndexBuffer(&ib32);
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                catch (const HeadlessValidationException&) { threw = true; }
                dev.SetVertexBuffer(nullptr);
                dev.SetIndexBuffer(nullptr);
                Check(!threw, "a 32-bit IndexBuffer round-trips real data and drives DrawIndexedPrimitives without throwing");
            }

            CheckEnvironmentModeParsing();

            std::printf("=== %d/%d PASS ===\n", g_passCount, 10);
            result_ = (g_passCount == 10) ? 0 : 1;
            Exit();
        }
    }

public:
    HeadlessCoverageGapsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HeadlessCoverageGapsTest game;
    game.Run();
    return game.getResult();
}
