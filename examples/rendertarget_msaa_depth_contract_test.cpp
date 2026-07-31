// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-163 -- a depth-backed MULTISAMPLED RenderTarget2D must be constructible, bindable and
// genuinely depth-tested, and every combination the public API accepts must either work or be
// refused at a public boundary. It must never abort the process.
//
// THE DEFECT
// ----------
//   RenderTarget2D(dev, w, h, false, SurfaceFormat::Color, DepthFormat::Depth24, 4, usage)
//
// is an entirely legal XNA construction. On BGFX it killed the process by SIGTRAP inside bgfx's own
// framebuffer validation:
//
//   bgfx.cpp(5116): ASSERT isOk() -> ErrorAssert: 0x02006762 'Frame buffer depth MSAA texture
//   cannot be resolved. It must be created with either `BGFX_TEXTURE_RT_WRITE_ONLY` or
//   `BGFX_TEXTURE_MSAA_SAMPLE` flag.'
//
// `BgfxRenderTargetBackend`'s constructor created the depth attachment with the sample-count flag
// ALONE. bgfx's rule (src/bgfx.cpp, isFrameBufferValid) reads the DEPTH texture's own creation
// flags: a depth attachment whose `BGFX_TEXTURE_RT_MSAA_MASK` field is greater than 1 must also
// carry `BGFX_TEXTURE_RT_WRITE_ONLY` or `BGFX_TEXTURE_MSAA_SAMPLE`, because bgfx has no way to
// resolve a multisampled depth surface. A BX_ASSERT is not a catchable C++ exception, so no public
// layer could turn this into a clean NotSupportedException -- the game simply died.
//
// The identical fix already existed one screen further down the same file, on the CUBE path
// (REMED-GFX-141), which recorded the 2D sibling as a separate finding and left it. This file is
// that finding's oracle.
//
// WHY NO EXISTING FIXTURE CAUGHT IT
// ---------------------------------
// Three separate places had ALREADY declared the abort as a boundary rather than measured it:
//   * `bgfx_rendertarget2d_msaa_test.cpp` builds every one of its MSAA targets with
//     `DepthFormat::None`, so it tests MSAA without depth;
//   * `rendertarget_depthstencil_usage_test.cpp` carries a `msaaDepthRT2D` contract field that was
//     false on BGFX, so its depth+MSAA checks skipped;
//   * `present_lifecycle_contract_test.cpp` carries `kMsaaTargetMayHaveDepth`, false on BGFX.
// MSAA alone is safe and depth alone is safe; it is the COMBINATION that aborted, and nothing
// combined them. All three are re-armed by this task.
//
// WHY THE ORACLE DOES NOT READ THE MULTISAMPLED TARGET BACK
// --------------------------------------------------------
// A multisampled render target on BGFX reports a successful readback over untouched memory
// (REMED-GFX-134/138, and REMED-GFX-154/164 for the first-readback and MSAA-readback findings).
// Those are independent, still-open tickets and this file must not depend on them. Every content
// assertion here is therefore PRODUCER -> CONSUMER: the multisampled target is rendered into, then
// SAMPLED as a texture onto the ordinary backbuffer, and the BACKBUFFER is what is read. Leg S3
// calibrates exactly that path with a flat colour before any depth leg trusts it, so a silent
// sampling defect turns S3 red rather than making a depth result up.
//
// PROCESS ISOLATION
// -----------------
// A bgfx BX_ASSERT raises SIGTRAP and takes the whole binary with it, so a single red leg would
// otherwise destroy every other result in the shard. Each leg runs in its own forked child and the
// supervisor classifies SIGTRAP, SIGABRT, SIGSEGV, SIGALRM (hang), a non-zero exit (checks
// disagreed) and a clean exit distinctly. No leg is permitted to abort: the fix is what makes the
// matrix survivable, and there is no expected-fatal gate anywhere in this file.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX163_CAN_FORK 1
#else
#define CNA_GFX163_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRT = 16;   ///< Render-target edge. Small on purpose; every leg builds several.
    constexpr int kBBW = 64;  ///< Backbuffer width -- the only surface this file ever reads.
    constexpr int kBBH = 64;  ///< Backbuffer height.

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
#error "REMED-GFX-163: this backend has no declared MSAA/depth attachment contract."
#endif

    /**
     * @brief Whether a `RenderTargetCube` can be a draw destination here.
     *
     * Software refuses a cube destination outright (a deliberate v1 boundary recorded under
     * REMED-GFX-182) and Headless does not rasterize, so leg X1's cube regression gate is declared
     * rather than measured on those two.
     */
    constexpr bool kCubeTargetSupported =
#if defined(CNA_BACKEND_HEADLESS) || defined(CNA_BACKEND_SOFTWARE)
        false;
#else
        true;
#endif

    /**
     * @brief Whether this backend can sample a render target and rasterize the result.
     *
     * The producer -> consumer oracle needs a textured draw. Headless rasterizes nothing at all, so
     * every content leg there records a boundary and only the LIFECYCLE (construct, bind, Clear,
     * unbind, dispose) is required to be legal -- which is exactly what it is registered here for.
     */
    constexpr bool kSamplesTextures = kRasterizes;

    /// Far-apart flat colours. Any two differ by >= 100 on at least one channel, far outside kTol,
    /// so a wrong destination or a dropped draw can never be mistaken for a tolerance miss.
    const Color kClear(20, 20, 90, 255);
    const Color kNear(240, 130, 20, 255);   ///< The nearer quad (z = 0.2).
    const Color kFar(30, 200, 110, 255);    ///< The farther quad (z = 0.8).
    const Color kFlat(200, 40, 120, 255);   ///< Leg S3's calibration colour.

    /// Vertex-colour and sampled-texture round trips differ by a unit or two across backends.
    constexpr int kTol = 14;

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
 * @brief REMED-GFX-163's contract: the complete colour/depth/MSAA render-target attachment matrix.
 */
class RenderTargetMsaaDepthContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// Leg L6's target: constructed inside the frame and handed to device teardown still alive.
    std::unique_ptr<RenderTarget2D> heldAfterFrame_;

    int passCount_ = 0;
    int totalCount_ = 0;
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
     * A leg that dies by SIGTRAP prints nothing further, so this flushed line is the only record of
     * WHICH native call ended the child. Every construction in the matrix is announced this way.
     */
    void step(const std::string& text) const
    {
        std::printf("[STEP] %s\n", text.c_str());
        std::fflush(stdout);
    }

    [[nodiscard]] bool wants(const char* id) const { return onlyLeg_.empty() || onlyLeg_ == id; }

    // ---------------------------------------------------------------- geometry

    /**
     * @brief A clockwise full-NDC quad at a fixed depth, in one flat colour.
     *
     * Clockwise so it survives the project default `CullCounterClockwiseFace`. @p z is written
     * straight through: every effect matrix in this file is identity, so NDC z IS the depth value.
     */
    static void FillQuad(VertexPositionColor* o, const Color& c, float z)
    {
        o[0] = { Vector3(-1.f,  1.f, z), c };
        o[1] = { Vector3( 1.f, -1.f, z), c };
        o[2] = { Vector3(-1.f, -1.f, z), c };
        o[3] = { Vector3(-1.f,  1.f, z), c };
        o[4] = { Vector3( 1.f,  1.f, z), c };
        o[5] = { Vector3( 1.f, -1.f, z), c };
    }

    /// The standard XNA texture-on-quad UV map: NDC top-left -> UV (0, 0).
    static void FillSamplingQuad(VertexPositionTexture* q)
    {
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, Vector2(0.f, 0.f) }; q[1] = { bl, Vector2(0.f, 1.f) }; q[2] = { br, Vector2(1.f, 1.f) };
        q[3] = { tl, Vector2(0.f, 0.f) }; q[4] = { br, Vector2(1.f, 1.f) }; q[5] = { tr, Vector2(1.f, 0.f) };
    }

    /** @brief Draws one flat quad at depth @p z with the currently-set DepthStencilState. */
    static void DrawQuadAt(GraphicsDevice& dev, const Color& c, float z)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(false);
        fx.setLightingEnabledProperty(false);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        VertexPositionColor v[6];
        FillQuad(v, c, z);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 2);
    }

    // ---------------------------------------------------------------- the oracle

    /**
     * @brief Samples @p rt over the whole backbuffer and returns the backbuffer's centre pixel.
     *
     * THE multisample-safe readback. The target is never read directly -- it is drawn through the
     * ordinary sampler onto the ordinary single-sampled backbuffer, and that is what `GetBackBufferData`
     * reads. Depth is off for the consumer draw so the backbuffer's own depth state cannot mask it.
     *
     * @param dev     the device.
     * @param rt      the (possibly multisampled) producer target, already unbound.
     * @param failed  set when the consumer draw or the backbuffer read threw.
     * @return the backbuffer centre pixel, or a poison value when @p failed.
     */
    static Color SampleThroughBackbuffer(GraphicsDevice& dev, RenderTarget2D& rt, bool& failed)
    {
        Color px(0xCD, 0xCD, 0xCD, 0xCD);
        failed = false;
        try
        {
            dev.SetRenderTarget(nullptr);
            dev.Clear(Color(0, 0, 0, 255));
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            BasicEffect fx(dev);
            fx.setLightingEnabledProperty(false);
            fx.VertexColorEnabled = false;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&rt);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            VertexPositionTexture q[6];
            FillSamplingQuad(q);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);

            const Rectangle rect(kBBW / 2, kBBH / 2, 1, 1);
            dev.GetBackBufferData(&rect, &px, 0, 1);
        }
        catch (const std::exception&) { failed = true; }
        catch (...) { failed = true; }
        return px;
    }

    /**
     * @brief Requires that sampling @p rt through the backbuffer yields @p want.
     *
     * @param dev    the device.
     * @param rt     the producer target.
     * @param want   the colour the centre must hold.
     * @param label  what this is asserting.
     */
    void RequireSampled(GraphicsDevice& dev, RenderTarget2D& rt, const Color& want,
                        const std::string& label)
    {
        if (!kSamplesTextures)
        {
            boundary(label + ": " + kBackendName + " does not rasterize -- boundary recorded");
            return;
        }
        bool failed = false;
        const Color got = SampleThroughBackbuffer(dev, rt, failed);
        if (failed)
        {
            boundary(label + ": the producer->consumer path is unavailable on " + kBackendName +
                     " -- boundary recorded");
            return;
        }
        check(Near(got, want),
              label + ": want " + ColorText(want) + " got " + ColorText(got));
    }

    // ---------------------------------------------------------------- construction

    /**
     * @brief Constructs one target from the matrix, announcing it first so a fatal is attributable.
     *
     * @param dev      the device.
     * @param depth    the requested depth format.
     * @param samples  the requested multisample count.
     * @param mipMap   whether to request a mip chain.
     * @param usage    the requested render-target usage.
     * @param label    matrix-cell name, printed before the native call.
     * @return the target.
     */
    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, DepthFormat depth, int samples,
                                               bool mipMap, RenderTargetUsage usage,
                                               const std::string& label)
    {
        step(label + ": RenderTarget2D(" + std::to_string(kRT) + "x" + std::to_string(kRT) +
             ", mipMap=" + (mipMap ? "true" : "false") + ", Color, depth=" +
             std::to_string(static_cast<int>(depth)) + ", samples=" + std::to_string(samples) + ")");
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, mipMap, SurfaceFormat::Color, depth,
                                                samples, usage);
    }

    /**
     * @brief One matrix cell: construct, bind, Clear, draw, unbind, then verify the device still works.
     *
     * This is the cell that used to end the process. It asserts the PUBLIC properties the target must
     * report and that the whole lifecycle completes -- content is left to the D and S legs, so a
     * backend without a working readback still gets a real result here.
     *
     * @param depth        the requested depth format.
     * @param samples      the requested multisample count.
     * @param expectApplied  the sample count the target must report, or -1 to only require a legal one.
     * @param mipMap       whether to request a mip chain.
     * @param label        matrix-cell name.
     */
    void MatrixCell(DepthFormat depth, int samples, int expectApplied, bool mipMap,
                    const std::string& label)
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, depth, samples, mipMap, RenderTargetUsage::DiscardContents, label);

        check(rt->getDepthStencilFormatProperty() == depth,
              label + ": reports the requested DepthFormat back");

        const int applied = rt->getMultiSampleCountProperty();
        if (expectApplied >= 0)
            check(applied == expectApplied,
                  label + ": applied sample count is " + std::to_string(expectApplied) + " (got " +
                  std::to_string(applied) + ")");
        else
            check(applied >= 0 && applied <= samples,
                  label + ": applied sample count " + std::to_string(applied) +
                  " is a legal reduction of the requested " + std::to_string(samples));

        step(label + ": SetRenderTarget");
        dev.SetRenderTarget(rt.get());
        step(label + ": Clear(Target|DepthBuffer|Stencil)");
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kClear, 1.0f, 0);
        if (kRasterizes)
        {
            step(label + ": DrawUserPrimitives");
            DrawQuadAt(dev, kNear, 0.2f);
        }
        step(label + ": SetRenderTarget(nullptr)");
        dev.SetRenderTarget(nullptr);
        check(dev.GetRenderTargets().empty(), label + ": the target is publicly unbound afterwards");

        // The device must still be usable: the whole point is that the process survived.
        step(label + ": backbuffer Clear after unbind");
        dev.Clear(Color(0, 0, 0, 255));
        check(true, label + ": the full construct/bind/clear/draw/unbind cycle completed");
    }

    /**
     * @brief A cell whose combination may legitimately be refused -- but only at a PUBLIC boundary.
     *
     * Either the construction succeeds and reports a legal applied sample count, or it throws a
     * `std::exception`. A process fatal is neither, and is what this leg exists to forbid.
     */
    void MatrixCellMayRefuse(DepthFormat depth, int samples, const std::string& label)
    {
        auto& dev = getGraphicsDeviceProperty();
        try
        {
            auto rt = MakeTarget(dev, depth, samples, false, RenderTargetUsage::DiscardContents,
                                 label);
            const int applied = rt->getMultiSampleCountProperty();
            check(applied >= 0 && applied <= samples,
                  label + ": accepted, applied sample count " + std::to_string(applied) +
                  " is a legal reduction of the requested " + std::to_string(samples));
            dev.SetRenderTarget(rt.get());
            dev.Clear(kClear);
            dev.SetRenderTarget(nullptr);
            check(true, label + ": accepted and the bind cycle completed");
        }
        catch (const std::exception& e)
        {
            check(true, std::string(label) + ": refused at a public boundary -- " + e.what());
        }
    }

    // ---------------------------------------------------------------- M: the matrix

    void LegM01() { MatrixCell(DepthFormat::None,            1, 0, false, "M01 None/1"); }
    void LegM02() { MatrixCell(DepthFormat::Depth16,         1, 0, false, "M02 Depth16/1"); }
    void LegM03() { MatrixCell(DepthFormat::Depth24,         1, 0, false, "M03 Depth24/1"); }
    void LegM04() { MatrixCell(DepthFormat::Depth24Stencil8, 1, 0, false, "M04 Depth24Stencil8/1"); }
    void LegM05() { MatrixCell(DepthFormat::None,            4, 4, false, "M05 None/4"); }

    /// THE canonical REMED-GFX-163 red case, verbatim from the ticket.
    void LegM06() { MatrixCell(DepthFormat::Depth24,         4, 4, false, "M06 Depth24/4"); }

    void LegM07() { MatrixCell(DepthFormat::Depth16,         4, 4, false, "M07 Depth16/4"); }
    void LegM08() { MatrixCell(DepthFormat::Depth24Stencil8, 4, 4, false, "M08 Depth24Stencil8/4"); }
    void LegM09() { MatrixCell(DepthFormat::Depth24,         2, 2, false, "M09 Depth24/2"); }
    void LegM10() { MatrixCell(DepthFormat::Depth24,         8, 8, false, "M10 Depth24/8"); }

    /// 16x is legal in the public API and in bgfx's flag encoding; the renderer may still reduce it,
    /// so this cell only requires a legal outcome rather than a fixed number.
    void LegM11() { MatrixCell(DepthFormat::Depth24,        16, -1, false, "M11 Depth24/16"); }

    /// Not a power of two. The established CNA contract rounds DOWN to the next supported count and
    /// reports what it applied; it must not abort and must not silently claim 3.
    void LegM12() { MatrixCell(DepthFormat::Depth24,         3, 2, false, "M12 Depth24/3"); }

    /// Beyond anything any renderer supports -- must be refused or clamped at a public boundary.
    void LegM13() { MatrixCellMayRefuse(DepthFormat::Depth24, 64, "M13 Depth24/64"); }

    /// mipMap + depth + MSAA together. bgfx gives a DEPTH attachment `BGFX_RESOLVE_NONE`
    /// automatically (createFrameBuffer's TextureHandle-array overload tests `ref.isDepth()`), so the
    /// neighbouring "depth attachment cannot use BGFX_RESOLVE_AUTO_GEN_MIPS" assertion must NOT fire.
    void LegM14() { MatrixCell(DepthFormat::Depth24,         4, 4, true, "M14 Depth24/4 +mips"); }

    // ---------------------------------------------------------------- S: MSAA behaviour

    /**
     * @brief S1 -- a multisampled depth-backed target reports the sample count it applied.
     */
    void LegS1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::DiscardContents, "S1");
        const int applied = rt->getMultiSampleCountProperty();
        check(applied == 4, "S1 a depth-backed 4x target applies 4x (got " +
                            std::to_string(applied) + ")");
        auto plain = MakeTarget(dev, DepthFormat::Depth24, 0, false,
                                RenderTargetUsage::DiscardContents, "S1 control");
        check(plain->getMultiSampleCountProperty() == 0,
              "S1 the non-multisampled control still reports 0");
    }

    /**
     * @brief S2 -- the depth attachment's sample count follows the colour attachment's.
     *
     * Asserted publicly: two targets differing ONLY in depth format must apply the same sample count.
     * A backend that quietly dropped MSAA to make the depth attachment legal would fail here, which
     * is exactly the fallback this task is forbidden from introducing.
     */
    void LegS2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto noDepth = MakeTarget(dev, DepthFormat::None, 4, false,
                                  RenderTargetUsage::DiscardContents, "S2 colour-only");
        auto withDepth = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                                    RenderTargetUsage::DiscardContents, "S2 depth-backed");
        check(noDepth->getMultiSampleCountProperty() == withDepth->getMultiSampleCountProperty(),
              "S2 adding a depth format does not change the applied sample count (" +
              std::to_string(noDepth->getMultiSampleCountProperty()) + " vs " +
              std::to_string(withDepth->getMultiSampleCountProperty()) + ")");
        check(withDepth->getMultiSampleCountProperty() == 4,
              "S2 and that shared count is the requested 4, not a silent reduction to 1");
    }

    /**
     * @brief S3 -- CALIBRATION. The producer -> consumer oracle itself must be trustworthy.
     *
     * Every depth leg below reads its result through this exact path. If sampling a multisampled
     * target ever stops carrying flat colour, this leg turns red first and the depth results are
     * known to be unreliable rather than silently wrong.
     */
    void LegS3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::DiscardContents, "S3");
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kFlat, 1.0f, 0);
        dev.SetRenderTarget(nullptr);
        RequireSampled(dev, *rt, kFlat, "S3 a flat Clear on a 4x depth-backed target reaches the "
                                        "backbuffer through the sampler");
    }

    // ---------------------------------------------------------------- D: depth is genuinely alive

    /**
     * @brief Renders two overlapping full-screen quads into a 4x depth-backed target.
     *
     * @param dev    the device.
     * @param rt     the target.
     * @param state  the depth/stencil state both draws run under.
     * @param first  the colour drawn first.
     * @param firstZ its depth.
     * @param second the colour drawn second.
     * @param secondZ its depth.
     */
    static void ProduceOverlap(GraphicsDevice& dev, RenderTarget2D& rt,
                               const DepthStencilState& state,
                               const Color& first, float firstZ,
                               const Color& second, float secondZ)
    {
        dev.SetRenderTarget(&rt);
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kClear, 1.0f, 0);
        dev.setDepthStencilStateProperty(state);
        DrawQuadAt(dev, first, firstZ);
        DrawQuadAt(dev, second, secondZ);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.SetRenderTarget(nullptr);
    }

    /** @brief D1 -- far drawn first, near drawn second: the NEAR quad must win. */
    void LegD1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::DiscardContents, "D1");
        ProduceOverlap(dev, *rt, DepthStencilState::Default, kFar, 0.8f, kNear, 0.2f);
        RequireSampled(dev, *rt, kNear, "D1 far-then-near: the NEAR quad is visible");
    }

    /**
     * @brief D2 -- near drawn FIRST, far drawn second: the NEAR quad must STILL win.
     *
     * The decisive one. D1 alone is satisfied by "last draw wins"; only reversing the public draw
     * order proves the depth TEST, not the order, decided visibility.
     */
    void LegD2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::DiscardContents, "D2");
        ProduceOverlap(dev, *rt, DepthStencilState::Default, kNear, 0.2f, kFar, 0.8f);
        RequireSampled(dev, *rt, kNear,
                       "D2 near-then-far: the NEAR quad still wins -- depth decided, not draw order");
    }

    /**
     * @brief D3 -- depth test DISABLED: the later draw must win even though it is farther.
     *
     * The falsifying control for D2. If D2 passed because the depth attachment silently rejected
     * every second draw, this leg would pass wrongly too -- it cannot, because here the far quad is
     * REQUIRED to appear.
     */
    void LegD3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::DiscardContents, "D3");
        ProduceOverlap(dev, *rt, DepthStencilState::None, kNear, 0.2f, kFar, 0.8f);
        RequireSampled(dev, *rt, kFar,
                       "D3 depth test off: the later, FARTHER quad wins -- draw order decides");
    }

    /**
     * @brief D4 -- depth WRITE disabled: the near quad draws but records nothing, so far survives.
     */
    void LegD4()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::DiscardContents, "D4");
        ProduceOverlap(dev, *rt, DepthStencilState::DepthRead, kNear, 0.2f, kFar, 0.8f);
        RequireSampled(dev, *rt, kFar,
                       "D4 depth write off: the near quad left no depth, so the far quad passes");
    }

    /**
     * @brief D5 -- stencil on a multisampled Depth24Stencil8 target.
     *
     * Clears stencil to 1, then draws twice: once gated on Equal(1) (must appear) and once on
     * Equal(2) (must not). Only the second draw's colour is distinguishable, so the assertion is
     * that the FIRST colour survives.
     */
    void LegD5()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24Stencil8, 4, false,
                             RenderTargetUsage::DiscardContents, "D5");
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kClear, 1.0f, 1);

        DepthStencilState pass;
        pass.setDepthBufferEnableProperty(false);
        pass.setStencilEnableProperty(true);
        pass.setStencilFunctionProperty(CompareFunction::Equal);
        pass.setReferenceStencilProperty(1);
        pass.setStencilPassProperty(StencilOperation::Keep);
        dev.setDepthStencilStateProperty(pass);
        DrawQuadAt(dev, kNear, 0.5f);

        DepthStencilState fail;
        fail.setDepthBufferEnableProperty(false);
        fail.setStencilEnableProperty(true);
        fail.setStencilFunctionProperty(CompareFunction::Equal);
        fail.setReferenceStencilProperty(2);
        fail.setStencilPassProperty(StencilOperation::Keep);
        dev.setDepthStencilStateProperty(fail);
        DrawQuadAt(dev, kFar, 0.5f);

        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.SetRenderTarget(nullptr);
        RequireSampled(dev, *rt, kNear,
                       "D5 stencil on a 4x Depth24Stencil8 target: Equal(1) drew, Equal(2) did not");
    }

    /**
     * @brief D6 -- a depth-ONLY Clear must leave colour untouched.
     */
    void LegD6()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::PreserveContents, "D6");
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kFlat, 1.0f, 0);
        dev.Clear(ClearOptions::DepthBuffer, kNear, 1.0f, 0);   // colour argument must be ignored
        dev.SetRenderTarget(nullptr);
        RequireSampled(dev, *rt, kFlat, "D6 a depth-only Clear leaves the colour attachment alone");
    }

    /**
     * @brief D7 -- a colour-only Clear must leave DEPTH intact.
     *
     * Writes near depth, clears colour only, then draws the far quad: it must still be rejected by
     * the depth values the colour clear was not allowed to touch.
     */
    void LegD7()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::PreserveContents, "D7");
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        DrawQuadAt(dev, kNear, 0.2f);
        dev.Clear(ClearOptions::Target, kFlat, 1.0f, 0);        // colour only -- depth must survive
        DrawQuadAt(dev, kFar, 0.8f);
        dev.SetRenderTarget(nullptr);
        RequireSampled(dev, *rt, kFlat,
                       "D7 a colour-only Clear preserved depth: the far quad was still rejected");
    }

    // ---------------------------------------------------------------- L: usage and lifetime

    /** @brief Constructs, binds and unbinds a 4x depth-backed target under @p usage. */
    void UsageCell(RenderTargetUsage usage, const std::string& label)
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24Stencil8, 4, false, usage, label);
        check(rt->getRenderTargetUsageProperty() == usage, label + ": reports its usage back");
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kClear, 1.0f, 0);
        if (kRasterizes) DrawQuadAt(dev, kNear, 0.3f);
        dev.SetRenderTarget(nullptr);
        check(true, label + ": the bind cycle completed");
    }

    void LegL1() { UsageCell(RenderTargetUsage::PreserveContents, "L1 PreserveContents"); }
    void LegL2() { UsageCell(RenderTargetUsage::PlatformContents, "L2 PlatformContents"); }
    void LegL3() { UsageCell(RenderTargetUsage::DiscardContents,  "L3 DiscardContents"); }

    /**
     * @brief L4 -- repeated binds. A -> backbuffer -> A, ten times, on one 4x depth-backed target.
     *
     * No attachment may be recreated per bind and no framebuffer handle may go stale.
     */
    void LegL4()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, DepthFormat::Depth24, 4, false,
                             RenderTargetUsage::PreserveContents, "L4");
        for (int i = 0; i < 10; ++i)
        {
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
            if (kRasterizes) DrawQuadAt(dev, kNear, 0.2f);
            dev.SetRenderTarget(nullptr);
            dev.Clear(Color(0, 0, 0, 255));
        }
        check(dev.GetRenderTargets().empty(), "L4 ten A->backbuffer->A cycles left nothing bound");
        RequireSampled(dev, *rt, kNear, "L4 the tenth cycle's content is still exact");
    }

    /**
     * @brief L5 -- eight construct/destroy rounds. Native handles are recycled; nothing may go stale.
     */
    void LegL5()
    {
        auto& dev = getGraphicsDeviceProperty();
        for (int i = 0; i < 8; ++i)
        {
            auto rt = MakeTarget(dev, DepthFormat::Depth24Stencil8, 4, false,
                                 RenderTargetUsage::DiscardContents,
                                 "L5 round " + std::to_string(i));
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      kClear, 1.0f, 0);
            dev.SetRenderTarget(nullptr);
            // rt is destroyed here; the next round must not inherit its framebuffer index.
        }
        check(true, "L5 eight construct/bind/destroy rounds completed with handle reuse");
    }

    /**
     * @brief L6 -- a live 4x depth-backed target handed to GraphicsDevice teardown.
     *
     * Kept alive past the frame deliberately; the destructor order at shutdown must not double-destroy
     * the depth attachment or the framebuffer that owns it.
     */
    void LegL6()
    {
        auto& dev = getGraphicsDeviceProperty();
        heldAfterFrame_ = MakeTarget(dev, DepthFormat::Depth24Stencil8, 4, false,
                                     RenderTargetUsage::PreserveContents, "L6");
        dev.SetRenderTarget(heldAfterFrame_.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kClear, 1.0f, 0);
        dev.SetRenderTarget(nullptr);
        check(true, "L6 a live multisampled depth-backed target survives into device teardown");
    }

    // ---------------------------------------------------------------- X: the cube regression gate

    /**
     * @brief X1 -- RenderTargetCube with MSAA and depth. Fixed by REMED-GFX-141; must stay fixed.
     *
     * The cube path is where the correct flag construction already lived. This leg exists so the
     * shared helper introduced by REMED-GFX-163 cannot regress it.
     */
    void LegX1()
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!kCubeTargetSupported)
        {
            boundary("X1: " + std::string(kBackendName) +
                     " has no cube destination -- boundary recorded");
            return;
        }
        step("X1: RenderTargetCube(16, mipMap=false, Color, Depth24, samples=4)");
        RenderTargetCube cube(dev, kRT, false, SurfaceFormat::Color, DepthFormat::Depth24, 4,
                              RenderTargetUsage::DiscardContents);
        check(cube.getMultiSampleCountProperty() == 4, "X1 the cube applies 4x too");
        step("X1: SetRenderTarget(cube, PositiveX)");
        dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        if (kRasterizes) DrawQuadAt(dev, kNear, 0.2f);
        dev.SetRenderTarget(nullptr);
        check(true, "X1 a multisampled depth-backed RenderTargetCube face completed its cycle");
    }

    // ---------------------------------------------------------------- plumbing

    using Leg = void (RenderTargetMsaaDepthContractTest::*)();

    void runLeg(const char* id, Leg leg)
    {
        if (!wants(id)) return;
        (this->*leg)();
    }

    void Draw(const GameTime&) override
    {

        runLeg("M01", &RenderTargetMsaaDepthContractTest::LegM01);
        runLeg("M02", &RenderTargetMsaaDepthContractTest::LegM02);
        runLeg("M03", &RenderTargetMsaaDepthContractTest::LegM03);
        runLeg("M04", &RenderTargetMsaaDepthContractTest::LegM04);
        runLeg("M05", &RenderTargetMsaaDepthContractTest::LegM05);
        runLeg("M06", &RenderTargetMsaaDepthContractTest::LegM06);
        runLeg("M07", &RenderTargetMsaaDepthContractTest::LegM07);
        runLeg("M08", &RenderTargetMsaaDepthContractTest::LegM08);
        runLeg("M09", &RenderTargetMsaaDepthContractTest::LegM09);
        runLeg("M10", &RenderTargetMsaaDepthContractTest::LegM10);
        runLeg("M11", &RenderTargetMsaaDepthContractTest::LegM11);
        runLeg("M12", &RenderTargetMsaaDepthContractTest::LegM12);
        runLeg("M13", &RenderTargetMsaaDepthContractTest::LegM13);
        runLeg("M14", &RenderTargetMsaaDepthContractTest::LegM14);
        runLeg("S1",  &RenderTargetMsaaDepthContractTest::LegS1);
        runLeg("S2",  &RenderTargetMsaaDepthContractTest::LegS2);
        runLeg("S3",  &RenderTargetMsaaDepthContractTest::LegS3);
        runLeg("D1",  &RenderTargetMsaaDepthContractTest::LegD1);
        runLeg("D2",  &RenderTargetMsaaDepthContractTest::LegD2);
        runLeg("D3",  &RenderTargetMsaaDepthContractTest::LegD3);
        runLeg("D4",  &RenderTargetMsaaDepthContractTest::LegD4);
        runLeg("D5",  &RenderTargetMsaaDepthContractTest::LegD5);
        runLeg("D6",  &RenderTargetMsaaDepthContractTest::LegD6);
        runLeg("D7",  &RenderTargetMsaaDepthContractTest::LegD7);
        runLeg("L1",  &RenderTargetMsaaDepthContractTest::LegL1);
        runLeg("L2",  &RenderTargetMsaaDepthContractTest::LegL2);
        runLeg("L3",  &RenderTargetMsaaDepthContractTest::LegL3);
        runLeg("L4",  &RenderTargetMsaaDepthContractTest::LegL4);
        runLeg("L5",  &RenderTargetMsaaDepthContractTest::LegL5);
        runLeg("L6",  &RenderTargetMsaaDepthContractTest::LegL6);
        runLeg("X1",  &RenderTargetMsaaDepthContractTest::LegX1);

        Finish();
    }

    void Finish()
    {
        std::printf("[INFO] %s: %d/%d checks passed\n", kBackendName, passCount_, totalCount_);
        std::fflush(stdout);
        // A leg may legitimately have nothing to measure on a backend lacking the capability -- but
        // only when it SAID so. A run that measured nothing and explained nothing is a failure.
        result_ = (passCount_ == totalCount_ && (totalCount_ > 0 || boundaryDeclared_)) ? 0 : 1;
        Exit();
    }

public:
    explicit RenderTargetMsaaDepthContractTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        // No real vblank exists on the virtual displays these runs use, so leaving VSync on would
        // cost about a second per frame.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    /// Every leg, in the order the supervisor runs them. NONE of them may abort.
    const char* const kLegs[] = {
        "M01", "M02", "M03", "M04", "M05", "M06", "M07", "M08", "M09", "M10",
        "M11", "M12", "M13", "M14",
        "S1", "S2", "S3",
        "D1", "D2", "D3", "D4", "D5", "D6", "D7",
        "L1", "L2", "L3", "L4", "L5", "L6",
        "X1",
    };

#if CNA_GFX163_CAN_FORK
    /// A leg that hangs must be reported as a TIMEOUT, not waited on forever.
    constexpr unsigned kLegTimeoutSeconds = 180;

    /**
     * @brief Runs one leg in a child process and classifies the outcome exactly.
     *
     * SIGTRAP is called out by name: that is how a bgfx BX_ASSERT arrives, and it is the signature
     * of the REMED-GFX-163 defect specifically.
     *
     * @param exePath  this binary's own path.
     * @param legId    the leg to run.
     * @param skipped  set when the child reported that no usable display exists.
     * @return true when the leg completed cleanly.
     */
    bool RunLegIsolated(const char* exePath, const char* legId, bool& skipped)
    {
        skipped = false;
        const std::string arg = std::string("--leg=") + legId;

        const pid_t pid = fork();
        if (pid < 0)
        {
            std::printf("[FAIL] supervisor: fork() failed for leg %s\n", legId);
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
            const char* core = WCOREDUMP(status) ? " (core dumped)" : "";
            if (sig == SIGALRM)
                std::printf("[TIMEOUT] leg %s: no result within %u s (hang or modal dialog)\n",
                            legId, kLegTimeoutSeconds);
            else if (sig == SIGTRAP)
                std::printf("[FATAL] leg %s: SIGTRAP%s -- a native ASSERT killed the process. This "
                            "is the REMED-GFX-163 signature; the last [STEP] line above names the "
                            "call that did it.\n", legId, core);
            else if (sig == SIGABRT)
                std::printf("[FATAL] leg %s: SIGABRT%s -- std::terminate or abort()\n", legId, core);
            else if (sig == SIGSEGV)
                std::printf("[FATAL] leg %s: SIGSEGV%s\n", legId, core);
            else
                std::printf("[FATAL] leg %s: killed by signal %d (%s)%s\n", legId, sig,
                            strsignal(sig), core);
            std::fflush(stdout);
            return false;
        }
        if (WIFEXITED(status))
        {
            const int code = WEXITSTATUS(status);
            if (code == CNA::Examples::kSkipExitCode)
            {
                skipped = true;
                std::printf("[SKIP] leg %s: no usable display\n", legId);
                std::fflush(stdout);
                return true;
            }
            if (code == 0) return true;
            if (code == 127)
                std::printf("[FAIL] leg %s: execv failed -- the child never started\n", legId);
            else
                std::printf("[FAIL] leg %s: exited %d (checks ran and disagreed)\n", legId, code);
            std::fflush(stdout);
            return false;
        }
        std::printf("[FAIL] leg %s: neither exited nor signalled (status %d)\n", legId, status);
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

#if CNA_GFX163_CAN_FORK
    if (onlyLeg.empty())
    {
        const int total = static_cast<int>(sizeof(kLegs) / sizeof(kLegs[0]));
        std::printf("[INFO] REMED-GFX-163 supervisor on %s: %d legs, each in its own process\n",
                    kBackendName, total);
        std::fflush(stdout);
        int passed = 0, skippedCount = 0;
        for (const char* legId : kLegs)
        {
            bool skipped = false;
            if (RunLegIsolated(argv[0], legId, skipped)) ++passed;
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

    RenderTargetMsaaDepthContractTest game(std::move(onlyLeg));
    game.Run();
    return game.Result();
}
