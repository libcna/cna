// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-18/19/20: real dynamic BlendState/DepthStencilState/RasterizerState proof
// for the SDL_GPU graphics renderer -- before this task every pipeline hardcoded Opaque blending,
// CullMode::None, FillMode::Solid, and no stencil test at all; GraphicsDevice.BlendState/
// DepthStencilState/RasterizerState assignments never reached SDL_gpu's own pipeline-baked state.
//
// Every check below asserts exact RenderTarget2D::GetData() pixel values (same bar as
// sdlgpu_rendertarget2d_test.cpp), not just "didn't throw" -- each expected value is chosen so
// that the OLD hardcoded behavior would read back a clearly different, wrong pixel.
//
// Check A -- BlendState.Additive: a Red-cleared target + a full-screen Green draw must read back
//   (255,128,0) (additive colour add -- XNA's Color::Green is (0,128,0), not lime), not plain
//   Green (which is what a stuck-Opaque pipeline would overwrite to).
// Check B -- BlendState.AlphaBlend: a White-cleared target + a half-alpha Black draw must read
//   back mid-grey (One/InverseSourceAlpha), not plain Black (Opaque overwrite).
// Check C -- DepthStencilState stencil test: a stencil mask is written (StencilFunction=Always,
//   StencilPass=Replace, ReferenceStencil=1) over the left half only, then a full-screen draw with
//   StencilFunction=Equal/ReferenceStencil=1 must show its new colour ONLY where the mask was
//   written (left) and leave the background untouched on the right -- proves a genuine per-pixel
//   stencil test, not StencilEnable being ignored.
// Check D -- RasterizerState.CullMode: the same quad winding drawn under CullCounterClockwise
//   (this project's own default -- must stay visible, matching every other 3D test), CullClockwise
//   (must be culled -- the opposite winding of the same quad), and CullNone (must stay visible
//   regardless) -- proves CullMode is no longer hardcoded to None.
// Check E -- RasterizerState.FillMode: the same triangle drawn Solid (centre pixel filled) vs
//   WireFrame (centre pixel must stay background -- wireframe never rasterizes a triangle's
//   interior) -- proves FillMode is no longer hardcoded to Solid. REMED-GFX-180 added the two
//   positive halves: WireFrame must still light SOME pixels (its edges), and far fewer than Solid
//   does, so a draw that was dropped entirely can no longer satisfy the centre-is-background half.
// Check F -- RasterizerState.ScissorTestEnable + GraphicsDevice.ScissorRectangle: a full-screen
//   draw with a left-half scissor rect must only affect the left half, leaving the right half as
//   background -- proves SetScissorRect/ApplyScissorForRef genuinely clip.
//
// REMED-GFX-180 (fixture defect, not a renderer one): Check E's triangle buffer holds THREE
// vertices and used to be drawn through a helper that hardcoded two primitives -- a request for six.
// REMED-GFX-113's range guard rejected it, the rejection unwound past RunFillModeChecks' unbind, and
// the frame then ended with a render target still bound, which Game::EndDraw's Present rightly
// refuses. Nothing caught that refusal, so the process aborted and its whole check log died in an
// unflushed stdout buffer. The primitive count is now named at every call site, every print is
// flushed, and a group that throws still unbinds so a failure stays a legible FAIL.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kTotalFrames = 120;
    /// 16 as originally written, plus REMED-GFX-180's two positive WireFrame checks.
    constexpr int kTotalChecks = 18;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 10)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color ReadPixel(RenderTarget2D& rt, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }
}

class SdlGpuRenderStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    std::unique_ptr<RenderTarget2D> rtAdditive_;
    std::unique_ptr<RenderTarget2D> rtAlphaBlend_;
    std::unique_ptr<RenderTarget2D> rtStencil_;
    std::unique_ptr<RenderTarget2D> rtCullCcw_;
    std::unique_ptr<RenderTarget2D> rtCullCw_;
    std::unique_ptr<RenderTarget2D> rtCullNone_;
    std::unique_ptr<RenderTarget2D> rtFillSolid_;
    std::unique_ptr<RenderTarget2D> rtFillWire_;
    std::unique_ptr<RenderTarget2D> rtScissor_;
    std::unique_ptr<RenderTarget2D> rtStencilWriteMask_;
    std::unique_ptr<RenderTarget2D> rtStencilReadMask_;

    std::unique_ptr<VertexBuffer> fullQuadGreenVb_;
    std::unique_ptr<VertexBuffer> fullQuadBlackAlphaVb_;
    std::unique_ptr<VertexBuffer> fullQuadBlueVb_;
    std::unique_ptr<VertexBuffer> leftHalfQuadBlueVb_;
    std::unique_ptr<VertexBuffer> rightHalfQuadBlueVb_;
    std::unique_ptr<VertexBuffer> cullQuadGreenVb_;
    std::unique_ptr<VertexBuffer> fillTriangleGreenVb_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;
    /// The message of the exception RunAllGuarded last caught, empty when the run was clean.
    std::string lastFailure_;
    /// The first failure seen in frames 2..120, so a later frame's exception is a FAIL, not an abort.
    std::string laterFrameFailure_;

    // REMED-GFX-180: flushed on every line. stdout is block-buffered to a pipe, so when this fixture
    // still ended in std::terminate its entire check log was discarded with the buffer -- and the
    // resulting empty output was read as "no check was ever reached", which was false. A test whose
    // diagnostics can be lost by the very failure they describe is not diagnosable.
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount_;
    }

    // Clockwise winding (TL, BR, BL / TL, TR, BR) -- with the identity view/projection this test
    // uses (no camera-induced winding flip, unlike sdlgpu_3d_test.cpp's LookAt+Orthographic
    // camera), a CW-wound triangle is what stays visible under this project's own default
    // RasterizerState.CullCounterClockwise (mirrors sdlgpu_rendertarget2d_test.cpp's own
      // CW-wound triVerts, which renders successfully under that same identity-matrix/default-cull
    // combination) -- every check below except the CullMode differential itself wants the quad
    // to be visible-by-default so the actual dimension under test (blend/stencil/scissor/fill) is
    // the only variable.
    static std::unique_ptr<VertexBuffer> MakeFullQuad(GraphicsDevice& dev, const Color& c)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f, 1.0f, 0.0f), c }, { Vector3(1.0f, -1.0f, 0.0f), c }, { Vector3(-1.0f, -1.0f, 0.0f), c },
            { Vector3(-1.0f, 1.0f, 0.0f), c }, { Vector3(1.0f, 1.0f, 0.0f), c }, { Vector3(1.0f, -1.0f, 0.0f), c },
        };
        auto vb = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb->SetData(verts, 0, 6);
        return vb;
    }

    // REMED-GFX-180: the primitive count is a PARAMETER, and every caller states it. It used to be a
    // hardcoded 2 inside a helper called DrawFullQuad, which RunFillModeChecks then handed a
    // THREE-vertex triangle buffer -- a request for six vertices out of three. REMED-GFX-113's range
    // guard in GraphicsDevice::DrawPrimitives rightly rejects that, the rejection unwound past
    // RunFillModeChecks' own SetRenderTarget(nullptr), and the frame then ended with a target still
    // bound, which Game::EndDraw's Present rightly refuses. Both guards are correct; the request was
    // not. Naming the count at the call site is what keeps the helper's name and its geometry from
    // drifting apart again.
    void DrawGeometry(GraphicsDevice& dev, VertexBuffer& vb, int primitiveCount)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, primitiveCount);
        dev.SetVertexBuffer(nullptr);
    }

    /** @brief The two-triangle quads MakeFullQuad builds. */
    void DrawFullQuad(GraphicsDevice& dev, VertexBuffer& vb) { DrawGeometry(dev, vb, 2); }

    /** @brief The single-triangle buffer Check E's FillMode differential uses. */
    void DrawTriangle(GraphicsDevice& dev, VertexBuffer& vb) { DrawGeometry(dev, vb, 1); }

    // Check A/B: BlendState.
    void RunBlendChecks(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtAdditive_.get());
        dev.Clear(Color::Red);
        dev.setBlendStateProperty(BlendState::Additive);
        DrawFullQuad(dev, *fullQuadGreenVb_);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtAlphaBlend_.get());
        dev.Clear(Color::White);
        dev.setBlendStateProperty(BlendState::AlphaBlend);
        DrawFullQuad(dev, *fullQuadBlackAlphaVb_);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // Check C: DepthStencilState's real per-pixel stencil test.
    void RunStencilCheck(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtStencil_.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color::Blue, 1.0f, 0);

        DepthStencilState writeMask;
        writeMask.setDepthBufferEnableProperty(false);
        writeMask.setStencilEnableProperty(true);
        writeMask.setStencilFunctionProperty(CompareFunction::Always);
        writeMask.setStencilPassProperty(StencilOperation::Replace);
        writeMask.setReferenceStencilProperty(1);
        dev.setDepthStencilStateProperty(writeMask);
        DrawFullQuad(dev, *leftHalfQuadBlueVb_);

        DepthStencilState maskedDraw;
        maskedDraw.setDepthBufferEnableProperty(false);
        maskedDraw.setStencilEnableProperty(true);
        maskedDraw.setStencilFunctionProperty(CompareFunction::Equal);
        maskedDraw.setStencilPassProperty(StencilOperation::Keep);
        maskedDraw.setReferenceStencilProperty(1);
        dev.setDepthStencilStateProperty(maskedDraw);
        DrawFullQuad(dev, *fullQuadGreenVb_);

        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // Check G: DepthStencilState.StencilWriteMask -- a write with StencilWriteMask=0x00 must NOT
    // change the stencil buffer at all, even with StencilFunction=Always/StencilPass=Replace.
    void RunStencilWriteMaskCheck(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtStencilWriteMask_.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color::Blue, 1.0f, 0);

        DepthStencilState writeLeft;
        writeLeft.setDepthBufferEnableProperty(false);
        writeLeft.setStencilEnableProperty(true);
        writeLeft.setStencilFunctionProperty(CompareFunction::Always);
        writeLeft.setStencilPassProperty(StencilOperation::Replace);
        writeLeft.setReferenceStencilProperty(1);
        writeLeft.setStencilWriteMaskProperty(0xFF);  // normal -- left half really gets stencil=1
        dev.setDepthStencilStateProperty(writeLeft);
        DrawFullQuad(dev, *leftHalfQuadBlueVb_);

        DepthStencilState writeRightBlocked;
        writeRightBlocked.setDepthBufferEnableProperty(false);
        writeRightBlocked.setStencilEnableProperty(true);
        writeRightBlocked.setStencilFunctionProperty(CompareFunction::Always);
        writeRightBlocked.setStencilPassProperty(StencilOperation::Replace);
        writeRightBlocked.setReferenceStencilProperty(1);
        writeRightBlocked.setStencilWriteMaskProperty(0x00);  // blocks the write entirely
        dev.setDepthStencilStateProperty(writeRightBlocked);
        DrawFullQuad(dev, *rightHalfQuadBlueVb_);

        DepthStencilState readBack;
        readBack.setDepthBufferEnableProperty(false);
        readBack.setStencilEnableProperty(true);
        readBack.setStencilFunctionProperty(CompareFunction::Equal);
        readBack.setStencilPassProperty(StencilOperation::Keep);
        readBack.setReferenceStencilProperty(1);
        dev.setDepthStencilStateProperty(readBack);
        DrawFullQuad(dev, *fullQuadGreenVb_);

        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // Check H: DepthStencilState.StencilMask (the compare mask) -- StencilMask=0x00 masks BOTH the
    // reference and stored value to 0 before comparing, so an Equal test must pass regardless of
    // the real stencil content, even when ReferenceStencil deliberately does not match it.
    void RunStencilReadMaskCheck(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtStencilReadMask_.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color::Blue, 1.0f, 0);

        DepthStencilState writeOne;
        writeOne.setDepthBufferEnableProperty(false);
        writeOne.setStencilEnableProperty(true);
        writeOne.setStencilFunctionProperty(CompareFunction::Always);
        writeOne.setStencilPassProperty(StencilOperation::Replace);
        writeOne.setReferenceStencilProperty(1);
        dev.setDepthStencilStateProperty(writeOne);
        DrawFullQuad(dev, *fullQuadBlueVb_);

        DepthStencilState maskedCompare;
        maskedCompare.setDepthBufferEnableProperty(false);
        maskedCompare.setStencilEnableProperty(true);
        maskedCompare.setStencilFunctionProperty(CompareFunction::Equal);
        maskedCompare.setStencilPassProperty(StencilOperation::Keep);
        maskedCompare.setReferenceStencilProperty(0);  // deliberately mismatches the real stencil=1
        maskedCompare.setStencilMaskProperty(0x00);    // masks the mismatch away entirely
        dev.setDepthStencilStateProperty(maskedCompare);
        DrawFullQuad(dev, *fullQuadGreenVb_);

        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // Check D: RasterizerState.CullMode differential (same quad winding, 3 cull modes).
    void RunCullChecks(GraphicsDevice& dev)
    {
        RasterizerState rsCcw;
        rsCcw.setCullModeProperty(CullMode::CullCounterClockwiseFace);
        RasterizerState rsCw;
        rsCw.setCullModeProperty(CullMode::CullClockwiseFace);
        RasterizerState rsNone;
        rsNone.setCullModeProperty(CullMode::None);

        dev.SetRenderTarget(rtCullCcw_.get());
        dev.Clear(Color::Magenta);
        dev.setRasterizerStateProperty(rsCcw);
        DrawFullQuad(dev, *cullQuadGreenVb_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtCullCw_.get());
        dev.Clear(Color::Magenta);
        dev.setRasterizerStateProperty(rsCw);
        DrawFullQuad(dev, *cullQuadGreenVb_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtCullNone_.get());
        dev.Clear(Color::Magenta);
        dev.setRasterizerStateProperty(rsNone);
        DrawFullQuad(dev, *cullQuadGreenVb_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.setRasterizerStateProperty(RasterizerState());
    }

    // Check E: RasterizerState.FillMode (Solid vs WireFrame) on the SAME triangle, whose
    // centroid sits exactly at (0,0) -- render target centre -- regardless of NDC Y orientation.
    // The buffer holds exactly ONE triangle and is drawn as exactly one primitive; see DrawGeometry.
    void RunFillModeChecks(GraphicsDevice& dev)
    {
        RasterizerState rsSolid;
        rsSolid.setFillModeProperty(FillMode::Solid);
        RasterizerState rsWire;
        rsWire.setFillModeProperty(FillMode::WireFrame);

        dev.SetRenderTarget(rtFillSolid_.get());
        dev.Clear(Color::Magenta);
        dev.setRasterizerStateProperty(rsSolid);
        DrawTriangle(dev, *fillTriangleGreenVb_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtFillWire_.get());
        dev.Clear(Color::Magenta);
        dev.setRasterizerStateProperty(rsWire);
        DrawTriangle(dev, *fillTriangleGreenVb_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.setRasterizerStateProperty(RasterizerState());
    }

    // REMED-GFX-180: how many pixels of the whole target the draw coloured Green.
    //
    // Check E's WireFrame half asserts an ABSENCE -- the triangle's centre must stay background --
    // and an absence alone is satisfied just as well by a draw that never happened. Its Solid half
    // cannot cover that gap because it runs under a DIFFERENT RasterizerState, so nothing in the pair
    // distinguished "the interior was not filled" from "nothing was rasterized". Counting the lit
    // pixels turns the WireFrame half into a positive measurement without depending on which exact
    // pixels a driver's line rasterizer picks: the edges must light SOMETHING, and far less than the
    // filled interior does.
    static int CountGreenPixels(RenderTarget2D& rt)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kRTSize) * kRTSize, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kRTSize, kRTSize);
        rt.GetData(0, &whole, pixels.data(), 0, static_cast<int>(pixels.size()));
        int lit = 0;
        for (const Color& c : pixels)
            if (Matches(c, Color::Green)) ++lit;
        return lit;
    }

    // Check F: RasterizerState.ScissorTestEnable + GraphicsDevice.ScissorRectangle. Since
    // REMED-GFX-068 the scissor is captured PER DRAW at enqueue time (into each QueuedDrawRef) and
    // applied via SDL_SetGPUScissor at replay (see SdlGpuRenderer::ApplyScissorForRef), so
    // this readback would clip correctly even after SetRenderTarget(nullptr) resets ScissorRectangle
    // -- but it is deliberately kept BEFORE the unbind (the pre-GFX-068 requirement) as a stricter,
    // still-valid exercise of the live-then-flush path.
    void RunScissorCheck(GraphicsDevice& dev)
    {
        // Force everything queued by the EARLIER checks (still pending -- nothing has called
        // GetData() yet) to flush NOW, while scissor is still disabled. Pre-REMED-GFX-068 this was
        // required for correctness: the scissor was applied once per pass from the live state, and
        // all pending targets share a single EnsureFrameRendered() flush, so a scissor-enabled read
        // here as the frame's FIRST GetData() would retroactively clip every other still-pending
        // target too. GFX-068 made scissor per-draw (each queued draw carries its own captured rect),
        // so that contamination can no longer occur -- this early flush is kept as harmless hygiene.
        (void)ReadPixel(*rtAdditive_, 0, 0);

        dev.SetRenderTarget(rtScissor_.get());
        dev.Clear(Color::Blue);
        RasterizerState rsScissor;
        rsScissor.setScissorTestEnableProperty(true);
        dev.setRasterizerStateProperty(rsScissor);
        dev.setScissorRectangleProperty(Rectangle(0, 0, kRTSize / 2, kRTSize));
        DrawFullQuad(dev, *fullQuadGreenVb_);

        // Force this target's own pass to flush and read back NOW, while scissor is still live
        // and before unbinding resets ScissorRectangle back to the backbuffer's full size.
        scissorLeft_ = ReadPixel(*rtScissor_, kRTSize / 4, kRTSize / 2);
        scissorRight_ = ReadPixel(*rtScissor_, (kRTSize * 3) / 4, kRTSize / 2);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.setRasterizerStateProperty(RasterizerState());
    }

    Color scissorLeft_{0, 0, 0, 0};
    Color scissorRight_{0, 0, 0, 0};

    void RunAll(GraphicsDevice& dev)
    {
        RunBlendChecks(dev);
        RunStencilCheck(dev);
        RunStencilWriteMaskCheck(dev);
        RunStencilReadMaskCheck(dev);
        RunCullChecks(dev);
        RunFillModeChecks(dev);
        RunScissorCheck(dev);
    }

    /**
     * @brief Runs every check group, and guarantees the frame does not end with a target bound.
     *
     * REMED-GFX-180: each group binds a render target and unbinds it at its own end, so a group that
     * throws part-way skips its unbind. Returning from Draw in that state is exactly what
     * `Game::EndDraw` -> `GraphicsDevice::Present` refuses -- correctly, per FNA -- and with nothing
     * catching that refusal the process died by SIGABRT, replacing a legible failed check with a
     * silent abort. Unbinding here keeps a failure a FAIL. It does NOT weaken anything: the exception
     * is still reported, with its message, by the caller.
     *
     * @return the exception message, or an empty string when every group completed.
     */
    std::string RunAllGuarded(GraphicsDevice& dev)
    {
        lastFailure_.clear();
        try
        {
            RunAll(dev);
        }
        catch (const std::exception& e)
        {
            lastFailure_ = e.what();
        }
        catch (...)
        {
            lastFailure_ = "(non-std exception)";
        }
        try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
        return lastFailure_;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();

        rtAdditive_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtAlphaBlend_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                          DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtStencil_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                       DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        rtCullCcw_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtCullCw_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtCullNone_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtFillSolid_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                         DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtFillWire_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtScissor_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtStencilWriteMask_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                                DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        rtStencilReadMask_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                               DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);

        fullQuadGreenVb_ = MakeFullQuad(dev, Color::Green);
        fullQuadBlackAlphaVb_ = MakeFullQuad(dev, Color(0, 0, 0, 128));
        fullQuadBlueVb_ = MakeFullQuad(dev, Color::Blue);

        // Same CW winding as MakeFullQuad -- visible by default (left half only, x: -1..0).
        const VertexPositionColor leftHalfVerts[6] = {
            { Vector3(-1.0f, 1.0f, 0.0f), Color::Blue }, { Vector3(0.0f, -1.0f, 0.0f), Color::Blue }, { Vector3(-1.0f, -1.0f, 0.0f), Color::Blue },
            { Vector3(-1.0f, 1.0f, 0.0f), Color::Blue }, { Vector3(0.0f, 1.0f, 0.0f), Color::Blue }, { Vector3(0.0f, -1.0f, 0.0f), Color::Blue },
        };
        leftHalfQuadBlueVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        leftHalfQuadBlueVb_->SetData(leftHalfVerts, 0, 6);

        // Same CW winding, right half only (x: 0..1).
        const VertexPositionColor rightHalfVerts[6] = {
            { Vector3(0.0f, 1.0f, 0.0f), Color::Blue }, { Vector3(1.0f, -1.0f, 0.0f), Color::Blue }, { Vector3(0.0f, -1.0f, 0.0f), Color::Blue },
            { Vector3(0.0f, 1.0f, 0.0f), Color::Blue }, { Vector3(1.0f, 1.0f, 0.0f), Color::Blue }, { Vector3(1.0f, -1.0f, 0.0f), Color::Blue },
        };
        rightHalfQuadBlueVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        rightHalfQuadBlueVb_->SetData(rightHalfVerts, 0, 6);

        // Same CW winding as MakeFullQuad -- visible under the project default
        // (RasterizerState.CullCounterClockwise), culled under CullClockwise, the differential
        // Check D relies on.
        const VertexPositionColor cullQuadVerts[6] = {
            { Vector3(-0.6f, 0.6f, 0.0f), Color::Green }, { Vector3(0.6f, -0.6f, 0.0f), Color::Green }, { Vector3(-0.6f, -0.6f, 0.0f), Color::Green },
            { Vector3(-0.6f, 0.6f, 0.0f), Color::Green }, { Vector3(0.6f, 0.6f, 0.0f), Color::Green }, { Vector3(0.6f, -0.6f, 0.0f), Color::Green },
        };
        cullQuadGreenVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        cullQuadGreenVb_->SetData(cullQuadVerts, 0, 6);

        // Centroid = (0,0) -- render target centre, independent of NDC Y orientation. CW winding
        // (Top, BottomRight, BottomLeft) -- visible by default, same reasoning as MakeFullQuad.
        const VertexPositionColor fillTriangleVerts[3] = {
            { Vector3(0.0f, 0.8f, 0.0f), Color::Green },
            { Vector3(0.8f, -0.4f, 0.0f), Color::Green },
            { Vector3(-0.8f, -0.4f, 0.0f), Color::Green },
        };
        fillTriangleGreenVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        fillTriangleGreenVb_->SetData(fillTriangleVerts, 0, 3);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::CornflowerBlue);

        if (frame_ == 1)
        {
            if (RunAllGuarded(dev).empty())
                Check(true, "all RenderState checks render with no exception");
            else
                Check(false, ("threw during RunAll: " + lastFailure_).c_str());

            // XNA's Color::Green is (0,128,0), not lime (0,255,0) -- Additive's colour add over a
            // Red background lands on (255,128,0), not (255,255,0).
            const Color gotAdditive = ReadPixel(*rtAdditive_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotAdditive, Color(255, 128, 0)),
                  ("Check A: BlendState.Additive (Red bg + Green draw) -> (255,128,0): got=" + ColorStr(gotAdditive)).c_str());

            const Color gotAlphaBlend = ReadPixel(*rtAlphaBlend_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotAlphaBlend, Color(127, 127, 127), 16),
                  ("Check B: BlendState.AlphaBlend (White bg + half-alpha Black draw) -> mid-grey: got=" + ColorStr(gotAlphaBlend)).c_str());

            const Color gotStencilLeft = ReadPixel(*rtStencil_, kRTSize / 4, kRTSize / 2);
            Check(Matches(gotStencilLeft, Color::Green),
                  ("Check C: stencil-masked left half -> Green (stencil==ref passed): got=" + ColorStr(gotStencilLeft)).c_str());
            const Color gotStencilRight = ReadPixel(*rtStencil_, (kRTSize * 3) / 4, kRTSize / 2);
            Check(Matches(gotStencilRight, Color::Blue),
                  ("Check C: unmasked right half stays Blue (stencil test rejected): got=" + ColorStr(gotStencilRight)).c_str());

            const Color gotWriteMaskLeft = ReadPixel(*rtStencilWriteMask_, kRTSize / 4, kRTSize / 2);
            Check(Matches(gotWriteMaskLeft, Color::Green),
                  ("Check G: StencilWriteMask=0xFF left half really wrote stencil=1 -> Green: got=" + ColorStr(gotWriteMaskLeft)).c_str());
            const Color gotWriteMaskRight = ReadPixel(*rtStencilWriteMask_, (kRTSize * 3) / 4, kRTSize / 2);
            Check(Matches(gotWriteMaskRight, Color::Blue),
                  ("Check G: StencilWriteMask=0x00 right half blocked the write, stencil stayed 0 -> Blue: got=" + ColorStr(gotWriteMaskRight)).c_str());

            const Color gotReadMask = ReadPixel(*rtStencilReadMask_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotReadMask, Color::Green),
                  ("Check H: StencilMask=0x00 masks a mismatched ReferenceStencil away -> Equal still passes -> Green: got=" + ColorStr(gotReadMask)).c_str());

            const Color gotCullCcw = ReadPixel(*rtCullCcw_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotCullCcw, Color::Green),
                  ("Check D: CullCounterClockwise (project default) -> quad visible: got=" + ColorStr(gotCullCcw)).c_str());
            const Color gotCullCw = ReadPixel(*rtCullCw_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotCullCw, Color::Magenta),
                  ("Check D: CullClockwise (opposite winding) -> quad culled: got=" + ColorStr(gotCullCw)).c_str());
            const Color gotCullNone = ReadPixel(*rtCullNone_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotCullNone, Color::Green),
                  ("Check D: CullNone -> quad visible regardless: got=" + ColorStr(gotCullNone)).c_str());

            const Color gotFillSolid = ReadPixel(*rtFillSolid_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotFillSolid, Color::Green),
                  ("Check E: FillMode.Solid -> triangle centre filled: got=" + ColorStr(gotFillSolid)).c_str());
            const Color gotFillWire = ReadPixel(*rtFillWire_, kRTSize / 2, kRTSize / 2);
            Check(Matches(gotFillWire, Color::Magenta),
                  ("Check E: FillMode.WireFrame -> triangle centre NOT filled: got=" + ColorStr(gotFillWire)).c_str());

            // The absence above is only half the statement; a draw that never happened satisfies it
            // too. These two make the WireFrame half positive.
            const int solidLit = CountGreenPixels(*rtFillSolid_);
            const int wireLit = CountGreenPixels(*rtFillWire_);
            Check(wireLit > 0,
                  ("Check E: FillMode.WireFrame still rasterized the triangle's EDGES (a dropped draw "
                   "would leave zero): lit=" + std::to_string(wireLit)).c_str());
            Check(solidLit > 0 && wireLit * 2 < solidLit,
                  ("Check E: WireFrame lights far fewer pixels than Solid (edges vs interior): wire="
                   + std::to_string(wireLit) + " solid=" + std::to_string(solidLit)).c_str());

            Check(Matches(scissorLeft_, Color::Green),
                  ("Check F: scissored left half -> Green: got=" + ColorStr(scissorLeft_)).c_str());
            Check(Matches(scissorRight_, Color::Blue),
                  ("Check F: scissor-clipped right half stays Blue: got=" + ColorStr(scissorRight_)).c_str());
        }
        else
        {
            const std::string failure = RunAllGuarded(dev);
            if (!failure.empty() && laterFrameFailure_.empty())
                laterFrameFailure_ = "frame " + std::to_string(frame_) + ": " + failure;
        }

        if (frame_ == kTotalFrames)
        {
            // REMED-GFX-180: this used to be an unconditional Check(true). Frames 2..120 ran RunAll
            // outside any handler, so the only way they could "fail" was by unwinding out of Draw --
            // which ends the process rather than the check. Now a later frame's exception is a
            // recorded FAIL naming the frame it happened in.
            Check(laterFrameFailure_.empty(),
                  (laterFrameFailure_.empty()
                       ? std::string("120 frames of all RenderState checks render with no exception")
                       : "a later frame threw -- " + laterFrameFailure_).c_str());
            std::printf("=== %d/%d PASS ===\n", passCount_, kTotalChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kTotalChecks) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuRenderStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(100);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuRenderStateTest game;
    game.Run();
    return game.getResult();
}
