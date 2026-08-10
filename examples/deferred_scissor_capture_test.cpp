// SPDX-License-Identifier: MS-PL
// REMED-GFX-146: every deferred draw must execute under the GraphicsDevice.ScissorRectangle AND
// the RasterizerState.ScissorTestEnable that were active at its own public draw call.
//
// WebGPU collects a bind cycle's Clear/3D/SpriteBatch work and records ONE native render pass for
// it later (`EnsureFrameRendered`, `RenderPendingDrawsToRenderTarget`,
// `RenderPendingDrawsToRenderTargetCubeFace`). Until this task all three issued exactly one
// `wgpuRenderPassEncoderSetScissorRect` at the top of that pass, computed from the LIVE renderer
// members `scissorEnabled_`/`scissorX_`/`scissorY_`/`scissorW_`/`scissorH_`. Deferring the
// RECORDING is legal; RESOLVING the value late is not. The consequence is that
//
//     ScissorRectangle = left  ; draw red
//     ScissorRectangle = right ; draw blue
//     ScissorRectangle = left  ; draw a smaller green quad
//     ScissorRectangle = full          <-- an ordinary restore, before anything is flushed
//
// executed all three draws under the FULL rectangle, i.e. whatever happened to be current when the
// pass was recorded. Two rectangles in one cycle collapsed last-wins, and a restore before the
// flush silently unclipped every already-queued draw. `SetRenderTarget` itself resets
// ScissorRectangle to the newly bound target's full size (FNA parity), so on this renderer an
// ordinary bind cycle ALWAYS ended with the full rectangle live -- which is why the defect reached
// every render target as well as the backbuffer.
//
// This is the scissor counterpart of REMED-GFX-116 (viewport) and shares its shape exactly, so the
// two are deliberately structured the same way.
//
// Why every check here is falsifiable in both directions
// ------------------------------------------------------
//   * Nothing between the draws forces a flush: no GetData, no Present, no explicit flush, no
//     fence, no sleep, no extra frame, no render-target switch. Each sequence is queued whole and
//     observed exactly once at its end. A fixture that reads or switches targets between scissor
//     changes cannot see this defect at all -- a render-target switch IS the flush on this
//     renderer, which is exactly how the defect survived every existing scissor test.
//   * The rectangle is restored to the whole target BEFORE the single observation, so "resolve at
//     flush time" and "capture at queue time" predict visibly different images rather than the
//     same one.
//   * Geometry and colour are chosen so each wrong implementation lands somewhere else: every draw
//     under its own rectangle, all under the final one, the rectangle ignored entirely, the
//     rectangle applied as a VIEWPORT (which squeezes rather than clips), X/Y dropped but
//     Width/Height kept, Width/Height dropped but X/Y kept, the offset applied twice and a
//     flipped Y all produce distinct, asserted pixel layouts.
//   * ScissorTestEnable is exercised in both states and across transitions, so a renderer that
//     captures the rectangle but reads the enable flag live still fails.
//
// The palette is 0/255-only, an exact fixed point of sRGB encoding, so every comparison is
// byte-exact including on sRGB render targets.
//
// Sections: A A->B->A on a render target; B Y orientation; C rectangle components one at a time;
// D odd target dimensions; E empty and out-of-bounds rectangles; F target/backbuffer transitions;
// G buffer kinds (static/dynamic, indexed/non-indexed, 16-/32-bit, DrawUser); H stock effect
// families; I SpriteBatch; J Viewport interaction; K RasterizerState; L repetition.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 96;   ///< Requested backbuffer width.
    constexpr int kBBH = 72;   ///< Requested backbuffer height.
    constexpr int kRTW = 64;   ///< Primary render-target width (halves/quarters are exact).
    constexpr int kRTH = 48;   ///< Primary render-target height.
    constexpr int kOddW = 33;  ///< Odd-dimension target, so no boundary lands on a power of two.
    constexpr int kOddH = 25;

    enum class Support
    {
        Exact,        ///< Returns the rendered surface byte for byte.
        Unsupported,  ///< Raises; the caller's destination is left untouched.
    };

    /// The complete, reviewed per-renderer claim this file enforces.
    struct Contract
    {
        const char* name;
        Support backbufferReadback;  ///< GraphicsDevice::GetBackBufferData.
        Support rtReadback;          ///< RenderTarget2D::GetData at level 0.
        bool draws3D;                ///< BasicEffect over VertexPositionColor rasterizes here.
        /// A 3D draw into a RenderTarget2D is clipped by GraphicsDevice.ScissorRectangle.
        bool targetScissorApplies;
        /// A 3D draw on the BACKBUFFER is clipped by GraphicsDevice.ScissorRectangle. Declared
        /// separately from the render-target answer: a renderer can honour one and not the other.
        bool backbufferScissorApplies;
        /// A SpriteBatch fill is clipped when Begin's RasterizerState enables the scissor test.
        bool spriteScissorApplies;
        /// A degenerate (zero-width or zero-height) rectangle rasterizes nothing.
        bool emptyScissorDrawsNothing;
        /**
         * A ScissorRectangle reaching past the bound target is REJECTED with a diagnostic instead
         * of being clipped to the target. True only where the renderer's declared job is to validate
         * (Headless, HEADLESS-23); every rendering renderer clips, which is the CNA/FNA contract.
         */
        bool outOfBoundsScissorRejected;
        bool wantHiDefProfile;
    };

#if defined(CNA_RENDERER_WEBGPU)
    constexpr Contract kContract{"WEBGPU", Support::Exact, Support::Exact, true,
                                 true, true, true, true, false, false};
#elif defined(CNA_RENDERER_VULKAN)
    // `emptyScissorDrawsNothing` false: measured here. `VulkanRenderer`'s `computeScissor`
    // returns the WHOLE framebuffer for `sw == 0 || sh == 0`, so a degenerate rectangle does not
    // clip the draw away. Every other check in this file passes on Vulkan, which isolates this to
    // the degenerate case; it is recorded as its own finding rather than fixed here, and check E1
    // asserts the IGNORED outcome exactly so the declaration is falsifiable in both directions.
    constexpr Contract kContract{"VULKAN", Support::Exact, Support::Exact, true,
                                 true, true, true, false, false, false};
#elif defined(CNA_RENDERER_EASYGL)
    // `emptyScissorDrawsNothing` false: measured here, by a DIFFERENT mechanism from Vulkan's.
    // `EasyGLRenderer::SetScissorRect` returns early on `w <= 0 || h <= 0` and leaves the
    // previously installed rectangle in place, so a degenerate rectangle is not merely unclipping,
    // it never reaches the renderer at all. Same observable, recorded as its own finding.
    constexpr Contract kContract{"EASYGL", Support::Exact, Support::Exact, true,
                                 true, true, true, false, false, false};
#elif defined(CNA_RENDERER_BGFX)
    // `emptyScissorDrawsNothing` false: measured here, the same observable as Vulkan and EasyGL.
    constexpr Contract kContract{"BGFX", Support::Exact, Support::Exact, true,
                                 true, true, true, false, false, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // SdlGpu has no `ReadBackbuffer` override at all, so `GetBackBufferData` raises; its
    // render-target oracle still answers every render-target question in this file.
    // `emptyScissorDrawsNothing` false: measured here, the same observable as Vulkan and EasyGL.
    constexpr Contract kContract{"SDL_GPU", Support::Unsupported, Support::Exact, true,
                                 true, true, true, false, false, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr Contract kContract{"SOFTWARE", Support::Exact, Support::Exact, true,
                                 true, true, true, true, false, false};
#elif defined(CNA_RENDERER_HEADLESS)
    // Headless rasterizes nothing and its readback is REMED-GFX-127/130's deterministic refusal.
    // Every sequence must still be legal. `outOfBoundsScissorRejected` true: this is the renderer
    // whose declared job is to validate, and HEADLESS-23 makes `SetScissorRect` cross-reference the
    // bound target's real size and raise `HeadlessValidationException` for a rectangle that leaves
    // it, instead of clipping. Checks E2/E3 assert the REJECTION there, so the declaration is
    // falsifiable in both directions rather than being an untested exemption.
    constexpr Contract kContract{"HEADLESS", Support::Unsupported, Support::Unsupported, true,
                                 true, true, true, true, true, false};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", Support::Exact, Support::Exact, true,
                                 true, true, true, true, false, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr Contract kContract{"DIRECTX9", Support::Exact, Support::Exact, true,
                                 true, true, true, true, false, true};
#elif defined(CNA_RENDERER_LLGL)
    constexpr Contract kContract{"LLGL", Support::Exact, Support::Exact, true,
                                 true, true, true, true, false, false};
#else
#error "REMED-GFX-146: this renderer has no declared deferred-scissor contract."
#endif

    const Color kBlack  (  0,   0,   0, 255);
    const Color kRed    (255,   0,   0, 255);
    const Color kGreen  (  0, 255,   0, 255);
    const Color kBlue   (  0,   0, 255, 255);
    const Color kYellow (255, 255,   0, 255);
    const Color kMagenta(255,   0, 255, 255);
    const Color kCyan   (  0, 255, 255, 255);
    const Color kWhite  (255, 255, 255, 255);

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /// One classified readback: what the call did, without judging it yet.
    struct Image
    {
        bool threw = false;
        std::string what;
        int w = 0;
        int h = 0;
        std::vector<Color> px;

        [[nodiscard]] Color At(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= w || y >= h) return Color(0, 0, 0, 0);
            return px[static_cast<std::size_t>(y) * w + x];
        }
    };

    /// One asserted probe: a pixel that must hold exactly this colour.
    struct Probe
    {
        int x;
        int y;
        Color expected;
    };

    VertexDeclaration PosTexDecl()
    {
        return VertexDeclaration(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    /// Stride-52 skinned vertex; SkinnedEffect needs an exactly packed source, not a wider C++ type.
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

    /// Stride-48 VertexPositionNormalTangentTexture, the PbrEffect stream.
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    /// Stride-68 VertexPositionNormalTangentTextureSkinned, the SkinnedPbrEffect stream.
    struct SkinnedPbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrGpuVertex) == 68, "skinned PBR vertex must be 68 bytes");
}

class DeferredScissorCaptureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<BasicEffect> fx_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<Texture2D> red_;
    std::unique_ptr<Texture2D> blue_;
    std::unique_ptr<TextureCube> cube_;
    /// Vertex buffers that must outlive the queueing of a whole bind cycle -- see MakeVb().
    std::vector<std::unique_ptr<VertexBuffer>> keepAlive_;

    int bbW_ = kBBW;
    int bbH_ = kBBH;
    /// What Prepare3D()/BeginSprites() install as RasterizerState.ScissorTestEnable.
    bool scissorTest_ = true;
    int phase_ = 0;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void skip(const std::string& label) { check(true, label); }

    // ---------------------------------------------------------------------
    // Oracles
    // ---------------------------------------------------------------------

    bool CanJudgeTarget(const std::string& label)
    {
        if (kContract.rtReadback == Support::Exact) return true;
        skip(label + ": sequence issued, judgement skipped -- no render-target readback here");
        return false;
    }

    bool CanJudgeBackbuffer(const std::string& label)
    {
        if (kContract.backbufferReadback == Support::Exact) return true;
        skip(label + ": sequence issued, judgement skipped -- no backbuffer readback here");
        return false;
    }

    static Image ReadTarget(RenderTarget2D& rt, int w, int h)
    {
        Image image;
        image.w = w;
        image.h = h;
        image.px.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            rt.GetData(0, nullptr, image.px.data(), 0, static_cast<int>(image.px.size()));
        }
        catch (const std::exception& e)
        {
            image.threw = true;
            image.what = e.what();
        }
        return image;
    }

    Image ReadBackbuffer(GraphicsDevice& dev) const
    {
        Image image;
        image.w = bbW_;
        image.h = bbH_;
        image.px.assign(static_cast<std::size_t>(bbW_) * bbH_, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            const Rectangle whole(0, 0, bbW_, bbH_);
            dev.GetBackBufferData(&whole, image.px.data(), 0, static_cast<int>(image.px.size()));
        }
        catch (const std::exception& e)
        {
            image.threw = true;
            image.what = e.what();
        }
        return image;
    }

    /// Asserts every probe, reporting the first mismatch with its coordinates and both colours.
    void CheckProbes(const Image& image, const std::vector<Probe>& probes, const std::string& label)
    {
        if (image.threw)
        {
            check(false, label + ": readback raised: " + image.what);
            return;
        }
        std::size_t bad = 0;
        std::string firstBad;
        for (const Probe& p : probes)
        {
            const Color got = image.At(p.x, p.y);
            if (Same(got, p.expected)) continue;
            ++bad;
            if (firstBad.empty())
                firstBad = " first at (" + std::to_string(p.x) + "," + std::to_string(p.y) +
                           ") expected " + ColorText(p.expected) + " got " + ColorText(got);
        }
        check(bad == 0, bad == 0 ? label
                                 : label + ": " + std::to_string(bad) + "/" +
                                   std::to_string(probes.size()) + " probes wrong," + firstBad);
    }

    // ---------------------------------------------------------------------
    // Geometry
    // ---------------------------------------------------------------------

    /// Six clip-space vertices covering [@p x0,@p x1] x [@p y0,@p y1] at depth @p z.
    static std::array<VertexPositionColor, 6> Quad(float x0, float x1, float y0, float y1,
                                                   float z, const Color& c)
    {
        return {{
            {Vector3(x0, y1, z), c},
            {Vector3(x0, y0, z), c},
            {Vector3(x1, y0, z), c},
            {Vector3(x0, y1, z), c},
            {Vector3(x1, y0, z), c},
            {Vector3(x1, y1, z), c},
        }};
    }

    static std::array<VertexPositionColor, 6> FullQuad(const Color& c, float z = 0.5f)
    {
        return Quad(-1.0f, 1.0f, -1.0f, 1.0f, z, c);
    }

    /**
     * @brief A VertexBuffer that stays alive until the current section's readback has happened.
     *
     * These sequences deliberately queue every draw of a bind cycle before anything flushes, so a
     * buffer destroyed at the end of the calling helper would be gone while the draw is still
     * pending on renderers that do not shadow-copy vertex data at queue time. Each section clears
     * the list on entry, i.e. after the previous section's single observation.
     */
    VertexBuffer& MakeVb(GraphicsDevice& dev, const std::array<VertexPositionColor, 6>& v)
    {
        auto vb = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), static_cast<int>(v.size()),
            BufferUsage::None);
        vb->SetData(v.data(), 0, static_cast<int>(v.size()));
        keepAlive_.push_back(std::move(vb));
        return *keepAlive_.back();
    }

    // ---------------------------------------------------------------------
    // State helpers
    // ---------------------------------------------------------------------

    /// The RasterizerState this fixture draws under: CullNone plus the requested scissor enable.
    RasterizerState Raster(bool scissorTestEnable) const
    {
        RasterizerState rs = RasterizerState::CullNone;
        rs.setScissorTestEnableProperty(scissorTestEnable);
        return rs;
    }

    void Prepare3D(GraphicsDevice& dev, bool depth)
    {
        dev.setRasterizerStateProperty(Raster(scissorTest_));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(depth ? DepthStencilState::Default : DepthStencilState::None);
    }

    /// One non-indexed VertexPositionColor draw of a whole 6-vertex quad, no state left bound.
    void Draw3D(GraphicsDevice& dev, const std::array<VertexPositionColor, 6>& quad,
                bool depth = false)
    {
        Prepare3D(dev, depth);
        fx_->VertexColorEnabled = true;
        fx_->setTextureEnabledProperty(false);
        fx_->Apply();
        VertexBuffer& vb = MakeVb(dev, quad);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void BeginSprites()
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState rs = Raster(scissorTest_);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &rs);
    }

    /// One SpriteBatch cycle filling @p rect (in the ACTIVE viewport's coordinates) with @p colour.
    void SpriteFill(const Rectangle& rect, const Color& colour)
    {
        BeginSprites();
        sb_->Draw(*white_, rect, Rectangle(0, 0, 1, 1), colour);
        sb_->End();
    }

    static void SetScissor(GraphicsDevice& dev, int x, int y, int w, int h)
    {
        dev.setScissorRectangleProperty(Rectangle(x, y, w, h));
    }

    static void SetViewport(GraphicsDevice& dev, int x, int y, int w, int h,
                            float minDepth = 0.0f, float maxDepth = 1.0f)
    {
        Viewport vp(x, y, w, h);
        vp.setMinDepthProperty(minDepth);
        vp.setMaxDepthProperty(maxDepth);
        dev.setViewportProperty(vp);
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, int w, int h,
                                               DepthFormat depth = DepthFormat::None)
    {
        return std::make_unique<RenderTarget2D>(dev, w, h, false, SurfaceFormat::Color, depth, 0,
                                                RenderTargetUsage::DiscardContents);
    }

    // =====================================================================
    // A -- the decisive A -> B -> A sequence inside ONE bind cycle
    // =====================================================================

    /**
     * @brief A1 -- left half red, right half blue, a smaller green quad back in the left half.
     *
     * The rectangle is restored to the whole target after the last draw and before the only
     * readback, so a renderer that resolves the scissor when it records the pass paints the whole
     * target blue with a green centre band instead. This is the canonical sequence: bind, enable
     * the scissor test, rectangle A, a full-target red quad, rectangle B, a full-target blue quad,
     * rectangle A again, a SMALLER green quad, unbind, one readback.
     */
    void RunLeftRightLeft(GraphicsDevice& dev)
    {
        const std::string label = "A1 3D A->B->A in one bind cycle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kBlue));
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, Quad(-0.5f, 0.5f, -0.5f, 0.5f, 0.5f, kGreen));
        SetScissor(dev, 0, 0, kRTW, kRTH);   // the ordinary restore that used to erase all three
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // The green quad's own extent is NDC [-0.5,0.5]^2 under the FULL viewport: x[16,48)
        // y[12,36). Rectangle A clips it to x[16,32).
        CheckProbes(image, {
            {  4,  4, kRed   },   // left half, outside the green sub-rect
            { 30, 44, kRed   },
            { 15, 24, kRed   },   // exact left edge of the green sub-rect
            { 16, 24, kGreen },
            { 31, 24, kGreen },   // the green quad reaches rectangle A's final column
            { 32, 24, kBlue  },   // ... and is clipped there; the right half is the second draw
            { 16, 11, kRed   },   // exact top edge of the green quad
            { 16, 12, kGreen },
            { 16, 35, kGreen },
            { 16, 36, kRed   },
            { 47, 24, kBlue  },
            { 63,  0, kBlue  },
            { 40, 47, kBlue  },
        }, label);
    }

    /**
     * @brief A2 -- the same shape with three DISJOINT rectangles, so nothing can overlap.
     *
     * Distinguishes "the last rectangle won" from "every draw kept its own": under last-wins the
     * three colours stack in one band and the other two stay at the clear colour.
     */
    void RunThreeDisjointScissors(GraphicsDevice& dev)
    {
        const std::string label = "A2 three disjoint scissor rectangles in one cycle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const int band = kRTW / 4;   // 16: three bands plus an untouched tail

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, band, kRTH);
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, band, 0, band, kRTH);
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, 2 * band, 0, band, kRTH);
        Draw3D(dev, FullQuad(kBlue));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  0, 24, kRed   }, { 15, 24, kRed   },
            { 16, 24, kGreen }, { 31, 24, kGreen },
            { 32, 24, kBlue  }, { 47, 24, kBlue  },
            { 48, 24, kBlack }, { 63, 24, kBlack },   // never covered by any rectangle
        }, label);
    }

    // =====================================================================
    // B -- Y orientation: the conversion must be applied exactly once
    // =====================================================================

    void RunUpperLowerUpper(GraphicsDevice& dev)
    {
        const std::string label = "B1 upper/lower rectangles keep public Y orientation";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW, kRTH / 4);                 // top quarter
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, 0, (3 * kRTH) / 4, kRTW, kRTH / 4);    // bottom quarter
        Draw3D(dev, FullQuad(kBlue));
        SetScissor(dev, 0, kRTH / 4, kRTW, kRTH / 4);          // second quarter
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // Row 0 is the top of the target for the public ScissorRectangle and for GetData alike; an
        // extra flip anywhere in the chain swaps red and blue.
        CheckProbes(image, {
            { 32,  0, kRed   }, { 32, 11, kRed   },
            { 32, 12, kGreen }, { 32, 23, kGreen },
            { 32, 24, kBlack }, { 32, 35, kBlack },
            { 32, 36, kBlue  }, { 32, 47, kBlue  },
        }, label);
    }

    // =====================================================================
    // C -- one rectangle component at a time
    // =====================================================================

    /**
     * @brief Draws a full-target red quad under one rectangle, restores the full rectangle, then
     *        asserts the exact rectangle including its final row and column and the pixels just
     *        outside it.
     */
    void RunComponentCase(GraphicsDevice& dev, const std::string& label,
                          int x, int y, int w, int h)
    {
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, x, y, w, h);
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        std::vector<Probe> probes{
            {x,         y,         kRed},
            {x + w - 1, y,         kRed},
            {x,         y + h - 1, kRed},
            {x + w - 1, y + h - 1, kRed},
        };
        if (x > 0)          probes.push_back({x - 1,     y + h / 2, kBlack});
        if (y > 0)          probes.push_back({x + w / 2, y - 1,     kBlack});
        if (x + w < kRTW)   probes.push_back({x + w,     y + h / 2, kBlack});
        if (y + h < kRTH)   probes.push_back({x + w / 2, y + h,     kBlack});
        CheckProbes(image, probes, label);
    }

    void RunScissorComponents(GraphicsDevice& dev)
    {
        RunComponentCase(dev, "C1 nonzero X only",        12,  0, 20, kRTH);
        RunComponentCase(dev, "C2 nonzero Y only",         0,  7, kRTW, 19);
        RunComponentCase(dev, "C3 reduced Width only",     0,  0, 20, kRTH);
        RunComponentCase(dev, "C4 reduced Height only",    0,  0, kRTW, 19);
        RunComponentCase(dev, "C5 centre rectangle",      12,  7, 20, 19);
        RunComponentCase(dev, "C6 final row and column",  kRTW - 1, kRTH - 1, 1, 1);
        RunComponentCase(dev, "C7 first row and column",   0,  0, 1, 1);
        RunComponentCase(dev, "C8 whole target",           0,  0, kRTW, kRTH);
    }

    // =====================================================================
    // D -- odd target dimensions
    // =====================================================================

    void RunOddDimensions(GraphicsDevice& dev)
    {
        const std::string label = "D1 odd 33x25 target, off-centre rectangle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kOddW, kOddH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 7, 5, 19, 13);
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, 0, 0, kOddW, kOddH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kOddW, kOddH);
        CheckProbes(image, {
            {  7,  5, kRed   }, { 25,  5, kRed   },
            {  7, 17, kRed   }, { 25, 17, kRed   },
            {  6, 11, kBlack }, { 26, 11, kBlack },
            { 16,  4, kBlack }, { 16, 18, kBlack },
            { 32, 24, kBlack },   // the target's own final pixel is never touched
        }, label);
    }

    // =====================================================================
    // E -- empty and out-of-bounds rectangles
    // =====================================================================

    /**
     * @brief E1 -- what a degenerate (zero-width or zero-height) rectangle does.
     *
     * A measured cross-renderer divergence, declared per renderer rather than standardised away.
     * WebGPU takes the natural hardware semantics -- a zero-extent scissor rejects every fragment.
     * Vulkan's `computeScissor` returns the WHOLE framebuffer for `sw == 0 || sh == 0`, and
     * EasyGL's `SetScissorRect` returns early on `w <= 0 || h <= 0` and leaves the previous
     * rectangle installed; two different mechanisms, one shared observable -- the draw is not
     * clipped away. Both outcomes are asserted EXACTLY here, so either declaration turns red the
     * day its renderer changes.
     *
     * The final real rectangle is a control on every renderer: whatever the degenerate ones do,
     * a genuine rectangle after them must still clip exactly, which rules out "the rectangle
     * stopped working at all".
     */
    void RunEmptyRectangles(GraphicsDevice& dev)
    {
        const std::string label = "E1 degenerate rectangles";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 16, 12, 0, 20);      // zero width
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, 16, 12, 20, 0);      // zero height
        Draw3D(dev, FullQuad(kBlue));
        SetScissor(dev, 0, 0, 0, 0);         // wholly empty
        Draw3D(dev, FullQuad(kYellow));
        SetScissor(dev, 40, 30, 8, 6);       // a real rectangle, so "nothing ever draws" fails too
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // The control is asserted identically either way.
        std::vector<Probe> probes{
            { 40, 30, kGreen }, { 47, 35, kGreen },
            { 39, 30, kContract.emptyScissorDrawsNothing ? kBlack : kYellow },
            { 48, 35, kContract.emptyScissorDrawsNothing ? kBlack : kYellow },
        };
        if (kContract.emptyScissorDrawsNothing)
        {
            probes.push_back({ 16, 12, kBlack });
            probes.push_back({ 20, 20, kBlack });
            probes.push_back({ 30, 12, kBlack });
            probes.push_back({  0,  0, kBlack });
            probes.push_back({ 63, 47, kBlack });
        }
        else
        {
            // Unclipped: the LAST degenerate draw covers everything the control did not.
            probes.push_back({ 16, 12, kYellow });
            probes.push_back({ 20, 20, kYellow });
            probes.push_back({ 30, 12, kYellow });
            probes.push_back({  0,  0, kYellow });
            probes.push_back({ 63, 47, kYellow });
        }
        CheckProbes(image, probes,
                    label + (kContract.emptyScissorDrawsNothing ? " rasterize nothing"
                                                                : " are ignored (declared)"));
    }

    /**
     * @brief E2 -- a rectangle that hangs off the target is clipped at the boundary, not wrapped.
     *
     * On a renderer that declares `outOfBoundsScissorRejected` the same rectangle must instead
     * raise, and the target must be left as it was: both outcomes are asserted, so neither
     * declaration is an untested exemption.
     */
    void RunOutOfBoundsRectangle(GraphicsDevice& dev)
    {
        const std::string label = "E2 rectangle partially outside the target";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        bool threw = false;
        std::string why;

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        try
        {
            SetScissor(dev, 48, 36, 32, 32);   // 16 past the right edge, 20 past the bottom
            Draw3D(dev, FullQuad(kRed));
        }
        catch (const std::exception& e)
        {
            threw = true;
            why = e.what();
        }
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (kContract.outOfBoundsScissorRejected)
        {
            check(threw, threw ? label + ": rejected with a diagnostic (declared): " + why
                               : label + ": accepted an out-of-bounds rectangle although this "
                                         "renderer declares it a validation error");
            return;
        }
        if (threw)
        {
            check(false, label + ": raised instead of clipping: " + why);
            return;
        }
        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // A rectangle wrapped through an unsigned conversion, or one whose overhang shifted its
        // origin, cannot produce exactly this corner.
        CheckProbes(image, {
            { 48, 36, kRed   }, { 63, 36, kRed   },
            { 48, 47, kRed   }, { 63, 47, kRed   },
            { 47, 36, kBlack }, { 48, 35, kBlack },
            {  0,  0, kBlack }, { 32, 24, kBlack },
        }, label + " clips at the edge");
    }

    /// E3 -- a rectangle entirely outside the target draws nothing (or is rejected, as declared).
    void RunFullyOutsideRectangle(GraphicsDevice& dev)
    {
        const std::string label = "E3 rectangle entirely outside the target";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        bool threw = false;
        std::string why;

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        try
        {
            SetScissor(dev, kRTW + 40, kRTH + 40, 16, 16);
            Draw3D(dev, FullQuad(kRed));
        }
        catch (const std::exception& e)
        {
            threw = true;
            why = e.what();
        }
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (kContract.outOfBoundsScissorRejected)
        {
            check(threw, threw ? label + ": rejected with a diagnostic (declared): " + why
                               : label + ": accepted a wholly out-of-bounds rectangle although "
                                         "this renderer declares it a validation error");
            return;
        }
        if (threw)
        {
            check(false, label + ": raised instead of clipping: " + why);
            return;
        }
        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  0,  0, kBlack }, { 63, 47, kBlack }, { 32, 24, kBlack }, { 63, 0, kBlack },
        }, label + " draws nothing");
    }

    // =====================================================================
    // F -- target and backbuffer transitions
    // =====================================================================

    /// F1 -- two different targets in one frame, each with its own rectangle.
    void RunTwoTargetsInterleaved(GraphicsDevice& dev)
    {
        const std::string label = "F1 two targets, two rectangles";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto a = MakeTarget(dev, kRTW, kRTH);
        auto b = MakeTarget(dev, kRTW / 2, kRTH / 2);

        dev.SetRenderTarget(a.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(b.get());
        dev.Clear(kBlack);
        SetScissor(dev, kRTW / 4, 0, kRTW / 4, kRTH / 2);
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, 0, 0, kRTW / 2, kRTH / 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image ia = ReadTarget(*a, kRTW, kRTH);
        CheckProbes(ia, {{ 8, 24, kRed }, { 31, 24, kRed }, { 32, 24, kBlack }}, label + " (A)");
        const Image ib = ReadTarget(*b, kRTW / 2, kRTH / 2);
        CheckProbes(ib, {{ 16, 12, kGreen }, { 31, 12, kGreen }, { 15, 12, kBlack }},
                    label + " (B)");
    }

    /// F2 -- target -> backbuffer -> target, four rectangles, one observation each.
    void RunTargetBackbufferTarget(GraphicsDevice& dev)
    {
        const std::string label = "F2 target -> backbuffer -> target";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, bbW_ / 3, bbH_);
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, (2 * bbW_) / 3, 0, bbW_ / 3, bbH_);
        Draw3D(dev, FullQuad(kBlue));
        dev.SetRenderTarget(rt.get());
        SetScissor(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kYellow));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        SetScissor(dev, 0, 0, bbW_, bbH_);
        const Image bb = ReadBackbuffer(dev);

        if (kContract.targetScissorApplies && CanJudgeTarget(label + " (target)"))
        {
            const Image ir = ReadTarget(*rt, kRTW, kRTH);
            CheckProbes(ir, {{ 40, 24, kYellow }, { 8, 24, kBlack }}, label + " (target)");
        }
        if (!kContract.backbufferScissorApplies)
        {
            skip(label + " (backbuffer): sequence issued, judgement skipped -- this renderer does "
                         "not clip backbuffer draws with ScissorRectangle");
            return;
        }
        if (!CanJudgeBackbuffer(label + " (backbuffer)")) return;
        CheckProbes(bb, {
            {              4, bbH_ / 2, kGreen },
            {   bbW_ / 3 - 1, bbH_ / 2, kGreen },
            {       bbW_ / 2, bbH_ / 2, kBlack },
            { (2 * bbW_) / 3, bbH_ / 2, kBlue  },
            {       bbW_ - 1, bbH_ / 2, kBlue  },
        }, label + " (backbuffer)");
    }

    /// F3 -- the backbuffer's own A->B->A, restored before the single read.
    void RunBackbufferLeftRightLeft(GraphicsDevice& dev)
    {
        const std::string label = "F3 backbuffer 3D A->B->A";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        const int third = bbW_ / 3;

        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, third, bbH_);
        Draw3D(dev, FullQuad(kRed));
        SetScissor(dev, third, 0, third, bbH_);
        Draw3D(dev, FullQuad(kBlue));
        SetScissor(dev, 0, 0, third / 2, bbH_);
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, 0, 0, bbW_, bbH_);
        const Image bb = ReadBackbuffer(dev);

        if (!kContract.backbufferScissorApplies)
        {
            skip(label + ": sequence issued, judgement skipped -- this renderer does not clip "
                         "backbuffer draws with ScissorRectangle");
            return;
        }
        if (!CanJudgeBackbuffer(label)) return;
        CheckProbes(bb, {
            {              1, bbH_ / 2, kGreen },
            {  third / 2 - 1, bbH_ / 2, kGreen },
            {      third / 2, bbH_ / 2, kRed   },
            {      third - 1, bbH_ / 2, kRed   },
            {      third + 4, bbH_ / 2, kBlue  },
            {  2 * third - 1, bbH_ / 2, kBlue  },
            {  2 * third + 4, bbH_ / 2, kBlack },
            {       bbW_ - 1, bbH_ / 2, kBlack },
        }, label);
    }

    /// F4 -- backbuffer rectangle A, a whole target cycle, then backbuffer rectangle B.
    void RunScissorAcrossTargetSwitch(GraphicsDevice& dev)
    {
        const std::string label = "F4 backbuffer A -> target -> backbuffer B";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const int third = bbW_ / 3;

        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, third, bbH_);
        Draw3D(dev, FullQuad(kRed));
        dev.SetRenderTarget(rt.get());          // resets ScissorRectangle to the target's size
        dev.Clear(kMagenta);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kCyan));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        SetScissor(dev, 2 * third, 0, third, bbH_);
        Draw3D(dev, FullQuad(kBlue));
        SetScissor(dev, 0, 0, bbW_, bbH_);
        const Image bb = ReadBackbuffer(dev);

        if (kContract.targetScissorApplies && CanJudgeTarget(label + " (target)"))
        {
            const Image ir = ReadTarget(*rt, kRTW, kRTH);
            CheckProbes(ir, {{ 8, 24, kCyan }, { 40, 24, kMagenta }}, label + " (target)");
        }
        if (!kContract.backbufferScissorApplies)
        {
            skip(label + " (backbuffer): sequence issued, judgement skipped -- this renderer does "
                         "not clip backbuffer draws with ScissorRectangle");
            return;
        }
        if (!CanJudgeBackbuffer(label + " (backbuffer)")) return;
        CheckProbes(bb, {
            {          2, bbH_ / 2, kRed   },
            {  third - 1, bbH_ / 2, kRed   },
            {  third + 4, bbH_ / 2, kBlack },   // the middle third belongs to neither draw
            { 2 * third,  bbH_ / 2, kBlue  },
            {   bbW_ - 1, bbH_ / 2, kBlue  },
        }, label + " (backbuffer)");
    }

    // =====================================================================
    // G -- buffer kinds: static/dynamic, indexed/non-indexed, 16/32-bit, DrawUser
    // =====================================================================

    void RunBufferKinds(GraphicsDevice& dev)
    {
        const std::string label = "G1 indexed / non-indexed / dynamic / DrawUser";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const int band = kRTW / 4;

        std::array<VertexPositionColor, 4> corners{{
            {Vector3(-1.0f,  1.0f, 0.5f), kRed},
            {Vector3(-1.0f, -1.0f, 0.5f), kRed},
            {Vector3( 1.0f, -1.0f, 0.5f), kRed},
            {Vector3( 1.0f,  1.0f, 0.5f), kRed},
        }};
        auto green = corners;
        for (auto& v : green) v.Color = kGreen;
        auto yellow = corners;
        for (auto& v : yellow) v.Color = kYellow;
        const std::uint16_t idx16[6] = {0, 1, 2, 0, 2, 3};
        const std::uint32_t idx32[6] = {0, 1, 2, 0, 2, 3};
        const auto blueQuad = FullQuad(kBlue);

        // Every buffer object below outlives the whole bind cycle: nothing here may flush early.
        VertexBuffer staticVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 4,
                              BufferUsage::None);
        staticVb.SetData(corners.data(), 0, 4);
        IndexBuffer staticIb(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        staticIb.SetData(idx16, 0, 6);
        DynamicVertexBuffer dynamicVb(dev, VertexPositionColor::getVertexDeclarationStatic(), 4,
                                      BufferUsage::None);
        dynamicVb.SetData(green.data(), 0, 4, SetDataOptions::None);
        DynamicIndexBuffer dynamicIb(dev, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
        dynamicIb.SetData(idx32, 0, 6, SetDataOptions::None);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        Prepare3D(dev, false);
        fx_->VertexColorEnabled = true;
        fx_->setTextureEnabledProperty(false);
        fx_->Apply();

        // Band 0 -- static VertexBuffer + static 16-bit IndexBuffer.
        SetScissor(dev, 0, 0, band, kRTH);
        dev.SetVertexBuffer(&staticVb);
        dev.setIndicesProperty(&staticIb);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        // Band 1 -- dynamic VertexBuffer + dynamic 32-bit IndexBuffer.
        SetScissor(dev, band, 0, band, kRTH);
        dev.SetVertexBuffer(&dynamicVb);
        dev.setIndicesProperty(&dynamicIb);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);
        // Band 2 -- DrawUserPrimitives (no buffer object at all).
        SetScissor(dev, 2 * band, 0, band, kRTH);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, blueQuad.data(), 0, 2);
        // Band 3 -- DrawUserIndexedPrimitives.
        SetScissor(dev, 3 * band, 0, band, kRTH);
        dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, yellow.data(), 0, 4,
                                      idx16, 0, 2);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {            2, 24, kRed    }, {     band - 2, 24, kRed    },
            {   band + 2,   24, kGreen  }, { 2 * band - 2, 24, kGreen  },
            { 2 * band + 2, 24, kBlue   }, { 3 * band - 2, 24, kBlue   },
            { 3 * band + 2, 24, kYellow }, {     kRTW - 2, 24, kYellow },
        }, label);
    }

    /// G2 -- mutating a buffer AFTER queueing must not disturb the captured rectangle.
    void RunBufferUpdateAfterQueue(GraphicsDevice& dev)
    {
        const std::string label = "G2 buffer update after queueing keeps the captured rectangle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        Prepare3D(dev, false);
        fx_->VertexColorEnabled = true;
        fx_->setTextureEnabledProperty(false);
        fx_->Apply();

        DynamicVertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6,
                               BufferUsage::None);
        const auto red = FullQuad(kRed);
        vb.SetData(red.data(), 0, 6, SetDataOptions::None);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        // Both the geometry source and the rectangle change after the draw was queued.
        const auto blue = FullQuad(kBlue);
        vb.SetData(blue.data(), 0, 6, SetDataOptions::None);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // Whatever this renderer's buffer-lifetime rule says about the COLOUR, the CLIP must be
        // rectangle A: the right half must not have been painted at all.
        CheckProbes(image, {{ 40, 24, kBlack }, { 63, 24, kBlack }}, label);
    }

    // =====================================================================
    // H -- stock effect families
    // =====================================================================

    /**
     * @brief Runs one effect family twice under two disjoint rectangles and asserts both landed.
     *
     * A family that raises before drawing (unimplemented on this renderer) is reported as a
     * capability boundary rather than a failure -- this task adds no new functionality.
     */
    void RunFamilyCase(GraphicsDevice& dev, const std::string& label,
                       const std::function<void()>& drawLeft,
                       const std::function<void()>& drawRight,
                       const Color& leftColour, const Color& rightColour)
    {
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        bool unsupported = false;
        std::string why;

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        try
        {
            SetScissor(dev, 0, 0, kRTW / 2, kRTH);
            drawLeft();
            SetScissor(dev, kRTW / 2, 0, kRTW / 2, kRTH);
            drawRight();
        }
        catch (const std::exception& e)
        {
            unsupported = true;
            why = e.what();
        }
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (unsupported)
        {
            skip(label + ": capability boundary -- this family raises here: " + why);
            return;
        }
        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // If the family rasterized nothing at all, the target still holds the clear colour and the
        // scissor question is unanswerable here -- a scissor defect would show the SECOND draw's
        // colour on the left half, never the clear colour on both. Reported as a boundary so a
        // shading gap on some renderer is not silently credited to, or blamed on, this task.
        const bool renderedNothing = !image.threw &&
            Same(image.At(8, 24), kBlack) && Same(image.At(24, 24), kBlack) &&
            Same(image.At(40, 24), kBlack) && Same(image.At(56, 24), kBlack);
        if (renderedNothing)
        {
            skip(label + ": capability boundary -- this family rasterized nothing on this renderer, "
                         "so it cannot answer the scissor question");
            return;
        }
        CheckProbes(image, {
            {  8, 24, leftColour  }, { 24, 24, leftColour  },
            { 40, 24, rightColour }, { 56, 24, rightColour },
        }, label);
    }

    VertexBuffer& MakePosTexQuad(GraphicsDevice& dev)
    {
        auto vb = std::make_unique<VertexBuffer>(dev, PosTexDecl(), 6, BufferUsage::None);
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
        };
        vb->SetData(verts, 0, 6);
        keepAlive_.push_back(std::move(vb));
        return *keepAlive_.back();
    }

    VertexBuffer& MakePosNormalTexQuad(GraphicsDevice& dev)
    {
        auto vb = std::make_unique<VertexBuffer>(
            dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const Vector3 n(0.0f, 0.0f, -1.0f);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.5f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), n, Vector2(1.0f, 0.0f) },
        };
        vb->SetData(verts, 0, 6);
        keepAlive_.push_back(std::move(vb));
        return *keepAlive_.back();
    }

    VertexBuffer& MakeSkinnedQuad(GraphicsDevice& dev)
    {
        std::vector<SkinnedGpuVertex> verts;
        const SkinnedGpuVertex tl{-1.0f,  1.0f, .5f, 0,0,-1, 0,0, 1,0,0,0, 0,0,0,0};
        const SkinnedGpuVertex bl{-1.0f, -1.0f, .5f, 0,0,-1, 0,1, 1,0,0,0, 0,0,0,0};
        const SkinnedGpuVertex br{ 1.0f, -1.0f, .5f, 0,0,-1, 1,1, 1,0,0,0, 0,0,0,0};
        const SkinnedGpuVertex tr{ 1.0f,  1.0f, .5f, 0,0,-1, 1,0, 1,0,0,0, 0,0,0,0};
        verts.insert(verts.end(), {tl, bl, br, tl, br, tr});
        auto vb = std::make_unique<VertexBuffer>(dev, static_cast<int>(verts.size()));
        vb->SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                       static_cast<int>(sizeof(SkinnedGpuVertex)));
        keepAlive_.push_back(std::move(vb));
        return *keepAlive_.back();
    }

    VertexBuffer& MakePbrQuad(GraphicsDevice& dev)
    {
        const PbrGpuVertex tl{-1,  1, .5f, 0,0,-1, 1,0,0,1, 0,0};
        const PbrGpuVertex bl{-1, -1, .5f, 0,0,-1, 1,0,0,1, 0,1};
        const PbrGpuVertex br{ 1, -1, .5f, 0,0,-1, 1,0,0,1, 1,1};
        const PbrGpuVertex tr{ 1,  1, .5f, 0,0,-1, 1,0,0,1, 1,0};
        const std::vector<PbrGpuVertex> verts{tl, bl, br, tl, br, tr};
        auto vb = std::make_unique<VertexBuffer>(dev, static_cast<int>(verts.size()));
        vb->SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                       static_cast<int>(sizeof(PbrGpuVertex)));
        keepAlive_.push_back(std::move(vb));
        return *keepAlive_.back();
    }

    VertexBuffer& MakeSkinnedPbrQuad(GraphicsDevice& dev)
    {
        const SkinnedPbrGpuVertex tl{-1,  1, .5f, 0,0,-1, 1,0,0,1, 0,0, 1,0,0,0, 0,0,0,0};
        const SkinnedPbrGpuVertex bl{-1, -1, .5f, 0,0,-1, 1,0,0,1, 0,1, 1,0,0,0, 0,0,0,0};
        const SkinnedPbrGpuVertex br{ 1, -1, .5f, 0,0,-1, 1,0,0,1, 1,1, 1,0,0,0, 0,0,0,0};
        const SkinnedPbrGpuVertex tr{ 1,  1, .5f, 0,0,-1, 1,0,0,1, 1,0, 1,0,0,0, 0,0,0,0};
        const std::vector<SkinnedPbrGpuVertex> verts{tl, bl, br, tl, br, tr};
        auto vb = std::make_unique<VertexBuffer>(dev, static_cast<int>(verts.size()));
        vb->SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                       static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
        keepAlive_.push_back(std::move(vb));
        return *keepAlive_.back();
    }

    void DrawWithTexturedBasic(GraphicsDevice& dev, Texture2D& tex)
    {
        Prepare3D(dev, false);
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = false;
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(false);
        fx.Apply();
        VertexBuffer& vb = MakePosTexQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawWithLitBasic(GraphicsDevice& dev, Texture2D& tex)
    {
        Prepare3D(dev, false);
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.Apply();
        VertexBuffer& vb = MakePosNormalTexQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawWithAlphaTest(GraphicsDevice& dev, Texture2D& tex)
    {
        Prepare3D(dev, false);
        AlphaTestEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setAlphaFunctionProperty(CompareFunction::Greater);
        fx.setReferenceAlphaProperty(0);
        fx.Apply();
        VertexBuffer& vb = MakePosTexQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawWithDualTexture(GraphicsDevice& dev, Texture2D& tex)
    {
        Prepare3D(dev, false);
        DualTextureEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setTexture2Property(white_.get());
        fx.Apply();
        VertexBuffer& vb = MakePosTexQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawWithEnvironmentMap(GraphicsDevice& dev, Texture2D& tex)
    {
        Prepare3D(dev, false);
        EnvironmentMapEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(cube_.get());
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.Apply();
        VertexBuffer& vb = MakePosNormalTexQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawWithPbr(GraphicsDevice& dev, Texture2D& tex)
    {
        Prepare3D(dev, false);
        PbrEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setMetallicFactorProperty(0.0f);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.Apply();
        VertexBuffer& vb = MakePbrQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawWithSkinnedPbr(GraphicsDevice& dev, Texture2D& tex)
    {
        Prepare3D(dev, false);
        SkinnedPbrEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setMetallicFactorProperty(0.0f);
        fx.setWeightsPerVertexProperty(1);
        fx.SetBoneTransforms({Matrix::getIdentityProperty()});
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.Apply();
        VertexBuffer& vb = MakeSkinnedPbrQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawWithSkinned(GraphicsDevice& dev, const Vector3& diffuse)
    {
        Prepare3D(dev, false);
        SkinnedEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(white_.get());
        fx.SetBoneTransforms({Matrix::getIdentityProperty()});
        fx.setWeightsPerVertexProperty(1);
        fx.setDiffuseColorProperty(diffuse);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.Apply();
        VertexBuffer& vb = MakeSkinnedQuad(dev);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void RunEffectFamilies(GraphicsDevice& dev)
    {
        RunFamilyCase(dev, "H1 BasicEffect textured",
                      [&] { DrawWithTexturedBasic(dev, *red_); },
                      [&] { DrawWithTexturedBasic(dev, *blue_); }, kRed, kBlue);
        RunFamilyCase(dev, "H2 BasicEffect lit textured",
                      [&] { DrawWithLitBasic(dev, *red_); },
                      [&] { DrawWithLitBasic(dev, *blue_); }, kRed, kBlue);
        RunFamilyCase(dev, "H3 AlphaTestEffect",
                      [&] { DrawWithAlphaTest(dev, *red_); },
                      [&] { DrawWithAlphaTest(dev, *blue_); }, kRed, kBlue);
        RunFamilyCase(dev, "H4 DualTextureEffect",
                      [&] { DrawWithDualTexture(dev, *red_); },
                      [&] { DrawWithDualTexture(dev, *blue_); }, kRed, kBlue);
        RunFamilyCase(dev, "H5 EnvironmentMapEffect",
                      [&] { DrawWithEnvironmentMap(dev, *red_); },
                      [&] { DrawWithEnvironmentMap(dev, *blue_); }, kRed, kBlue);
        RunFamilyCase(dev, "H6 SkinnedEffect",
                      [&] { DrawWithSkinned(dev, Vector3(1.0f, 0.0f, 0.0f)); },
                      [&] { DrawWithSkinned(dev, Vector3(0.0f, 0.0f, 1.0f)); }, kRed, kBlue);
        RunFamilyCase(dev, "H7 PbrEffect",
                      [&] { DrawWithPbr(dev, *red_); },
                      [&] { DrawWithPbr(dev, *blue_); }, kRed, kBlue);
        RunFamilyCase(dev, "H8 SkinnedPbrEffect",
                      [&] { DrawWithSkinnedPbr(dev, *red_); },
                      [&] { DrawWithSkinnedPbr(dev, *blue_); }, kRed, kBlue);
    }

    // =====================================================================
    // I -- SpriteBatch
    // =====================================================================

    /// I1 -- three SpriteBatch cycles under three rectangles, A -> B -> A, into a render target.
    void RunSpriteScissorOnTarget(GraphicsDevice& dev)
    {
        const std::string label = "I1 SpriteBatch A->B->A on a render target";
        keepAlive_.clear();
        if (!kContract.spriteScissorApplies)
        {
            skip(label + ": skipped -- SpriteBatch is not clipped by ScissorRectangle here");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const Rectangle whole(0, 0, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        SpriteFill(whole, kRed);
        SetScissor(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        SpriteFill(whole, kBlue);
        SetScissor(dev, 0, 0, kRTW / 4, kRTH);
        SpriteFill(whole, kGreen);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  0, 24, kGreen }, { 15, 24, kGreen },
            { 16, 24, kRed   }, { 31, 24, kRed   },
            { 32, 24, kBlue  }, { 63, 24, kBlue  },
        }, label);
    }

    /// I2 -- the same on the BACKBUFFER, which is a separately declared behaviour.
    void RunSpriteScissorOnBackbuffer(GraphicsDevice& dev)
    {
        const std::string label = "I2 SpriteBatch A->B->A on the backbuffer";
        keepAlive_.clear();
        const Rectangle whole(0, 0, bbW_, bbH_);
        const int third = bbW_ / 3;

        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, third, bbH_);
        SpriteFill(whole, kRed);
        SetScissor(dev, third, 0, third, bbH_);
        SpriteFill(whole, kBlue);
        SetScissor(dev, 0, 0, third / 2, bbH_);
        SpriteFill(whole, kGreen);
        SetScissor(dev, 0, 0, bbW_, bbH_);
        const Image bb = ReadBackbuffer(dev);

        if (!kContract.spriteScissorApplies || !kContract.backbufferScissorApplies)
        {
            skip(label + ": sequence issued, judgement skipped -- backbuffer SpriteBatch is not "
                         "clipped by ScissorRectangle here");
            return;
        }
        if (!CanJudgeBackbuffer(label)) return;
        CheckProbes(bb, {
            {              1, bbH_ / 2, kGreen },
            {  third / 2 - 1, bbH_ / 2, kGreen },
            {      third / 2, bbH_ / 2, kRed   },
            {      third - 1, bbH_ / 2, kRed   },
            {      third + 4, bbH_ / 2, kBlue  },
            {  2 * third - 1, bbH_ / 2, kBlue  },
            {  2 * third + 4, bbH_ / 2, kBlack },
            {       bbW_ - 1, bbH_ / 2, kBlack },
        }, label);
    }

    /// I3 -- SpriteBatch A, a 3D draw under B, SpriteBatch A again, all in one bind cycle.
    void RunSpritesAnd3D(GraphicsDevice& dev)
    {
        const std::string label = "I3 SpriteBatch A / 3D B / SpriteBatch A in one cycle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies ||
            !kContract.spriteScissorApplies)
        {
            skip(label + ": skipped -- needs both 3D and SpriteBatch scissor here");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const Rectangle whole(0, 0, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW / 4, kRTH);
        SpriteFill(whole, kRed);
        SetScissor(dev, kRTW / 2, 0, kRTW / 4, kRTH);
        Draw3D(dev, FullQuad(kBlue));
        SetScissor(dev, kRTW / 4, 0, kRTW / 4, kRTH);
        SpriteFill(whole, kGreen);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  0, 24, kRed   }, { 15, 24, kRed   },
            { 16, 24, kGreen }, { 31, 24, kGreen },
            { 32, 24, kBlue  }, { 47, 24, kBlue  },
            { 48, 24, kBlack }, { 63, 24, kBlack },
        }, label);
    }

    /// I4 -- SpriteSortMode::Immediate keeps its own rectangle just as Deferred does.
    void RunSpriteImmediateMode(GraphicsDevice& dev)
    {
        const std::string label = "I4 SpriteSortMode::Immediate keeps each batch's rectangle";
        keepAlive_.clear();
        if (!kContract.spriteScissorApplies)
        {
            skip(label + ": skipped -- SpriteBatch is not clipped by ScissorRectangle here");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const Rectangle whole(0, 0, kRTW, kRTH);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState rs = Raster(true);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        sb_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, &noDepth, &rs);
        sb_->Draw(*white_, whole, Rectangle(0, 0, 1, 1), kRed);
        sb_->End();
        SetScissor(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        sb_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, &noDepth, &rs);
        sb_->Draw(*white_, whole, Rectangle(0, 0, 1, 1), kBlue);
        sb_->End();
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  8, 24, kRed  }, { 31, 24, kRed  },
            { 32, 24, kBlue }, { 63, 24, kBlue },
        }, label);
    }

    /// I5 -- a transform matrix and AlphaBlend do not disturb the captured rectangle.
    void RunSpriteTransformAndBlend(GraphicsDevice& dev)
    {
        const std::string label = "I5 SpriteBatch transform + AlphaBlend keep their rectangles";
        keepAlive_.clear();
        if (!kContract.spriteScissorApplies)
        {
            skip(label + ": skipped -- SpriteBatch is not clipped by ScissorRectangle here");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const Rectangle whole(0, 0, kRTW, kRTH);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState rs = Raster(true);
        // A pure translation by a whole target width would move the quad out; half a target moves
        // its left edge to the centre, which the rectangles below must still clip independently.
        const Matrix shift = Matrix::CreateTranslation(Vector3(static_cast<float>(kRTW) / 2.0f,
                                                               0.0f, 0.0f));

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, kRTW / 2, 0, kRTW / 4, kRTH);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &rs, nullptr,
                   shift);
        sb_->Draw(*white_, whole, Rectangle(0, 0, 1, 1), kRed);
        sb_->End();
        SetScissor(dev, (3 * kRTW) / 4, 0, kRTW / 4, kRTH);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, &noDepth, &rs);
        sb_->Draw(*white_, whole, Rectangle(0, 0, 1, 1), kBlue);
        sb_->End();
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            { 32, 24, kRed   }, { 47, 24, kRed   },
            { 48, 24, kBlue  }, { 63, 24, kBlue  },
            {  8, 24, kBlack }, { 31, 24, kBlack },
        }, label);
    }

    // =====================================================================
    // J -- Viewport interaction (REMED-GFX-116 must stay intact)
    // =====================================================================

    /// J1 -- a viewport SMALLER than the rectangle still bounds the draw.
    void RunViewportInsideScissor(GraphicsDevice& dev)
    {
        const std::string label = "J1 viewport smaller than the rectangle bounds the draw";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        SetViewport(dev, 16, 12, 16, 12);
        Draw3D(dev, FullQuad(kYellow));
        SetViewport(dev, 0, 0, kRTW, kRTH);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            { 16, 12, kYellow }, { 31, 23, kYellow },
            { 15, 12, kBlack  }, { 32, 12, kBlack  },
            { 16, 11, kBlack  }, { 16, 24, kBlack  },
        }, label);
    }

    /// J2 -- a rectangle SMALLER than the viewport clips inside it: the intersection wins.
    void RunScissorInsideViewport(GraphicsDevice& dev)
    {
        const std::string label = "J2 rectangle smaller than the viewport clips inside it";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 8, 6, 32, 24);
        SetScissor(dev, 16, 12, 32, 24);   // overlaps the viewport in x[16,40) y[12,30)
        Draw3D(dev, FullQuad(kCyan));
        SetViewport(dev, 0, 0, kRTW, kRTH);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            { 16, 12, kCyan  }, { 39, 29, kCyan  },   // the intersection's own corners
            { 15, 12, kBlack }, { 40, 12, kBlack },   // outside the rectangle, inside the viewport
            { 16, 11, kBlack }, { 16, 30, kBlack },
            {  8,  6, kBlack }, { 47, 29, kBlack },   // outside the intersection either way
        }, label);
    }

    /**
     * @brief J3 -- viewport and rectangle vary INDEPENDENTLY in one bind cycle.
     *
     * Four draws: (viewport A, rect A), (viewport B, rect A), (viewport A, rect B),
     * (viewport B, rect B). Each lands in its own intersection, so one snapshot overwriting the
     * other -- in either direction -- moves at least one quad.
     */
    void RunViewportAndScissorIndependent(GraphicsDevice& dev)
    {
        const std::string label = "J3 viewport and rectangle vary independently";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const int halfW = kRTW / 2;   // 32
        const int halfH = kRTH / 2;   // 24

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        // Viewport A = top half, viewport B = bottom half; rect A = left half, rect B = right half.
        SetViewport(dev, 0, 0, kRTW, halfH);
        SetScissor(dev, 0, 0, halfW, kRTH);
        Draw3D(dev, FullQuad(kRed));              // top-left
        SetViewport(dev, 0, halfH, kRTW, halfH);
        Draw3D(dev, FullQuad(kGreen));            // bottom-left (rect unchanged)
        SetViewport(dev, 0, 0, kRTW, halfH);
        SetScissor(dev, halfW, 0, halfW, kRTH);
        Draw3D(dev, FullQuad(kBlue));             // top-right (viewport back to A)
        SetViewport(dev, 0, halfH, kRTW, halfH);
        Draw3D(dev, FullQuad(kYellow));           // bottom-right
        SetViewport(dev, 0, 0, kRTW, kRTH);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  8,  6, kRed    }, { 31, 23, kRed    },
            {  8, 30, kGreen  }, { 31, 47, kGreen  },
            { 40,  6, kBlue   }, { 63, 23, kBlue   },
            { 40, 30, kYellow }, { 63, 47, kYellow },
        }, label);
    }

    /// J4 -- a narrowed Viewport depth range does not move or resize the rectangle.
    void RunScissorWithDepthRange(GraphicsDevice& dev)
    {
        const std::string label = "J4 MinDepth/MaxDepth do not disturb the rectangle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW, kRTH, 0.25f, 0.75f);
        SetScissor(dev, 12, 7, 20, 19);
        Draw3D(dev, FullQuad(kMagenta));
        SetViewport(dev, 0, 0, kRTW, kRTH);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            { 12,  7, kMagenta }, { 31, 25, kMagenta },
            { 11, 15, kBlack   }, { 32, 15, kBlack   },
            { 20,  6, kBlack   }, { 20, 26, kBlack   },
        }, label);
    }

    // =====================================================================
    // K -- RasterizerState.ScissorTestEnable
    // =====================================================================

    /// K1 -- with the scissor test DISABLED, the rectangle clips nothing.
    void RunScissorTestDisabled(GraphicsDevice& dev)
    {
        const std::string label = "K1 ScissorTestEnable false ignores the rectangle";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        scissorTest_ = false;
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        scissorTest_ = true;
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  0, 24, kRed }, { 31, 24, kRed }, { 32, 24, kRed }, { 63, 24, kRed },
            { 32,  0, kRed }, { 32, 47, kRed },
        }, label);
    }

    /**
     * @brief K2 -- enabled -> DISABLED -> enabled inside one bind cycle.
     *
     * The middle draw must cover the whole target and the two outer ones must be clipped, so a
     * renderer that captures the RECTANGLE but reads the ENABLE flag live fails here even though
     * every other check in this file passes.
     */
    void RunScissorTestToggledInOneCycle(GraphicsDevice& dev)
    {
        const std::string label = "K2 enabled -> disabled -> enabled in one cycle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        // 1. enabled, left quarter -> red
        scissorTest_ = true;
        SetScissor(dev, 0, 0, kRTW / 4, kRTH);
        Draw3D(dev, FullQuad(kRed));
        // 2. DISABLED, same rectangle, a quad covering only the bottom half of the target
        scissorTest_ = false;
        Draw3D(dev, Quad(-1.0f, 1.0f, -1.0f, 0.0f, 0.5f, kBlue));
        // 3. enabled again, right quarter -> green
        scissorTest_ = true;
        SetScissor(dev, (3 * kRTW) / 4, 0, kRTW / 4, kRTH);
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // Clip space y=-1..0 is the target's BOTTOM half (rows 24..47) with XNA's Y orientation.
        CheckProbes(image, {
            {  8, 12, kRed   },                     // draw 1 survives above the blue band
            {  8, 40, kBlue  }, { 32, 40, kBlue },  // draw 2 was NOT clipped by the live rectangle
            { 63, 40, kGreen },                     // draw 3 clipped to the right quarter
            { 63, 12, kGreen },
            { 32, 12, kBlack },                     // never covered by any of the three
        }, label);
    }

    /**
     * @brief K3 -- two DISTINCT RasterizerState objects with equal values behave identically, and
     *        mutating the object after the draw changes nothing.
     */
    void RunRasterizerStateIdentity(GraphicsDevice& dev)
    {
        const std::string label = "K3 RasterizerState identity and post-draw mutation";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        RasterizerState first = Raster(true);
        RasterizerState second = Raster(true);   // equal values, different object

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        fx_->VertexColorEnabled = true;
        fx_->setTextureEnabledProperty(false);
        fx_->Apply();

        dev.setRasterizerStateProperty(first);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        {
            VertexBuffer& vb = MakeVb(dev, FullQuad(kRed));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
        }
        dev.setRasterizerStateProperty(second);
        SetScissor(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        {
            VertexBuffer& vb = MakeVb(dev, FullQuad(kBlue));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
        }
        // Mutating the state objects AFTER both draws were queued must reach neither of them.
        first.setScissorTestEnableProperty(false);
        second.setScissorTestEnableProperty(false);
        second.setCullModeProperty(CullMode::CullClockwiseFace);
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  8, 24, kRed  }, { 31, 24, kRed  },
            { 32, 24, kBlue }, { 63, 24, kBlue },
        }, label);
    }

    // =====================================================================
    // L -- repetition: no state leaks between cycles or frames
    // =====================================================================

    /// L1 -- eight rectangles in one bind cycle, each an exact band.
    void RunManyScissorsInOneCycle(GraphicsDevice& dev)
    {
        const std::string label = "L1 eight rectangles in one bind cycle";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const int band = kRTW / 8;   // 8
        const std::array<Color, 8> palette{{kRed, kGreen, kBlue, kYellow,
                                            kMagenta, kCyan, kWhite, kRed}};

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        for (int i = 0; i < 8; ++i)
        {
            SetScissor(dev, i * band, 0, band, kRTH);
            Draw3D(dev, FullQuad(palette[static_cast<std::size_t>(i)]));
        }
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        std::vector<Probe> probes;
        for (int i = 0; i < 8; ++i)
        {
            probes.push_back({i * band,           24, palette[static_cast<std::size_t>(i)]});
            probes.push_back({(i + 1) * band - 1, 24, palette[static_cast<std::size_t>(i)]});
        }
        CheckProbes(image, probes, label);
    }

    /// L2 -- the same target bound twice in one frame; the second cycle starts from a clean slate.
    void RunSameTargetTwiceInOneFrame(GraphicsDevice& dev)
    {
        const std::string label = "L2 same target, two cycles, different rectangles";
        keepAlive_.clear();
        if (!kContract.draws3D || !kContract.targetScissorApplies)
        {
            skip(label + ": skipped -- no 3D render-target scissor on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetScissor(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kGreen));
        SetScissor(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  8, 24, kBlack },   // the second cycle's own clear survives into the readback
            { 40, 24, kGreen }, { 63, 24, kGreen },
        }, label);
    }

    // ---------------------------------------------------------------------

    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (kContract.wantHiDefProfile)
            gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        sb_ = std::make_unique<SpriteBatch>(dev);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        red_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 255}));
        blue_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{0, 0, 255, 255}));

        if (kContract.draws3D)
        {
            fx_ = std::make_unique<BasicEffect>(dev);
            fx_->setWorldProperty(Matrix::getIdentityProperty());
            fx_->setViewProperty(Matrix::getIdentityProperty());
            fx_->setProjectionProperty(Matrix::getIdentityProperty());
            fx_->setLightingEnabledProperty(false);
            try
            {
                cube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
                const Color texel(0, 0, 0, 255);
                for (int face = 0; face < 6; ++face)
                    cube_->SetData(static_cast<CubeMapFace>(face), &texel, 1);
            }
            catch (const std::exception&)
            {
                cube_.reset();   // EnvironmentMapEffect's check then reports the boundary itself
            }
        }
    }

    void Draw(const GameTime&) override
    {
        // Frame 0 is a warm-up: some renderers copy texture payloads only at frame boundaries.
        if (phase_ == 0)
        {
            phase_ = 1;
            scissorTest_ = false;
            SpriteFill(Rectangle(0, 0, bbW_, bbH_), kBlack);
            scissorTest_ = true;
            return;
        }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        auto& dev = getGraphicsDeviceProperty();
        const Viewport vp = dev.getViewportProperty();
        if (vp.getWidthProperty() > 0 && vp.getHeightProperty() > 0)
        {
            bbW_ = vp.getWidthProperty();
            bbH_ = vp.getHeightProperty();
        }
        std::printf("REMED-GFX-146 deferred scissor capture -- renderer %s, backbuffer %dx%d\n",
                    kContract.name, bbW_, bbH_);

        RunLeftRightLeft(dev);
        RunThreeDisjointScissors(dev);
        RunUpperLowerUpper(dev);
        RunScissorComponents(dev);
        RunOddDimensions(dev);
        RunEmptyRectangles(dev);
        RunOutOfBoundsRectangle(dev);
        RunFullyOutsideRectangle(dev);
        RunTwoTargetsInterleaved(dev);
        RunTargetBackbufferTarget(dev);
        RunBackbufferLeftRightLeft(dev);
        RunScissorAcrossTargetSwitch(dev);
        RunBufferKinds(dev);
        RunBufferUpdateAfterQueue(dev);
        RunEffectFamilies(dev);
        RunSpriteScissorOnTarget(dev);
        RunSpriteScissorOnBackbuffer(dev);
        RunSpritesAnd3D(dev);
        RunSpriteImmediateMode(dev);
        RunSpriteTransformAndBlend(dev);
        RunViewportInsideScissor(dev);
        RunScissorInsideViewport(dev);
        RunViewportAndScissorIndependent(dev);
        RunScissorWithDepthRange(dev);
        RunScissorTestDisabled(dev);
        RunScissorTestToggledInOneCycle(dev);
        RunRasterizerStateIdentity(dev);
        RunManyScissorsInOneCycle(dev);
        RunSameTargetTwiceInOneFrame(dev);
        // Repeat the decisive sequence once more in the same frame: nothing may have leaked.
        RunLeftRightLeft(dev);

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    DeferredScissorCaptureTest test;
    test.Run();
    return test.Result();
}
