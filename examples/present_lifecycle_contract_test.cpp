// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-180: the public render-target -> Present lifecycle contract, and the exact causal chain
// behind `SdlGpu_RenderState`'s abort.
//
// THE AUTHORITATIVE CONTRACT is FNA's, read from
// `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`:
//
//     public void Present()
//     {
//         if (renderTargetCount > 0)
//         {
//             throw new InvalidOperationException("Cannot present while render targets are bound");
//         }
//
// and NOTHING in FNA's frame loop unbinds on the application's behalf -- `Game.EndDraw()` calls
// `graphicsDeviceManager.EndDraw()`, which calls `graphicsDevice.Present()` directly. CNA reproduces
// both halves verbatim (`GraphicsDevice::Present`, `Game::EndDraw`). So the contract is REJECT, not
// auto-unbind: returning from `Draw` with a render target still bound is an application error, and
// the framework reports it rather than papering over it. Every leg below is written against that
// contract; none of them asks the framework to unbind anything.
//
// WHAT REMED-GFX-180 ACTUALLY WAS. `examples/sdlgpu_renderstate_test.cpp` aborted with
// `Cannot present while render targets are bound` before any check was visible. That message is the
// SECOND event in a two-stage chain, and it is not the defect:
//
//   1. `RunFillModeChecks` bound a target, cleared it, and drew a THREE-vertex triangle buffer
//      through a helper named `DrawFullQuad` that hardcodes `DrawPrimitives(TriangleList, 0, 2)` --
//      six vertices. REMED-GFX-113's range guard in `GraphicsDevice::DrawPrimitives` correctly
//      rejected it with `ArgumentOutOfRangeException: The requested primitive range exceeds the
//      bound vertex buffer. (Parameter 'primitiveCount')`.
//   2. That exception unwound past the leg's own `SetRenderTarget(nullptr)`, so the target was STILL
//      BOUND when `Draw` returned. `Game::EndDraw` then called `Present`, which -- correctly --
//      refused. Nothing caught it, so it left `main` and `std::terminate` raised SIGABRT.
//
// BOTH guards are right, and BOTH are load-bearing. The fixture was wrong. The "zero checks were
// reached" reading in the original report was an artefact of block-buffered stdout being discarded
// by the abort, not evidence that nothing ran -- which is why every print in this file is flushed.
//
// THE ORACLE IS NEVER "did not throw". Leg C1 asserts each link of that chain separately -- the
// range rejection, the target still being publicly bound afterwards, the refusal, and the recovery
// -- so a future backend that swallowed the range error, or a future Present that consulted backend
// state instead of the public binding, turns a specific leg red and names which link moved.
//
// LEG C2 IS THE PROCESS-LEVEL REPRODUCTION. It runs the same sequence with nothing catching it and
// the supervisor requires the child to die by SIGABRT. The terminate classification is therefore
// MEASURED rather than inferred from the message text.
//
// PROCESS ISOLATION: one leg deliberately aborts and several drive teardown, so a leg runs in its
// own child. With no arguments this binary is a SUPERVISOR: it re-execs itself once per leg as
// `--leg=<id>` and classifies each child as PASS / FAIL / ABORT / CRASH-with-the-signal-named /
// TIMEOUT / SKIP. Each leg prints a `[STEP]` line before every public operation, so the last line a
// child printed names the exact call that ended it.
//
// LEGS
//   A1  no target ever bound        device creation -> Present
//   A2  bind -> unbind -> Present   the minimal round trip
//   A3  draw                        bind -> draw -> unbind -> Present, content exact
//   A4  clear only                  bind -> Clear -> unbind -> Present
//   A5  no work at all              bind -> unbind with neither Clear nor draw -> Present
//   A6  A -> B -> backbuffer        two targets in one frame, both exact
//   A7  cube face                   RenderTargetCube face -> backbuffer -> Present
//   A8  MRT                         multi-target set -> backbuffer -> Present
//   A9  MSAA                        multisampled target -> unbind -> Present
//   A10 mid-cycle Clear             REMED-GFX-156's ordered Clear inside one bind cycle
//   A11 backbuffer readback         readback then Present, and Present then readback
//   A12 reset                       GraphicsDevice::Reset() then Present
//   A13 two frames                  Present on frame 1 and again on frame 2
//   A14 repetition                  120 bind/unbind cycles, then one Present
//   B1  THE NEGATIVE CONTRACT       Present with a target still bound: refuse, stay usable, recover
//   B2  destroyed while bound       the refusal is identical for a destroyed bound target
//   B3  destroyed after unbind      releasing an UNBOUND target leaves Present legal
//   B4  other binding shapes        a bound cube face and a bound MRT set refuse identically
//   C1  THE GFX-180 CHAIN           over-range draw -> still bound -> refusal -> recovery
//   C2  THE GFX-180 ABORT           the same, unhandled: the child must die by SIGABRT
//   C3  unwinding + recovery        any exception mid-cycle, caught and unbound, leaves Present legal
//   C4  teardown                    a live target handed to device teardown after a clean unbind
//
// Exit code 0 = every leg passed, 1 = any FAIL or unexpected CRASH, 77 = no usable display.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX180_CAN_FORK 1
#else
#define CNA_GFX180_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRT = 16;    ///< render-target extent, square and small on purpose
    constexpr int kBBW = 64;   ///< backbuffer width
    constexpr int kBBH = 48;   ///< backbuffer height

#if defined(CNA_BACKEND_HEADLESS)
    constexpr const char* kBackendName = "HEADLESS";
    constexpr bool kRasterizes = false;
#elif defined(CNA_BACKEND_SOFTWARE)
    constexpr const char* kBackendName = "SOFTWARE";
    constexpr bool kRasterizes = true;
#elif defined(CNA_BACKEND_EASYGL)
    constexpr const char* kBackendName = "EASYGL";
    constexpr bool kRasterizes = true;
#elif defined(CNA_BACKEND_BGFX)
    constexpr const char* kBackendName = "BGFX";
    constexpr bool kRasterizes = true;
#elif defined(CNA_BACKEND_VULKAN)
    constexpr const char* kBackendName = "VULKAN";
    constexpr bool kRasterizes = true;
#elif defined(CNA_BACKEND_WEBGPU)
    constexpr const char* kBackendName = "WEBGPU";
    constexpr bool kRasterizes = true;
#elif defined(CNA_BACKEND_SDL_GPU)
    constexpr const char* kBackendName = "SDL_GPU";
    constexpr bool kRasterizes = true;
#else
#error "REMED-GFX-180: this backend has no declared present-lifecycle contract."
#endif

    /**
     * @brief Whether a `RenderTargetCube` face can be bound as a destination here.
     *
     * Declared rather than asserted, exactly as the neighbouring render-target fixtures do. Software
     * refuses a cube destination outright (REMED-GFX-182 records that as a deliberate v1 boundary),
     * and Headless does not rasterize at all.
     */
    constexpr bool kCubeTargetSupported =
#if defined(CNA_BACKEND_HEADLESS) || defined(CNA_BACKEND_SOFTWARE)
        false;
#else
        true;
#endif

    /**
     * @brief Whether a multisampled render target may be given a real DepthFormat here.
     *
     * True on every backend. This was false on BGFX while REMED-GFX-163 was open: a depth-backed
     * multisampled target aborted the PROCESS by SIGTRAP inside `bgfx::createFrameBuffer`, so leg A9
     * had to drop the depth attachment THERE and declare the boundary rather than measure it. That
     * defect is fixed -- `BgfxDepthAttachmentFlags` now gives a multisampled depth attachment
     * `BGFX_TEXTURE_RT_WRITE_ONLY` -- so A9 asks for `DepthFormat::Depth24` everywhere again and
     * this constant is kept only as the place any future backend would declare the same limit.
     */
    constexpr bool kMsaaTargetMayHaveDepth = true;

    /// Distinct, far-apart flat colours. Any two of them differ by far more than kTol on at least one
    /// channel, so a wrong destination or a dropped draw can never land inside the tolerance.
    const Color kClearA(30, 60, 200, 255);
    const Color kDrawA(240, 130, 20, 255);
    const Color kClearB(200, 40, 120, 255);
    const Color kDrawB(40, 200, 90, 255);

    /// Vertex-colour interpolation across a flat quad is exact in principle, but the GPU backends
    /// differ by a unit or two on the sRGB round trip. 12 is far below the >= 100 separation between
    /// any two colours this fixture uses.
    constexpr int kTol = 12;

    bool Near(const Color& got, const Color& want)
    {
        auto d = [](int a, int b) { return a > b ? a - b : b - a; };
        return d(got.getRProperty(), want.getRProperty()) <= kTol
            && d(got.getGProperty(), want.getGProperty()) <= kTol
            && d(got.getBProperty(), want.getBProperty()) <= kTol;
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + ")";
    }
}

/**
 * @brief REMED-GFX-180's public contract: what a frame may and may not do with a bound render target
 *        before the framework presents it.
 */
class PresentLifecycleContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// Full-NDC quad, clockwise -- visible under the project default CullCounterClockwiseFace.
    std::unique_ptr<VertexBuffer> quadA_;
    std::unique_ptr<VertexBuffer> quadB_;
    /// THREE vertices. Leg C1/C2 ask for two primitives from it on purpose; that is the whole point.
    std::unique_ptr<VertexBuffer> triangle_;

    /// Leg C4's target: created inside the frame and handed to device teardown still alive.
    std::unique_ptr<RenderTarget2D> heldAfterFrame_;

    int passCount_ = 0;
    int totalCount_ = 0;
    int frame_ = 0;
    int result_ = 1;
    bool boundaryDeclared_ = false;
    std::string onlyLeg_;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    /** @brief Records something this backend genuinely cannot measure, and marks the run explained. */
    void boundary(const std::string& text)
    {
        boundaryDeclared_ = true;
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
    }

    /**
     * @brief Names the public operation about to be issued.
     *
     * Leg C2's outcome is a signal, so the last line a child printed is the only record of which call
     * ended it. Flushed immediately for exactly that reason -- the original GFX-180 report read
     * "zero checks executed" from a run whose checks had in fact all executed into a stdout buffer
     * the abort then discarded.
     */
    void step(const std::string& text) const
    {
        std::printf("[STEP] %s\n", text.c_str());
        std::fflush(stdout);
    }

    [[nodiscard]] bool wants(const char* id) const { return onlyLeg_.empty() || onlyLeg_ == id; }

    // ---------------------------------------------------------------- geometry

    /** @brief A clockwise full-NDC quad in one flat colour, or its left half when @p leftHalf. */
    static std::unique_ptr<VertexBuffer> MakeQuad(GraphicsDevice& dev, const Color& c, bool leftHalf)
    {
        const float right = leftHalf ? 0.0f : 1.0f;
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), c }, { Vector3(right, -1.0f, 0.0f), c },
            { Vector3(-1.0f, -1.0f, 0.0f), c }, { Vector3(-1.0f,  1.0f, 0.0f), c },
            { Vector3(right,  1.0f, 0.0f), c }, { Vector3(right, -1.0f, 0.0f), c },
        };
        auto vb = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb->SetData(verts, 0, 6);
        return vb;
    }

    /** @brief A clockwise triangle covering the target centre -- exactly THREE vertices. */
    static std::unique_ptr<VertexBuffer> MakeTriangle(GraphicsDevice& dev, const Color& c)
    {
        const VertexPositionColor verts[3] = {
            { Vector3( 0.0f,  0.8f, 0.0f), c },
            { Vector3( 0.8f, -0.4f, 0.0f), c },
            { Vector3(-0.8f, -0.4f, 0.0f), c },
        };
        auto vb = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        vb->SetData(verts, 0, 3);
        return vb;
    }

    /** @brief Issues one non-indexed draw of @p primitiveCount triangles from @p vb. */
    static void IssueDraw(GraphicsDevice& dev, VertexBuffer& vb, int primitiveCount)
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

    // ---------------------------------------------------------------- oracles

    /**
     * @brief Reads one pixel of @p rt, poison-prefilled so "wrote nothing" is distinguishable.
     *
     * @param rt      the target to read.
     * @param x       pixel column.
     * @param y       pixel row.
     * @param failed  set when the read itself threw, so the caller can declare a boundary.
     * @return the pixel, or the poison value when the read failed.
     */
    static Color ReadPixel(RenderTarget2D& rt, int x, int y, bool& failed)
    {
        Color px(0xCD, 0xCD, 0xCD, 0xCD);
        const Rectangle rect(x, y, 1, 1);
        failed = false;
        try { rt.GetData(0, &rect, &px, 0, 1); }
        catch (const std::exception&) { failed = true; }
        return px;
    }

    /**
     * @brief Asserts @p rt holds @p drawn on its left half and @p cleared on its right.
     *
     * Two pixels rather than one: in every leg that draws, the left half is the DRAW's colour and the
     * right half is the CLEAR's, so "the whole cycle was discarded" and "the draw alone was dropped"
     * are different failures. Legs that only clear pass the same colour for both.
     */
    void RequireHalfAndHalf(RenderTarget2D& rt, const std::string& label,
                            const Color& drawn, const Color& cleared)
    {
        if (!kRasterizes)
        {
            boundary(label + ": " + kBackendName + " does not rasterize -- boundary recorded");
            return;
        }
        bool leftFailed = false, rightFailed = false;
        const Color left = ReadPixel(rt, kRT / 4, kRT / 2, leftFailed);
        const Color right = ReadPixel(rt, (kRT * 3) / 4, kRT / 2, rightFailed);
        if (leftFailed || rightFailed)
        {
            boundary(label + ": GetData is unavailable on " + kBackendName + " -- boundary recorded");
            return;
        }
        check(Near(left, drawn),
              label + " the LEFT half is exact: want " + ColorText(drawn) + " got " + ColorText(left));
        check(Near(right, cleared),
              label + " the RIGHT half is exact: want " + ColorText(cleared) + " got " +
              ColorText(right));
    }

    /** @brief Binds @p rt, clears it to @p cleared and draws @p vb over its left half. */
    static void ProduceInto(GraphicsDevice& dev, RenderTarget2D& rt, const Color& cleared,
                            VertexBuffer& vb)
    {
        dev.SetRenderTarget(&rt);
        dev.Clear(cleared);
        IssueDraw(dev, vb, 2);
    }

    /** @brief How many render targets the PUBLIC layer currently considers bound. */
    static std::size_t BoundCount(GraphicsDevice& dev) { return dev.GetRenderTargets().size(); }

    /**
     * @brief Calls Present, requires the authoritative refusal, and returns its message.
     *
     * The Present call and the diagnostic label are deliberately NOT built in one expression: the
     * evaluation order of a call's arguments is unspecified, so a label that interpolated the
     * outcome inline could print the PREVIOUS call's message.
     *
     * @param dev    the device to present.
     * @param label  what this call is asserting.
     * @return the message Present refused with, or a diagnostic naming what happened instead.
     */
    std::string RequirePresentRefused(GraphicsDevice& dev, const std::string& label)
    {
        std::string what;
        bool refused = false;
        try { dev.Present(); what = "(Present returned normally)"; }
        catch (const System::InvalidOperationException& e) { refused = true; what = e.what(); }
        catch (const std::exception& e) { what = std::string("WRONG TYPE: ") + e.what(); }
        catch (...) { what = "(non-std exception)"; }
        check(refused, label + ": " + what);
        return what;
    }

    /**
     * @brief Calls Present and requires it to complete, naming anything it threw.
     *
     * @param dev    the device to present.
     * @param label  what this call is asserting.
     */
    void RequirePresentSucceeds(GraphicsDevice& dev, const std::string& label)
    {
        std::string what;
        bool ok = true;
        try { dev.Present(); }
        catch (const std::exception& e) { ok = false; what = e.what(); }
        catch (...) { ok = false; what = "(non-std exception)"; }
        check(ok, ok ? label : label + " -- it threw: " + what);
    }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState());
    }

    // ---------------------------------------------------------------- group A: valid lifecycles

    /** @brief Leg A1 (matrix 1) -- a device that never bound a target at all may present. */
    void LegA1(GraphicsDevice& dev)
    {
        step("A1: Present with no render target ever bound");
        check(BoundCount(dev) == 0, "A1 a fresh device reports zero bound render targets");
        RequirePresentSucceeds(dev, "A1 Present succeeds with no target bound");
    }

    /** @brief Leg A2 (matrix 2) -- the minimal round trip: bind, unbind, present. */
    void LegA2(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A2: bind A");
        dev.SetRenderTarget(&a);
        check(BoundCount(dev) == 1, "A2 binding a target makes the public layer report exactly one");
        step("A2: unbind to the backbuffer");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        check(BoundCount(dev) == 0, "A2 unbinding returns the public layer to zero bound targets");
        RequirePresentSucceeds(dev, "A2 Present succeeds after a clean unbind");
    }

    /** @brief Leg A3 (matrix 3) -- a target that was drawn into presents cleanly and stays exact. */
    void LegA3(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A3: bind A, clear, draw");
        ProduceInto(dev, a, kClearA, *quadA_);
        step("A3: unbind");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "A3 Present succeeds after a drawn-into target");
        RequireHalfAndHalf(a, "A3", kDrawA, kClearA);
    }

    /** @brief Leg A4 (matrix 4) -- a bind cycle whose only work is a Clear. */
    void LegA4(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A4: bind A and Clear only -- no draw");
        dev.SetRenderTarget(&a);
        dev.Clear(kClearB);
        step("A4: unbind");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "A4 Present succeeds after a Clear-only cycle");
        RequireHalfAndHalf(a, "A4", kClearB, kClearB);
    }

    /** @brief Leg A5 (matrix 6) -- a bind cycle with neither a Clear nor a draw. */
    void LegA5(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A5: bind A and immediately unbind -- no work at all");
        dev.SetRenderTarget(&a);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        check(BoundCount(dev) == 0, "A5 an empty bind cycle still returns to zero bound targets");
        RequirePresentSucceeds(dev, "A5 Present succeeds after an empty bind cycle");
    }

    /** @brief Leg A6 (matrix 7) -- A then B then the backbuffer, both targets exact. */
    void LegA6(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        RenderTarget2D b(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A6: bind A, produce");
        ProduceInto(dev, a, kClearA, *quadA_);
        step("A6: bind B WITHOUT returning to the backbuffer first, produce");
        ProduceInto(dev, b, kClearB, *quadB_);
        check(BoundCount(dev) == 1, "A6 a direct target-to-target switch still reports one binding");
        step("A6: unbind to the backbuffer");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "A6 Present succeeds after a two-target frame");
        RequireHalfAndHalf(a, "A6 target A", kDrawA, kClearA);
        RequireHalfAndHalf(b, "A6 target B", kDrawB, kClearB);
    }

    /** @brief Leg A7 (matrix 10) -- a RenderTargetCube face bound, then the backbuffer, then Present. */
    void LegA7(GraphicsDevice& dev)
    {
        if (!kCubeTargetSupported)
        {
            boundary("A7: " + std::string(kBackendName) +
                     " has no RenderTargetCube destination -- boundary recorded");
            return;
        }
        RenderTargetCube cube(dev, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        step("A7: bind cube face PositiveX");
        dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        check(BoundCount(dev) == 1, "A7 a bound cube face reports exactly one binding");
        dev.Clear(kClearA);
        step("A7: unbind to the backbuffer");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        check(BoundCount(dev) == 0, "A7 unbinding a cube face returns to zero bound targets");
        RequirePresentSucceeds(dev, "A7 Present succeeds after a cube-face cycle");
    }

    /** @brief Leg A8 (matrix 11) -- a multi-target set bound, then the backbuffer, then Present. */
    void LegA8(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        RenderTarget2D b(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        std::vector<RenderTargetBinding> set;
        set.emplace_back(static_cast<Texture*>(&a));
        set.emplace_back(static_cast<Texture*>(&b));
        step("A8: bind a two-slot MRT set");
        try { dev.SetRenderTargets(set); }
        catch (const std::exception& e)
        {
            boundary("A8: " + std::string(kBackendName) + " refuses this MRT set (" + e.what() +
                     ") -- boundary recorded");
            return;
        }
        check(BoundCount(dev) == 2, "A8 a two-slot MRT set reports exactly two bindings");
        dev.Clear(kClearA);
        step("A8: unbind the whole set to the backbuffer");
        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});
        check(BoundCount(dev) == 0, "A8 unbinding an MRT set returns to zero bound targets");
        RequirePresentSucceeds(dev, "A8 Present succeeds after an MRT cycle");
    }

    /** @brief Leg A9 (matrix 12) -- a multisampled target unbinds (resolving) and then presents. */
    void LegA9(GraphicsDevice& dev)
    {
        std::unique_ptr<RenderTarget2D> a;
        // REMED-GFX-163 re-armed this: BGFX used to abort the process here, so this leg dropped the
        // depth attachment on that backend alone. It no longer does, and every backend now measures
        // the resolve-on-unbind-then-Present transition with a real depth attachment attached.
        const DepthFormat msaaDepth =
            kMsaaTargetMayHaveDepth ? DepthFormat::Depth24 : DepthFormat::None;
        if (!kMsaaTargetMayHaveDepth)
            boundary("A9: " + std::string(kBackendName) + " cannot build a depth-backed multisampled "
                     "render target -- measured without a depth attachment");
        step("A9: create a 4x multisampled DEPTH-BACKED target");
        try
        {
            a = std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                 msaaDepth, 4,
                                                 RenderTargetUsage::DiscardContents);
        }
        catch (const std::exception& e)
        {
            boundary("A9: " + std::string(kBackendName) + " refuses a multisampled target (" +
                     e.what() + ") -- boundary recorded");
            return;
        }
        step("A9: bind, clear, draw, unbind -- the unbind is where a resolve happens");
        ProduceInto(dev, *a, kClearA, *quadA_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev,
              "A9 Present succeeds after a multisampled target resolved on unbind");
    }

    /**
     * @brief Leg A10 (matrix 13) -- REMED-GFX-156's ordered mid-cycle Clear before the unbind.
     *
     * The Clear lands BETWEEN two draws inside a single bind cycle, so a backend that batches the
     * cycle into one pass has to segment it. This leg's job here is only that segmenting a cycle does
     * not leave the device unpresentable; GFX-156's own fixture owns the ordering oracle.
     */
    void LegA10(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A10: bind A, clear, draw, clear AGAIN mid-cycle, draw");
        dev.SetRenderTarget(&a);
        dev.Clear(kClearB);
        IssueDraw(dev, *quadB_, 2);
        dev.Clear(kClearA);
        IssueDraw(dev, *quadA_, 2);
        step("A10: unbind");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev,
              "A10 Present succeeds after a mid-cycle Clear segmented the bind cycle");
        RequireHalfAndHalf(a, "A10", kDrawA, kClearA);
    }

    /**
     * @brief Leg A11 (matrix 14/15) -- a backbuffer readback on either side of a Present.
     *
     * SDL_GPU serves this through REMED-GFX-165's lazily created proxy, which changes the present
     * path the first time it is asked for. This leg exists to prove that turning that path on does
     * not make the device unpresentable, and that a read before a Present and a read after one are
     * both legal.
     */
    void LegA11(GraphicsDevice& dev)
    {
        step("A11: clear the backbuffer and read it back BEFORE presenting");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.Clear(kClearA);
        std::vector<Color> pixels(static_cast<std::size_t>(kBBW) * kBBH,
                                  Color(0xCD, 0xCD, 0xCD, 0xCD));
        bool readable = true;
        try { dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size())); }
        catch (const std::exception& e)
        {
            readable = false;
            boundary("A11: GetBackBufferData is unavailable on " + std::string(kBackendName) + " (" +
                     e.what() + ") -- boundary recorded");
        }
        if (readable && kRasterizes)
            check(Near(pixels[static_cast<std::size_t>(kBBH / 2) * kBBW + kBBW / 2], kClearA),
                  "A11 the pre-Present backbuffer read is exact: want " + ColorText(kClearA) +
                  " got " + ColorText(pixels[static_cast<std::size_t>(kBBH / 2) * kBBW + kBBW / 2]));
        RequirePresentSucceeds(dev, "A11 Present succeeds after a backbuffer readback");
        if (readable)
        {
            step("A11: read the backbuffer again AFTER the Present");
            bool again = true;
            try { dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size())); }
            catch (const std::exception&) { again = false; }
            check(again, "A11 the backbuffer stays readable after a Present");
        }
    }

    /** @brief Leg A12 (matrix 16) -- a device Reset, then a Present. */
    void LegA12(GraphicsDevice& dev)
    {
        step("A12: Reset() with the current presentation parameters");
        bool reset = true;
        std::string resetWhat;
        try { dev.Reset(); }
        catch (const std::exception& e) { reset = false; resetWhat = e.what(); }
        check(reset, "A12 Reset() completes: " + resetWhat);
        check(BoundCount(dev) == 0, "A12 a Reset leaves zero render targets bound");
        RequirePresentSucceeds(dev, "A12 Present succeeds after a Reset");
        // A PHYSICAL surface resize is not exercisable here: SDL_SetWindowSize is a no-op under the
        // headless X server these runs use, which REMED-GFX-165 already measured and declared. This
        // leg therefore asserts the reset-then-present transition only, not a changed extent.
        boundary("A12: a physical surface resize is not exercisable under a headless X server -- the "
                 "reset-then-Present transition is what is asserted");
    }

    /** @brief Leg A14 (matrix 20) -- many bind/unbind cycles must not accumulate anything. */
    void LegA14(GraphicsDevice& dev)
    {
        step("A14: 120 bind/clear/draw/unbind cycles in one frame");
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        int completed = 0;
        std::string limitWhat;
        try
        {
            for (int i = 0; i < 120; ++i)
            {
                ProduceInto(dev, a, kClearA, *quadA_);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ++completed;
            }
        }
        catch (const std::exception& e)
        {
            limitWhat = e.what();
        }
        if (!limitWhat.empty())
        {
            // BGFX allocates ordered view ids from a fixed per-frame range (REMED-GFX-065, and
            // GFX-018/GFX-155 before it), so a frame has a documented ceiling on how many bind
            // cycles it can hold. It reports that as a clear exception rather than corrupting
            // anything, which is the behaviour this leg accepts and records. The tail of the leg is
            // still asserted below, so the boundary costs coverage of the COUNT only.
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            boundary("A14: " + std::string(kBackendName) + " has a documented per-frame bind-cycle "
                     "ceiling and reached it after " + std::to_string(completed) +
                     " cycles: " + limitWhat);
        }
        check(BoundCount(dev) == 0,
              "A14 " + std::to_string(completed) + " cycles end with zero bound targets");
        RequirePresentSucceeds(dev, "A14 Present succeeds after " + std::to_string(completed) +
                                        " bind cycles");
        RequireHalfAndHalf(a, "A14", kDrawA, kClearA);
    }

    // ---------------------------------------------------------------- group B: the negative contract

    /**
     * @brief Leg B1 (matrix 5) -- THE NEGATIVE PRESENT TEST.
     *
     * Present with a target still bound must refuse deterministically, and refusing must not cost
     * anything: the target's own content is still correct afterwards, unbinding still works, the very
     * next Present succeeds, and a further bind cycle after that is still exact. A guard that
     * poisoned the device or dropped the cycle's queued work would fail here even though it threw the
     * right exception.
     */
    void LegB1(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("B1: bind A, clear, draw -- then Present WITHOUT unbinding");
        ProduceInto(dev, a, kClearA, *quadA_);
        const std::string what = RequirePresentRefused(
            dev, "B1 Present with a target still bound throws InvalidOperationException");
        check(what == "Cannot present while render targets are bound",
              "B1 the refusal carries FNA's exact message: \"" + what + "\"");
        check(BoundCount(dev) == 1,
              "B1 a refused Present leaves the binding exactly as it was, neither cleared nor forced");
        step("B1: the refusal must not have cost the cycle its queued work");
        RequireHalfAndHalf(a, "B1", kDrawA, kClearA);
        step("B1: unbind and present for real");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "B1 the very next Present succeeds after unbinding");
        step("B1: the device is still fully usable -- one more complete bind cycle");
        RenderTarget2D b(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, b, kClearB, *quadB_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "B1 a further frame presents normally");
        RequireHalfAndHalf(b, "B1 after recovery", kDrawB, kClearB);
    }

    /**
     * @brief Leg B2 (matrix 8) -- the refusal is decided from the public binding, not the object.
     *
     * REMED-GFX-168's leg P1 owns this contract in depth; it is restated here because GFX-180's whole
     * chain depends on the refusal being a thrown exception rather than a dereference of whatever the
     * binding used to point at.
     */
    void LegB2(GraphicsDevice& dev)
    {
        {
            step("B2: bind a target and let its destructor run while it is still bound");
            RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, kClearA, *quadA_);
        }
        step("B2: Present with the bound target already destroyed");
        (void)RequirePresentRefused(dev, "B2 Present refuses identically for a DESTROYED bound target");
        step("B2: recover");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "B2 Present succeeds once the binding is released");
    }

    /** @brief Leg B3 (matrix 9) -- releasing an already-unbound target leaves Present legal. */
    void LegB3(GraphicsDevice& dev)
    {
        {
            step("B3: bind, produce, UNBIND, then destroy");
            RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, kClearA, *quadA_);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        check(BoundCount(dev) == 0, "B3 destroying an unbound target leaves nothing bound");
        RequirePresentSucceeds(dev,
              "B3 Present succeeds after an unbound target was destroyed");
    }

    /** @brief Leg B4 -- the refusal covers every binding shape, not just RenderTarget2D. */
    void LegB4(GraphicsDevice& dev)
    {
        if (kCubeTargetSupported)
        {
            RenderTargetCube cube(dev, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            step("B4: Present with a CUBE FACE bound");
            dev.SetRenderTarget(&cube, CubeMapFace::NegativeZ);
            dev.Clear(kClearA);
            (void)RequirePresentRefused(dev, "B4 Present refuses with a bound cube face");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        else
        {
            boundary("B4: " + std::string(kBackendName) +
                     " has no RenderTargetCube destination -- cube half skipped");
        }

        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        RenderTarget2D b(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        std::vector<RenderTargetBinding> set;
        set.emplace_back(static_cast<Texture*>(&a));
        set.emplace_back(static_cast<Texture*>(&b));
        step("B4: Present with an MRT SET bound");
        try { dev.SetRenderTargets(set); }
        catch (const std::exception& e)
        {
            boundary("B4: " + std::string(kBackendName) + " refuses this MRT set (" + e.what() +
                     ") -- MRT half skipped");
            return;
        }
        (void)RequirePresentRefused(dev, "B4 Present refuses with a bound MRT set");
        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});
        RequirePresentSucceeds(dev, "B4 Present succeeds once the MRT set is unbound");
    }

    // ---------------------------------------------------------------- group C: the GFX-180 chain

    /**
     * @brief Leg C1 -- THE REMED-GFX-180 CAUSAL CHAIN, one assertion per link.
     *
     * This is the exact sequence `sdlgpu_renderstate_test.cpp`'s `RunFillModeChecks` issued: a bound
     * target, a Clear, and then a request for TWO triangles out of a THREE-vertex buffer. Every link
     * is asserted separately so a future change that moves any one of them names itself:
     *
     *   1. the over-range request is rejected by REMED-GFX-113's range guard, naming `primitiveCount`;
     *   2. the rejection unwinds without unbinding, so the target is STILL publicly bound;
     *   3. Present therefore refuses -- the secondary exception the original report saw;
     *   4. unbinding is all that is needed to make the device presentable again.
     */
    void LegC1(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("C1: bind A and clear it");
        dev.SetRenderTarget(&a);
        dev.Clear(kClearA);

        step("C1: ask for TWO triangles out of a THREE-vertex buffer");
        bool rejected = false;
        std::string drawWhat;
        try { IssueDraw(dev, *triangle_, 2); }
        catch (const System::ArgumentOutOfRangeException& e) { rejected = true; drawWhat = e.what(); }
        catch (const std::exception& e) { drawWhat = std::string("WRONG TYPE: ") + e.what(); }
        check(rejected, "C1 link 1: an over-range DrawPrimitives throws ArgumentOutOfRangeException: " +
                        drawWhat);
        check(drawWhat.find("primitiveCount") != std::string::npos,
              "C1 link 1: the rejection names the offending parameter: " + drawWhat);
        check(drawWhat.find("exceeds the bound vertex buffer") != std::string::npos,
              "C1 link 1: the rejection names the range it exceeded: " + drawWhat);

        check(BoundCount(dev) == 1,
              "C1 link 2: the rejection unwound WITHOUT unbinding -- the target is still bound");
        check(dev.GetRenderTargets()[0].getRenderTargetProperty() == static_cast<Texture*>(&a),
              "C1 link 2: the still-bound target is the one the leg bound, not some stale entry");

        step("C1: Present with that target still bound -- the secondary exception GFX-180 reported");
        const std::string presentWhat = RequirePresentRefused(
            dev, "C1 link 3: Present refuses while the target is still bound");
        check(presentWhat == "Cannot present while render targets are bound",
              "C1 link 3: it is exactly the message GFX-180 reported: \"" + presentWhat + "\"");

        step("C1: unbind -- the only thing needed to recover");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "C1 link 4: unbinding alone makes Present legal again");
        // The Clear that was queued before the rejected draw is still the cycle's work, and it must
        // not have been discarded by the failed draw or by the refused Present.
        RequireHalfAndHalf(a, "C1 link 4", kClearA, kClearA);

        step("C1: a correctly counted ONE-triangle draw of the same buffer is accepted");
        RenderTarget2D b(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&b);
        dev.Clear(kClearB);
        bool accepted = true;
        std::string oneWhat;
        try { IssueDraw(dev, *triangle_, 1); }
        catch (const std::exception& e) { accepted = false; oneWhat = e.what(); }
        check(accepted, "C1 link 5: the SAME buffer drawn as ONE primitive is accepted: " + oneWhat);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "C1 link 5: and the frame presents");
    }

    /**
     * @brief Leg C3 (matrix 17) -- any exception mid-cycle, caught and unbound, leaves Present legal.
     *
     * This is the shape the repaired `sdlgpu_renderstate_test.cpp` now uses: a leg that throws is
     * reported as a failed CHECK, and the harness unbinds before the frame ends so the failure stays
     * a failure instead of becoming a process abort. Asserting it here is what keeps that recovery
     * pattern a contract rather than a habit.
     */
    void LegC3(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        bool caught = false;
        try
        {
            step("C3: bind A, produce, then throw a plain exception mid-cycle");
            ProduceInto(dev, a, kClearA, *quadA_);
            throw std::runtime_error("C3's deliberate mid-cycle failure");
        }
        catch (const std::runtime_error&)
        {
            caught = true;
            step("C3: the handler unbinds, exactly as a test harness must");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        check(caught, "C3 the mid-cycle exception was caught by the leg, not by the frame loop");
        check(BoundCount(dev) == 0, "C3 the handler's unbind returned the device to the backbuffer");
        RequirePresentSucceeds(dev,
              "C3 Present succeeds after an exception was caught and unbound");
        RequireHalfAndHalf(a, "C3", kDrawA, kClearA);
    }

    /**
     * @brief Leg C4 (matrix 18) -- a live target handed to device teardown after a clean unbind.
     *
     * The matrix asks for "teardown with a target logically BOUND". That state is unreachable through
     * the public frame loop by construction: a frame cannot end with a binding, because `EndDraw`
     * presents and `Present` refuses -- which is precisely what leg C2 measures. So what teardown can
     * actually be handed is a live, UNBOUND target, and that is what this leg does; the bound variant
     * is declared unreachable rather than left as an untested gap.
     */
    void LegC4(GraphicsDevice& dev)
    {
        step("C4: create a target, produce into it, unbind, and keep it alive past the frame");
        heldAfterFrame_ = std::make_unique<RenderTarget2D>(
            dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        ProduceInto(dev, *heldAfterFrame_, kClearA, *quadA_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequireHalfAndHalf(*heldAfterFrame_, "C4", kDrawA, kClearA);
        RequirePresentSucceeds(dev,
              "C4 Present succeeds with a live unbound target awaiting teardown");
        boundary("C4: teardown with a target still BOUND is unreachable through the public frame loop "
                 "-- EndDraw presents and Present refuses; leg C2 measures that outcome directly");
    }

    // ---------------------------------------------------------------- multi-frame legs

    /** @brief Leg A13 (matrix 19) frame 1 -- a complete cycle and an explicit Present. */
    void LegA13Frame1(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A13 frame 1: bind, produce, unbind, Present");
        ProduceInto(dev, a, kClearA, *quadA_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "A13 the FIRST frame presents");
        RequireHalfAndHalf(a, "A13 frame 1", kDrawA, kClearA);
    }

    /** @brief Leg A13 frame 2 -- the same cycle one real frame boundary later. */
    void LegA13Frame2(GraphicsDevice& dev)
    {
        RenderTarget2D b(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        step("A13 frame 2: the same cycle, after a real frame boundary");
        ProduceInto(dev, b, kClearB, *quadB_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RequirePresentSucceeds(dev, "A13 a LATER frame presents identically");
        RequireHalfAndHalf(b, "A13 frame 2", kDrawB, kClearB);
    }

    /**
     * @brief Leg C2 -- THE GFX-180 ABORT, reproduced end to end and left unhandled.
     *
     * Runs the identical sequence to leg C1 and then simply RETURNS with the target still bound. The
     * frame loop's `EndDraw` calls `Present`, `Present` refuses, nothing catches it, and the process
     * dies by SIGABRT. The supervisor requires exactly that signal, so "the fixture aborted" is a
     * measured outcome attached to a named cause rather than a message read out of a log.
     */
    void LegC2(GraphicsDevice& dev)
    {
        RenderTarget2D* leaked = new RenderTarget2D(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                    DepthFormat::None, 0,
                                                    RenderTargetUsage::DiscardContents);
        step("C2: bind a target and clear it");
        dev.SetRenderTarget(leaked);
        dev.Clear(kClearA);
        step("C2: issue the over-range draw and swallow ONLY that exception");
        try { IssueDraw(dev, *triangle_, 2); }
        catch (const System::ArgumentOutOfRangeException&) {}
        step("C2: return from Draw with the target STILL BOUND -- EndDraw must now abort");
        // Deliberately leaked: the process is about to terminate, and freeing the still-bound target
        // here would add a second, unrelated question to what this leg measures.
    }

    // ---------------------------------------------------------------- frame loop

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        ResetState(dev);

        if (onlyLeg_ == "A13")
        {
            if (frame_ == 1) { LegA13Frame1(dev); return; }
            LegA13Frame2(dev);
            Finish();
            return;
        }
        if (onlyLeg_ == "C2")
        {
            LegC2(dev);
            return;   // no Finish(): EndDraw is the subject of this leg
        }

        auto runLeg = [&](const char* id, void (PresentLifecycleContractTest::*leg)(GraphicsDevice&))
        {
            if (!wants(id)) return;
            try { (this->*leg)(dev); }
            catch (const std::exception& e)
            {
                check(false, std::string("leg ") + id + ": an unexpected exception escaped: " + e.what());
            }
            catch (...)
            {
                check(false, std::string("leg ") + id + ": an unexpected non-std exception escaped");
            }
            // The contract this file pins is exactly why this line exists: a leg that threw mid-cycle
            // would otherwise end the frame with a binding, and EndDraw would turn a reported FAIL
            // into a process abort that reports nothing.
            try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
            ResetState(dev);
        };

        runLeg("A1",  &PresentLifecycleContractTest::LegA1);
        runLeg("A2",  &PresentLifecycleContractTest::LegA2);
        runLeg("A3",  &PresentLifecycleContractTest::LegA3);
        runLeg("A4",  &PresentLifecycleContractTest::LegA4);
        runLeg("A5",  &PresentLifecycleContractTest::LegA5);
        runLeg("A6",  &PresentLifecycleContractTest::LegA6);
        runLeg("A7",  &PresentLifecycleContractTest::LegA7);
        runLeg("A8",  &PresentLifecycleContractTest::LegA8);
        runLeg("A9",  &PresentLifecycleContractTest::LegA9);
        runLeg("A10", &PresentLifecycleContractTest::LegA10);
        runLeg("A11", &PresentLifecycleContractTest::LegA11);
        runLeg("A12", &PresentLifecycleContractTest::LegA12);
        runLeg("A14", &PresentLifecycleContractTest::LegA14);
        runLeg("B1",  &PresentLifecycleContractTest::LegB1);
        runLeg("B2",  &PresentLifecycleContractTest::LegB2);
        runLeg("B3",  &PresentLifecycleContractTest::LegB3);
        runLeg("B4",  &PresentLifecycleContractTest::LegB4);
        runLeg("C1",  &PresentLifecycleContractTest::LegC1);
        runLeg("C3",  &PresentLifecycleContractTest::LegC3);
        runLeg("C4",  &PresentLifecycleContractTest::LegC4);

        Finish();
    }

    void Finish()
    {
        std::printf("[INFO] %s: %d/%d checks passed\n", kBackendName, passCount_, totalCount_);
        std::fflush(stdout);
        // A leg may legitimately have nothing to measure on a backend that lacks the capability --
        // but only when it SAID so. A run that measured nothing and explained nothing is a failure.
        result_ = (passCount_ == totalCount_ && (totalCount_ > 0 || boundaryDeclared_)) ? 0 : 1;
        Exit();
    }

    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        quadA_ = MakeQuad(dev, kDrawA, true);
        quadB_ = MakeQuad(dev, kDrawB, true);
        triangle_ = MakeTriangle(dev, kDrawA);
    }

public:
    explicit PresentLifecycleContractTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        // No real vblank exists on the virtual displays these runs use, so leaving VSync on would
        // cost about a second per frame. Requested at the property level before CreateDevice reads it.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
        // Leg A9's subject is a MULTISAMPLED target, and on backends that tie render-target MSAA to
        // the device's own sample count it can only engage when the backbuffer was created
        // multisampled. Requested in A9's own child only, so no other leg's measurement moves.
        if (onlyLeg_ == "A9") gdm_->setPreferMultiSamplingProperty(true);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    /** @brief One leg, and whether its child is EXPECTED to die by SIGABRT. */
    struct LegSpec
    {
        const char* id;
        bool expectAbort;
    };

    /// Every leg, in the order the supervisor runs them. C2 is the only one whose correct outcome is
    /// a signal rather than an exit code.
    const LegSpec kLegs[] = {
        { "A1", false }, { "A2", false }, { "A3", false }, { "A4", false }, { "A5", false },
        { "A6", false }, { "A7", false }, { "A8", false }, { "A9", false }, { "A10", false },
        { "A11", false }, { "A12", false }, { "A13", false }, { "A14", false },
        { "B1", false }, { "B2", false }, { "B3", false }, { "B4", false },
        { "C1", false }, { "C2", true }, { "C3", false }, { "C4", false },
    };

#if CNA_GFX180_CAN_FORK
    /// A leg that hangs must be reported as a TIMEOUT, not waited on forever.
    constexpr unsigned kLegTimeoutSeconds = 180;

    /**
     * @brief Runs one leg in a child process and classifies the outcome.
     *
     * @param exePath  this binary's own path.
     * @param leg      the leg to run, and whether SIGABRT is its expected outcome.
     * @param skipped  set when the child reported that no usable display exists.
     * @return true when the child's outcome matched what the leg expects.
     */
    bool RunLegIsolated(const char* exePath, const LegSpec& leg, bool& skipped)
    {
        skipped = false;
        const std::string arg = std::string("--leg=") + leg.id;

        const pid_t pid = fork();
        if (pid < 0)
        {
            std::printf("[FAIL] supervisor: fork() failed for leg %s\n", leg.id);
            std::fflush(stdout);
            return false;
        }
        if (pid == 0)
        {
            alarm(kLegTimeoutSeconds);   // a hang arrives as SIGALRM, reported distinctly below
            char* argv[3];
            argv[0] = const_cast<char*>(exePath);
            argv[1] = const_cast<char*>(arg.c_str());
            argv[2] = nullptr;
            execv(exePath, argv);
            std::_Exit(127);
        }

        int status = 0;
        while (waitpid(pid, &status, 0) < 0) { /* EINTR */ }

        if (WIFSIGNALED(status))
        {
            const int sig = WTERMSIG(status);
            if (sig == SIGALRM)
            {
                std::printf("[TIMEOUT] leg %s: no result within %u s (hang or modal dialog)\n",
                            leg.id, kLegTimeoutSeconds);
                std::fflush(stdout);
                return false;
            }
            if (sig == SIGABRT)
            {
                const bool wanted = leg.expectAbort;
                std::printf("[%s] leg %s: SIGABRT%s\n", wanted ? "PASS" : "CRASH", leg.id,
                            wanted ? " -- the expected unhandled Present refusal"
                                   : " -- an UNEXPECTED abort");
                std::fflush(stdout);
                return wanted;
            }
            std::printf("[CRASH] leg %s: killed by signal %d (%s)%s\n", leg.id, sig, strsignal(sig),
                        WCOREDUMP(status) ? " (core dumped)" : "");
            std::fflush(stdout);
            return false;
        }
        if (WIFEXITED(status))
        {
            const int code = WEXITSTATUS(status);
            if (leg.expectAbort)
            {
                std::printf("[FAIL] leg %s: exited %d, but the contract requires an abort here\n",
                            leg.id, code);
                std::fflush(stdout);
                return false;
            }
            if (code == CNA::Examples::kSkipExitCode)
            {
                skipped = true;
                std::printf("[SKIP] leg %s: no usable display\n", leg.id);
                std::fflush(stdout);
                return true;
            }
            if (code == 0) return true;
            if (code == 127)
                std::printf("[FAIL] leg %s: execv failed -- the child never started\n", leg.id);
            else
                std::printf("[FAIL] leg %s: exited %d (checks ran and disagreed)\n", leg.id, code);
            std::fflush(stdout);
            return false;
        }
        std::printf("[FAIL] leg %s: neither exited nor signalled (status %d)\n", leg.id, status);
        std::fflush(stdout);
        return false;
    }
#endif
}

int main(int argc, char** argv)
{
    std::string onlyLeg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind("--leg=", 0) == 0) onlyLeg = a.substr(6);
    }

#if CNA_GFX180_CAN_FORK
    if (onlyLeg.empty())
    {
        const int total = static_cast<int>(sizeof(kLegs) / sizeof(kLegs[0]));
        std::printf("[INFO] REMED-GFX-180 supervisor on %s: %d legs, each in its own process\n",
                    kBackendName, total);
        std::fflush(stdout);
        int passed = 0, skippedCount = 0;
        for (const LegSpec& leg : kLegs)
        {
            bool skipped = false;
            if (RunLegIsolated(argv[0], leg, skipped)) ++passed;
            if (skipped) ++skippedCount;
        }
        std::printf("[INFO] supervisor: %d/%d legs passed (%d skipped)\n", passed, total,
                    skippedCount);
        std::fflush(stdout);
        if (skippedCount == total) return CNA::Examples::kSkipExitCode;
        return (passed == total) ? 0 : 1;
    }
#endif

    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    PresentLifecycleContractTest game(onlyLeg);
    game.Run();
    return game.Result();
}
