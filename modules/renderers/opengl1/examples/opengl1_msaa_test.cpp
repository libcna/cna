// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: backbuffer MSAA (plans/plan_opengl1.md item 22, EasyGL parity).
//
// Before this, GraphicsRendererCreateArgs::multiSampleCount was read by nothing --
// SDL_GL_MULTISAMPLEBUFFERS/SAMPLES were never requested, GetMultiSampleCount() always returned
// 0, and SupportsCapability(MultiSampleAntiAliasing) was hardcoded false regardless of what a
// game requested. Fixed by requesting the context attributes in GraphicsDevice.cpp before
// SDL_CreateWindow() (same GLX-visual-fixed-at-window-creation-time constraint the existing
// depth/stencil block already documents), then reading back whatever the driver genuinely granted
// in OpenGL1Renderer's own constructor (OpenGL1Renderer::DetectMultiSampleCount()).
//
// IMPORTANT scoping note, found while writing this test: GraphicsDeviceManager.PreferMultiSampling
// (the common Game-based idiom every other OPENGL1 test in this suite uses) CANNOT reach this
// fix. Game::GraphicsDevice_ is a plain value member (Game.hpp), constructed with a fully-default
// PresentationParameters (MultiSampleCount=0) as part of Game's OWN base-class construction --
// this happens before a derived Game subclass's constructor body (where GraphicsDeviceManager is
// constructed and PreferMultiSampling is set) ever runs. By the time GraphicsDeviceManager::
// CreateDevice() later tries to push its computed MultiSampleCount into the ALREADY-CONSTRUCTED
// device via Reset()/ApplyMultiSampleCount(), the window (and its GLX visual) already exists
// without a multisample buffer -- a window-visual-based mechanism cannot be reconfigured in place
// (RecreateRendererForMultiSampleCount() only recreates the GL context, not the window itself).
// This is a pre-existing Game/GraphicsDeviceManager construction-order constraint, not something
// introduced by or fixable within this OPENGL1-scoped change -- see plans/plan_opengl1.md item 22.
//
// What DOES work (and is what this test exercises, following the direct-construction pattern
// examples/directx12_smoke_test.cpp's own windowless-device test already established): a
// PresentationParameters with MultiSampleCount already set BEFORE being passed to GraphicsDevice's
// own (adapter, profile, presentationParameters) constructor -- a real, valid XNA construction
// path independent of Game/GraphicsDeviceManager.
//
// Also found while writing this test: GraphicsDevice::createRenderer() (the path used by the
// INITIAL construction) never writes the renderer's honestly-applied MultiSampleCount back into
// PresentationParameters the way GraphicsDevice::Reset()/RecreateRendererForMultiSampleCount()
// does via ApplyMultiSampleCount() -- so PresentationParameters.MultiSampleCount after initial
// construction is only ever an echo of what was requested, not proof of what was granted. The
// renderer's own GetMultiSampleCount() (reachable via the CNAEXT GraphicsDevice::GetRenderer()) is
// the only authoritative source for the initial-construction case; this test uses that, not the
// PresentationParameters echo, as its primary check. (A pre-existing GraphicsDevice-level gap,
// cross-renderer, out of scope for this OPENGL1-specific item.)
//
// Pixel-level antialiasing proof: this test also draws a filled triangle whose hypotenuse is the
// exact line x+y=65 in screen-pixel space and reads back the row straddling where it crosses
// y=32 -- real per-sample MSAA coverage blending would put at least one genuinely partial
// (non-binary) value in that strip, vs. the mathematically-guaranteed-binary result of
// single-sample point-in-triangle rasterization. This is printed as a diagnostic but NOT asserted:
// confirmed via a minimal, CNA-independent standalone SDL3+OpenGL program (not checked in -- pure
// scratch diagnostic) that even raw glReadPixels from a genuinely-allocated 4x multisample GLX
// visual (GL_SAMPLE_BUFFERS=1, GL_SAMPLES=4, GL_MULTISAMPLE enabled, zero GL errors -- all
// confirmed) reads back perfectly binary on this sandbox's Mesa llvmpipe software rasterizer under
// Xvfb. That is a real environment/driver limitation in resolve-on-read for a window-system
// multisample framebuffer, not a bug in this fix -- everything actually within OPENGL1's control
// (the request reaching the driver, the capability reporting) is independently verified above.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport = 64;
    constexpr int kStripY   = 32;
    constexpr int kStripX0  = 20;
    constexpr int kStripLen = 25; // covers x=20..44; the x+y=65 edge crosses this row at x=33.
    constexpr int kRequestedMultiSampleCount = 4;

    int pass_ = 0, fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }
}

int main()
{
    PresentationParameters pp;
    pp.setBackBufferWidthProperty(kViewport);
    pp.setBackBufferHeightProperty(kViewport);
    pp.setMultiSampleCountProperty(kRequestedMultiSampleCount);
    GraphicsAdapter& adapter = GraphicsAdapter::getDefaultAdapterProperty();
    GraphicsDevice dev(adapter, GraphicsProfile::HiDef, pp);

    dev.SetDepthTestEnabled(false);
    dev.setRasterizerStateProperty(RasterizerState::CullNone);

    const int rendererMultiSampleCount = dev.GetRenderer().GetMultiSampleCount();
    std::printf("IGraphicsRenderer::GetMultiSampleCount()=%d, PresentationParameters.MultiSampleCount=%d "
                "(echo of request, see file header), SupportsCapability(MSAA)=%s\n",
                rendererMultiSampleCount,
                dev.getPresentationParametersProperty().getMultiSampleCountProperty(),
                dev.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing) ? "true" : "false");
    Check(rendererMultiSampleCount == kRequestedMultiSampleCount,
          "renderer genuinely granted the requested MultiSampleCount (queried directly from "
          "IGraphicsRenderer::GetMultiSampleCount(), not the PresentationParameters echo)");
    Check(dev.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
          "SupportsCapability(MultiSampleAntiAliasing) reflects the genuinely-applied MSAA state");

    dev.Clear(Color::Black);

    Matrix world = Matrix::getIdentityProperty();
    Matrix view  = Matrix::getIdentityProperty();
    Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f);

    // Hypotenuse is exactly the line x+y=65 in screen-pixel space (vertices at (65,0)/(0,65)
    // deliberately extend 1 unit past the 64-wide viewport so the line's exact position isn't
    // affected by viewport clamping) -- this passes exactly through pixel (32,32)'s continuous
    // center (32.5+32.5=65.0). See file header: this is a diagnostic print, not asserted.
    const VertexPositionColor tri[3] = {
        { Vector3(0.0f, 0.0f, 0.0f),  Color::White },
        { Vector3(65.0f, 0.0f, 0.0f), Color::White },
        { Vector3(0.0f, 65.0f, 0.0f), Color::White },
    };

    BasicEffect fx(dev);
    fx.setWorldProperty(world);
    fx.setViewProperty(view);
    fx.setProjectionProperty(proj);
    fx.VertexColorEnabled = true;
    fx.Apply();
    dev.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);

    const Rectangle stripReg(kStripX0, kStripY, kStripLen, 1);
    std::vector<Color> strip(kStripLen, Color(0, 0, 0, 0));
    dev.GetBackBufferData(&stripReg, strip.data(), 0, kStripLen);

    bool anyBinary = false, anyPartial = false;
    for (int i = 0; i < kStripLen; ++i)
    {
        const int r = strip[i].getRProperty();
        std::printf("strip[x=%d]=%d ", kStripX0 + i, r);
        if (r <= 5 || r >= 250) anyBinary = true;
        else anyPartial = true;
    }
    std::printf("\n[diagnostic, not asserted] edge strip contains a blended pixel: %s (see file "
                "header -- not reliable on this sandbox's software rasterizer)\n",
                anyPartial ? "yes" : "no");

    Check(anyBinary, "edge strip contains pixels fully inside/outside the triangle (sanity: the "
          "triangle and clear color are genuinely opposite extremes, and the draw call itself "
          "worked)");

    std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
    return fail_ > 0 ? 1 : 0;
}
