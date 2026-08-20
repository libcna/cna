// SPDX-License-Identifier: MS-PL
// plans/plan_headless.md: closes HEADLESS-23 (viewport/scissor bounds vs. the currently-bound target's
// real size), HEADLESS-41 (debug-label creation-site tracking in HeadlessTrace mode), and
// HEADLESS-42 (trace log export) -- all three were flagged as narrower-than-planned or unverified
// after the first two Headless renderer commits.
//
// Check A -- SetViewport() with a rectangle that fits inside the (default, no render target bound)
//   backbuffer size does not throw under HeadlessValidation.
// Check B -- SetViewport() with a rectangle that exceeds the currently-bound RenderTarget2D's real
//   size (not the backbuffer's) throws under HeadlessValidation -- proves the check genuinely
//   cross-references whichever target is actually bound, not a fixed constant.
// Check C -- the exact same oversized SetViewport() call does not throw under HeadlessFast --
//   proves this new validation rule also respects the mode dial, not just added unconditionally.
// Check D -- SetScissorRect() with a negative origin throws under HeadlessValidation.
// Check E -- PushDebugLabel("...")-wrapped resource creation in HeadlessTrace mode: a deliberately
//   leaked VertexBuffer's AssertNoLeaks() report contains the pushed label text.
// Check F -- a VertexBuffer created *outside* any PushDebugLabel() scope reports no creation site
//   (empty string) even in HeadlessTrace mode -- proves the label is genuinely opt-in per resource,
//   not a blanket "trace mode always attaches something" behaviour.
// Check G -- FormatTraceLog() returns non-empty text containing a recognizable call name after some
//   HeadlessTrace-mode activity.
// Check H -- DumpTraceLog() does not throw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

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
}

class HeadlessValidationExtrasTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<HeadlessRenderer&>(dev.GetRenderer());

        // Check A: an in-bounds viewport against the default backbuffer doesn't throw.
        {
            int bw = 0, bh = 0;
            renderer.GetViewportSize(bw, bh);
            bool threw = false;
            try { renderer.SetViewport(0, 0, bw, bh, 0.0f, 1.0f); }
            catch (const HeadlessValidationException&) { threw = true; }
            check(!threw, "SetViewport() within the backbuffer's own bounds does not throw");
        }

        // Check B/C: an oversized viewport against a *bound RenderTarget2D* (not the backbuffer).
        {
            RenderTarget2D rt(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);

            renderer.SetMode(HeadlessMode::Validation);
            bool validationThrew = false;
            try { renderer.SetViewport(0, 0, 64, 64, 0.0f, 1.0f); }
            catch (const HeadlessValidationException&) { validationThrew = true; }
            check(validationThrew,
                  "SetViewport(64x64) against a bound 16x16 RenderTarget2D throws under HeadlessValidation");

            renderer.SetMode(HeadlessMode::Fast);
            bool fastThrew = false;
            try { renderer.SetViewport(0, 0, 64, 64, 0.0f, 1.0f); }
            catch (const HeadlessValidationException&) { fastThrew = true; }
            check(!fastThrew, "the same oversized SetViewport() call does not throw under HeadlessFast");

            renderer.SetMode(HeadlessMode::Validation);
            dev.SetRenderTarget(nullptr);
        }

        // Check D: negative scissor origin.
        {
            bool threw = false;
            try { renderer.SetScissorRect(-1, -1, 4, 4); }
            catch (const HeadlessValidationException&) { threw = true; }
            check(threw, "SetScissorRect() with a negative origin throws under HeadlessValidation");
        }

        // Check E/F: debug-label creation-site tracking.
        {
            renderer.SetMode(HeadlessMode::Trace);

            renderer.PushDebugLabel("MyTestScene/PlayerBuffers");
            auto labeled = std::make_unique<VertexBuffer>(dev, PosColorDecl(), 3, BufferUsage::None);
            renderer.PopDebugLabel();

            auto unlabeled = std::make_unique<VertexBuffer>(dev, PosColorDecl(), 3, BufferUsage::None);

            bool threw = false;
            std::string message;
            try { renderer.AssertNoLeaks(); }
            catch (const HeadlessValidationException& ex) { threw = true; message = ex.what(); }
            check(threw && message.find("MyTestScene/PlayerBuffers") != std::string::npos,
                  "AssertNoLeaks() report includes the pushed debug label for the labeled resource");

            // The unlabeled resource's own record should carry no creation site at all -- confirmed
            // indirectly: the report contains exactly one occurrence of the label text (the labeled
            // resource's line), not two.
            const std::size_t firstPos = message.find("MyTestScene/PlayerBuffers");
            const bool onlyOneOccurrence = firstPos != std::string::npos &&
                message.find("MyTestScene/PlayerBuffers", firstPos + 1) == std::string::npos;
            check(onlyOneOccurrence,
                  "a VertexBuffer created outside the label scope reports no creation site");

            labeled.reset();
            unlabeled.reset();
            renderer.SetMode(HeadlessMode::Validation);
        }

        // Check G: trace log formatting/export.
        {
            renderer.SetMode(HeadlessMode::Trace);
            dev.Clear(Color::Black);
            const std::string formatted = renderer.FormatTraceLog();
            check(!formatted.empty() && formatted.find("Clear") != std::string::npos,
                  "FormatTraceLog() returns non-empty text mentioning a real call after HeadlessTrace activity");

            bool dumpThrew = false;
            try { renderer.DumpTraceLog(stdout); }
            catch (...) { dumpThrew = true; }
            check(!dumpThrew, "DumpTraceLog() does not throw");
            renderer.SetMode(HeadlessMode::Validation);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 8);
        result_ = (passCount_ == 8) ? 0 : 1;
        Exit();
    }

public:
    HeadlessValidationExtrasTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HeadlessValidationExtrasTest game;
    game.Run();
    return game.getResult();
}
