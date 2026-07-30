// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-174: a public TextureFilter ordinal names FOUR independent components -- a minification
// filter, a magnification filter, a mipmap filter and whether anisotropy is enabled -- and a backend
// must write EVERY one of them on EVERY application. Writing a component only when it is "on" is a
// defect that no single-draw fixture can see, because the wrong state is left behind by the
// PREVIOUS draw rather than produced by the current one.
//
// THE DEFECT THIS FIXTURE PINS. EasyGL keeps one long-lived GL sampler object per slot
// (EasyGLGraphicsBackend::samplers_[slot]) and mutates it in place on every ApplySamplerState. Its
// min filter, mag filter, wrapS and wrapT were written unconditionally, but GL_TEXTURE_MAX_ANISOTROPY
// was written ONLY inside the `filter == 2` branch. A write that can only ever RAISE the value
// leaves it raised forever: the first TextureFilter::Anisotropic draw set it to SamplerState's
// default MaxAnisotropy of 4, and every subsequent Point or Linear draw ON THAT SLOT kept sampling
// anisotropically. Anisotropic taps average across the whole pixel footprint, so a contaminated slot
// returns the same wide box average for all nine ordinals and NO Point draw can return a stored
// texel again.
//
// WHY THE EXISTING FIXTURES MISSED IT, measured not assumed. REMED-GFX-170's ordinal fixture DOES
// see it (EasyGL 50/70) but attributes it to two unrelated-looking symptoms. Its SpriteBatch legs
// all pass, and the reason is ORDERING rather than immunity. EasyGLSpriteBatchBackend calls
// ApplySamplerState with a HARDCODED maxAnisotropy of 1, so the sprite path can never RAISE the
// value; only the 3D path forwards the public SamplerState.MaxAnisotropy (default 4). But because
// the pre-fix write happened only for ordinal 2, a sprite draw could not LOWER the value either --
// so SpriteBatch never causes the contamination and still inherits it. GFX-170's sprite legs run
// before any 3D leg, which is the whole of why they were green; legs F1..F3 below run them after a
// 3D Anisotropic draw and they fail pre-fix. "3D interpolates under Point while SpriteBatch is
// correct" is therefore one defect and an ordering artefact, not two defects.
//
// THE ORDER DEPENDENCE IS THE POINT. Every leg here that matters draws Anisotropic FIRST and then
// asserts the next ordinal. A fixture that resets the device between legs, or that never uses
// Anisotropic at all, is structurally blind to this class of defect however many ordinals it covers.
//
// COMPLETENESS. Ordinals 2..8 legitimately select a GL *_MIPMAP_* minification filter. On a texture
// with one level that is only safe because GL_TEXTURE_MAX_LEVEL is clamped to the real level count
// (Task 924) -- GL evaluates mipmap completeness over [BASE_LEVEL, MAX_LEVEL], so a chain of one
// level is COMPLETE and samples level 0. Legs B and C assert that for both texture kinds a game can
// sample, because an incomplete texture samples as opaque black and no pattern here contains black.
//
// MIP BOUNDARY, declared and measured rather than assumed: the legs that need a real chain first ask
// the public API how many levels it actually got, and assert the backend's declared behaviour when
// it is one. No mip chain is forced onto any texture by this fixture.
//
// ANISOTROPY BOUNDARY, declared: anisotropic output is unspecified per GPU, so no leg requires
// particular anisotropic pixels. What IS required is that Anisotropic remains a LINEAR filter, and
// that it does not survive into the next ordinal.
//
// LEGS:
//   A  anisotropy is a component, not a latch   the defect: Anisotropic then Point must be exact
//   B  single-level Texture2D completeness      all 9 ordinals sample level 0, never black
//   C  single-level RenderTarget2D completeness the same question for the other sampleable kind
//   D  real mip chain                           magnification stays level 0; mip selection is exact
//   E  sampler transitions and slot isolation   states stay distinct across draws and slots
//   F  SpriteBatch and 3D do not leak           both directions, across the same slot
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(CNA_BACKEND_HEADLESS)
    constexpr bool kRasterizes = false;
    constexpr const char* kBackendName = "HEADLESS";
#elif defined(CNA_BACKEND_SOFTWARE)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "SOFTWARE";
#elif defined(CNA_BACKEND_EASYGL)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "EASYGL";
#elif defined(CNA_BACKEND_BGFX)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "BGFX";
#elif defined(CNA_BACKEND_VULKAN)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "VULKAN";
#elif defined(CNA_BACKEND_WEBGPU)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "WEBGPU";
#elif defined(CNA_BACKEND_SDL_GPU)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "SDL_GPU";
#elif defined(CNA_BACKEND_D3D9)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "D3D9";
#elif defined(CNA_BACKEND_D3D11)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "D3D11";
#elif defined(CNA_BACKEND_D3D12)
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "D3D12";
#else
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "UNKNOWN";
#endif

    constexpr int kBBW = 160;
    constexpr int kBBH = 120;

    /// Absent from every pattern, so an uncovered destination pixel is distinguishable.
    const Color kSentinel(13, 17, 19, 255);
    /// What an incomplete GL texture samples as. No pattern here contains it.
    const Color kIncomplete(0, 0, 0, 255);

    struct FilterOrdinal
    {
        TextureFilter filter;
        int ordinal;
        const char* name;
        bool magLinear;
        bool minLinear;
        bool anisotropic;
    };

    constexpr std::array<FilterOrdinal, 9> kOrdinals{{
        {TextureFilter::Linear,                     0, "Linear",                     true,  true,  false},
        {TextureFilter::Point,                      1, "Point",                      false, false, false},
        {TextureFilter::Anisotropic,                2, "Anisotropic",                true,  true,  true },
        {TextureFilter::LinearMipPoint,             3, "LinearMipPoint",             true,  true,  false},
        {TextureFilter::PointMipLinear,             4, "PointMipLinear",             false, false, false},
        {TextureFilter::MinLinearMagPointMipLinear, 5, "MinLinearMagPointMipLinear", false, true,  false},
        {TextureFilter::MinLinearMagPointMipPoint,  6, "MinLinearMagPointMipPoint",  false, true,  false},
        {TextureFilter::MinPointMagLinearMipLinear, 7, "MinPointMagLinearMipLinear", true,  false, false},
        {TextureFilter::MinPointMagLinearMipPoint,  8, "MinPointMagLinearMipPoint",  true,  false, false},
    }};

    struct Pattern
    {
        int w = 0;
        int h = 0;
        std::vector<Color> texels;

        const Color& At(int x, int y) const
        {
            return texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                          static_cast<std::size_t>(x)];
        }
    };

    /// 8x4, every texel unique, adjacent texels far apart in every channel, no texel black.
    Pattern Make8x4()
    {
        Pattern p{8, 4, {}};
        p.texels.reserve(32);
        static const int kR[8] = {10, 250, 30, 230, 50, 210, 70, 190};
        static const int kG[4] = {15, 235, 45, 205};
        static const int kB[8] = {240, 20, 200, 60, 170, 90, 140, 110};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x)
                p.texels.emplace_back(static_cast<std::uint8_t>(kR[x]),
                                      static_cast<std::uint8_t>(kG[y]),
                                      static_cast<std::uint8_t>(kB[(x + y) % 8]),
                                      static_cast<std::uint8_t>(255));
        return p;
    }

    bool SameColor(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }

    /// Stride-20 VertexPositionTexture (REMED-GFX-125: object size is not the stream stride).
    struct VtxPT { float px, py, pz; float u, v; };
    static_assert(sizeof(VtxPT) == 20, "stride 20");

    /// A full-viewport quad in NDC with identity matrices, so destination pixel (x,y) reads
    /// ((x+0.5)/W * uvMax, (y+0.5)/H * uvMax) with no projection to reason about.
    std::vector<VtxPT> MakeQuad(float uvMax)
    {
        VtxPT tl{}, bl{}, br{}, tr{};
        auto place = [](VtxPT& v, float x, float y) { v.px = x; v.py = y; v.pz = 0.0f; };
        place(tl, -1.0f,  1.0f); tl.u = 0.0f;  tl.v = 0.0f;
        place(bl, -1.0f, -1.0f); bl.u = 0.0f;  bl.v = uvMax;
        place(br,  1.0f, -1.0f); br.u = uvMax; br.v = uvMax;
        place(tr,  1.0f,  1.0f); tr.u = uvMax; tr.v = 0.0f;
        return { tl, bl, br, tl, br, tr };
    }
}

class SamplerComponentIsolationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    Pattern p8x4_ = Make8x4();
    Texture2D tex8x4_, texB_;
    /// Distinct flat colour per mip level, so a sampled level names itself.
    std::unique_ptr<Texture2D> mipTex_;
    std::vector<Color> mipLevelColors_;
    int mipLevels_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void note(const std::string& label)
    {
        std::printf("[NOTE] %s\n", label.c_str());
        std::fflush(stdout);
    }

    // ---------------------------------------------------------------------------------------------
    // Rendering helpers
    // ---------------------------------------------------------------------------------------------

    static SamplerState MakeSampler(TextureFilter filter, int maxAnisotropy = 4,
                                    TextureAddressMode u = TextureAddressMode::Clamp,
                                    TextureAddressMode v = TextureAddressMode::Clamp)
    {
        SamplerState s;
        s.setFilterProperty(filter);
        s.setAddressUProperty(u);
        s.setAddressVProperty(v);
        s.setMaxAnisotropyProperty(maxAnisotropy);
        return s;
    }

    /// A preceding SpriteBatch leaves CullCounterClockwise behind, which culls this winding
    /// (REMED-GFX-160).
    static void ResetDeviceState(GraphicsDevice& dev, int w, int h)
    {
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    static void DrawQuad(GraphicsDevice& dev, const std::vector<VtxPT>& verts)
    {
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(VtxPT)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    /// Draws through @p draw into a fresh target and returns the whole target. Deliberately does NOT
    /// reset SamplerStates before the draw: the ORDER of applications across calls is what this
    /// fixture measures.
    std::vector<Color> Render3D(GraphicsDevice& dev, int rtW, int rtH,
                                const std::function<void(GraphicsDevice&)>& draw)
    {
        RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, rtW, rtH);
        dev.Clear(kSentinel);
        draw(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    /// One plain textured BasicEffect quad under an explicit SamplerStates[0]. Unlit and untinted,
    /// so the modulation is the identity and a stored texel is asserted byte-for-byte.
    std::vector<Color> Draw3D(GraphicsDevice& dev, int rtW, int rtH, Texture2D& tex,
                              const SamplerState& sampler)
    {
        return Render3D(dev, rtW, rtH, [&](GraphicsDevice& d) {
            d.getSamplerStatesProperty()[0] = sampler;
            BasicEffect fx(d);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);
            fx.setLightingEnabledProperty(false);
            fx.Apply();
            DrawQuad(d, MakeQuad(1.0f));
        });
    }

    std::vector<Color> DrawSprite(GraphicsDevice& dev, int rtW, int rtH, Texture2D& tex,
                                  SamplerState sampler)
    {
        RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        dev.setViewportProperty(Viewport(0, 0, rtW, rtH));
        dev.Clear(kSentinel);
        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr, nullptr,
                 Matrix::getIdentityProperty());
        sb.Draw(tex, Rectangle(0, 0, rtW, rtH), Rectangle(0, 0, tex.getWidthProperty(),
                tex.getHeightProperty()), Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
                SpriteEffects::None, 0.0f);
        sb.End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    // ---------------------------------------------------------------------------------------------
    // Oracles
    // ---------------------------------------------------------------------------------------------

    /// How many pixels hold a colour that appears in NO texel of @p pat. Point filtering owes 0.
    static int Foreign(const std::vector<Color>& pix, const Pattern& pat)
    {
        int foreign = 0;
        for (const Color& c : pix)
        {
            if (SameColor(c, kSentinel)) continue;
            bool found = false;
            for (const Color& t : pat.texels)
                if (SameColor(c, t)) { found = true; break; }
            if (!found) ++foreign;
        }
        return foreign;
    }

    /// Byte-exact point magnification: an integer NxN magnification puts every destination pixel
    /// wholly inside one texel, so pixel (x,y) MUST equal texel (x/scale, y/scale) exactly.
    static int PointMismatches(const std::vector<Color>& pix, int rtW, int rtH, const Pattern& pat)
    {
        const int sx = rtW / pat.w;
        const int sy = rtH / pat.h;
        int bad = 0;
        for (int y = 0; y < rtH; ++y)
            for (int x = 0; x < rtW; ++x)
            {
                const Color& a = pix[static_cast<std::size_t>(y) * static_cast<std::size_t>(rtW) +
                                     static_cast<std::size_t>(x)];
                if (!SameColor(a, pat.At(x / sx, y / sy))) ++bad;
            }
        return bad;
    }

    static int CountEqual(const std::vector<Color>& pix, const Color& c)
    {
        int n = 0;
        for (const Color& p : pix) if (SameColor(p, c)) ++n;
        return n;
    }

    static int DistinctColors(const std::vector<Color>& pix)
    {
        std::set<std::uint32_t> s;
        for (const Color& c : pix)
            s.insert((static_cast<std::uint32_t>(c.getRProperty()) << 16) |
                     (static_cast<std::uint32_t>(c.getGProperty()) << 8) |
                      static_cast<std::uint32_t>(c.getBProperty()));
        return static_cast<int>(s.size());
    }

    // ---------------------------------------------------------------------------------------------
    // A -- anisotropy is a COMPONENT of the ordinal, not a latch
    // ---------------------------------------------------------------------------------------------
    void RunA(GraphicsDevice& dev)
    {
        const SamplerState aniso = MakeSampler(TextureFilter::Anisotropic, 4);

        // A0: the clean-slot control. Point BEFORE any Anisotropic draw was already correct pre-fix,
        // so this leg passing proves the later ones isolate the ORDER and not point sampling itself.
        {
            const std::vector<Color> pix = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Point));
            check(PointMismatches(pix, 16, 8, p8x4_) == 0 && Foreign(pix, p8x4_) == 0,
                  "A0 Point on a clean slot magnifies byte-exactly");
        }

        // A1: THE DEFECT. One Anisotropic draw, then Point on the SAME slot. Pre-fix the anisotropy
        // stayed latched at 4 and every subsequent point fetch averaged across the footprint.
        {
            (void)Draw3D(dev, 16, 8, tex8x4_, aniso);
            const std::vector<Color> pix = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Point));
            const int bad = PointMismatches(pix, 16, 8, p8x4_);
            const int foreign = Foreign(pix, p8x4_);
            check(bad == 0 && foreign == 0,
                  "A1 Point AFTER Anisotropic magnifies byte-exactly (mismatches=" +
                  std::to_string(bad) + ", manufactured=" + std::to_string(foreign) + ")");
        }

        // A2: every point-MAGNIFYING ordinal, each preceded by an Anisotropic draw on the same slot.
        for (const FilterOrdinal& o : kOrdinals)
        {
            if (o.magLinear || o.anisotropic) continue;
            (void)Draw3D(dev, 16, 8, tex8x4_, aniso);
            const std::vector<Color> pix = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(o.filter));
            const int bad = PointMismatches(pix, 16, 8, p8x4_);
            check(bad == 0, std::string("A2 ") + o.name +
                  " magnifies POINT after Anisotropic (mismatches=" + std::to_string(bad) + ")");
        }

        // A3: every point-MINIFYING ordinal, same construction. Minification is where anisotropy
        // actually takes its extra taps, so this is the half that stayed broken for every ordinal.
        for (const FilterOrdinal& o : kOrdinals)
        {
            if (o.minLinear || o.anisotropic) continue;
            (void)Draw3D(dev, 3, 2, tex8x4_, aniso);
            const std::vector<Color> pix = Draw3D(dev, 3, 2, tex8x4_, MakeSampler(o.filter));
            const int foreign = Foreign(pix, p8x4_);
            check(foreign == 0, std::string("A3 ") + o.name +
                  " minifies POINT after Anisotropic (manufactured=" + std::to_string(foreign) + ")");
        }

        // A4: the correction must not turn Anisotropic into nearest -- it is a LINEAR filter, and
        // that is precisely the half REMED-GFX-170 found SDL_GPU had lost.
        {
            const std::vector<Color> pix = Draw3D(dev, 3, 2, tex8x4_, aniso);
            check(Foreign(pix, p8x4_) > 0,
                  "A4 Anisotropic still minifies LINEAR -- it did not become nearest");
        }

        // A5: a LARGER MaxAnisotropy must not leak either. 16 is above the default, so a latch would
        // be even more visible; the following Point draw must still be exact.
        {
            (void)Draw3D(dev, 3, 2, tex8x4_, MakeSampler(TextureFilter::Anisotropic, 16));
            const std::vector<Color> pix = Draw3D(dev, 3, 2, tex8x4_, MakeSampler(TextureFilter::Point));
            check(Foreign(pix, p8x4_) == 0,
                  "A5 MaxAnisotropy=16 does not survive into the next Point draw");
        }

        // A6: two consecutive draws in ONE frame, Anisotropic then Point, with no target switch
        // between them -- a target switch is a flush, and flushing can hide a state-order defect.
        {
            const std::vector<Color> pix = Render3D(dev, 16, 8, [&](GraphicsDevice& d) {
                BasicEffect fx(d);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(&tex8x4_);
                fx.setLightingEnabledProperty(false);
                d.getSamplerStatesProperty()[0] = MakeSampler(TextureFilter::Anisotropic, 4);
                fx.Apply();
                DrawQuad(d, MakeQuad(1.0f));
                d.getSamplerStatesProperty()[0] = MakeSampler(TextureFilter::Point);
                fx.Apply();
                DrawQuad(d, MakeQuad(1.0f));
            });
            check(PointMismatches(pix, 16, 8, p8x4_) == 0,
                  "A6 Anisotropic then Point in ONE frame, no flush between -- Point wins exactly");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // B -- a single-level ordinary Texture2D is COMPLETE under every ordinal
    // ---------------------------------------------------------------------------------------------
    void RunB(GraphicsDevice& dev)
    {
        check(tex8x4_.getLevelCountProperty() == 1,
              "B0 the ordinary texture really has ONE level (levels=" +
              std::to_string(tex8x4_.getLevelCountProperty()) + ")");

        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(o.filter));
            const int black = CountEqual(pix, kIncomplete);
            const int sentinel = CountEqual(pix, kSentinel);
            check(black == 0 && sentinel == 0,
                  std::string("B ") + o.name +
                  ": single-level texture is COMPLETE -- no incomplete-texture black, fully covered"
                  " (black=" + std::to_string(black) + ", uncovered=" + std::to_string(sentinel) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // C -- the same completeness question for a RenderTarget2D used as a source texture
    // ---------------------------------------------------------------------------------------------
    void RunC(GraphicsDevice& dev)
    {
        // Fill a render target with the pattern, then SAMPLE it. A render target is the other
        // texture kind a game can bind, and it is created by a different code path, so its level
        // count and its GL_TEXTURE_MAX_LEVEL are a separate question from an ordinary texture's.
        RenderTarget2D src(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&src);
        ResetDeviceState(dev, 16, 8);
        dev.Clear(kSentinel);
        {
            dev.getSamplerStatesProperty()[0] = MakeSampler(TextureFilter::Point);
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex8x4_);
            fx.setLightingEnabledProperty(false);
            fx.Apply();
            DrawQuad(dev, MakeQuad(1.0f));
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);

        check(src.getLevelCountProperty() == 1,
              "C0 the render target really has ONE level (levels=" +
              std::to_string(src.getLevelCountProperty()) + ")");

        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix = Draw3D(dev, 16, 8, src, MakeSampler(o.filter));
            const int black = CountEqual(pix, kIncomplete);
            check(black == 0,
                  std::string("C ") + o.name +
                  ": single-level RENDER TARGET is COMPLETE when sampled (incomplete-black=" +
                  std::to_string(black) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // D -- a real mip chain: magnification stays level 0, mip selection is exact
    // ---------------------------------------------------------------------------------------------
    void RunD(GraphicsDevice& dev)
    {
        if (mipLevels_ <= 1)
        {
            note(std::string(kBackendName) +
                 " allocated ONE level for a mipMap=true Texture2D -- mip level selection is not"
                 " observable on this backend and is asserted as a declared boundary, not measured");
            check(mipTex_->getLevelCountProperty() == 1,
                  "D0 declared boundary: no mip storage, sampling is level 0 only");
            const std::vector<Color> pix = Draw3D(dev, 32, 32, *mipTex_,
                                                  MakeSampler(TextureFilter::Point));
            check(CountEqual(pix, mipLevelColors_[0]) == 32 * 32,
                  "D1 without a chain every fetch is level 0");
            return;
        }

        check(mipLevels_ >= 3, "D0 the mip texture really has a chain (levels=" +
              std::to_string(mipLevels_) + ")");

        // D1: MAGNIFICATION must stay on level 0 whatever the mip component says. 8x8 -> 32x32 is a
        // 4x magnification, so lambda is negative and no mip level below 0 exists to select.
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix = Draw3D(dev, 32, 32, *mipTex_, MakeSampler(o.filter));
            const int lvl0 = CountEqual(pix, mipLevelColors_[0]);
            check(lvl0 == 32 * 32,
                  std::string("D1 ") + o.name + ": magnification samples LEVEL 0 only (level-0 px=" +
                  std::to_string(lvl0) + "/1024)");
        }

        // D2: a MIP-POINT ordinal minifying must land on exactly ONE level -- one flat colour, and
        // it must be one of the levels' own colours rather than a blend of two.
        {
            const std::vector<Color> pix = Draw3D(dev, 2, 2, *mipTex_,
                                                  MakeSampler(TextureFilter::MinPointMagLinearMipPoint));
            const int distinct = DistinctColors(pix);
            bool isALevel = false;
            for (const Color& c : mipLevelColors_)
                if (CountEqual(pix, c) == 4) isALevel = true;
            check(distinct == 1 && isALevel,
                  "D2 mip-POINT minification selects exactly ONE stored level (distinct=" +
                  std::to_string(distinct) + ", is-a-level=" + (isALevel ? "yes" : "no") + ")");
        }

        // D3: the single-level texture must be unaffected by any of this -- no chain was forced onto
        // it, and it still samples level 0.
        check(tex8x4_.getLevelCountProperty() == 1,
              "D3 no mip chain was forced onto the ordinary single-level texture");

        // D4: MEASURED AND REPORTED, NOT ASSERTED. The public table gives Linear(0) a mip component
        // of Linear and Point(1) a mip component of Point, but EasyGL maps both onto a GL filter
        // with no mipmap term at all, so a texture that DOES own a chain never mip-filters under
        // either. That is a real divergence from the table and it is NOT the defect REMED-GFX-174
        // records, so it is reported here with its measurement and carried as its own ticket rather
        // than silently absorbed or asserted to the wrong contract.
        {
            const std::vector<Color> lin = Draw3D(dev, 2, 2, *mipTex_, MakeSampler(TextureFilter::Linear));
            const bool lvl0 = CountEqual(lin, mipLevelColors_[0]) == 4;
            note(std::string("D4 mip component of ordinal 0/1 -- minifying a ") +
                 std::to_string(mipLevels_) + "-level chain under Linear samples " +
                 (lvl0 ? "LEVEL 0 (no mip term)" : "a MIP LEVEL") +
                 "; the public table says mip=Linear. Reported as REMED-GFX-175, not changed here.");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // E -- sampler transitions, cache distinctness and slot isolation
    // ---------------------------------------------------------------------------------------------
    void RunE(GraphicsDevice& dev)
    {
        // E1: the same texture under three different samplers must give three different images. A
        // cache that collapses distinct descriptions returns the first one three times.
        {
            const std::vector<Color> point  = Draw3D(dev, 3, 2, tex8x4_, MakeSampler(TextureFilter::Point));
            const std::vector<Color> linear = Draw3D(dev, 3, 2, tex8x4_, MakeSampler(TextureFilter::Linear));
            check(point != linear, "E1 one texture, Point vs Linear -- distinct states, distinct images");
        }

        // E2: a sampler change between CONSECUTIVE draws must take effect, and changing back must
        // restore the first result exactly. This catches a cache that is keyed on the texture alone.
        {
            const std::vector<Color> first  = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Point));
            (void)Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Linear));
            const std::vector<Color> again  = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Point));
            check(first == again, "E2 Point -> Linear -> Point returns the first image byte-for-byte");
        }

        // E3: two DIFFERENT textures under ONE sampler must each be sampled correctly -- the sampler
        // must not be keyed to, or captured by, a texture identity.
        {
            const std::vector<Color> a = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Point));
            const std::vector<Color> b = Draw3D(dev, 16, 8, texB_,   MakeSampler(TextureFilter::Point));
            check(PointMismatches(a, 16, 8, p8x4_) == 0 && a != b,
                  "E3 two textures, one sampler -- each sampled on its own, both exact");
        }

        // E4: SLOT ISOLATION, measured MODULATION-INDEPENDENTLY. DualTextureEffect computes
        // tex0 * (2 * tex1), which saturates rather than being the identity, so this leg cannot
        // compare against the raw palette. Instead it draws the SAME thing twice, changing only
        // SLOT 1's filter. Slot 1's texture is a single white texel, so its own filter cannot alter
        // its own sample -- any difference between the two images is slot 1 reaching slot 0.
        {
            auto dual = [&](TextureFilter slot1Filter, int slot1Aniso) {
                return Render3D(dev, 16, 8, [&](GraphicsDevice& d) {
                    d.getSamplerStatesProperty()[0] = MakeSampler(TextureFilter::Point);
                    d.getSamplerStatesProperty()[1] = MakeSampler(slot1Filter, slot1Aniso);
                    DualTextureEffect fx(d);
                    fx.setWorldProperty(Matrix::getIdentityProperty());
                    fx.setViewProperty(Matrix::getIdentityProperty());
                    fx.setProjectionProperty(Matrix::getIdentityProperty());
                    fx.setTextureProperty(&tex8x4_);
                    fx.setTexture2Property(&white_);
                    fx.Apply();
                    DrawQuad(d, MakeQuad(1.0f));
                });
            };
            const std::vector<Color> withAniso = dual(TextureFilter::Anisotropic, 16);
            const std::vector<Color> withPoint = dual(TextureFilter::Point, 4);
            check(withAniso == withPoint,
                  "E4 slot 1's filter does not reach slot 0 -- the two slots are independent");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // F -- SpriteBatch and the 3D path must not leak into each other
    // ---------------------------------------------------------------------------------------------
    void RunF(GraphicsDevice& dev)
    {
        // F1: SpriteBatch positive control, and the reason the defect hid for so long -- the sprite
        // path forwards a hardcoded MaxAnisotropy of 1, so it was always correct.
        {
            const std::vector<Color> pix = DrawSprite(dev, 16, 8, tex8x4_,
                                                      MakeSampler(TextureFilter::Point));
            check(PointMismatches(pix, 16, 8, p8x4_) == 0,
                  "F1 SpriteBatch Point magnifies byte-exactly (the always-correct control)");
        }

        // F2: a 3D Anisotropic draw must not leave state behind that a following SpriteBatch sees.
        {
            (void)Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Anisotropic, 16));
            const std::vector<Color> pix = DrawSprite(dev, 16, 8, tex8x4_,
                                                      MakeSampler(TextureFilter::Point));
            check(PointMismatches(pix, 16, 8, p8x4_) == 0,
                  "F2 3D Anisotropic does not leak into a following SpriteBatch Point");
        }

        // F3: and the other direction -- a SpriteBatch draw must not disturb the 3D path's slot.
        {
            (void)DrawSprite(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Linear));
            const std::vector<Color> pix = Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Point));
            check(PointMismatches(pix, 16, 8, p8x4_) == 0,
                  "F3 a SpriteBatch draw does not leak into a following 3D Point");
        }
    }

    Texture2D white_;

    void LoadContent() override
    {
        GraphicsDevice& dev = getGraphicsDeviceProperty();

        tex8x4_ = Texture2D(dev, p8x4_.w, p8x4_.h);
        tex8x4_.SetData(p8x4_.texels.data(), static_cast<int>(p8x4_.texels.size()));

        // A second, visibly different texture for the "one sampler, two textures" leg.
        Pattern b = p8x4_;
        for (Color& c : b.texels)
            c = Color(static_cast<std::uint8_t>(255 - c.getRProperty()),
                      static_cast<std::uint8_t>(255 - c.getGProperty()),
                      static_cast<std::uint8_t>(255 - c.getBProperty()),
                      static_cast<std::uint8_t>(255));
        texB_ = Texture2D(dev, b.w, b.h);
        texB_.SetData(b.texels.data(), static_cast<int>(b.texels.size()));

        white_ = Texture2D(dev, 1, 1);
        const Color w(255, 255, 255, 255);
        white_.SetData(&w, 1);

        // A real chain, each level a distinct flat colour so a sampled level names itself. Created
        // through the public mipMap=true constructor -- nothing here forces storage that the backend
        // did not already choose to allocate.
        mipTex_ = std::make_unique<Texture2D>(dev, 8, 8, true, SurfaceFormat::Color);
        mipLevels_ = mipTex_->getLevelCountProperty();
        mipLevelColors_ = { Color(255, 40, 40, 255), Color(40, 255, 40, 255),
                            Color(40, 40, 255, 255), Color(255, 255, 40, 255),
                            Color(255, 40, 255, 255) };
        for (int level = 0; level < mipLevels_ && level < static_cast<int>(mipLevelColors_.size());
             ++level)
        {
            const int dim = 8 >> level;
            std::vector<Color> data(static_cast<std::size_t>(dim) * static_cast<std::size_t>(dim),
                                    mipLevelColors_[static_cast<std::size_t>(level)]);
            mipTex_->SetData(level, nullptr, data.data(), 0, static_cast<int>(data.size()));
        }
    }

    void Draw(const GameTime&) override
    {
        if (done_) { Exit(); return; }
        done_ = true;

        GraphicsDevice& dev = getGraphicsDeviceProperty();

        if (!kRasterizes)
        {
            note(std::string(kBackendName) + " does not rasterize; asserting the documented"
                 " rejection instead of sampled pixels");
            bool threw = false;
            try {
                (void)Draw3D(dev, 16, 8, tex8x4_, MakeSampler(TextureFilter::Point));
            } catch (...) { threw = true; }
            check(threw, "HEADLESS rejects render-target readback (REMED-GFX-127 contract)");
        }
        else
        {
            RunA(dev);
            RunB(dev);
            RunC(dev);
            RunD(dev);
            RunE(dev);
            RunF(dev);
        }

        std::printf("\n=== %s: %d/%d PASS ===\n", kBackendName, passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SamplerComponentIsolationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    SamplerComponentIsolationTest game;
    game.Run();
    return game.getResult();
}
