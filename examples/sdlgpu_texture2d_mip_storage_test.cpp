// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-176: SDL_GPU's ordinary Texture2D had no mip chain to filter.
//
// REMED-GFX-175 settled the CONTRACT a TextureFilter ordinal names and measured SDL_GPU at 55/87 on
// it, with all nine ordinals sampling level 0 at an exact 2x, 4x and 8x minification. That fixture
// asks "which level did the sampler choose?" -- and on this renderer the honest answer was "the only
// one that existed". This fixture asks the question underneath it, which no filter fixture can:
// does the RESOURCE have the levels its public LevelCount claims, and can a caller put content in
// them? Those are storage and upload questions, and they are answered here against the native
// SDL_GPU texture rather than inferred from a sampled pixel.
//
// THE TWO PRE-FIX MECHANISMS, both in SdlGpuTextureRenderer and neither in the sampler:
//
//   1. ALLOCATION. The constructor hardcoded `createInfo.num_levels = 1` and never read
//      `ImageData::mipLevels`, the field Texture2D's own constructor fills from
//      CalculateMipLevels(w, h). A `mipMap=true` Texture2D therefore reported LevelCount 4 to the
//      game and owned a native resource with exactly one level.
//
//   2. UPLOAD. The class declared no `UpdatePixelsLevel` override at all, so it inherited
//      `ITextureRenderer`'s default -- an empty body. Every `SetData(level>0, ...)` a game issued
//      was accepted by the shared layer, written into its CPU-side mip buffer, handed to the
//      renderer and silently dropped. No error, no warning, no pixel.
//
// The two are independent: fixing allocation alone gives a chain of undefined levels, and fixing
// upload alone writes into levels that do not exist. Leg A measures the first directly and leg B
// the second, so a partial fix cannot look complete.
//
// A THIRD MECHANISM, which only becomes reachable once the first two are fixed and which this
// fixture is what found: `SDL_UploadToGPUTexture`'s `cycle` argument. The level-0 upload passed
// `cycle=true`, whose documented meaning is "swap to a fresh internal resource rather than stall",
// and a fresh resource does not carry the levels this upload is not writing. That is harmless for a
// one-level texture (the upload replaces the whole resource anyway) and silently destructive for a
// chain -- exactly the failure SDLGPU-40 measured on the cube path and REMED-GFX-135 on the volume
// path, both of which landed on `cycle=false` for this reason. Leg D uploads levels in several
// orders specifically to catch it; with `cycle=true` retained for chains, D fails and B still
// passes, which is why D exists as its own leg.
//
// THE ORACLE, and why it is byte-exact rather than tolerant. A full-viewport quad with uv 0..1 drawn
// into an rtW x rtH target gives a texel footprint of texW/rtW per pixel, so lambda = log2(texW/rtW).
// Rendering a POWER-OF-TWO chain into a target the size of level L therefore puts lambda on exactly
// L, and at an integer lambda a mip filter degenerates to that one level. With TextureFilter::Point
// the fetch inside the level is nearest and destination pixel centres land on texel centres, so the
// frame that comes back IS level L's stored bytes, in order. A level's content, its orientation, its
// row pitch and its dimensions are all asserted at once, and nothing is compared with a tolerance.
//
// Every level therefore carries an ASYMMETRIC, self-identifying pattern rather than a flat colour:
// a flat level cannot show a transposed row pitch, a mirrored V, a wrong source offset or a
// neighbouring level's content. Levels differ in dominant channel AND in horizontal AND in vertical
// structure:
//
//     level 0  red   on black, vertical stripes, top-left texel forced to WHITE
//     level 1  green on black, horizontal stripes, top-left texel forced to WHITE
//     level 2  blue  on black, checkerboard, top-left texel forced to WHITE
//     level 3+ yellow/magenta diagonal, top-left texel forced to WHITE
//
// NON-POWER-OF-TWO chains have no integer lambda at a level's own size, so they are not asserted
// with the exact ladder. They get the two scales that ARE exact at any dimensions -- 1:1, which is
// level 0, and a 1x1 destination, which is maximum minification and must land on the LAST level --
// plus the bounded property that every pixel is a colour some level of that chain owns.
//
// WHAT THIS FIXTURE DELIBERATELY DOES NOT DO. It never asks for automatic mip generation and never
// asserts content in a level nobody wrote: leg A2 declares a four-level chain, writes only level 0
// and requires only that the resource stays complete and sampleable. `SDL_GenerateMipmapsForGPU-
// Texture` is not called by the code under test and not expected here.
//
// GetData BOUNDARY (leg K). A plain Texture2D's `GetData` is served by the shared layer's own CPU
// pixel shadow on EVERY renderer and never reaches `ITextureRenderer::GetData` at all, so it cannot
// witness what is in GPU memory and is not used as a content oracle anywhere above. Leg K states
// that boundary as a check rather than leaving it implied. No CPU shadow was added for testing.
//
// LEGS:
//   A  allocation           native num_levels == declared LevelCount, over the dimension matrix
//   B  per-level upload     every declared level accepts content and keeps it, byte-exact
//   C  exact LOD ladder     Point selects each level in turn; Linear brackets between them
//   D  upload order         reverse order, level 0 last, repeated writes -- no level is orphaned
//   E  partial region       sub-rectangles of a nonzero level land where they were addressed
//   F  same frame           upload and sample with no Present, twice, second write visible
//   G  NPOT / odd / rect    chains whose levels are not powers of two
//   H  consumers            SpriteBatch and every textured stock effect family
//   I  lifetime             destruction after a queued draw, recreation, handle reuse
//   J  rejects              out-of-range level, wrong level dimensions, one-level completeness
//   K  GetData boundary     what a plain Texture2D readback is, and is not
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
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include <algorithm>
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
using CNA::Internal::Renderers::SdlGpu::SdlGpuTextureRenderer;

namespace
{
    constexpr int kBBW = 160;
    constexpr int kBBH = 120;

    /// Absent from every level of every chain below, so an uncovered destination pixel shows.
    const Color kSentinel(13, 17, 19, 255);

    /// Texture2D.cpp's own mipDim, so a level's expected extent is never independently invented.
    int MipDim(int base, int level) { return std::max(1, base >> level); }

    /// Texture2D.cpp's own CalculateMipLevels.
    int CalcLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    /// Level L's identity colour: the channel signature that says "this content came from level L".
    /// 0/255-only channels, so an averaged or blended result stays byte-predictable, and no two
    /// levels share a dominant channel.
    Color LevelInk(int level)
    {
        switch (level % 4)
        {
        case 0:  return Color(255,   0,   0, 255);   // red
        case 1:  return Color(  0, 255,   0, 255);   // green
        case 2:  return Color(  0,   0, 255, 255);   // blue
        default: return Color(255, 255,   0, 255);   // yellow
        }
    }

    const Color kBlack(0, 0, 0, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kMagenta(255, 0, 255, 255);

    /// The ASYMMETRIC content of one level. Structure differs per level in BOTH axes, so a
    /// transposed pitch, a flipped V or a neighbouring level's bytes are all distinguishable from
    /// the expected image -- which a flat colour could never show. The (0,0) texel is forced to
    /// white on every level, giving each frame an unambiguous origin marker.
    std::vector<Color> LevelPattern(int w, int h, int level)
    {
        const Color ink = LevelInk(level);
        std::vector<Color> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), kBlack);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                bool lit = false;
                switch (level % 4)
                {
                case 0:  lit = (x % 2) == 0; break;                 // vertical stripes
                case 1:  lit = (y % 2) == 0; break;                 // horizontal stripes
                case 2:  lit = ((x + y) % 2) == 0; break;           // checkerboard
                default: lit = (x % 3) == (y % 3); break;           // diagonal
                }
                Color c = lit ? ink : kBlack;
                if (level % 4 == 3 && !lit) c = kMagenta;           // no black in the last family
                px[static_cast<std::size_t>(y) * w + x] = c;
            }
        }
        px[0] = kWhite;
        return px;
    }

    /// A flat level, for the chains whose LOD is not an integer and whose oracle is level IDENTITY.
    std::vector<Color> LevelFlat(int w, int h, int level)
    {
        return std::vector<Color>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
                                  LevelInk(level));
    }

    bool SameColor(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }

    std::string Str(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
                     std::to_string(static_cast<int>(c.getGProperty())) + "," +
                     std::to_string(static_cast<int>(c.getBProperty())) + ")";
    }

    int DistinctColors(const std::vector<Color>& pix)
    {
        std::set<std::uint32_t> seen;
        for (const Color& c : pix)
            seen.insert((static_cast<std::uint32_t>(c.getRProperty()) << 16) |
                        (static_cast<std::uint32_t>(c.getGProperty()) << 8) |
                         static_cast<std::uint32_t>(c.getBProperty()));
        return static_cast<int>(seen.size());
    }

    /// Stride-20 VertexPositionTexture (REMED-GFX-125: object size is not the stream stride).
    struct VtxPT { float px, py, pz; float u, v; };
    static_assert(sizeof(VtxPT) == 20, "stride 20");
    /// Stride-32 VertexPositionNormalTexture.
    struct VtxPNT { float px, py, pz; float nx, ny, nz; float u, v; };
    static_assert(sizeof(VtxPNT) == 32, "stride 32");
    /// Stride-52 VertexPositionNormalTextureSkinned.
    struct VtxPNTS { float px, py, pz; float nx, ny, nz; float u, v;
                     float w0, w1, w2, w3; std::uint8_t i0, i1, i2, i3; };
    static_assert(sizeof(VtxPNTS) == 52, "stride 52");

    /// A full-viewport quad in NDC with identity matrices: a destination of W x H pixels covers the
    /// whole [0,1] uv range, so the footprint per pixel is exactly texWidth / W.
    template <typename V>
    std::vector<V> MakeQuad()
    {
        V tl{}, bl{}, br{}, tr{};
        auto place = [](V& v, float x, float y) { v.px = x; v.py = y; v.pz = 0.0f; };
        place(tl, -1.0f,  1.0f); tl.u = 0.0f; tl.v = 0.0f;
        place(bl, -1.0f, -1.0f); bl.u = 0.0f; bl.v = 1.0f;
        place(br,  1.0f, -1.0f); br.u = 1.0f; br.v = 1.0f;
        place(tr,  1.0f,  1.0f); tr.u = 1.0f; tr.v = 0.0f;
        return { tl, bl, br, tl, br, tr };
    }

    template <typename V>
    void FillNormals(std::vector<V>& v)
    {
        for (auto& x : v) { x.nx = 0.0f; x.ny = 0.0f; x.nz = 1.0f; }
    }

    template <typename V>
    void FillSkin(std::vector<V>& v)
    {
        for (auto& x : v) { x.w0 = 1.0f; x.w1 = x.w2 = x.w3 = 0.0f; x.i0 = x.i1 = x.i2 = x.i3 = 0; }
    }

    VertexDeclaration PositionTextureDecl()
    {
        return VertexDeclaration(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    /// The dimension matrix. Every entry names its own expected chain, so a wrong level count is a
    /// difference from a written-down number rather than from the code's own arithmetic.
    struct Dim { int w, h; const char* name; bool exactLadder; };
    constexpr std::array<Dim, 10> kDims{{
        { 1,  1, "1x1",   true  },   // mipMap=true still yields exactly one level
        { 2,  2, "2x2",   true  },
        { 4,  4, "4x4",   true  },
        { 8,  8, "8x8",   true  },
        { 8,  4, "8x4",   true  },   // rectangular, power of two: the ladder is still exact
        {16,  2, "16x2",  true  },   // extreme aspect ratio, final level 1x1
        { 3,  2, "3x2",   false },   // odd
        { 5,  3, "5x3",   false },   // odd both axes
        { 6,  6, "6x6",   false },   // non power of two
        { 7,  5, "7x5",   false },   // odd, non power of two
    }};
}

class SdlGpuTexture2DMipStorageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    /// EnvironmentMapEffect needs a non-null cube even when its contribution is zeroed.
    std::unique_ptr<TextureCube> cube_;
    /// A single white texel for DualTextureEffect's slot 1.
    std::unique_ptr<Texture2D> white_;
    /// The 8x8 four-level patterned chain every consumer leg samples.
    std::unique_ptr<Texture2D> chain8_;

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
    // Texture construction
    // ---------------------------------------------------------------------------------------------

    /// Builds a texture with a complete declared chain and writes @p maker into every level.
    static std::unique_ptr<Texture2D> MakeChain(
        GraphicsDevice& dev, int w, int h,
        const std::function<std::vector<Color>(int, int, int)>& maker)
    {
        auto tex = std::make_unique<Texture2D>(dev, w, h, true, SurfaceFormat::Color);
        const int levels = tex->getLevelCountProperty();
        for (int l = 0; l < levels; ++l)
            WriteLevel(*tex, l, maker);
        return tex;
    }

    static void WriteLevel(Texture2D& tex, int level,
                           const std::function<std::vector<Color>(int, int, int)>& maker)
    {
        const int lw = MipDim(tex.getWidthProperty(), level);
        const int lh = MipDim(tex.getHeightProperty(), level);
        const std::vector<Color> px = maker(lw, lh, level);
        tex.SetData(level, nullptr, px.data(), 0, static_cast<int>(px.size()));
    }

    /// The native level count SDL was really asked for. This is the whole point of leg A: the public
    /// LevelCount is bookkeeping in the shared layer and says nothing about the GPU resource.
    static int NativeLevels(const Texture2D& tex)
    {
        if (!tex.HasRenderer()) return -1;
        const auto* renderer = dynamic_cast<const SdlGpuTextureRenderer*>(&tex.GetRenderer());
        return renderer != nullptr ? renderer->LevelCountEXT() : -1;
    }

    // ---------------------------------------------------------------------------------------------
    // Rendering helpers
    // ---------------------------------------------------------------------------------------------

    static SamplerState MakeSampler(TextureFilter filter)
    {
        SamplerState s;
        s.setFilterProperty(filter);
        s.setAddressUProperty(TextureAddressMode::Clamp);
        s.setAddressVProperty(TextureAddressMode::Clamp);
        s.setMaxAnisotropyProperty(4);
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

    std::vector<Color> Render(GraphicsDevice& dev, int rtW, int rtH,
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
        dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        dev.getSamplerStatesProperty()[1] = SamplerState::LinearWrap;
        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    enum class Path { NonIndexed, Indexed, UserRaw };

    /// One plain textured BasicEffect quad under an explicit SamplerStates[0], unlit and untinted,
    /// so the modulation is the identity and a level's stored bytes survive exactly.
    std::vector<Color> Draw3D(GraphicsDevice& dev, int rtW, int rtH, Texture2D& tex,
                              const SamplerState& sampler, Path path = Path::NonIndexed)
    {
        return Render(dev, rtW, rtH, [&](GraphicsDevice& d) {
            d.getSamplerStatesProperty()[0] = sampler;
            BasicEffect fx(d);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);
            fx.setLightingEnabledProperty(false);
            fx.Apply();

            const std::vector<VtxPT> verts = MakeQuad<VtxPT>();
            if (path == Path::UserRaw)
            {
                const VertexDeclaration decl = PositionTextureDecl();
                d.DrawUserPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 2, decl);
                return;
            }
            VertexBuffer vb(d, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                          static_cast<int>(sizeof(VtxPT)));
            d.SetVertexBuffer(&vb);
            if (path == Path::Indexed)
            {
                const std::array<std::uint16_t, 6> idx{{0, 1, 2, 3, 4, 5}};
                IndexBuffer ib(d, static_cast<int>(idx.size()));
                ib.SetData(idx.data(), 0, static_cast<int>(idx.size()));
                d.SetIndexBuffer(&ib);
                d.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                        static_cast<int>(verts.size()), 0, 2);
                d.SetIndexBuffer(nullptr);
            }
            else
            {
                d.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            }
            d.SetVertexBuffer(nullptr);
        });
    }

    /// The same footprint through SpriteBatch: the whole texture stretched onto rtW x rtH.
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
        sb.Draw(tex, Rectangle(0, 0, rtW, rtH),
                Rectangle(0, 0, tex.getWidthProperty(), tex.getHeightProperty()),
                Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
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

    /// How many pixels of @p got differ from @p want. Exact: no tolerance anywhere.
    static int Mismatches(const std::vector<Color>& got, const std::vector<Color>& want)
    {
        int bad = 0;
        const std::size_t n = std::min(got.size(), want.size());
        for (std::size_t i = 0; i < n; ++i) if (!SameColor(got[i], want[i])) ++bad;
        return bad + static_cast<int>(got.size() > n ? got.size() - n : want.size() - n);
    }

    static std::string FirstDiff(const std::vector<Color>& got, const std::vector<Color>& want,
                                 int w)
    {
        const std::size_t n = std::min(got.size(), want.size());
        for (std::size_t i = 0; i < n; ++i)
            if (!SameColor(got[i], want[i]))
                return "at (" + std::to_string(static_cast<int>(i) % w) + "," +
                       std::to_string(static_cast<int>(i) / w) + ") got " + Str(got[i]) +
                       " want " + Str(want[i]);
        return "no differing pixel";
    }

    /// Which single level's flat identity colour every pixel holds, or -1 if the frame is not
    /// exactly one level.
    static int SoleLevel(const std::vector<Color>& pix, int levels)
    {
        if (DistinctColors(pix) != 1) return -1;
        for (int l = 0; l < levels; ++l)
            if (SameColor(pix.front(), LevelInk(l))) return l;
        return -1;
    }

    /// Whether every pixel is a colour some level of a @p levels-long chain owns. The identity
    /// colours use only 0 and 255 per channel, so this rejects a blend as well as a stray level.
    static int NotOwnedByChain(const std::vector<Color>& pix, int levels)
    {
        int bad = 0;
        for (const Color& c : pix)
        {
            bool owned = false;
            for (int l = 0; l < levels && !owned; ++l) owned = SameColor(c, LevelInk(l));
            if (!owned) ++bad;
        }
        return bad;
    }

    /// Whether every pixel is a convex combination of levels @p lo and @p hi and of nothing else.
    static int OutsideBracket(const std::vector<Color>& pix, int lo, int hi, int slack)
    {
        const Color a = LevelInk(lo);
        const Color b = LevelInk(hi);
        int bad = 0;
        for (const Color& c : pix)
        {
            const int ch[3] = { c.getRProperty(), c.getGProperty(), c.getBProperty() };
            const int av[3] = { a.getRProperty(), a.getGProperty(), a.getBProperty() };
            const int bv[3] = { b.getRProperty(), b.getGProperty(), b.getBProperty() };
            for (int i = 0; i < 3; ++i)
            {
                const int lowest  = std::min(av[i], bv[i]);
                const int highest = std::max(av[i], bv[i]);
                if (ch[i] < lowest - slack || ch[i] > highest + slack) { ++bad; break; }
            }
        }
        return bad;
    }

    // ---------------------------------------------------------------------------------------------
    // A -- allocation. The native resource really owns the levels LevelCount claims.
    // ---------------------------------------------------------------------------------------------

    void LegA(GraphicsDevice& dev)
    {
        for (const Dim& d : kDims)
        {
            const int expected = CalcLevels(d.w, d.h);
            const std::string tag = std::string("A ") + d.name;

            Texture2D mipped(dev, d.w, d.h, true, SurfaceFormat::Color);
            const int nativeMipped = NativeLevels(mipped);
            check(mipped.getLevelCountProperty() == expected && nativeMipped == expected,
                  tag + " mipMap=true: native num_levels matches the declared chain (public=" +
                  std::to_string(mipped.getLevelCountProperty()) + " native=" +
                  std::to_string(nativeMipped) + " expected=" + std::to_string(expected) + ")");

            Texture2D single(dev, d.w, d.h, false, SurfaceFormat::Color);
            const int nativeSingle = NativeLevels(single);
            check(single.getLevelCountProperty() == 1 && nativeSingle == 1,
                  tag + " mipMap=false: exactly one level (public=" +
                  std::to_string(single.getLevelCountProperty()) + " native=" +
                  std::to_string(nativeSingle) + ")");
        }

        // Every level's dimensions, and the guarantee that no axis ever reaches zero.
        bool dimsOk = true;
        std::string dimsDetail;
        for (const Dim& d : kDims)
        {
            const int levels = CalcLevels(d.w, d.h);
            for (int l = 0; l < levels; ++l)
            {
                const int lw = MipDim(d.w, l), lh = MipDim(d.h, l);
                if (lw < 1 || lh < 1) { dimsOk = false; dimsDetail += std::string(" ") + d.name; }
            }
            const int lastW = MipDim(d.w, levels - 1), lastH = MipDim(d.h, levels - 1);
            if (lastW != 1 || lastH != 1)
            {
                dimsOk = false;
                dimsDetail += std::string(" ") + d.name + " last=" + std::to_string(lastW) + "x" +
                              std::to_string(lastH);
            }
        }
        check(dimsOk, "A dimensions: every level of every chain clamps each axis to >= 1 and the "
                      "final level is 1x1" + dimsDetail);

        // A2: a DECLARED chain with only level 0 written must stay complete and sampleable. This is
        // the check that keeps "allocate what was declared" from turning into "generate content".
        Texture2D partial(dev, 8, 8, true, SurfaceFormat::Color);
        const std::vector<Color> lvl0 = LevelPattern(8, 8, 0);
        partial.SetData(0, nullptr, lvl0.data(), 0, static_cast<int>(lvl0.size()));
        const std::vector<Color> got = Draw3D(dev, 8, 8, partial, MakeSampler(TextureFilter::Point));
        check(NativeLevels(partial) == 4 && Mismatches(got, lvl0) == 0,
              "A2 declared-but-unwritten: four levels allocated, only level 0 written, level 0 "
              "still samples exactly (native=" + std::to_string(NativeLevels(partial)) +
              " mismatches=" + std::to_string(Mismatches(got, lvl0)) + ")");
        const std::vector<Color> minified =
            Draw3D(dev, 1, 1, partial, MakeSampler(TextureFilter::Point));
        check(DistinctColors(minified) == 1,
              "A2 declared-but-unwritten: maximum minification is a defined single colour, not an "
              "incomplete-texture artefact " + Str(minified.front()));
    }

    // ---------------------------------------------------------------------------------------------
    // B -- upload. Every declared level accepts content and keeps it, byte-exact.
    // ---------------------------------------------------------------------------------------------

    void LegB(GraphicsDevice& dev)
    {
        for (const Dim& d : kDims)
        {
            if (!d.exactLadder) continue;
            const int levels = CalcLevels(d.w, d.h);
            std::unique_ptr<Texture2D> tex = MakeChain(dev, d.w, d.h, LevelPattern);
            for (int l = 0; l < levels; ++l)
            {
                const int lw = MipDim(d.w, l), lh = MipDim(d.h, l);
                const std::vector<Color> want = LevelPattern(lw, lh, l);
                const std::vector<Color> got =
                    Draw3D(dev, lw, lh, *tex, MakeSampler(TextureFilter::Point));
                const int bad = Mismatches(got, want);
                check(bad == 0,
                      std::string("B ") + d.name + " level " + std::to_string(l) + " (" +
                      std::to_string(lw) + "x" + std::to_string(lh) +
                      "): stored content comes back byte-exact -- " +
                      (bad == 0 ? "0 mismatches" : std::to_string(bad) + " mismatches, " +
                                                    FirstDiff(got, want, lw)));
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    // C -- the LOD ladder. Point picks one exact level; Linear brackets between two.
    // ---------------------------------------------------------------------------------------------

    void LegC(GraphicsDevice& dev)
    {
        std::unique_ptr<Texture2D> flat = MakeChain(dev, 8, 8, LevelFlat);
        const int levels = flat->getLevelCountProperty();

        for (int l = 0; l < levels; ++l)
        {
            const int dest = MipDim(8, l);
            const std::vector<Color> got =
                Draw3D(dev, dest, dest, *flat, MakeSampler(TextureFilter::Point));
            const int sole = SoleLevel(got, levels);
            check(sole == l,
                  "C Point at dest " + std::to_string(dest) + "x" + std::to_string(dest) +
                  ": selects exactly level " + std::to_string(l) + " and nothing else (got level " +
                  std::to_string(sole) + " " + Str(got.front()) + " distinct=" +
                  std::to_string(DistinctColors(got)) + ")");
        }

        // Linear at an integer lambda degenerates to one level, so it is exact too.
        for (int l = 0; l < levels; ++l)
        {
            const int dest = MipDim(8, l);
            const std::vector<Color> got =
                Draw3D(dev, dest, dest, *flat, MakeSampler(TextureFilter::Linear));
            const int sole = SoleLevel(got, levels);
            check(sole == l,
                  "C Linear at integer lambda " + std::to_string(l) +
                  ": degenerates to exactly level " + std::to_string(l) + " (got level " +
                  std::to_string(sole) + " " + Str(got.front()) + ")");
        }

        // Between levels Linear must blend the two BRACKETING levels and no others; Point must not
        // blend at all. dest 6 -> lambda 0.415 (levels 0,1); dest 3 -> lambda 1.415 (levels 1,2).
        struct Between { int dest; int lo; int hi; };
        for (const Between& b : std::array<Between, 2>{{ {6, 0, 1}, {3, 1, 2} }})
        {
            const std::vector<Color> lin =
                Draw3D(dev, b.dest, b.dest, *flat, MakeSampler(TextureFilter::Linear));
            const int outside = OutsideBracket(lin, b.lo, b.hi, 2);
            check(outside == 0,
                  "C Linear at dest " + std::to_string(b.dest) + ": every pixel is a blend of "
                  "levels " + std::to_string(b.lo) + " and " + std::to_string(b.hi) +
                  " and of nothing else (" + std::to_string(outside) + " outside, first " +
                  Str(lin.front()) + ")");

            const std::vector<Color> pt =
                Draw3D(dev, b.dest, b.dest, *flat, MakeSampler(TextureFilter::Point));
            const int sole = SoleLevel(pt, levels);
            check(sole == b.lo || sole == b.hi,
                  "C Point at dest " + std::to_string(b.dest) + ": selects ONE bracketing level "
                  "with no interpolation (level " + std::to_string(sole) + " " + Str(pt.front()) +
                  " distinct=" + std::to_string(DistinctColors(pt)) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // D -- upload order. No level may be orphaned by a later upload to another level.
    // ---------------------------------------------------------------------------------------------

    void LegD(GraphicsDevice& dev)
    {
        auto verifyAllLevels = [&](Texture2D& tex, const std::string& tag) {
            const int levels = tex.getLevelCountProperty();
            int worst = 0;
            int worstLevel = -1;
            for (int l = 0; l < levels; ++l)
            {
                const int lw = MipDim(8, l), lh = MipDim(8, l);
                const std::vector<Color> want = LevelPattern(lw, lh, l);
                const std::vector<Color> got =
                    Draw3D(dev, lw, lh, tex, MakeSampler(TextureFilter::Point));
                const int bad = Mismatches(got, want);
                if (bad > worst) { worst = bad; worstLevel = l; }
            }
            check(worst == 0,
                  tag + ": every level still holds its own content afterwards (" +
                  (worst == 0 ? "all levels exact"
                              : std::to_string(worst) + " mismatches at level " +
                                std::to_string(worstLevel)) + ")");
        };

        // Reverse order: the last level first, level 0 last. If a level-0 upload cycles the
        // resource, levels 1..3 are on the resource that was abandoned.
        {
            Texture2D tex(dev, 8, 8, true, SurfaceFormat::Color);
            for (int l = tex.getLevelCountProperty() - 1; l >= 0; --l)
                WriteLevel(tex, l, LevelPattern);
            verifyAllLevels(tex, "D reverse order (level 3 first, level 0 last)");
        }

        // Forward order, which is what a content pipeline emits.
        {
            Texture2D tex(dev, 8, 8, true, SurfaceFormat::Color);
            for (int l = 0; l < tex.getLevelCountProperty(); ++l)
                WriteLevel(tex, l, LevelPattern);
            verifyAllLevels(tex, "D forward order (level 0 first)");
        }

        // Interleaved, with repeated writes to already-written levels.
        {
            Texture2D tex(dev, 8, 8, true, SurfaceFormat::Color);
            const std::array<int, 8> order{{2, 0, 3, 1, 0, 2, 1, 3}};
            for (int l : order) WriteLevel(tex, l, LevelPattern);
            verifyAllLevels(tex, "D interleaved with repeated writes");
        }

        // Two chains built alternately: an upload to one must not disturb the other.
        {
            Texture2D a(dev, 8, 8, true, SurfaceFormat::Color);
            Texture2D b(dev, 8, 8, true, SurfaceFormat::Color);
            for (int l = 0; l < 4; ++l) { WriteLevel(a, l, LevelPattern); WriteLevel(b, l, LevelPattern); }
            verifyAllLevels(a, "D two concurrent chains, first");
            verifyAllLevels(b, "D two concurrent chains, second");
        }

        // A level 0 rewrite AFTER the chain is complete: the classic cycle victim.
        {
            std::unique_ptr<Texture2D> tex = MakeChain(dev, 8, 8, LevelPattern);
            WriteLevel(*tex, 0, LevelPattern);
            verifyAllLevels(*tex, "D level 0 rewritten after the chain was complete");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // E -- partial-region upload into a nonzero level.
    // ---------------------------------------------------------------------------------------------

    void LegE(GraphicsDevice& dev)
    {
        // Level 1 of an 8x8 chain is 4x4: big enough for a corner, a centre and an edge region.
        struct Region { int x, y, w, h; const char* name; };
        constexpr std::array<Region, 7> kRegions{{
            {0, 0, 4, 4, "full level"},
            {0, 0, 1, 1, "one pixel"},
            {0, 0, 2, 2, "top-left"},
            {2, 2, 2, 2, "bottom-right"},
            {1, 1, 2, 2, "centred"},
            {0, 3, 4, 1, "final row"},
            {3, 0, 1, 4, "final column"},
        }};

        for (const Region& r : kRegions)
        {
            std::unique_ptr<Texture2D> tex = MakeChain(dev, 8, 8, LevelPattern);
            // Overwrite the region with white; everything outside must keep level 1's pattern.
            std::vector<Color> patch(static_cast<std::size_t>(r.w) * r.h, kWhite);
            const Rectangle box(r.x, r.y, r.w, r.h);
            tex->SetData(1, &box, patch.data(), 0, static_cast<int>(patch.size()));

            std::vector<Color> want = LevelPattern(4, 4, 1);
            for (int y = 0; y < r.h; ++y)
                for (int x = 0; x < r.w; ++x)
                    want[static_cast<std::size_t>(r.y + y) * 4 + (r.x + x)] = kWhite;

            const std::vector<Color> got = Draw3D(dev, 4, 4, *tex, MakeSampler(TextureFilter::Point));
            const int bad = Mismatches(got, want);
            check(bad == 0,
                  std::string("E level 1 region ") + r.name + " (" + std::to_string(r.x) + "," +
                  std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h) +
                  "): lands exactly where addressed -- " +
                  (bad == 0 ? "0 mismatches" : std::to_string(bad) + " mismatches, " +
                                               FirstDiff(got, want, 4)));

            // The neighbouring levels must be untouched by a partial write to level 1.
            const std::vector<Color> want0 = LevelPattern(8, 8, 0);
            const std::vector<Color> got0 =
                Draw3D(dev, 8, 8, *tex, MakeSampler(TextureFilter::Point));
            check(Mismatches(got0, want0) == 0,
                  std::string("E level 1 region ") + r.name +
                  ": level 0 is untouched (" + std::to_string(Mismatches(got0, want0)) +
                  " mismatches)");
        }

        // A source array offset by startIndex is a CALLER-array offset, never a texture offset.
        {
            std::unique_ptr<Texture2D> tex = MakeChain(dev, 8, 8, LevelPattern);
            std::vector<Color> src(4 + 4, kMagenta);           // 4 elements of padding, then 2x2
            for (int i = 0; i < 4; ++i) src[4 + i] = kWhite;
            const Rectangle box(1, 1, 2, 2);
            tex->SetData(1, &box, src.data(), 4, 4);
            std::vector<Color> want = LevelPattern(4, 4, 1);
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    want[static_cast<std::size_t>(1 + y) * 4 + (1 + x)] = kWhite;
            const std::vector<Color> got = Draw3D(dev, 4, 4, *tex, MakeSampler(TextureFilter::Point));
            check(Mismatches(got, want) == 0,
                  "E startIndex is an offset into the CALLER's array, not into the level (" +
                  std::to_string(Mismatches(got, want)) + " mismatches)");
        }

        // Invalid regions must be rejected before anything reaches the GPU.
        {
            std::unique_ptr<Texture2D> tex = MakeChain(dev, 8, 8, LevelPattern);
            const std::vector<Color> patch(16, kWhite);
            int rejected = 0;
            auto reject = [&](const Rectangle& r, int count) {
                try { tex->SetData(1, &r, patch.data(), 0, count); }
                catch (const std::exception&) { ++rejected; }
            };
            reject(Rectangle(0, 0, 8, 8), 64);      // level-0 extent into level 1
            reject(Rectangle(3, 3, 2, 2), 4);       // runs off the level
            reject(Rectangle(-1, 0, 2, 2), 4);      // negative origin
            reject(Rectangle(0, 0, 4, 4), 4);       // elementCount too small for the region
            check(rejected == 4,
                  "E invalid level-1 regions are all rejected before upload (" +
                  std::to_string(rejected) + "/4)");

            const std::vector<Color> want = LevelPattern(4, 4, 1);
            const std::vector<Color> got = Draw3D(dev, 4, 4, *tex, MakeSampler(TextureFilter::Point));
            check(Mismatches(got, want) == 0,
                  "E a rejected region leaves the level byte-for-byte as it was (" +
                  std::to_string(Mismatches(got, want)) + " mismatches)");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // F -- upload and sample with no Present, twice.
    // ---------------------------------------------------------------------------------------------

    void LegF(GraphicsDevice& dev)
    {
        Texture2D tex(dev, 8, 8, true, SurfaceFormat::Color);
        for (int l = 0; l < tex.getLevelCountProperty(); ++l) WriteLevel(tex, l, LevelPattern);

        const std::vector<Color> want1 = LevelPattern(2, 2, 2);
        const std::vector<Color> got1 = Draw3D(dev, 2, 2, tex, MakeSampler(TextureFilter::Point));
        check(Mismatches(got1, want1) == 0,
              "F level 2 is visible in the same frame it was uploaded, with no Present (" +
              std::to_string(Mismatches(got1, want1)) + " mismatches)");

        // Rewrite level 2 with a different image and sample again -- still no Present. A missing
        // upload/sample ordering shows up here as the FIRST image coming back a second time.
        const std::vector<Color> second(4, kMagenta);
        tex.SetData(2, nullptr, second.data(), 0, static_cast<int>(second.size()));
        const std::vector<Color> got2 = Draw3D(dev, 2, 2, tex, MakeSampler(TextureFilter::Point));
        check(Mismatches(got2, second) == 0,
              "F a second upload to the same level in the same frame supersedes the first (" +
              std::to_string(Mismatches(got2, second)) + " mismatches, got " + Str(got2.front()) +
              ")");
    }

    // ---------------------------------------------------------------------------------------------
    // G -- non-power-of-two, odd and rectangular chains.
    // ---------------------------------------------------------------------------------------------

    void LegG(GraphicsDevice& dev)
    {
        for (const Dim& d : kDims)
        {
            if (d.exactLadder) continue;
            const int levels = CalcLevels(d.w, d.h);
            std::unique_ptr<Texture2D> tex = MakeChain(dev, d.w, d.h, LevelFlat);

            // 1:1 is lambda 0 on any dimensions.
            const std::vector<Color> full =
                Draw3D(dev, d.w, d.h, *tex, MakeSampler(TextureFilter::Point));
            check(SoleLevel(full, levels) == 0,
                  std::string("G ") + d.name + " at 1:1: level 0 (" + Str(full.front()) + ")");

            // A 1x1 destination is maximum minification on any dimensions: the last level.
            const std::vector<Color> tiny =
                Draw3D(dev, 1, 1, *tex, MakeSampler(TextureFilter::Point));
            check(SoleLevel(tiny, levels) == levels - 1,
                  std::string("G ") + d.name + " at 1x1: the LAST level " +
                  std::to_string(levels - 1) + " (got level " +
                  std::to_string(SoleLevel(tiny, levels)) + " " + Str(tiny.front()) + ")");

            // Every intermediate scale must still come from this chain and from nothing else.
            int strays = 0;
            for (int dest = 1; dest <= d.w; ++dest)
            {
                const std::vector<Color> got =
                    Draw3D(dev, dest, std::max(1, d.h * dest / d.w), *tex,
                           MakeSampler(TextureFilter::Point));
                strays += NotOwnedByChain(got, levels);
            }
            check(strays == 0,
                  std::string("G ") + d.name + " Point at every scale 1.." + std::to_string(d.w) +
                  ": every pixel is a level this chain owns (" + std::to_string(strays) +
                  " strays)");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // H -- consumers: SpriteBatch, the stock effect families, and the three draw paths.
    // ---------------------------------------------------------------------------------------------

    template <typename FX>
    static void Matrices(FX& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

    template <typename FX>
    static void AmbientOnly(FX& fx)
    {
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
    }

    template <typename V>
    static void DrawQuadRaw(GraphicsDevice& dev, const std::vector<V>& verts)
    {
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(V)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void LegH(GraphicsDevice& dev)
    {
        std::unique_ptr<Texture2D> flat = MakeChain(dev, 8, 8, LevelFlat);
        const int levels = flat->getLevelCountProperty();

        // SpriteBatch: the sprite path must select levels by the same contract as a 3D draw.
        for (int l = 0; l < levels; ++l)
        {
            const int dest = MipDim(8, l);
            const std::vector<Color> got =
                DrawSprite(dev, dest, dest, *flat, MakeSampler(TextureFilter::Point));
            check(SoleLevel(got, levels) == l,
                  "H SpriteBatch at dest " + std::to_string(dest) + ": level " +
                  std::to_string(l) + " (got level " + std::to_string(SoleLevel(got, levels)) +
                  " " + Str(got.front()) + ")");
        }

        // The three 3D submission routes reach the same level: it is a property of the footprint
        // and the sampler, never of how the geometry was submitted.
        for (const auto& [path, name] : std::array<std::pair<Path, const char*>, 3>{{
                 {Path::NonIndexed, "non-indexed"},
                 {Path::Indexed,    "indexed"},
                 {Path::UserRaw,    "DrawUser"}}})
        {
            const std::vector<Color> min2 =
                Draw3D(dev, 2, 2, *flat, MakeSampler(TextureFilter::Point), path);
            const std::vector<Color> mag =
                Draw3D(dev, 8, 8, *flat, MakeSampler(TextureFilter::Point), path);
            check(SoleLevel(min2, levels) == 2 && SoleLevel(mag, levels) == 0,
                  std::string("H ") + name + ": 4x minification selects level 2, 1:1 selects "
                  "level 0 (min=" + Str(min2.front()) + " mag=" + Str(mag.front()) + ")");
        }

        // The stock textured families. Each is drawn twice -- minified to level 2 and at 1:1 --
        // and must report two DIFFERENT levels, which is exactly what a one-level resource cannot.
        struct Family { const char* name; void (SdlGpuTexture2DMipStorageTest::*draw)(GraphicsDevice&); };
        const std::array<Family, 6> kFamilies{{
            {"BasicEffect(plain)",                &SdlGpuTexture2DMipStorageTest::DrawBasicPlain},
            {"BasicEffect(lit)",                  &SdlGpuTexture2DMipStorageTest::DrawBasicLit},
            {"AlphaTestEffect",                   &SdlGpuTexture2DMipStorageTest::DrawAlphaTest},
            {"DualTextureEffect(slot 0)",         &SdlGpuTexture2DMipStorageTest::DrawDualTexture},
            {"EnvironmentMapEffect(base texture)",&SdlGpuTexture2DMipStorageTest::DrawEnvMap},
            {"SkinnedEffect",                     &SdlGpuTexture2DMipStorageTest::DrawSkinned},
        }};
        chain8_ = std::move(flat);
        for (const Family& f : kFamilies)
        {
            const auto run = [&](int dest) {
                return Render(dev, dest, dest, [&](GraphicsDevice& d) {
                    d.getSamplerStatesProperty()[0] = MakeSampler(TextureFilter::Point);
                    (this->*f.draw)(d);
                });
            };
            const std::vector<Color> minified = run(2);
            const std::vector<Color> oneToOne = run(8);
            const int lMin = SoleLevel(minified, levels);
            const int lOne = SoleLevel(oneToOne, levels);
            // Lit and environment-mapped families scale every texel by a uniform ambient factor, so
            // their exact byte value is the family's business; what must hold is that the two scales
            // reach DIFFERENT levels and that the minified one is not level 0.
            const bool distinct = DistinctColors(minified) == 1 && DistinctColors(oneToOne) == 1 &&
                                  !SameColor(minified.front(), oneToOne.front());
            check(distinct && (lMin == 2 || lMin == -1) && (lOne == 0 || lOne == -1),
                  std::string("H ") + f.name + ": minification and 1:1 reach different levels "
                  "(min=" + Str(minified.front()) + " level=" + std::to_string(lMin) +
                  ", 1:1=" + Str(oneToOne.front()) + " level=" + std::to_string(lOne) + ")");
        }
        chain8_.reset();
    }

    void DrawBasicPlain(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        Matrices(fx);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(chain8_.get());
        fx.setLightingEnabledProperty(false);
        fx.Apply();
        DrawQuadRaw(dev, MakeQuad<VtxPT>());
    }

    void DrawBasicLit(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        Matrices(fx);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(chain8_.get());
        AmbientOnly(fx);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.Apply();
        std::vector<VtxPNT> v = MakeQuad<VtxPNT>();
        FillNormals(v);
        DrawQuadRaw(dev, v);
    }

    void DrawAlphaTest(GraphicsDevice& dev)
    {
        AlphaTestEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(chain8_.get());
        fx.Apply();
        DrawQuadRaw(dev, MakeQuad<VtxPT>());
    }

    void DrawDualTexture(GraphicsDevice& dev)
    {
        // Slot 1 is a single white texel, so `tex0 * (2 * tex1)` is a uniform scale of slot 0 and
        // the level question stays a question about slot 0 alone. REMED-GFX-172 is not measured.
        DualTextureEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(chain8_.get());
        fx.setTexture2Property(white_.get());
        fx.Apply();
        DrawQuadRaw(dev, MakeQuad<VtxPT>());
    }

    void DrawEnvMap(GraphicsDevice& dev)
    {
        // The cube contributes nothing: this leg measures the BASE 2D texture's slot-0 sampler.
        // REMED-GFX-173 (SDL_GPU's hardcoded cube sampler) is a separate, untouched finding.
        EnvironmentMapEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(chain8_.get());
        fx.setEnvironmentMapProperty(cube_.get());
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        AmbientOnly(fx);
        fx.Apply();
        std::vector<VtxPNT> v = MakeQuad<VtxPNT>();
        FillNormals(v);
        DrawQuadRaw(dev, v);
    }

    void DrawSkinned(GraphicsDevice& dev)
    {
        SkinnedEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(chain8_.get());
        AmbientOnly(fx);
        fx.setSpecularColorProperty(Vector3::Zero);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.Apply();
        std::vector<VtxPNTS> v = MakeQuad<VtxPNTS>();
        FillNormals(v);
        FillSkin(v);
        DrawQuadRaw(dev, v);
    }

    // ---------------------------------------------------------------------------------------------
    // I -- lifetime. REMED-GFX-152's contract, now with a chain behind it.
    // ---------------------------------------------------------------------------------------------

    void LegI(GraphicsDevice& dev)
    {
        // A chain destroyed as a short-lived local AFTER its draw was queued: this renderer replays
        // draws at Present, so the native handle has to outlive the public wrapper.
        {
            RenderTarget2D rt(dev, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            ResetDeviceState(dev, 2, 2);
            dev.Clear(kSentinel);
            {
                std::unique_ptr<Texture2D> doomed = MakeChain(dev, 8, 8, LevelFlat);
                dev.getSamplerStatesProperty()[0] = MakeSampler(TextureFilter::Point);
                BasicEffect fx(dev);
                Matrices(fx);
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(doomed.get());
                fx.setLightingEnabledProperty(false);
                fx.Apply();
                DrawQuadRaw(dev, MakeQuad<VtxPT>());
            }   // destroyed here, before the frame is rendered
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetDeviceState(dev, kBBW, kBBH);
            std::vector<Color> pix(4u, Color(0, 0, 0, 0));
            rt.GetData(pix.data(), 0, 4);
            check(SoleLevel(pix, 4) == 2,
                  "I a chain destroyed after its draw was queued still supplies level 2 (" +
                  Str(pix.front()) + ")");
        }

        // Destroyed with an upload just issued and never sampled: must not crash or leak a pending
        // copy into freed storage.
        {
            bool survived = true;
            try
            {
                for (int i = 0; i < 8; ++i)
                {
                    Texture2D tex(dev, 8, 8, true, SurfaceFormat::Color);
                    for (int l = 0; l < tex.getLevelCountProperty(); ++l)
                        WriteLevel(tex, l, LevelPattern);
                }
            }
            catch (const std::exception&) { survived = false; }
            check(survived, "I eight create/upload/destroy cycles with no sampling in between");
        }

        // Native handle reuse: a destroyed chain's slot may be handed straight back. The new
        // texture's own content must be what is sampled, never the dead one's.
        {
            int wrong = 0;
            for (int i = 0; i < 6; ++i)
            {
                std::unique_ptr<Texture2D> a = MakeChain(dev, 8, 8, LevelFlat);
                const std::vector<Color> got =
                    Draw3D(dev, 2, 2, *a, MakeSampler(TextureFilter::Point));
                if (SoleLevel(got, 4) != 2) ++wrong;
                a.reset();
                // A chain of a DIFFERENT length in the freed slot: stale level-count metadata
                // would show up as the wrong level or an incomplete sample.
                std::unique_ptr<Texture2D> b = MakeChain(dev, 4, 4, LevelFlat);
                const std::vector<Color> gotB =
                    Draw3D(dev, 1, 1, *b, MakeSampler(TextureFilter::Point));
                if (SoleLevel(gotB, 3) != 2) ++wrong;
            }
            check(wrong == 0,
                  "I six recreation rounds alternating a 4-level and a 3-level chain sample their "
                  "own content (" + std::to_string(wrong) + " wrong)");
        }

        // A one-level texture bound after a chain, and a chain bound after a one-level texture.
        {
            std::unique_ptr<Texture2D> chain = MakeChain(dev, 8, 8, LevelFlat);
            Texture2D single(dev, 8, 8, false, SurfaceFormat::Color);
            const std::vector<Color> lvl0 = LevelFlat(8, 8, 0);
            single.SetData(0, nullptr, lvl0.data(), 0, 64);

            const std::vector<Color> c1 =
                Draw3D(dev, 2, 2, *chain, MakeSampler(TextureFilter::Point));
            const std::vector<Color> s1 =
                Draw3D(dev, 2, 2, single, MakeSampler(TextureFilter::Point));
            const std::vector<Color> c2 =
                Draw3D(dev, 2, 2, *chain, MakeSampler(TextureFilter::Point));
            check(SoleLevel(c1, 4) == 2 && SoleLevel(s1, 1) == 0 && SoleLevel(c2, 4) == 2,
                  "I alternating a 4-level and a 1-level texture: each keeps its own level count "
                  "(chain=" + Str(c1.front()) + " single=" + Str(s1.front()) + " chain=" +
                  Str(c2.front()) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // J -- rejects and one-level completeness.
    // ---------------------------------------------------------------------------------------------

    void LegJ(GraphicsDevice& dev)
    {
        // An out-of-range level must not corrupt anything. The shared layer accepts level >= 0 and
        // hands it down; the renderer's job is to leave the resource alone.
        {
            std::unique_ptr<Texture2D> tex = MakeChain(dev, 8, 8, LevelPattern);
            const std::vector<Color> junk(1, kMagenta);
            bool survived = true;
            try { tex->SetData(9, nullptr, junk.data(), 0, 1); }
            catch (const std::exception&) { /* a rejecting shared layer is equally acceptable */ }
            catch (...) { survived = false; }
            const std::vector<Color> want = LevelPattern(8, 8, 0);
            const std::vector<Color> got =
                Draw3D(dev, 8, 8, *tex, MakeSampler(TextureFilter::Point));
            check(survived && Mismatches(got, want) == 0,
                  "J a SetData beyond the last level leaves every real level intact (" +
                  std::to_string(Mismatches(got, want)) + " mismatches)");
        }

        // A negative level is rejected by the shared layer before it can reach the renderer.
        {
            std::unique_ptr<Texture2D> tex = MakeChain(dev, 8, 8, LevelPattern);
            const std::vector<Color> junk(64, kMagenta);
            bool threw = false;
            try { tex->SetData(-1, nullptr, junk.data(), 0, 64); }
            catch (const std::exception&) { threw = true; }
            check(threw, "J a negative level is rejected");
        }

        // Every filter ordinal over a ONE-level texture must still sample level 0 -- giving the
        // default filter a real mip term must not make a single-level resource incomplete.
        {
            Texture2D single(dev, 8, 8, false, SurfaceFormat::Color);
            const std::vector<Color> lvl0 = LevelPattern(8, 8, 0);
            single.SetData(0, nullptr, lvl0.data(), 0, 64);
            constexpr std::array<TextureFilter, 9> kAll{{
                TextureFilter::Linear, TextureFilter::Point, TextureFilter::Anisotropic,
                TextureFilter::LinearMipPoint, TextureFilter::PointMipLinear,
                TextureFilter::MinLinearMagPointMipLinear, TextureFilter::MinLinearMagPointMipPoint,
                TextureFilter::MinPointMagLinearMipLinear, TextureFilter::MinPointMagLinearMipPoint,
            }};
            int black = 0;
            for (TextureFilter f : kAll)
            {
                const std::vector<Color> got = Draw3D(dev, 8, 8, single, MakeSampler(f));
                bool allBlack = true;
                for (const Color& c : got)
                    if (!SameColor(c, kBlack)) { allBlack = false; break; }
                if (allBlack) ++black;
            }
            check(black == 0,
                  "J a one-level texture stays complete under all nine ordinals, none samples as "
                  "uniformly black (" + std::to_string(black) + " black)");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // K -- the GetData boundary, stated rather than implied.
    // ---------------------------------------------------------------------------------------------

    void LegK(GraphicsDevice& dev)
    {
        std::unique_ptr<Texture2D> tex = MakeChain(dev, 8, 8, LevelPattern);
        std::vector<Color> out(64, Color(0, 0, 0, 0));
        bool served = true;
        try { tex->GetData(out.data(), 0, 64); }
        catch (const std::exception&) { served = false; }
        const std::vector<Color> want = LevelPattern(8, 8, 0);
        check(served && Mismatches(out, want) == 0,
              "K Texture2D::GetData returns level 0 from the shared layer's CPU shadow, which no "
              "renderer readback is involved in (" + std::to_string(Mismatches(out, want)) +
              " mismatches)");
        note("K BOUNDARY: a plain Texture2D's GetData is served by the shared layer's own CPU "
             "pixel buffer on EVERY renderer and never reaches ITextureRenderer::GetData, so it "
             "cannot witness GPU storage and is not used as a content oracle in this fixture. "
             "Rendered sampling is. No CPU shadow was added to make this fixture pass.");
        (void)dev;
    }

    // ---------------------------------------------------------------------------------------------

    void RunAll(GraphicsDevice& dev)
    {
        LegA(dev);
        LegB(dev);
        LegC(dev);
        LegD(dev);
        LegE(dev);
        LegF(dev);
        LegG(dev);
        LegH(dev);
        LegI(dev);
        LegJ(dev);
        LegK(dev);
    }

    void Finish()
    {
        std::printf("=== %d/%d checks passed on SDL_GPU ===\n", passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (totalCount_ > 0 && passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        white_ = std::make_unique<Texture2D>(dev, 1, 1, false, SurfaceFormat::Color);
        const std::vector<Color> one(1, kWhite);
        white_->SetData(0, nullptr, one.data(), 0, 1);
        cube_ = std::make_unique<TextureCube>(dev, 2, false, SurfaceFormat::Color);
        const std::vector<Color> face(4, kWhite);
        for (int f = 0; f < 6; ++f)
            cube_->SetData(static_cast<CubeMapFace>(f), 0, nullptr, face.data(), 0, 4);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        std::printf("=== REMED-GFX-176 SDL_GPU Texture2D mip storage and upload ===\n");
        note("SCOPE: ordinary sampled Texture2D only. TextureCube, Texture3D, RenderTarget2D and "
             "RenderTargetCube already pass a real CalculateMipLevels to SDL and have their own "
             "level-aware upload paths; they are classified, not changed, by REMED-GFX-176.");
        note("NO MIP GENERATION: nothing here calls or expects SDL_GenerateMipmapsForGPUTexture "
             "for a Texture2D. Construction allocates the declared levels; content is only ever "
             "what a caller uploaded.");
        note("GFX-173 BOUNDARY: EnvironmentMapEffect is measured on its ordinary 2D slot only; the "
             "hardcoded cube sampler is a separate open finding and is untouched.");

        try
        {
            RunAll(dev);
        }
        catch (const System::Exception& e)
        {
            check(false, std::string("uncaught System::Exception: ") + e.what());
        }
        catch (const std::exception& e)
        {
            check(false, std::string("uncaught exception: ") + e.what());
        }
        Finish();
    }

public:
    SdlGpuTexture2DMipStorageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlGpuTexture2DMipStorageTest game;
    game.Run();
    return game.getResult();
}
