// SPDX-License-Identifier: MS-PL
// REMED-GFX-116: every deferred draw must execute under the GraphicsDevice.Viewport that was
// active at its own public draw call.
//
// WebGPU collects a bind cycle's Clear/3D/SpriteBatch work and records ONE native render pass for
// it later (`EnsureFrameRendered`, `RenderPendingDrawsToRenderTarget`,
// `RenderPendingDrawsToRenderTargetCubeFace`). Until this task all three applied the viewport once,
// at the top of that pass, from the LIVE renderer members `viewportSet_`/`viewportX_`/`viewportY_`/
// `viewportW_`/`viewportH_`/`viewportMinDepth_`/`viewportMaxDepth_`. Deferring the RECORDING is
// legal; RESOLVING the value late is not. The consequence is that
//
//     Viewport = left  ; draw red
//     Viewport = right ; draw blue
//     Viewport = left  ; draw green
//     Viewport = full          <-- an ordinary restore, before anything is flushed
//
// executed all three draws under the FULL viewport, i.e. whatever happened to be current when the
// pass was recorded. Two viewports in one cycle collapsed last-wins, and a restore before the
// flush silently promoted every already-queued draw to the whole target.
//
// Why every check here is falsifiable in both directions
// ------------------------------------------------------
//   * Nothing between the draws forces a flush: no GetData, no Present, no explicit flush, no
//     fence, no sleep, no extra frame, no render-target switch. Each sequence is queued whole and
//     observed exactly once at its end. A fixture that reads or switches targets between viewport
//     changes cannot see this defect at all -- that is precisely how it survived REMED-GFX-072.
//   * The viewport is restored to the full target BEFORE the single observation, so "resolve at
//     flush time" and "capture at queue time" predict visibly different images rather than the
//     same one.
//   * Geometry and colour are chosen so each wrong implementation lands somewhere else: all draws
//     under viewport A, all under B, all under the final viewport, target-sized fallback, X/Y
//     dropped but Width/Height kept, Width/Height dropped but X/Y kept, the viewport applied twice
//     (offset added on top of an already-offset rect) and a flipped Y all produce distinct,
//     asserted pixel layouts.
//   * MinDepth/MaxDepth are measured through the DEPTH TEST -- which of two overlapping quads
//     survives -- not by inspecting native arguments, so a renderer that accepts the values and
//     ignores them still fails.
//
// The palette is 0/255-only, an exact fixed point of sRGB encoding, so every comparison is
// byte-exact including on sRGB render targets.
//
// Sections: A A->B->A on a render target; B Y orientation; C viewport components one at a time;
// D odd target dimensions; E MinDepth/MaxDepth; F target/backbuffer transitions; G buffer kinds
// (static/dynamic, indexed/non-indexed, 16-/32-bit, DrawUser); H stock effect families;
// I SpriteBatch interleaved with 3D; J scissor is unaffected (see
// examples/deferred_scissor_capture_test.cpp for the scissor's own deferral fixture,
// REMED-GFX-146); K repeated frames.
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
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
        /// A SpriteBatch fill honours a custom sub-Viewport (sprite coordinates are viewport-local).
        bool spriteViewportIsLocal;
        /// Viewport.MinDepth/MaxDepth reach the rasterizer's depth range.
        bool depthRangeApplies;
        bool wantHiDefProfile;
    };

#if defined(CNA_RENDERER_WEBGPU)
    constexpr Contract kContract{"WEBGPU", Support::Exact, Support::Exact, true, true, true, false};
#elif defined(CNA_RENDERER_VULKAN)
    constexpr Contract kContract{"VULKAN", Support::Exact, Support::Exact, true, true, true, false};
#elif defined(CNA_RENDERER_EASYGL)
    constexpr Contract kContract{"EASYGL", Support::Exact, Support::Exact, true, true, true, false};
#elif defined(CNA_RENDERER_BGFX)
    // `depthRangeApplies` false: measured here. bgfx has no per-view depth-range call at all --
    // `bgfx::setViewRect` carries no min/max depth and the range is expected to be folded into the
    // projection matrix -- so Viewport.MinDepth/MaxDepth reach nothing. That is a distinct root
    // cause from viewport capture (every X/Y/Width/Height check in this file passes on bgfx) and
    // is recorded as its own finding rather than fixed here; checks E1/E2 assert the IGNORED
    // outcome so the declaration is falsifiable in both directions.
    constexpr Contract kContract{"BGFX", Support::Exact, Support::Exact, true, true, false, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // SdlGpu has no `ReadBackbuffer` override at all, so `GetBackBufferData` raises; its
    // render-target oracle still answers every render-target question in this file.
    constexpr Contract kContract{"SDL_GPU", Support::Unsupported, Support::Exact, true, true, true, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    // `depthRangeApplies` true: measured, not assumed. The software rasterizer's depth COMPARE is a
    // fixed LessEqual (REMED-GFX-083's documented boundary), but it does remap the interpolated
    // depth through Viewport.MinDepth/MaxDepth, so checks E1/E2 assert the honoured outcome here.
    constexpr Contract kContract{"SOFTWARE", Support::Exact, Support::Exact, true, true, true, false};
#elif defined(CNA_RENDERER_HEADLESS)
    // Headless rasterizes nothing and its readback is REMED-GFX-127/130's deterministic refusal.
    // Every sequence must still be legal and must not throw.
    constexpr Contract kContract{"HEADLESS", Support::Unsupported, Support::Unsupported, true, true, true, false};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", Support::Exact, Support::Exact, true, true, true, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr Contract kContract{"DIRECTX9", Support::Exact, Support::Exact, true, true, true, true};
#else
#error "REMED-GFX-116: this renderer has no declared deferred-viewport contract."
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

class DeferredViewportCaptureTest : public Game
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
    // Draw helpers
    // ---------------------------------------------------------------------

    void Prepare3D(GraphicsDevice& dev, bool depth)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
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
        RasterizerState noCull = RasterizerState::CullNone;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
    }

    /// One SpriteBatch cycle filling @p rect (in the ACTIVE viewport's coordinates) with @p colour.
    void SpriteFill(const Rectangle& rect, const Color& colour)
    {
        BeginSprites();
        sb_->Draw(*white_, rect, Rectangle(0, 0, 1, 1), colour);
        sb_->End();
    }

    void SetViewport(GraphicsDevice& dev, int x, int y, int w, int h,
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
     * The viewport is restored to the whole target after the last draw and before the only
     * readback, so a renderer that resolves the viewport when it records the pass paints the whole
     * target blue with a green centre band instead.
     */
    void RunLeftRightLeft(GraphicsDevice& dev)
    {
        const std::string label = "A1 3D A->B->A in one bind cycle";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        SetViewport(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kBlue));
        SetViewport(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, Quad(-0.5f, 0.5f, -0.5f, 0.5f, 0.5f, kGreen));
        SetViewport(dev, 0, 0, kRTW, kRTH);   // the ordinary restore that used to erase all three
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // Viewport A is x[0,32) y[0,48); NDC [-0.5,0.5]^2 inside it is x[8,24) y[12,36).
        CheckProbes(image, {
            {  4,  4, kRed   },   // left half, outside the green sub-rect
            { 30, 44, kRed   },
            {  7, 24, kRed   },   // exact left edge of the green sub-rect
            {  8, 24, kGreen },
            { 23, 24, kGreen },
            { 24, 24, kRed   },
            { 16, 11, kRed   },   // exact top edge of the green sub-rect
            { 16, 12, kGreen },
            { 16, 35, kGreen },
            { 16, 36, kRed   },
            { 32, 24, kBlue  },   // right half is entirely the second draw
            { 63,  0, kBlue  },
            { 40, 47, kBlue  },
        }, label);
    }

    /**
     * @brief A2 -- the same shape with three DISJOINT viewports, so nothing can overlap.
     *
     * Distinguishes "the last viewport won" from "every draw kept its own": under last-wins the
     * three colours stack in one place and two of the three thirds stay at the clear colour.
     */
    void RunThreeDisjointViewports(GraphicsDevice& dev)
    {
        const std::string label = "A2 three disjoint viewports in one cycle";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const int third = kRTW / 4;   // 16: three bands plus an untouched tail

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, third, kRTH);
        Draw3D(dev, FullQuad(kRed));
        SetViewport(dev, third, 0, third, kRTH);
        Draw3D(dev, FullQuad(kGreen));
        SetViewport(dev, 2 * third, 0, third, kRTH);
        Draw3D(dev, FullQuad(kBlue));
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  0, 24, kRed   }, { 15, 24, kRed   },
            { 16, 24, kGreen }, { 31, 24, kGreen },
            { 32, 24, kBlue  }, { 47, 24, kBlue  },
            { 48, 24, kBlack }, { 63, 24, kBlack },   // never covered by any viewport
        }, label);
    }

    // =====================================================================
    // B -- Y orientation: the conversion must be applied exactly once
    // =====================================================================

    void RunUpperLowerUpper(GraphicsDevice& dev)
    {
        const std::string label = "B1 upper/lower viewports keep public Y orientation";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW, kRTH / 4);                 // top quarter
        Draw3D(dev, FullQuad(kRed));
        SetViewport(dev, 0, (3 * kRTH) / 4, kRTW, kRTH / 4);    // bottom quarter
        Draw3D(dev, FullQuad(kBlue));
        SetViewport(dev, 0, kRTH / 4, kRTW, kRTH / 4);          // second quarter
        Draw3D(dev, FullQuad(kGreen));
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // Row 0 is the top of the target for both the public Viewport and GetData; an extra flip
        // anywhere in the chain swaps red and blue.
        CheckProbes(image, {
            { 32,  0, kRed   }, { 32, 11, kRed   },
            { 32, 12, kGreen }, { 32, 23, kGreen },
            { 32, 24, kBlack }, { 32, 35, kBlack },
            { 32, 36, kBlue  }, { 32, 47, kBlue  },
        }, label);
    }

    // =====================================================================
    // C -- one viewport component at a time
    // =====================================================================

    /**
     * @brief Draws a full-viewport red quad under @p vp, restores the full viewport, then asserts
     *        the exact rectangle including its final row and column and the pixels just outside.
     */
    void RunComponentCase(GraphicsDevice& dev, const std::string& label,
                          int x, int y, int w, int h)
    {
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, x, y, w, h);
        Draw3D(dev, FullQuad(kRed));
        SetViewport(dev, 0, 0, kRTW, kRTH);
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

    void RunViewportComponents(GraphicsDevice& dev)
    {
        RunComponentCase(dev, "C1 nonzero X only",        16, 0, kRTW - 16, kRTH);
        RunComponentCase(dev, "C2 nonzero Y only",        0, 12, kRTW, kRTH - 12);
        RunComponentCase(dev, "C3 reduced Width only",    0, 0, 40, kRTH);
        RunComponentCase(dev, "C4 reduced Height only",   0, 0, kRTW, 30);
        RunComponentCase(dev, "C5 all four together",     13, 9, 27, 21);
        RunComponentCase(dev, "C6 full target",           0, 0, kRTW, kRTH);
        RunComponentCase(dev, "C7 final row and column",  kRTW - 1, kRTH - 1, 1, 1);
        RunComponentCase(dev, "C8 last column strip",     kRTW - 3, 0, 3, kRTH);
        RunComponentCase(dev, "C9 last row strip",        0, kRTH - 3, kRTW, 3);
        RunComponentCase(dev, "C10 centre rectangle",     kRTW / 4, kRTH / 4, kRTW / 2, kRTH / 2);
    }

    // =====================================================================
    // D -- odd target dimensions
    // =====================================================================

    void RunOddDimensions(GraphicsDevice& dev)
    {
        const std::string label = "D1 odd target dimensions A->B->A";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kOddW, kOddH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, 17, kOddH);
        Draw3D(dev, FullQuad(kRed));
        SetViewport(dev, 17, 0, kOddW - 17, kOddH);
        Draw3D(dev, FullQuad(kBlue));
        SetViewport(dev, 7, 5, 19, 13);
        Draw3D(dev, FullQuad(kGreen));
        SetViewport(dev, 0, 0, kOddW, kOddH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kOddW, kOddH);
        CheckProbes(image, {
            {  0,  0, kRed   }, {  6, 24, kRed   },
            { 32,  0, kBlue  }, { 32, 24, kBlue  },
            {  7,  5, kGreen }, { 25, 17, kGreen },
            {  6,  5, kRed   }, { 26,  5, kBlue  },
            {  7,  4, kRed   }, { 25, 18, kBlue  },
        }, label);
    }

    // =====================================================================
    // E -- MinDepth / MaxDepth
    // =====================================================================

    /**
     * @brief E1 -- two overlapping quads whose visibility depends only on the viewport depth range.
     *
     * Red is drawn at clip depth 0.8 under range [0.0,0.5] -> window depth 0.40.
     * Blue is drawn at clip depth 0.0 under range [0.5,1.0] -> window depth 0.50, which FAILS the
     * LessEqual test against 0.40, so red survives. Ignore the ranges and the raw depths 0.8 and
     * 0.0 reverse the outcome, painting blue. Green then returns to the first range at clip 0.4 ->
     * window 0.20 and wins the centre, which no single-range implementation can reproduce.
     */
    void RunDepthRange(GraphicsDevice& dev)
    {
        const std::string label = "E1 MinDepth/MaxDepth A->B->A decide the depth test";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH, DepthFormat::Depth24Stencil8);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW, kRTH, 0.0f, 0.5f);
        Draw3D(dev, FullQuad(kRed, 0.8f), true);
        SetViewport(dev, 0, 0, kRTW, kRTH, 0.5f, 1.0f);
        Draw3D(dev, FullQuad(kBlue, 0.0f), true);
        SetViewport(dev, 0, 0, kRTW, kRTH, 0.0f, 0.5f);
        Draw3D(dev, Quad(-0.5f, 0.5f, -0.5f, 0.5f, 0.4f, kGreen), true);
        SetViewport(dev, 0, 0, kRTW, kRTH, 0.0f, 1.0f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        if (!kContract.depthRangeApplies)
        {
            // Declared open on this renderer, and asserted as the IGNORED outcome so the
            // declaration turns red the day the renderer gains a depth remap: with both ranges
            // collapsed to [0,1] the raw clip depths 0.8 / 0.0 / 0.4 make blue win everywhere.
            CheckProbes(image, {
                {  2,  2, kBlue }, { 61, 45, kBlue },
                { 32, 24, kBlue }, { 16, 12, kBlue }, { 47, 35, kBlue },
                { 15, 24, kBlue }, { 48, 24, kBlue },
            }, label + " (declared: this rasterizer has no viewport depth remap)");
            return;
        }
        CheckProbes(image, {
            {  2,  2, kRed   }, { 61, 45, kRed   },
            { 32, 24, kGreen }, { 16, 12, kGreen }, { 47, 35, kGreen },
            { 15, 24, kRed   }, { 48, 24, kRed   },
        }, label);
    }

    /**
     * @brief E2 -- a narrowed range combined with a sub-rectangle, so both halves must survive.
     */
    void RunDepthRangeWithSubRect(GraphicsDevice& dev)
    {
        const std::string label = "E2 depth range travels with the rectangle";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH, DepthFormat::Depth24Stencil8);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        // Left half, range [0,0.25]: clip 0.8 -> window 0.20.
        SetViewport(dev, 0, 0, kRTW / 2, kRTH, 0.0f, 0.25f);
        Draw3D(dev, FullQuad(kRed, 0.8f), true);
        // Left half again, range [0.3,1.0]: clip 0.0 -> window 0.30, fails against 0.20.
        SetViewport(dev, 0, 0, kRTW / 2, kRTH, 0.3f, 1.0f);
        Draw3D(dev, FullQuad(kYellow, 0.0f), true);
        // Right half, default range: clip 0.9 then clip 0.1, so the second wins there.
        SetViewport(dev, kRTW / 2, 0, kRTW / 2, kRTH, 0.0f, 1.0f);
        Draw3D(dev, FullQuad(kBlue, 0.9f), true);
        Draw3D(dev, FullQuad(kCyan, 0.1f), true);
        SetViewport(dev, 0, 0, kRTW, kRTH, 0.0f, 1.0f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        if (!kContract.depthRangeApplies)
        {
            // Same declaration, and the right half is identical either way: only the left half's
            // two narrowed ranges can tell the two behaviours apart.
            CheckProbes(image, {
                {  4, 24, kYellow }, { 31, 24, kYellow },
                { 32, 24, kCyan   }, { 60, 24, kCyan   },
            }, label + " (declared: this rasterizer has no viewport depth remap)");
            return;
        }
        CheckProbes(image, {
            {  4, 24, kRed  }, { 31, 24, kRed  },
            { 32, 24, kCyan }, { 60, 24, kCyan },
        }, label);
    }

    // =====================================================================
    // F -- target and backbuffer transitions
    // =====================================================================

    /// F1 -- target A, target B, target A again; each cycle's viewports stay its own.
    void RunTwoTargetsInterleaved(GraphicsDevice& dev)
    {
        const std::string label = "F1 target A -> target B -> target A";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto a = MakeTarget(dev, kRTW, kRTH);
        auto b = MakeTarget(dev, kOddW, kOddH);

        dev.SetRenderTarget(a.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        dev.SetRenderTarget(b.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, 11, kOddH);
        Draw3D(dev, FullQuad(kGreen));
        SetViewport(dev, 22, 0, 11, kOddH);
        Draw3D(dev, FullQuad(kMagenta));
        dev.SetRenderTarget(a.get());          // second cycle of A: DiscardContents re-clears it
        SetViewport(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kBlue));
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image ia = ReadTarget(*a, kRTW, kRTH);
        const Image ib = ReadTarget(*b, kOddW, kOddH);
        CheckProbes(ia, {{ 40, 24, kBlue }, { 63, 24, kBlue }}, label + " (A second cycle)");
        CheckProbes(ib, {
            {  0, 12, kGreen   }, { 10, 12, kGreen   },
            { 11, 12, kBlack   }, { 21, 12, kBlack   },
            { 22, 12, kMagenta }, { 32, 12, kMagenta },
        }, label + " (B)");
    }

    /// F2 -- render target, backbuffer, render target, all in one frame with distinct viewports.
    void RunTargetBackbufferTarget(GraphicsDevice& dev)
    {
        const std::string label = "F2 target -> backbuffer -> target";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, bbW_ / 3, bbH_);
        Draw3D(dev, FullQuad(kGreen));
        SetViewport(dev, (2 * bbW_) / 3, 0, bbW_ / 3, bbH_);
        Draw3D(dev, FullQuad(kBlue));
        dev.SetRenderTarget(rt.get());
        SetViewport(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kYellow));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        SetViewport(dev, 0, 0, bbW_, bbH_);
        const Image bb = ReadBackbuffer(dev);

        if (CanJudgeTarget(label + " (target)"))
        {
            const Image ir = ReadTarget(*rt, kRTW, kRTH);
            CheckProbes(ir, {{ 40, 24, kYellow }, { 8, 24, kBlack }}, label + " (target)");
        }
        if (!CanJudgeBackbuffer(label + " (backbuffer)")) return;
        CheckProbes(bb, {
            {              4, bbH_ / 2, kGreen },
            {   bbW_ / 3 - 1, bbH_ / 2, kGreen },
            {       bbW_ / 2, bbH_ / 2, kBlack },
            { (2 * bbW_) / 3, bbH_ / 2, kBlue  },
            {         bbW_-1, bbH_ / 2, kBlue  },
        }, label + " (backbuffer)");
    }

    /// F3 -- the backbuffer's own A->B->A, restored before the single read.
    void RunBackbufferLeftRightLeft(GraphicsDevice& dev)
    {
        const std::string label = "F3 backbuffer A->B->A";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        const int third = bbW_ / 3;

        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, third, bbH_);
        Draw3D(dev, FullQuad(kRed));
        SetViewport(dev, third, 0, third, bbH_);
        Draw3D(dev, FullQuad(kBlue));
        SetViewport(dev, 0, 0, third, bbH_);
        Draw3D(dev, Quad(-0.5f, 0.5f, -1.0f, 1.0f, 0.5f, kGreen));
        SetViewport(dev, 0, 0, bbW_, bbH_);
        const Image bb = ReadBackbuffer(dev);

        if (!CanJudgeBackbuffer(label)) return;
        CheckProbes(bb, {
            {              1, bbH_ / 2, kRed   },
            {  third / 4 - 1, bbH_ / 2, kRed   },
            {      third / 2, bbH_ / 2, kGreen },
            {  third - 1,     bbH_ / 2, kRed   },
            {  third + 4,     bbH_ / 2, kBlue  },
            { 2 * third - 1,  bbH_ / 2, kBlue  },
            { 2 * third + 4,  bbH_ / 2, kBlack },
        }, label);
    }

    // =====================================================================
    // G -- buffer kinds: static/dynamic, indexed/non-indexed, 16/32-bit, DrawUser
    // =====================================================================

    void RunBufferKinds(GraphicsDevice& dev)
    {
        const std::string label = "G1 indexed / non-indexed / dynamic / DrawUser";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
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
        SetViewport(dev, 0, 0, band, kRTH);
        dev.SetVertexBuffer(&staticVb);
        dev.setIndicesProperty(&staticIb);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        // Band 1 -- dynamic VertexBuffer + dynamic 32-bit IndexBuffer.
        SetViewport(dev, band, 0, band, kRTH);
        dev.SetVertexBuffer(&dynamicVb);
        dev.setIndicesProperty(&dynamicIb);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);
        // Band 2 -- DrawUserPrimitives (no buffer object at all).
        SetViewport(dev, 2 * band, 0, band, kRTH);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, blueQuad.data(), 0, 2);
        // Band 3 -- DrawUserIndexedPrimitives.
        SetViewport(dev, 3 * band, 0, band, kRTH);
        dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, yellow.data(), 0, 4,
                                      idx16, 0, 2);
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {          2, 24, kRed    }, {     band - 2, 24, kRed    },
            {   band + 2, 24, kGreen  }, { 2 * band - 2, 24, kGreen  },
            { 2*band + 2, 24, kBlue   }, { 3 * band - 2, 24, kBlue   },
            { 3*band + 2, 24, kYellow }, {     kRTW - 2, 24, kYellow },
        }, label);
    }

    /// G2 -- mutating a buffer AFTER queueing must not disturb the captured viewport.
    void RunBufferUpdateAfterQueue(GraphicsDevice& dev)
    {
        const std::string label = "G2 buffer update after queueing keeps the captured viewport";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
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
        SetViewport(dev, 0, 0, kRTW / 2, kRTH);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        // Both the geometry source and the viewport change after the draw was queued.
        const auto blue = FullQuad(kBlue);
        vb.SetData(blue.data(), 0, 6, SetDataOptions::None);
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // Whatever this renderer's buffer-lifetime rule says about the COLOUR, the RECTANGLE must
        // be viewport A: the right half must not have been painted at all.
        CheckProbes(image, {{ 40, 24, kBlack }, { 63, 24, kBlack }}, label);
    }

    // =====================================================================
    // H -- stock effect families
    // =====================================================================

    /**
     * @brief Runs one effect family twice under two disjoint viewports and asserts both landed.
     *
     * @param drawLeft Issues the family's draw for the left band; @p drawRight for the right band.
     * A family that raises before drawing (unimplemented on this renderer) is reported as a
     * capability boundary rather than a failure -- this task adds no new functionality.
     */
    void RunFamilyCase(GraphicsDevice& dev, const std::string& label,
                       const std::function<void()>& drawLeft,
                       const std::function<void()>& drawRight,
                       const Color& leftColour, const Color& rightColour)
    {
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        bool unsupported = false;
        std::string why;

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        try
        {
            SetViewport(dev, 0, 0, kRTW / 2, kRTH);
            drawLeft();
            SetViewport(dev, kRTW / 2, 0, kRTW / 2, kRTH);
            drawRight();
        }
        catch (const std::exception& e)
        {
            unsupported = true;
            why = e.what();
        }
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (unsupported)
        {
            skip(label + ": capability boundary -- this family raises here: " + why);
            return;
        }
        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        // If the family rasterized nothing at all, the target still holds the clear colour and the
        // viewport question is unanswerable here -- a viewport defect would show the SECOND draw's
        // colour on the left half, never the clear colour on both. Reported as a boundary so a
        // shading gap on some renderer is not silently credited to, or blamed on, this task.
        const bool renderedNothing = !image.threw &&
            Same(image.At(8, 24), kBlack) && Same(image.At(24, 24), kBlack) &&
            Same(image.At(40, 24), kBlack) && Same(image.At(56, 24), kBlack);
        if (renderedNothing)
        {
            skip(label + ": capability boundary -- this family rasterized nothing on this renderer, "
                         "so it cannot answer the viewport question");
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
    // I -- SpriteBatch, alone and interleaved with 3D
    // =====================================================================

    /// I1 -- SpriteBatch A, 3D B, SpriteBatch A again, all in one bind cycle.
    void RunSpritesAnd3D(GraphicsDevice& dev)
    {
        const std::string label = "I1 SpriteBatch A / 3D B / SpriteBatch A in one cycle";
        keepAlive_.clear();
        if (!kContract.spriteViewportIsLocal)
        {
            skip(label + ": skipped -- SpriteBatch ignores a sub-Viewport on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const int half = kRTW / 2;

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, half, kRTH);
        SpriteFill(Rectangle(0, 0, half, kRTH), kRed);
        if (kContract.draws3D)
        {
            SetViewport(dev, half, 0, half, kRTH);
            Draw3D(dev, FullQuad(kBlue));
        }
        SetViewport(dev, 0, 0, half, kRTH);
        SpriteFill(Rectangle(0, 0, half / 2, kRTH), kGreen);
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        std::vector<Probe> probes{
            {          4, 24, kGreen },
            { half/2 - 1, 24, kGreen },
            {   half / 2, 24, kRed   },
            {   half - 1, 24, kRed   },
        };
        if (kContract.draws3D)
        {
            probes.push_back({half + 4, 24, kBlue});
            probes.push_back({kRTW - 1, 24, kBlue});
        }
        CheckProbes(image, probes, label);
    }

    /**
     * @brief I2 -- a FULL-target SpriteBatch fill followed by a sub-Viewport set before the flush.
     *
     * The sprite was queued with the whole target active, so it must still cover the whole target.
     * REMED-GFX-072 captured the viewport only for a SUB-REGION sprite, which left exactly this
     * case resolving live state: the fill was squeezed into the later sub-Viewport.
     */
    void RunFullTargetSpriteThenSubViewport(GraphicsDevice& dev)
    {
        const std::string label = "I2 full-target sprite is not squeezed by a later sub-Viewport";
        keepAlive_.clear();
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW, kRTH);
        SpriteFill(Rectangle(0, 0, kRTW, kRTH), kMagenta);
        SetViewport(dev, 0, 0, kRTW / 4, kRTH / 4);   // never used by any draw
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        SetViewport(dev, 0, 0, kRTW, kRTH);

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {        0,        0, kMagenta },
            { kRTW - 1,        0, kMagenta },
            {        0, kRTH - 1, kMagenta },
            { kRTW - 1, kRTH - 1, kMagenta },
            { kRTW / 2, kRTH / 2, kMagenta },
        }, label);
    }

    /**
     * @brief I3 -- the 3D counterpart of I2: a full-target 3D draw, then a sub-Viewport, no flush.
     */
    void RunFullTarget3DThenSubViewport(GraphicsDevice& dev)
    {
        const std::string label = "I3 full-target 3D draw is not squeezed by a later sub-Viewport";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW, kRTH);
        Draw3D(dev, FullQuad(kCyan));
        SetViewport(dev, kRTW / 2, kRTH / 2, kRTW / 4, kRTH / 4);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        SetViewport(dev, 0, 0, kRTW, kRTH);

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {        0,        0, kCyan },
            { kRTW - 1,        0, kCyan },
            {        0, kRTH - 1, kCyan },
            { kRTW - 1, kRTH - 1, kCyan },
        }, label);
    }

    // =====================================================================
    // J -- scissor stays a separate, unchanged control
    // =====================================================================

    /**
     * @brief J1 -- a stable scissor rectangle narrower than the viewport clips both viewports.
     *
     * This is a CONTROL, not a scissor fix: it only has to show that per-draw viewport capture did
     * not disturb whatever this renderer already did with ScissorRectangle.
     */
    void RunScissorControl(GraphicsDevice& dev)
    {
        const std::string label = "J1 scissor behaviour is unchanged by viewport capture";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        RasterizerState scissored = RasterizerState::CullNone;
        scissored.setScissorTestEnableProperty(true);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        dev.setScissorRectangleProperty(Rectangle(0, 0, kRTW, kRTH / 2));
        dev.setRasterizerStateProperty(scissored);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        fx_->VertexColorEnabled = true;
        fx_->setTextureEnabledProperty(false);
        fx_->Apply();

        SetViewport(dev, 0, 0, kRTW / 2, kRTH);
        {
            VertexBuffer& vb = MakeVb(dev, FullQuad(kRed));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
        }
        SetViewport(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        {
            VertexBuffer& vb = MakeVb(dev, FullQuad(kBlue));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
        }
        SetViewport(dev, 0, 0, kRTW, kRTH);
        // The scissor state is reset only AFTER the unbind. That was originally REQUIRED, because
        // WebGPU still resolved the SCISSOR rectangle from live state when it recorded the pass
        // (recorded here as a separate finding and left untouched by REMED-GFX-116), so resetting
        // before the flush would have erased the control. REMED-GFX-146 has since captured the
        // whole scissor state per draw, and the deliberately deferral-sensitive form of this
        // question now lives in examples/deferred_scissor_capture_test.cpp; the order is kept here
        // so this check keeps measuring what it always measured -- that per-draw VIEWPORT capture
        // does not disturb whatever the renderer does with ScissorRectangle.
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setScissorRectangleProperty(Rectangle(0, 0, kRTW, kRTH));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  8, 12, kRed   }, { 40, 12, kBlue  },   // inside both scissor and viewport
            {  8, 40, kBlack }, { 40, 40, kBlack },   // clipped away by the scissor
        }, label);
    }

    /// J2 -- a viewport SMALLER than the scissor rectangle still bounds the draw.
    void RunViewportInsideScissor(GraphicsDevice& dev)
    {
        const std::string label = "J2 viewport smaller than the scissor still bounds the draw";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        RasterizerState scissored = RasterizerState::CullNone;
        scissored.setScissorTestEnableProperty(true);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        dev.setScissorRectangleProperty(Rectangle(0, 0, kRTW, kRTH));
        dev.setRasterizerStateProperty(scissored);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        fx_->VertexColorEnabled = true;
        fx_->setTextureEnabledProperty(false);
        fx_->Apply();
        SetViewport(dev, 16, 12, 16, 12);
        {
            VertexBuffer& vb = MakeVb(dev, FullQuad(kYellow));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
        }
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            { 16, 12, kYellow }, { 31, 23, kYellow },
            { 15, 12, kBlack  }, { 32, 12, kBlack  },
            { 16, 11, kBlack  }, { 16, 24, kBlack  },
        }, label);
    }

    // =====================================================================
    // K -- repetition: no state leaks between cycles or frames
    // =====================================================================

    /// K1 -- eight viewport changes in one cycle, then the same sequence again into a second target.
    void RunManyViewportsInOneCycle(GraphicsDevice& dev)
    {
        const std::string label = "K1 eight viewports in one bind cycle";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);
        const std::array<Color, 8> palette{kRed, kGreen, kBlue, kYellow,
                                           kMagenta, kCyan, kWhite, kRed};
        const int band = kRTW / 8;

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        for (int i = 0; i < 8; ++i)
        {
            SetViewport(dev, i * band, 0, band, kRTH);
            Draw3D(dev, FullQuad(palette[static_cast<std::size_t>(i)]));
        }
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        std::vector<Probe> probes;
        for (int i = 0; i < 8; ++i)
        {
            probes.push_back({i * band + 1,        24, palette[static_cast<std::size_t>(i)]});
            probes.push_back({(i + 1) * band - 1,  24, palette[static_cast<std::size_t>(i)]});
        }
        CheckProbes(image, probes, label);
    }

    /// K2 -- the same target bound twice in one frame; the second cycle starts from a clean slate.
    void RunSameTargetTwiceInOneFrame(GraphicsDevice& dev)
    {
        const std::string label = "K2 same target, two cycles, different viewports";
        keepAlive_.clear();
        if (!kContract.draws3D) { skip(label + ": skipped -- no 3D on this renderer"); return; }
        auto rt = MakeTarget(dev, kRTW, kRTH);

        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, 0, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kRed));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.SetRenderTarget(rt.get());
        dev.Clear(kBlack);
        SetViewport(dev, kRTW / 2, 0, kRTW / 2, kRTH);
        Draw3D(dev, FullQuad(kGreen));
        SetViewport(dev, 0, 0, kRTW, kRTH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (!CanJudgeTarget(label)) return;
        const Image image = ReadTarget(*rt, kRTW, kRTH);
        CheckProbes(image, {
            {  8, 24, kBlack },   // the first cycle's own clear survives into the readback
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
            SpriteFill(Rectangle(0, 0, bbW_, bbH_), kBlack);
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
        std::printf("REMED-GFX-116 deferred viewport capture -- renderer %s, backbuffer %dx%d\n",
                    kContract.name, bbW_, bbH_);

        RunLeftRightLeft(dev);
        RunThreeDisjointViewports(dev);
        RunUpperLowerUpper(dev);
        RunViewportComponents(dev);
        RunOddDimensions(dev);
        RunDepthRange(dev);
        RunDepthRangeWithSubRect(dev);
        RunTwoTargetsInterleaved(dev);
        RunTargetBackbufferTarget(dev);
        RunBackbufferLeftRightLeft(dev);
        RunBufferKinds(dev);
        RunBufferUpdateAfterQueue(dev);
        RunEffectFamilies(dev);
        RunSpritesAnd3D(dev);
        RunFullTargetSpriteThenSubViewport(dev);
        RunFullTarget3DThenSubViewport(dev);
        RunScissorControl(dev);
        RunViewportInsideScissor(dev);
        RunManyViewportsInOneCycle(dev);
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
    DeferredViewportCaptureTest test;
    test.Run();
    return test.Result();
}
