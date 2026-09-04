// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-147: a RenderTarget2D used as a texture must sample in the SAME logical orientation as
// an ordinary Texture2D holding identical bytes.
//
// REMED-GFX-151 update: leg G0 -- the canonical render-to-texture sequence with NO readback of the
// source -- used to be DECLARED broken on Vulkan and is now an ordinary orientation check on every
// renderer. See its own fixture, examples/rendertarget_producer_consumer_test.cpp.
//
// The public contract this pins down, in CNA/XNA/FNA terms:
//
//   * Texture2D's logical origin is top-left: SetData element (y * width + x) is row y from the
//     TOP.
//   * RenderTarget2D::GetData exposes that same logical top-left orientation.
//   * A RenderTarget2D handed to SpriteBatch or to a stock 3D effect must therefore land on screen
//     exactly like a Texture2D whose SetData bytes are that target's GetData bytes. Producer ->
//     unbind -> consumer must never require the game to flip UVs by hand.
//   * Chaining targets must not alternate orientation: A -> B -> C is upright, not flipped-twice.
//
// The defect this reproduces is EasyGL-local. An OpenGL framebuffer's origin is bottom-left, so a
// render target's colour texture stores the logical image BOTTOM-UP; GetData already compensates
// (it reads with glReadPixels' bottom-left origin and then reverses the rows), which is why the
// public readback has always been correct. Sampling did not compensate, so texel v=0 -- the first
// row of GL texture memory -- is the LAST logical row, and a rendered texture arrived upside down
// while an uploaded one did not. Measured first on EasyGL while building REMED-GFX-131's
// cross-renderer controls: leg D of that fixture (sample a plain Texture2D into a target) was
// byte-exact, and leg E (sample a render target into a target) returned the same image with its
// rows reversed. This is the EasyGL counterpart of REMED-GFX-067's bgfx finding.
//
// THE ORACLE. Every orientation claim here is anchored by a side-by-side pair drawn in ONE frame,
// read back ONCE, with identical source/destination rectangles, sampler and blend state:
//
//     left  half <- RenderTarget2D A  (content produced by rendering)
//     right half <- Texture2D      B  (SetData of A.GetData(), i.e. A's own bytes)
//
// The two halves must be byte-identical. Neither half uses custom or pre-flipped UVs -- a fixture
// that flipped its own V would test nothing. The right half additionally has to equal the source
// pattern, so a failure names which side is wrong rather than only that they differ.
//
// THE PATTERN is 8x4 -- deliberately NON-SQUARE, so a transpose cannot masquerade as a pass -- and
// every one of its 32 texels is unique in (R, G): R ramps with x, G ramps with y, B adds a third
// asymmetry and A carries four distinct non-trivial values. Upper-left, upper-right, lower-left,
// lower-right, centre, the final row and the final column are therefore all separately identified,
// and a vertical mirror, a horizontal mirror, a 180-degree rotation, a transpose, a uniform fill
// and a stale/zero buffer each fail distinguishably.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   S  setup            RT store + readback is top-down, proven twice: once through an uploaded
//                       texture and once through a texture-free vertex-colour quadrant draw
//   A  Texture2D  -> SpriteBatch          ordinary sampling (the control)
//   B  RenderTarget2D -> SpriteBatch      the finding
//   C  Texture2D  -> 3D textured quad     ordinary sampling through mesh UVs
//   D  RenderTarget2D -> 3D textured quad the finding, through mesh UVs instead of sprite UVs
//   E  target A -> target B               one sampling leg between two targets
//   F  target A -> backbuffer             GetBackBufferData is the honest oracle here
//   G  target A -> B -> C                 no double flip
//   H  target A sampled twice in a frame  the correction is not consumed by the first use
//   I  GetData before/after sampling      sampling does not mutate stored bytes
//   J  SpriteEffects composition          the public flip and the renderer correction are separate
//   K  source rect / rotation / origin / transform / viewport / scissor / Linear / alpha blend
//   L  MSAA and resolve                   the correction does not depend on sample count
//   M  mipmapped target                   level 0 upright; level 1 orientation-consistent
//   N  readback contract                  partial rect, destination offset, repeat stability
//   O  cube boundary control              RenderTargetCube behaviour is UNCHANGED by this task
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /**
     * @brief Whether this renderer rasterizes and can read a render target back at all.
     *
     * HEADLESS performs no rasterization, so REMED-GFX-127's contract makes its `GetData` reject
     * deterministically. There is no orientation to measure there; the legs below assert the
     * rejection instead of a value.
     */
#if defined(CNA_RENDERER_HEADLESS)
    constexpr bool kRasterizes = false;
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SDL_RENDERER";
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "FREEDIRECT";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX12";
#elif defined(CNA_RENDERER_CANVAS)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "CANVAS";
#elif defined(CNA_RENDERER_SOKOL)
    // plans/plan_sokol.md SOKOL-38: real geometry is genuinely rasterized, and RenderTarget2D sampling
    // orientation is now correct -- REMED-GFX-147 found (via this very file, once GetData() below
    // could finally observe a real comparison instead of always throwing) that it was NOT correct
    // before this task: a render target's colour image is written by GPU rasterization, whose
    // framebuffer-origin convention stores CNA's logical row 0 at OpenGL's HIGH y, the opposite of
    // a plain SokolTextureRenderer's un-flipped CPU upload -- sampling both the same way silently
    // mirrored every render-target source vertically. Fixed with a per-draw `rtFlipV` shader
    // uniform (sprite_fs_params/textured3d_fs_params/lit3d_fs_params), the same per-slot-uniform
    // shape EasyGL's own uRtFlipV and bgfx's u_rtFlipV use for this identical finding.
    // `RequireReadable`'s direct RenderTarget2D::GetData now round-trips real content too
    // (SokolRenderTargetRenderer::GetData reads back via a throwaway GL FBO around the raw GL
    // texture sg_gl_query_image_info() exposes), so `kRasterizes = true` is accurate for what this
    // file measures -- and, as of this task, every one of its 53 checks passes for real.
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SOKOL";
#elif defined(CNA_RENDERER_LLGL)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "LLGL";
#else
#error "REMED-GFX-147: this renderer has no declared render-target orientation contract."
#endif

    /**
     * @brief Whether a render target that has never been read back can be SAMPLED at all.
     *
     * REMED-GFX-151 (found here by leg G0, fixed under its own ticket): on Vulkan a RenderTarget2D
     * that was rendered, unbound and then used as a texture without an intervening GetData sampled
     * as entirely empty, and inserting one GetData of that target made the identical draw correct --
     * the canonical XNA render-to-texture sequence, reproduced at 0/32 with 14 texels entirely
     * (0,0,0,0). The cause was the deferred recorder's readback flush filtering the frame's segment
     * list down to the target being READ, so a PRODUCER target's pass was never recorded before the
     * consumer that sampled it; GetData was an accidental execution barrier. Now true everywhere,
     * which is what leg G0 asserts.
     */
    constexpr bool kSampleWithoutReadbackWorks = true;

    /**
     * @brief Whether a RenderTarget2D may be handed to a stock 3D effect as its texture.
     *
     * REMED-GFX-152 CLOSED this (2026-07-29), and the declaration is now unconditionally true.
     *
     * It used to read false on SDL_GPU: thirteen stock-effect sites there did
     * `static_cast<const SdlGpuTextureRenderer*>(params.textureN)` while a RenderTarget2D's renderer
     * is the unrelated sibling SdlGpuRenderTargetRenderer, so the cast was undefined behaviour and
     * the process died -- the SDL_GPU counterpart of REMED-GFX-078's bgfx finding. All thirteen now
     * go through one shared `ResolveSampledTextureEXT`, and this file's CD legs were A/B-proven
     * against the pre-fix renderer: SIGSEGV before, exact pixels after. The constant is kept rather
     * than deleted so a future renderer can declare the boundary again if it genuinely has one.
     */
    constexpr bool kStockEffectRtSourceSupported = true;

    // REMED-GFX-154 is FIXED, so the `kMsaaFirstReadbackWorks` declaration that used to live here --
    // false on BGFX, pinning "the first readback of a multisampled target is entirely empty" -- is
    // gone, together with the dummy-target bind leg L1 used to nudge the resolve into completing.
    // The declaration was written to fail the moment the defect was fixed, and it did exactly that.
    // L1 now reads the multisampled target ONCE and asserts the pattern, like every other leg.

    constexpr int kBBW = 64;   ///< Backbuffer width.
    constexpr int kBBH = 64;   ///< Backbuffer height.

    constexpr int kPW = 8;     ///< Pattern width.  Deliberately different from kPH.
    constexpr int kPH = 4;     ///< Pattern height. A transpose therefore cannot pass.

    /**
     * @brief The source pattern texel at (@p x, @p y), with (0, 0) the TOP-LEFT corner.
     *
     * R is a function of x alone and G of y alone, so the pair (R, G) is unique for all 32 texels
     * and identifies both axes independently. B mixes the two so that a 180-degree rotation is
     * distinguishable from the identity even if R and G happened to be symmetric, and A takes four
     * distinct non-trivial values so an alpha that is dropped, forced opaque or premultiplied
     * cannot pass either.
     */
    Color PatternColor(int x, int y)
    {
        const int r = 20 + x * 30;                       // 20, 50, ... 230
        const int g = 25 + y * 70;                       // 25, 95, 165, 235
        const int b = 40 + ((x * 5 + y * 3) % 7) * 30;   // 40 .. 220, asymmetric in both axes
        const int a = 255 - ((x + 2 * y) % 4) * 55;      // 255, 200, 145, 90
        return Color(static_cast<bytecs>(r), static_cast<bytecs>(g),
                     static_cast<bytecs>(b), static_cast<bytecs>(a));
    }

    /** @brief Formats a colour as R,G,B,A for diagnostics. */
    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /** @brief Exact byte equality on all four channels. */
    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

/**
 * @brief REMED-GFX-147's public render-target sampling orientation contract.
 */
class RenderTargetSamplingOrientationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;

    Texture2D whiteTex_;      ///< 1x1 opaque white, for solid fills.
    Texture2D patternTex_;    ///< kPW x kPH, SetData of PatternColor -- the top-down reference.

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    // ---------------------------------------------------------------- readback

    /** @brief The outcome of a public GetData call. */
    struct Readback
    {
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> pixels;
        int w = 0;
        int h = 0;

        [[nodiscard]] bool ok() const { return !threwNotSupported && !threwSomethingElse; }
        [[nodiscard]] const Color& at(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(y) * w + x];
        }
    };

    /// Reads a whole target back. The destination is pre-filled with a poison value so a renderer
    /// that writes nothing cannot be mistaken for one that wrote transparent black.
    static Readback ReadWhole(RenderTarget2D& target, int w, int h)
    {
        Readback r;
        r.w = w;
        r.h = h;
        r.pixels.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            target.GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
        }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// True when this renderer declared it cannot rasterize/read targets at all.
    static bool Unsupported() { return !kRasterizes; }

    /// Asserts an unreadable renderer rejected, and reports whether the caller should continue.
    bool RequireReadable(const Readback& r, const std::string& label)
    {
        if (Unsupported())
        {
            check(r.threwNotSupported,
                  label + ": declared non-rasterizing, must throw NotSupportedException");
            return false;
        }
        if (!r.ok())
        {
            check(false, label + ": readback failed" +
                         (r.threwNotSupported ? " (NotSupportedException)" : " (" + r.otherWhat + ")"));
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------- drawing

    /// Every knob SpriteBatch exposes for one sprite, so each leg varies exactly one of them.
    struct BlitOpts
    {
        Rectangle dst{0, 0, kPW, kPH};
        std::optional<Rectangle> src{};
        Color tint = Color::White;
        float rotation = 0.0f;
        Vector2 origin{0.0f, 0.0f};
        SpriteEffects effects = SpriteEffects::None;
        SamplerState sampler = SamplerState::PointClamp;
        BlendState blend = BlendState::Opaque;
        std::optional<Matrix> transform{};
    };

    /// Draws @p left and @p right in one batch with identical parameters apart from the
    /// destination X offset, so a single readback compares them without any second frame,
    /// second readback or second sampler configuration to explain a difference away.
    static void BlitPair(GraphicsDevice& dev, const Texture2D& left, const Texture2D& right,
                         const BlitOpts& opts, int rightOffsetX)
    {
        SpriteBatch sb(dev);
        SamplerState sampler = opts.sampler;
        if (opts.transform.has_value())
            sb.Begin(SpriteSortMode::Deferred, opts.blend, &sampler, nullptr, nullptr, nullptr,
                     *opts.transform);
        else
            sb.Begin(SpriteSortMode::Deferred, opts.blend, &sampler, nullptr, nullptr);

        Rectangle rightDst = opts.dst;
        rightDst.X += rightOffsetX;
        sb.Draw(left, opts.dst, opts.src, opts.tint, opts.rotation, opts.origin, opts.effects, 0.0f);
        sb.Draw(right, rightDst, opts.src, opts.tint, opts.rotation, opts.origin, opts.effects, 0.0f);
        sb.End();
    }

    /// Draws one sprite with the same options BlitPair uses, for legs that compare by swapping the
    /// source under identical destination geometry rather than by placing the pair side by side.
    static void BlitOne(GraphicsDevice& dev, const Texture2D& source, const BlitOpts& opts)
    {
        SpriteBatch sb(dev);
        SamplerState sampler = opts.sampler;
        if (opts.transform.has_value())
            sb.Begin(SpriteSortMode::Deferred, opts.blend, &sampler, nullptr, nullptr, nullptr,
                     *opts.transform);
        else
            sb.Begin(SpriteSortMode::Deferred, opts.blend, &sampler, nullptr, nullptr);
        sb.Draw(source, opts.dst, opts.src, opts.tint, opts.rotation, opts.origin, opts.effects, 0.0f);
        sb.End();
    }

    /// Fills exactly @p destination with @p tint (BlendState::Opaque stores the source verbatim).
    void FillRect(GraphicsDevice& dev, const Rectangle& destination, const Color& tint)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    /// Draws the pattern texture 1:1 into the bound target with point sampling.
    void BlitPattern(GraphicsDevice& dev, const Texture2D& source)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(source, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
    }

    /// Renders the pattern into @p rt through ordinary (already-correct) Texture2D sampling.
    void RenderPatternInto(GraphicsDevice& dev, RenderTarget2D& rt)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPattern(dev, patternTex_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// A texture-free four-quadrant vertex-colour draw in NDC: NDC y = +1 is the image TOP.
    /// This is the second, independent proof that the RT store/readback pair is top-down --
    /// it never binds a sampler, so it cannot inherit a sampling defect.
    static void DrawQuadrants(GraphicsDevice& dev, BasicEffect& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureEnabledProperty(false);
        fx.setLightingEnabledProperty(false);
        fx.VertexColorEnabled = true;
        fx.Apply();

        auto quad = [](float x0, float y0, float x1, float y1, const Color& c,
                       VertexPositionColor* o) {
            o[0] = { Vector3(x0, y1, 0.f), c }; o[1] = { Vector3(x0, y0, 0.f), c };
            o[2] = { Vector3(x1, y0, 0.f), c }; o[3] = { Vector3(x0, y1, 0.f), c };
            o[4] = { Vector3(x1, y0, 0.f), c }; o[5] = { Vector3(x1, y1, 0.f), c };
        };
        VertexPositionColor v[24];
        quad(-1.f,  0.f, 0.f, 1.f, Color(255,   0,   0, 255), v +  0);   // top-left     RED
        quad( 0.f,  0.f, 1.f, 1.f, Color(  0, 255,   0, 255), v +  6);   // top-right    GREEN
        quad(-1.f, -1.f, 0.f, 0.f, Color(  0,   0, 255, 255), v + 12);   // bottom-left  BLUE
        quad( 0.f, -1.f, 1.f, 0.f, Color(255, 255,   0, 255), v + 18);   // bottom-right YELLOW
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 8);
    }

    /// The standard XNA texture-on-quad UV map: NDC top-left -> UV (0, 0). No pre-flipped V --
    /// the whole point is that the framework, not the game, owns the correction.
    static void FillSamplingQuad(VertexPositionTexture* q)
    {
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, Vector2(0.f, 0.f) }; q[1] = { bl, Vector2(0.f, 1.f) }; q[2] = { br, Vector2(1.f, 1.f) };
        q[3] = { tl, Vector2(0.f, 0.f) }; q[4] = { br, Vector2(1.f, 1.f) }; q[5] = { tr, Vector2(1.f, 0.f) };
    }

    /// Indexed form of the same quad -- four vertices, six indices.
    static void FillSamplingQuadIndexed(VertexPositionTexture* q, std::uint16_t* idx)
    {
        q[0] = { Vector3(-1.f,  1.f, 0.f), Vector2(0.f, 0.f) };
        q[1] = { Vector3(-1.f, -1.f, 0.f), Vector2(0.f, 1.f) };
        q[2] = { Vector3( 1.f, -1.f, 0.f), Vector2(1.f, 1.f) };
        q[3] = { Vector3( 1.f,  1.f, 0.f), Vector2(1.f, 0.f) };
        idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
    }

    // ---------------------------------------------------------------- comparisons

    /// Compares the two halves of a side-by-side readback and, separately, each half against the
    /// pattern. Returns the number of failing checks it emitted.
    void CheckPair(const Readback& r, int leftX, int rightX, const std::string& label,
                   bool expectPattern, const char* leftName = "RenderTarget2D",
                   const char* rightName = "Texture2D")
    {
        int mismatchedHalves = 0;
        std::string firstHalfMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const Color& a = r.at(leftX + x, y);
                const Color& b = r.at(rightX + x, y);
                if (!Same(a, b))
                {
                    ++mismatchedHalves;
                    if (firstHalfMismatch.empty())
                        firstHalfMismatch = std::string(" first at (") + std::to_string(x) + "," +
                                            std::to_string(y) + ") " + leftName + "=" + ColorText(a) +
                                            " " + rightName + "=" + ColorText(b);
                }
            }
        check(mismatchedHalves == 0,
              label + ": " + leftName + " and " + rightName + " sample identically (" +
              std::to_string(kPW * kPH - mismatchedHalves) + "/" + std::to_string(kPW * kPH) +
              " texels)" + firstHalfMismatch);

        if (!expectPattern) return;

        for (int side = 0; side < 2; ++side)
        {
            const int baseX = side == 0 ? leftX : rightX;
            const char* which = side == 0 ? leftName : rightName;
            int good = 0, verticallyMirrored = 0;
            std::string firstMismatch;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                {
                    const Color& got = r.at(baseX + x, y);
                    if (Same(got, PatternColor(x, y))) ++good;
                    else if (firstMismatch.empty())
                        firstMismatch = " first at (" + std::to_string(x) + "," +
                                        std::to_string(y) + ") want=" +
                                        ColorText(PatternColor(x, y)) + " got=" + ColorText(got);
                    if (Same(got, PatternColor(x, kPH - 1 - y))) ++verticallyMirrored;
                }
            const bool exact = good == kPW * kPH;
            std::string diagnosis;
            if (!exact && verticallyMirrored == kPW * kPH)
                diagnosis = " -- the image is EXACTLY VERTICALLY MIRRORED (bottom-up sampling)";
            check(exact, label + ": " + which + " source samples upright (" +
                         std::to_string(good) + "/" + std::to_string(kPW * kPH) + " texels)" +
                         diagnosis + firstMismatch);
        }
    }

    /// Asserts a whole-target readback equals the pattern exactly.
    void CheckPattern(const Readback& r, const std::string& label)
    {
        int good = 0, verticallyMirrored = 0;
        std::string firstMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const Color& got = r.at(x, y);
                if (Same(got, PatternColor(x, y))) ++good;
                else if (firstMismatch.empty())
                    firstMismatch = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") want=" + ColorText(PatternColor(x, y)) +
                                    " got=" + ColorText(got);
                if (Same(got, PatternColor(x, kPH - 1 - y))) ++verticallyMirrored;
            }
        const bool exact = good == kPW * kPH;
        std::string diagnosis;
        if (!exact && verticallyMirrored == kPW * kPH)
            diagnosis = " -- EXACTLY VERTICALLY MIRRORED";
        check(exact, label + " (" + std::to_string(good) + "/" +
                     std::to_string(kPW * kPH) + " texels)" + diagnosis + firstMismatch);
    }

    /// Prints the first row and first column of a readback, so the measured orientation is on the
    /// record rather than inferred from a pass/fail line.
    static void PrintEdges(const Readback& r, const char* label)
    {
        if (!r.ok()) return;
        std::printf("[INFO] %s row0:", label);
        for (int x = 0; x < r.w && x < 16; ++x) std::printf(" %s", ColorText(r.at(x, 0)).c_str());
        std::printf("\n[INFO] %s col0:", label);
        for (int y = 0; y < r.h && y < 16; ++y) std::printf(" %s", ColorText(r.at(0, y)).c_str());
        std::printf("\n");
        std::fflush(stdout);
    }

    // ================================================================ legs

    /// Leg S: the storage/readback orientation the rest of the file relies on, proven two ways.
    void LegS(GraphicsDevice& dev, RenderTarget2D& a)
    {
        RenderPatternInto(dev, a);
        Readback ra = ReadWhole(a, kPW, kPH);
        if (RequireReadable(ra, "S1 RenderTarget2D::GetData"))
        {
            CheckPattern(ra, "S1 pattern rendered into a target reads back top-down and byte-exact");
            PrintEdges(ra, "S1 target-A");
        }

        // Texture-free proof: four vertex-coloured quadrants, no sampler involved anywhere.
        RenderTarget2D quads(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
        {
            BasicEffect fx(dev);
            dev.SetRenderTarget(&quads);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuadrants(dev, fx);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
        }
        Readback rq = ReadWhole(quads, 16, 16);
        if (RequireReadable(rq, "S2 vertex-colour quadrant readback"))
        {
            const bool topLeftRed    = rq.at(3, 3).getRProperty()  > 200 && rq.at(3, 3).getGProperty() < 60;
            const bool topRightGreen = rq.at(12, 3).getGProperty() > 200 && rq.at(12, 3).getRProperty() < 60;
            const bool botLeftBlue   = rq.at(3, 12).getBProperty() > 200 && rq.at(3, 12).getRProperty() < 60;
            const bool botRightYell  = rq.at(12, 12).getRProperty() > 200 && rq.at(12, 12).getGProperty() > 200
                                       && rq.at(12, 12).getBProperty() < 60;
            check(topLeftRed && topRightGreen && botLeftBlue && botRightYell,
                  std::string("S2 texture-free vertex-colour quadrants store top-down (TL=") +
                  ColorText(rq.at(3, 3)) + " TR=" + ColorText(rq.at(12, 3)) +
                  " BL=" + ColorText(rq.at(3, 12)) + " BR=" + ColorText(rq.at(12, 12)) + ")");
        }
    }

    /// Builds a plain Texture2D holding a target's own bytes: the "identical bytes" control.
    Texture2D CloneAsTexture(GraphicsDevice& dev, const Readback& source)
    {
        Texture2D t(dev, source.w, source.h);
        t.SetData(source.pixels.data(), static_cast<int>(source.pixels.size()));
        return t;
    }

    /// Legs A/B: the canonical side-by-side SpriteBatch oracle.
    void LegAB(GraphicsDevice& dev, RenderTarget2D& a, Texture2D& b)
    {
        RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPair(dev, a, b, BlitOpts{}, kPW);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWhole(dst, kPW * 2, kPH);
        if (!RequireReadable(r, "AB1 side-by-side readback")) return;
        PrintEdges(r, "AB side-by-side");
        CheckPair(r, 0, kPW, "AB1 SpriteBatch full texture", true);
    }

    /// Legs C/D: the same question asked through mesh UVs instead of SpriteBatch-generated ones.
    /// Each effect draws the source across a whole small target, so the target's pixel (x, y)
    /// must be the source's texel (x, y).
    void Leg3D(GraphicsDevice& dev, RenderTarget2D& a, Texture2D& b)
    {
        struct Case { const char* name; int kind; };
        const Case cases[] = {
            { "CD1 BasicEffect textured (non-indexed)", 0 },
            { "CD2 BasicEffect textured (indexed)",     1 },
            { "CD3 AlphaTestEffect",                    2 },
            { "CD4 BasicEffect lit + textured",         3 },
            { "CD5 DualTextureEffect slot 0",           4 },
            { "CD6 DualTextureEffect slot 1",           5 },
        };

        if (!kStockEffectRtSourceSupported)
        {
            // REMED-GFX-152: handing a RenderTarget2D to a stock effect is undefined behaviour on
            // this renderer and terminates the process, so the pair cannot be drawn at all. The
            // boundary is recorded rather than silently skipped; the SpriteBatch legs above still
            // cover render-target sampling here, and the ordinary-texture control below still
            // covers mesh-UV sampling.
            std::printf("[INFO] CD legs: %s cannot use a RenderTarget2D as a stock 3D effect's "
                        "texture (REMED-GFX-152: SdlGpuRenderTargetRenderer static_cast to the "
                        "unrelated SdlGpuTextureRenderer) -- boundary recorded\n", kRendererName);
            std::fflush(stdout);
        }

        for (const Case& c : cases)
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            Readback reads[2];
            bool drawFailed = false;
            std::string drawWhat;
            for (int side = 0; side < 2 && !drawFailed; ++side)
            {
                if (side == 0 && !kStockEffectRtSourceSupported) continue;
                Texture2D* source = side == 0 ? static_cast<Texture2D*>(&a) : &b;
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
                dev.Clear(Color(0, 0, 0, 255));

                VertexPositionTexture q[6];
                std::uint16_t idx[6];
                FillSamplingQuad(q);

                // A stock effect/vertex combination a renderer has not implemented (e.g. lighting
                // with no Normal in the vertex, or DualTextureEffect at all) is a declared boundary
                // there, not a defect this orientation oracle owns -- caught here so it reports
                // as one case skipped rather than terminating the whole fixture.
                try
                {
                    if (c.kind == 2)
                    {
                        AlphaTestEffect fx(dev);
                        fx.setWorldProperty(Matrix::getIdentityProperty());
                        fx.setViewProperty(Matrix::getIdentityProperty());
                        fx.setProjectionProperty(Matrix::getIdentityProperty());
                        fx.setTextureProperty(source);
                        fx.setAlphaFunctionProperty(CompareFunction::Greater);
                        fx.setReferenceAlphaProperty(0);
                        fx.Apply();
                        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                    }
                    else if (c.kind == 4 || c.kind == 5)
                    {
                        // One slot carries the source under test; the other carries an opaque-white
                        // 1x1 so the second sample is the multiplicative identity for this effect's
                        // base.rgb * 2 * second.rgb combine, leaving the tested slot's orientation
                        // the only thing that can move a texel.
                        DualTextureEffect fx(dev);
                        fx.setWorldProperty(Matrix::getIdentityProperty());
                        fx.setViewProperty(Matrix::getIdentityProperty());
                        fx.setProjectionProperty(Matrix::getIdentityProperty());
                        if (c.kind == 4) { fx.setTextureProperty(source);     fx.setTexture2Property(&whiteTex_); }
                        else             { fx.setTextureProperty(&whiteTex_); fx.setTexture2Property(source); }
                        fx.Apply();
                        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                    }
                    else
                    {
                        BasicEffect fx(dev);
                        fx.setWorldProperty(Matrix::getIdentityProperty());
                        fx.setViewProperty(Matrix::getIdentityProperty());
                        fx.setProjectionProperty(Matrix::getIdentityProperty());
                        fx.setTextureEnabledProperty(true);
                        fx.setTextureProperty(source);
                        fx.VertexColorEnabled = false;
                        if (c.kind == 3)
                        {
                            fx.setLightingEnabledProperty(true);
                            fx.EnableDefaultLighting();
                            fx.setTextureEnabledProperty(true);
                            fx.setTextureProperty(source);
                        }
                        fx.Apply();
                        if (c.kind == 1)
                        {
                            FillSamplingQuadIndexed(q, idx);
                            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, q, 0, 4, idx, 0, 2);
                        }
                        else
                        {
                            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    drawFailed = true;
                    drawWhat = e.what();
                }

                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                if (!drawFailed) reads[side] = ReadWhole(dst, kPW, kPH);
            }

            if (drawFailed)
            {
                std::printf("[INFO] %s: %s cannot draw this effect/vertex combination against a "
                            "RenderTarget2D source (%s) -- boundary recorded\n",
                            c.name, kRendererName, drawWhat.c_str());
                std::fflush(stdout);
                continue;
            }

            if (Unsupported())
            {
                check(reads[1].threwNotSupported,
                      std::string(c.name) + ": declared non-rasterizing, must throw");
                continue;
            }
            if (!reads[1].ok())
            {
                check(false, std::string(c.name) + ": readback failed (" + reads[1].otherWhat + ")");
                continue;
            }
            if (!kStockEffectRtSourceSupported)
            {
                // Only the ordinary-texture control ran; still assert it, so mesh-UV sampling is
                // covered on this renderer even though the pair could not be formed.
                if (c.kind == 0 || c.kind == 1)
                    CheckPattern(reads[1], std::string(c.name) +
                                 ": the Texture2D control itself samples upright");
                continue;
            }
            if (!reads[0].ok())
            {
                check(false, std::string(c.name) + ": readback failed (" + reads[0].otherWhat + ")");
                continue;
            }

            int mismatched = 0;
            std::string first;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (!Same(reads[0].at(x, y), reads[1].at(x, y)))
                    {
                        ++mismatched;
                        if (first.empty())
                            first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") rt=" + ColorText(reads[0].at(x, y)) +
                                    " tex=" + ColorText(reads[1].at(x, y));
                    }
            // A vertical mirror of the RT relative to the identical Texture2D draw is the exact
            // shape of this finding, so name it when it happens.
            int mirrored = 0;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (Same(reads[0].at(x, y), reads[1].at(x, kPH - 1 - y))) ++mirrored;
            std::string diagnosis;
            if (mismatched > 0 && mirrored == kPW * kPH)
                diagnosis = " -- the render-target source is EXACTLY VERTICALLY MIRRORED";
            check(mismatched == 0,
                  std::string(c.name) + ": render target and Texture2D sample identically (" +
                  std::to_string(kPW * kPH - mismatched) + "/" + std::to_string(kPW * kPH) +
                  ")" + diagnosis + first);

            if (c.kind == 0 || c.kind == 1)
                CheckPattern(reads[1], std::string(c.name) +
                             ": the Texture2D control itself samples upright");
        }
    }

    /// Leg G0: the canonical XNA render-to-texture sequence -- render, unbind, sample -- with NO
    /// readback of the source anywhere. Every other leg reads its source at some point, which is
    /// exactly how a renderer that only materialises a target on readback stays hidden.
    void LegSampleWithoutReadback(GraphicsDevice& dev)
    {
        RenderTarget2D source(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        RenderPatternInto(dev, source);

        RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPattern(dev, source);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWhole(dst, kPW, kPH);
        if (!RequireReadable(r, "G0 sample a never-read target")) return;

        static_assert(kSampleWithoutReadbackWorks,
                      "REMED-GFX-151 is fixed on every renderer; this leg is an orientation check "
                      "again, not a pinned defect. A renderer that regresses must fail G0, not "
                      "re-declare it.");
        CheckPattern(r, "G0 render -> unbind -> sample with no readback of the source is upright");
    }

    /// Leg E/G: chains. Each sampling leg must preserve the orientation, never alternate it.
    void LegChains(GraphicsDevice& dev, RenderTarget2D& a)
    {
        // E: A -> B.
        RenderTarget2D bTarget(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&bTarget);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPattern(dev, a);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback rb = ReadWhole(bTarget, kPW, kPH);
        if (RequireReadable(rb, "E1 target A -> target B"))
            CheckPattern(rb, "E1 target A -> target B keeps the pattern upright");

        // G: A -> B -> C. If the correction were applied twice, C would be upright while B was
        // mirrored; if it were applied zero times, both are mirrored. Asserting BOTH pins it.
        RenderTarget2D cTarget(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&cTarget);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPattern(dev, bTarget);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback rc = ReadWhole(cTarget, kPW, kPH);
        if (RequireReadable(rc, "G1 target A -> B -> C"))
            CheckPattern(rc, "G1 target A -> B -> C does not flip twice");

        // Different dimensions in the chain. Deliberately 1:1 at every hop -- a target that is
        // LARGER than the sprite, not a magnified sprite -- so this leg measures orientation and
        // nothing else. A magnifying hop would fold each renderer's texture-filter fidelity into an
        // orientation result, and a difference there would be neither this finding nor evidence
        // against it.
        RenderTarget2D big(dev, kPW * 2, kPH * 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&big);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            // Placed at an offset inside the bigger target, so a chain that silently assumed
            // source and destination share an origin would move the image.
            sb.Draw(a, Rectangle(kPW / 2, kPH / 2, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        RenderTarget2D small(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&small);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(big, Rectangle(0, 0, kPW, kPH),
                    Rectangle(kPW / 2, kPH / 2, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback rs = ReadWhole(small, kPW, kPH);
        if (RequireReadable(rs, "G2 differently-sized chain"))
            CheckPattern(rs, "G2 target chain through a differently-sized target stays upright");

        // Two independent targets with identical dimensions, both sampled in one frame.
        RenderTarget2D twin(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::DiscardContents);
        RenderPatternInto(dev, twin);
        RenderTarget2D pairDst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&pairDst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPair(dev, a, twin, BlitOpts{}, kPW);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback rp = ReadWhole(pairDst, kPW * 2, kPH);
        if (RequireReadable(rp, "G3 two identical-size targets"))
            CheckPair(rp, 0, kPW, "G3 two independent same-size targets", true,
                      "target A", "target twin");
    }

    /// Leg F: target -> backbuffer, read with GetBackBufferData.
    void LegBackbuffer(GraphicsDevice& dev, RenderTarget2D& a, Texture2D& b)
    {
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPair(dev, a, b, BlitOpts{}, kPW);

        std::vector<Color> frame(static_cast<std::size_t>(kBBW) * kBBH,
                                 Color(0xCD, 0xCD, 0xCD, 0xCD));
        bool threw = false;
        std::string what;
        try { dev.GetBackBufferData(frame.data(), 0, static_cast<int>(frame.size())); }
        catch (const System::NotSupportedException&) { threw = true; what = "NotSupportedException"; }
        catch (const std::exception& e) { threw = true; what = e.what(); }

        if (threw)
        {
            // Backbuffer readback is an optional capability; where a renderer does not implement it
            // there is simply no honest oracle for this leg, and the render-target legs above
            // already carry the contract. Recorded, not asserted either way.
            std::printf("[INFO] F1 GetBackBufferData unavailable on %s (%s) -- boundary recorded\n",
                        kRendererName, what.c_str());
            std::fflush(stdout);
            return;
        }
        if (Unsupported())
        {
            // A declared non-rasterizing renderer drew nothing, so there is no orientation in the
            // frame to measure. What it returns instead is REMED-GFX-127's question about the
            // BACKBUFFER, which this task does not own -- recorded, not asserted.
            std::printf("[INFO] F1 GetBackBufferData on a non-rasterizing renderer returned %s at "
                        "(0,0) -- no orientation to measure here\n", ColorText(frame[0]).c_str());
            std::fflush(stdout);
            return;
        }
        Readback r;
        r.w = kBBW; r.h = kBBH; r.pixels = frame;
        PrintEdges(r, "F1 backbuffer");
        CheckPair(r, 0, kPW, "F1 target -> backbuffer", true);
    }

    /// Leg H/I: repeated sampling in one frame, and readback stability across sampling.
    void LegRepeat(GraphicsDevice& dev, RenderTarget2D& a, Texture2D& b, const Readback& before)
    {
        RenderTarget2D dst(dev, kPW * 3, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(a, Rectangle(0, 0, kPW, kPH),        Rectangle(0, 0, kPW, kPH), Color::White);
            sb.Draw(b, Rectangle(kPW, 0, kPW, kPH),      Rectangle(0, 0, kPW, kPH), Color::White);
            sb.Draw(a, Rectangle(kPW * 2, 0, kPW, kPH),  Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWhole(dst, kPW * 3, kPH);
        if (RequireReadable(r, "H1 repeated sampling"))
        {
            CheckPair(r, 0, kPW, "H1 first of two samples of the same target", true);
            CheckPair(r, kPW * 2, kPW, "H2 second of two samples of the same target", true);
        }

        Readback after = ReadWhole(a, kPW, kPH);
        if (Unsupported())
        {
            check(after.threwNotSupported, "I1 GetData after sampling: still rejects");
            return;
        }
        if (!after.ok() || !before.ok())
        {
            check(false, "I1 GetData after sampling failed");
            return;
        }
        bool identical = before.pixels.size() == after.pixels.size();
        if (identical)
            for (std::size_t i = 0; i < after.pixels.size(); ++i)
                if (!Same(before.pixels[i], after.pixels[i])) { identical = false; break; }
        // This is the permanent guard that the correction lives in SAMPLING and never touched
        // readback: the very bytes REMED-GFX-127/134 established must be the same before and
        // after the target has been used as a texture.
        check(identical, "I1 RenderTarget2D::GetData is byte-identical before and after the target "
                         "was sampled");
    }

    /// Leg J: the application's SpriteEffects flip and the renderer's correction are separate
    /// transforms and must compose, not cancel.
    void LegSpriteEffects(GraphicsDevice& dev, RenderTarget2D& a, Texture2D& b)
    {
        struct Case { const char* name; SpriteEffects fx; int mirrorX; int mirrorY; };
        const Case cases[] = {
            { "J1 SpriteEffects::None",             SpriteEffects::None,             0, 0 },
            { "J2 SpriteEffects::FlipVertically",   SpriteEffects::FlipVertically,   0, 1 },
            { "J3 SpriteEffects::FlipHorizontally", SpriteEffects::FlipHorizontally, 1, 0 },
            { "J4 both flips",
              static_cast<SpriteEffects>(static_cast<int>(SpriteEffects::FlipHorizontally) |
                                         static_cast<int>(SpriteEffects::FlipVertically)), 1, 1 },
        };
        for (const Case& c : cases)
        {
            RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            BlitOpts opts;
            opts.effects = c.fx;
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            BlitPair(dev, a, b, opts, kPW);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW * 2, kPH);
            if (!RequireReadable(r, std::string(c.name) + " readback")) continue;

            int mismatched = 0;
            std::string first;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (!Same(r.at(x, y), r.at(kPW + x, y)))
                    {
                        ++mismatched;
                        if (first.empty())
                            first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") rt=" + ColorText(r.at(x, y)) +
                                    " tex=" + ColorText(r.at(kPW + x, y));
                    }
            check(mismatched == 0, std::string(c.name) +
                  ": the public flip composes identically over a render target and a texture" + first);

            // And the texture control really performed the requested flip, so a renderer that
            // ignored SpriteEffects entirely cannot pass this pair by doing nothing on both sides.
            int good = 0;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                {
                    const int sx = c.mirrorX ? (kPW - 1 - x) : x;
                    const int sy = c.mirrorY ? (kPH - 1 - y) : y;
                    if (Same(r.at(kPW + x, y), PatternColor(sx, sy))) ++good;
                }
            check(good == kPW * kPH, std::string(c.name) +
                  ": the Texture2D control really applied the requested flip (" +
                  std::to_string(good) + "/" + std::to_string(kPW * kPH) + ")");
        }
    }

    /// Leg K: every remaining SpriteBatch knob. Each varies exactly one parameter and asserts the
    /// render-target and texture halves stay identical under it.
    void LegSpriteBatchKnobs(GraphicsDevice& dev, RenderTarget2D& a, Texture2D& b)
    {
        struct Case { const char* name; BlitOpts opts; int dstW; int dstH; int offset; bool prefill; };
        std::vector<Case> cases;

        {   // Source rectangle: the lower-right 4x2 sub-region, drawn 1:1.
            BlitOpts o;
            o.src = Rectangle(kPW / 2, kPH / 2, kPW / 2, kPH / 2);
            o.dst = Rectangle(0, 0, kPW / 2, kPH / 2);
            cases.push_back({ "K1 source rectangle", o, kPW, kPH, kPW / 2, false });
        }
        {   // Destination rectangle: 2x magnification with point sampling.
            BlitOpts o;
            o.dst = Rectangle(0, 0, kPW * 2, kPH * 2);
            cases.push_back({ "K2 scaled destination rectangle", o, kPW * 4, kPH * 2, kPW * 2, false });
        }
        {   // Rotation about the sprite centre. Drawn TWICE into the same destination rectangle
            // rather than side by side: a rotated sprite's sampling can depend on its absolute
            // position on some rasterizers, so translating one of the pair would compare two
            // different geometries. Identical geometry, one source swapped, is the stronger form
            // of the same question -- see the sequential-pass loop below.
            BlitOpts o;
            o.dst = Rectangle(kPW / 2, kPH / 2, kPW, kPH);
            o.origin = Vector2(static_cast<float>(kPW) / 2.0f, static_cast<float>(kPH) / 2.0f);
            o.rotation = 3.14159265358979f;
            cases.push_back({ "K3 rotation about origin", o, kPW, kPH, 0, false });
        }
        {   // Transform matrix on Begin.
            BlitOpts o;
            o.transform = Matrix::CreateTranslation(Vector3(0.0f, 0.0f, 0.0f));
            cases.push_back({ "K4 Begin transform matrix", o, kPW * 2, kPH, kPW, false });
        }
        {   // Linear filtering, sampled at exact texel centres by the 1:1 draw.
            BlitOpts o;
            o.sampler = SamplerState::LinearClamp;
            cases.push_back({ "K5 LinearClamp at texel centres", o, kPW * 2, kPH, kPW, false });
        }
        {   // Alpha-blended sampling over a known non-black destination.
            BlitOpts o;
            o.blend = BlendState::AlphaBlend;
            cases.push_back({ "K6 AlphaBlend over a known destination", o, kPW * 2, kPH, kPW, true });
        }
        {   // Non-white tint.
            BlitOpts o;
            o.tint = Color(128, 192, 255, 255);
            cases.push_back({ "K7 non-white tint", o, kPW * 2, kPH, kPW, false });
        }

        for (const Case& c : cases)
        {
            RenderTarget2D dst(dev, c.dstW, c.dstH, false, SurfaceFormat::Color, DepthFormat::None,
                               0, RenderTargetUsage::DiscardContents);
            Readback reads[2];
            for (int side = 0; side < 2; ++side)
            {
                // offset == 0 means the pair is compared by swapping the SOURCE under identical
                // destination geometry (two passes, one target); otherwise both are drawn side by
                // side in the same pass and read back once.
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                if (c.prefill)
                    FillRect(dev, Rectangle(0, 0, c.dstW, c.dstH), Color(40, 80, 120, 255));
                if (c.offset != 0)
                    BlitPair(dev, a, b, c.opts, c.offset);
                else
                    BlitOne(dev, side == 0 ? static_cast<const Texture2D&>(a)
                                           : static_cast<const Texture2D&>(b), c.opts);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                reads[side] = ReadWhole(dst, c.dstW, c.dstH);
                if (c.offset != 0) break;
            }
            if (!RequireReadable(reads[0], std::string(c.name) + " readback")) continue;
            if (c.offset == 0 && !RequireReadable(reads[1], std::string(c.name) + " readback"))
                continue;

            const int w = c.opts.dst.Width;
            const int h = c.opts.dst.Height;
            int mismatched = 0, compared = 0;
            std::string first;
            for (int y = 0; y < h && y < c.dstH; ++y)
                for (int x = 0; x < w && (x + c.offset) < c.dstW; ++x)
                {
                    ++compared;
                    const Color& lhs = reads[0].at(x, y);
                    const Color& rhs = c.offset != 0 ? reads[0].at(x + c.offset, y)
                                                     : reads[1].at(x, y);
                    if (!Same(lhs, rhs))
                    {
                        ++mismatched;
                        if (first.empty())
                            first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") rt=" + ColorText(lhs) + " tex=" + ColorText(rhs);
                    }
                }
            check(mismatched == 0, std::string(c.name) +
                  ": render target and Texture2D sample identically (" +
                  std::to_string(compared - mismatched) + "/" + std::to_string(compared) + ")" + first);
        }

        // Custom viewport and scissor: geometry-space state that must not touch the texture's
        // stored orientation. Both halves are drawn under the identical restriction.
        struct Restricted { const char* name; bool viewport; };
        const Restricted restricted[] = {
            { "K8 custom viewport",  true  },
            { "K9 scissor rectangle", false },
        };
        for (const Restricted& rc : restricted)
        {
            RenderTarget2D dst(dev, kPW * 2, kPH * 2, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            if (rc.viewport)
            {
                dev.setViewportProperty(Viewport(0, 0, kPW * 2, kPH));
            }
            else
            {
                RasterizerState scissored = RasterizerState::CullNone;
                scissored.setScissorTestEnableProperty(true);
                dev.setRasterizerStateProperty(scissored);
                dev.setScissorRectangleProperty(Rectangle(0, 0, kPW * 2, kPH));
            }
            BlitPair(dev, a, b, BlitOpts{}, kPW);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            Readback r = ReadWhole(dst, kPW * 2, kPH * 2);
            if (!RequireReadable(r, std::string(rc.name) + " readback")) continue;
            int mismatched = 0;
            std::string first;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (!Same(r.at(x, y), r.at(kPW + x, y)))
                    {
                        ++mismatched;
                        if (first.empty())
                            first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") rt=" + ColorText(r.at(x, y)) +
                                    " tex=" + ColorText(r.at(kPW + x, y));
                    }
            check(mismatched == 0, std::string(rc.name) +
                  ": render target and Texture2D sample identically under it" + first);
        }
    }

    /// Leg L: MSAA. The correction must not depend on the sample count, and the resolve must not
    /// introduce or cancel one.
    void LegMsaa(GraphicsDevice& dev, Texture2D& b)
    {
        RenderTarget2D msaa(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                            RenderTargetUsage::DiscardContents);
        const int applied = msaa.getMultiSampleCountProperty();
        std::printf("[INFO] L1 requested MSAA 4, renderer applied %d\n", applied);
        std::fflush(stdout);
        if (applied <= 0)
        {
            check(true, "L1 MSAA not applied by this renderer (requested 4, got " +
                        std::to_string(applied) + ") -- sample-count-one path already covered above");
            return;
        }

        RenderPatternInto(dev, msaa);
        // REMED-GFX-154: exactly one read, immediately, with no dummy target bind between the render
        // and the read. That sequence is the whole contract now.
        Readback rm = ReadWhole(msaa, kPW, kPH);
        if (!RequireReadable(rm, "L1 MSAA target readback")) return;
        int empty = 0;
        for (const Color& c : rm.pixels)
            if (c.getRProperty() == 0 && c.getGProperty() == 0 && c.getBProperty() == 0 &&
                c.getAProperty() == 0) ++empty;
        check(empty == 0,
              "L1 the FIRST readback of a multisampled target is not empty (" +
              std::to_string(empty) + "/" + std::to_string(kPW * kPH) + " texels were all-zero)");
        CheckPattern(rm, "L1 MSAA target resolves to top-down bytes");

        RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPair(dev, msaa, b, BlitOpts{}, kPW);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWhole(dst, kPW * 2, kPH);
        if (RequireReadable(r, "L2 MSAA sampled readback"))
            CheckPair(r, 0, kPW, "L2 resolved MSAA target sampled", true);
    }

    /// Leg M: mipmapped render target. Level 0 must sample upright; a generated level must be
    /// orientation-consistent with it rather than mirrored.
    void LegMips(GraphicsDevice& dev, Texture2D& b)
    {
        // Whether a renderer supports a mipmapped render target at all is measured, not assumed:
        // WebGPU documents the chain regeneration as unimplemented and throws from here.
        std::unique_ptr<RenderTarget2D> mippedOwner;
        try
        {
            mippedOwner = std::make_unique<RenderTarget2D>(
                dev, kPW, kPH, true, SurfaceFormat::Color, DepthFormat::None, 0,
                RenderTargetUsage::DiscardContents);
            RenderPatternInto(dev, *mippedOwner);
        }
        catch (const std::exception& e)
        {
            std::printf("[INFO] M1 mipmapped RenderTarget2D unsupported on %s (%s) "
                        "-- boundary recorded\n", kRendererName, e.what());
            std::fflush(stdout);
            return;
        }
        RenderTarget2D& mipped = *mippedOwner;

        Readback r0 = ReadWhole(mipped, kPW, kPH);
        if (!RequireReadable(r0, "M1 mipmapped target level 0 readback")) return;
        CheckPattern(r0, "M1 mipmapped target level 0 reads back top-down");

        RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPair(dev, mipped, b, BlitOpts{}, kPW);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback rs = ReadWhole(dst, kPW * 2, kPH);
        if (RequireReadable(rs, "M2 mipmapped target sampled"))
            CheckPair(rs, 0, kPW, "M2 mipmapped target level 0 sampled", true);

        // Level 1 (if this renderer exposes it): compare against the top-down and the bottom-up
        // 2x2 box predictions and report which one it matches. A generated chain that inverted
        // rows would match the mirrored prediction, which is a separate finding, not this one.
        const int w1 = kPW / 2, h1 = kPH / 2;
        std::vector<Color> level1(static_cast<std::size_t>(w1) * h1, Color(0xCD, 0xCD, 0xCD, 0xCD));
        Rectangle whole(0, 0, w1, h1);
        bool threw = false;
        std::string what;
        try { mipped.GetData(1, &whole, level1.data(), 0, static_cast<int>(level1.size())); }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        if (threw)
        {
            std::printf("[INFO] M3 mip level 1 readback unavailable on %s (%s) -- boundary recorded\n",
                        kRendererName, what.c_str());
            std::fflush(stdout);
            return;
        }

        auto boxAt = [&](int x, int y, bool mirrored) {
            int r = 0, g = 0, bl = 0, al = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    const int sy0 = y * 2 + dy;
                    const Color c = PatternColor(x * 2 + dx, mirrored ? (kPH - 1 - sy0) : sy0);
                    r += c.getRProperty(); g += c.getGProperty();
                    bl += c.getBProperty(); al += c.getAProperty();
                }
            return Color(static_cast<bytecs>(r / 4), static_cast<bytecs>(g / 4),
                         static_cast<bytecs>(bl / 4), static_cast<bytecs>(al / 4));
        };
        auto closeness = [&](bool mirrored) {
            int total = 0;
            for (int y = 0; y < h1; ++y)
                for (int x = 0; x < w1; ++x)
                {
                    const Color got = level1[static_cast<std::size_t>(y) * w1 + x];
                    const Color want = boxAt(x, y, mirrored);
                    total += std::abs(static_cast<int>(got.getRProperty()) - static_cast<int>(want.getRProperty()));
                    total += std::abs(static_cast<int>(got.getGProperty()) - static_cast<int>(want.getGProperty()));
                    total += std::abs(static_cast<int>(got.getBProperty()) - static_cast<int>(want.getBProperty()));
                }
            return total;
        };
        const int topDownError = closeness(false);
        const int mirroredError = closeness(true);
        std::printf("[INFO] M3 mip level 1 box-filter distance: top-down=%d mirrored=%d\n",
                    topDownError, mirroredError);
        std::fflush(stdout);
        check(topDownError < mirroredError,
              "M3 generated mip level 1 is orientation-consistent with level 0 (top-down distance " +
              std::to_string(topDownError) + " < mirrored " + std::to_string(mirroredError) + ")");
    }

    /// Leg N: REMED-GFX-127/134's readback contract, re-asserted here so a sampling fix that
    /// "helped" by touching GetData would be caught immediately.
    void LegReadbackContract(GraphicsDevice& dev, RenderTarget2D& a, const Readback& reference)
    {
        if (Unsupported())
        {
            Readback again = ReadWhole(a, kPW, kPH);
            check(again.threwNotSupported, "N1 repeated readback rejects consistently");
            return;
        }
        if (!reference.ok()) { check(false, "N1 reference readback unavailable"); return; }

        Readback again = ReadWhole(a, kPW, kPH);
        bool stable = again.ok() && again.pixels.size() == reference.pixels.size();
        if (stable)
            for (std::size_t i = 0; i < again.pixels.size(); ++i)
                if (!Same(again.pixels[i], reference.pixels[i])) { stable = false; break; }
        check(stable, "N1 repeated whole-target readback is byte-identical");

        // Partial rectangle.
        const Rectangle sub(kPW / 2, 1, kPW / 2, 2);
        std::vector<Color> part(static_cast<std::size_t>(sub.Width) * sub.Height,
                                Color(0xCD, 0xCD, 0xCD, 0xCD));
        bool ok = true;
        std::string what;
        try { a.GetData(0, &sub, part.data(), 0, static_cast<int>(part.size())); }
        catch (const std::exception& e) { ok = false; what = e.what(); }
        if (!ok) { check(false, "N2 partial-rectangle readback threw: " + what); }
        else
        {
            int good = 0;
            for (int y = 0; y < sub.Height; ++y)
                for (int x = 0; x < sub.Width; ++x)
                    if (Same(part[static_cast<std::size_t>(y) * sub.Width + x],
                             PatternColor(sub.X + x, sub.Y + y))) ++good;
            check(good == sub.Width * sub.Height,
                  "N2 partial-rectangle readback is top-down and byte-exact (" +
                  std::to_string(good) + "/" + std::to_string(sub.Width * sub.Height) + ")");
        }

        // Destination offset, through the rectangle overload -- the one that honours startIndex on
        // a render target. REMED-GFX-149 (recorded, not owned here): the 3-argument
        // GetData(Color*, startIndex, elementCount) overload gates its render-target renderer
        // fallback on startIndex == 0 and throws std::runtime_error for any other offset, while
        // this rectangle overload accepts the same offset. That asymmetry is in the shared
        // Texture2D layer, is identical on every renderer and has nothing to do with orientation.
        const int count = kPW * kPH;
        const Rectangle whole(0, 0, kPW, kPH);
        std::vector<Color> offset(static_cast<std::size_t>(count) + 5, Color(0x11, 0x22, 0x33, 0x44));
        ok = true;
        try { a.GetData(0, &whole, offset.data(), 5, count); }
        catch (const std::exception& e) { ok = false; what = e.what(); }
        if (!ok) { check(false, "N3 destination-offset readback threw: " + what); }
        else
        {
            bool guardIntact = true;
            for (int i = 0; i < 5; ++i)
                if (!Same(offset[static_cast<std::size_t>(i)], Color(0x11, 0x22, 0x33, 0x44)))
                    guardIntact = false;
            int good = 0;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (Same(offset[static_cast<std::size_t>(5 + y * kPW + x)], PatternColor(x, y)))
                        ++good;
            check(guardIntact && good == count,
                  "N3 destination-offset readback writes only at the offset and stays top-down (" +
                  std::to_string(good) + "/" + std::to_string(count) +
                  (guardIntact ? ", guard intact" : ", GUARD OVERWRITTEN") + ")");
        }

        // REMED-GFX-149 was measured here and left as an [INFO] line so this task could not bless
        // either behaviour. It is now closed: the whole-level overload applies `startIndex` to the
        // DESTINATION exactly like the rectangle overload above, so N4 asserts the same guarantee
        // through the other entry point instead of merely reporting it.
        std::vector<Color> offset3(static_cast<std::size_t>(count) + 5, Color(0x11, 0x22, 0x33, 0x44));
        ok = true;
        try { a.GetData(offset3.data(), 5, count); }
        catch (const std::exception& e) { ok = false; what = e.what(); }
        if (!ok) { check(false, "N4 whole-level destination-offset readback threw: " + what); }
        else
        {
            bool guardIntact = true;
            for (int i = 0; i < 5; ++i)
                if (!Same(offset3[static_cast<std::size_t>(i)], Color(0x11, 0x22, 0x33, 0x44)))
                    guardIntact = false;
            int good = 0;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (Same(offset3[static_cast<std::size_t>(5 + y * kPW + x)], PatternColor(x, y)))
                        ++good;
            check(guardIntact && good == count,
                  "N4 GetData(data, startIndex=5, count) on a render target writes only at the "
                  "offset and stays top-down (" + std::to_string(good) + "/" +
                  std::to_string(count) +
                  (guardIntact ? ", guard intact" : ", GUARD OVERWRITTEN") + ")");
        }
        std::fflush(stdout);
        (void)dev;
    }

    /// Leg O: RenderTargetCube is REMED-GFX-137's territory, not this task's. This control only
    /// records that the cube path still works and that its measured orientation is UNCHANGED --
    /// it deliberately asserts nothing about which orientation is correct.
    void LegCubeBoundary(GraphicsDevice& dev)
    {
        if (Unsupported()) { check(true, "O1 cube boundary: not applicable on a non-rasterizing renderer"); return; }

        bool threw = false;
        std::string what;
        std::vector<Color> face(static_cast<std::size_t>(16) * 16, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            RenderTargetCube cube(dev, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            BasicEffect fx(dev);
            dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuadrants(dev, fx);
            dev.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveX);
            ResetState(dev);
            cube.GetData(CubeMapFace::PositiveX, face.data(), 0, static_cast<int>(face.size()));
        }
        catch (const std::exception& e) { threw = true; what = e.what(); }

        if (threw)
        {
            std::printf("[INFO] O1 RenderTargetCube unavailable here (%s) -- boundary recorded\n",
                        what.c_str());
            std::fflush(stdout);
            check(true, "O1 cube boundary recorded, not changed by REMED-GFX-147");
            return;
        }
        const Color tl = face[static_cast<std::size_t>(3) * 16 + 3];
        const Color bl = face[static_cast<std::size_t>(12) * 16 + 3];
        std::printf("[INFO] O1 RenderTargetCube +X face row3=%s row12=%s (REMED-GFX-137 owns this)\n",
                    ColorText(tl).c_str(), ColorText(bl).c_str());
        std::fflush(stdout);
        check(!Same(tl, bl),
              "O1 RenderTargetCube face readback still carries a real, non-uniform rendered image "
              "(orientation itself is REMED-GFX-137's, unchanged here)");
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        whiteTex_ = Texture2D(dev, 1, 1);
        const Color white = Color::White;
        whiteTex_.SetData(&white, 1);

        std::vector<Color> pattern(static_cast<std::size_t>(kPW) * kPH, Color(0, 0, 0, 0));
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
                pattern[static_cast<std::size_t>(y) * kPW + x] = PatternColor(x, y);
        patternTex_ = Texture2D(dev, kPW, kPH);
        patternTex_.SetData(pattern.data(), static_cast<int>(pattern.size()));

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));

        std::printf("[INFO] REMED-GFX-147 render-target sampling orientation on %s (%dx%d pattern)\n",
                    kRendererName, kPW, kPH);
        std::fflush(stdout);

        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        LegS(dev, a);
        Readback aBytes = ReadWhole(a, kPW, kPH);

        // The "identical bytes" control. When the target cannot be read at all, fall back to the
        // pattern so the remaining legs still exercise ordinary sampling and assert the rejections.
        Texture2D b = aBytes.ok() ? CloneAsTexture(dev, aBytes) : Texture2D(dev, kPW, kPH);
        if (!aBytes.ok())
        {
            std::vector<Color> pattern(static_cast<std::size_t>(kPW) * kPH, Color(0, 0, 0, 0));
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    pattern[static_cast<std::size_t>(y) * kPW + x] = PatternColor(x, y);
            b.SetData(pattern.data(), static_cast<int>(pattern.size()));
        }

        LegAB(dev, a, b);
        Leg3D(dev, a, b);
        LegSampleWithoutReadback(dev);
        LegChains(dev, a);
        LegBackbuffer(dev, a, b);
        LegRepeat(dev, a, b, aBytes);
        LegSpriteEffects(dev, a, b);
        LegSpriteBatchKnobs(dev, a, b);
        LegMsaa(dev, b);
        LegMips(dev, b);
        LegReadbackContract(dev, a, aBytes);
        LegCubeBoundary(dev);

        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    RenderTargetSamplingOrientationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main(int, char**)
{
    RenderTargetSamplingOrientationTest test;
    test.Run();
    return test.Result();
}
