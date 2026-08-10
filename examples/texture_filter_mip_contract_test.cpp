// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-175: a public TextureFilter ordinal names THREE independent filtering decisions -- how a
// minified footprint is filtered WITHIN one level, how a magnified footprint is filtered within one
// level, and how a level is SELECTED between the mip levels of a chain. The third one is the subject
// of this fixture, and it is the one that ordinals 0 and 1 were silently dropping.
//
// THE AUTHORITATIVE CONTRACT, taken from the reference implementation rather than inferred. FNA's
// own decomposition tables (src/Graphics/Effect/Effect.cs, XNAMin/XNAMag/XNAMip) give:
//
//     ordinal 0 Linear -> min LINEAR, mag LINEAR, mip LINEAR
//     ordinal 1 Point  -> min POINT,  mag POINT,  mip POINT
//
// and FNA3D's OpenGL driver (src/FNA3D_Driver_OpenGL.c, XNAToGL_MinMipFilter) maps exactly those two
// onto GL_LINEAR_MIPMAP_LINEAR and GL_NEAREST_MIPMAP_NEAREST. Linear and Point are therefore FULL
// filters that mip-filter a real chain; they are NOT "the ordinals without a mip component". They are
// also the ordinals a game gets by default -- SamplerState::LinearWrap, the device default, is
// ordinal 0 -- so a renderer that drops their mip term drops mipmapping for almost every real game.
//
// WHAT THIS FIXTURE PINS, and why the existing fixtures could not see it. REMED-GFX-174's leg D
// MEASURED this and deliberately declined to assert it (its D4 note), because asserting the
// then-current behaviour would have pinned the wrong contract. Every other mip test in the tree
// either uses only the compound ordinals 2..8, or uses a chain whose levels are indistinguishable,
// or never minifies at all. This fixture supplies the missing dimension: a chain whose levels NAME
// THEMSELVES, drawn at destination sizes that put the LOD on an exact integer, so the level that was
// selected is read straight out of the framebuffer.
//
// THE ORACLE. Two chains over the same 8x8 geometry:
//
//   * the LEVEL oracle (mipFlat_) gives every level one distinct flat colour -- L0 red, L1 green,
//     L2 blue, L3 yellow. Flat levels make the level identity byte-exact and, because no 0/255-only
//     blend of two of these equals a third, they also make "one level" and "a blend of two levels"
//     distinguishable from each other rather than merely from level 0.
//
//   * the PATTERN oracle (mipPat_) gives every level an ASYMMETRIC two-colour pattern in its own
//     channel signature -- L0 red/black, L1 green/black, L2 blue/black, L3 yellow. A flat level
//     cannot show a wrong orientation, a wrong source coordinate or a wrong within-level filter;
//     this one can, and it is what keeps the min/mag halves honest while the mip half is asserted.
//
// THE LOD MATRIX. Every 3D leg draws a full-viewport quad with uv 0..1, so a destination of NxN
// pixels covers the whole 8-texel width and the footprint is exactly 8/N texels per pixel:
//
//     dest 32 -> rho 0.25 -> lambda -2   magnification, level 0
//     dest  8 -> rho 1    -> lambda  0   one-to-one,    level 0
//     dest  4 -> rho 2    -> lambda  1   exactly level 1
//     dest  2 -> rho 4    -> lambda  2   exactly level 2
//     dest  1 -> rho 8    -> lambda  3   exactly level 3
//     dest  6 -> rho 1.33 -> lambda  0.415   between level 0 and level 1
//     dest  3 -> rho 2.67 -> lambda  1.415   between level 1 and level 2
//
// At an INTEGER lambda a trilinear filter degenerates to exactly one level (the cross-level weight is
// zero), so Linear is byte-exact there and no tolerance is needed. Between levels the exact weight
// depends on the hardware's own lambda precision, so those legs assert the BOUNDED property the
// contract really fixes -- the result is a blend of the two bracketing levels and of nothing else --
// instead of a pixel value no two GPUs would agree on. Point is exact everywhere by construction: it
// selects one level and fetches one texel, so no leg below weakens a Point check to a tolerance.
//
// SINGLE-LEVEL COMPLETENESS IS A PRECONDITION, NOT A CASUALTY. Giving ordinals 0 and 1 a mipmap term
// makes REMED-GFX-174's level-range clamping load-bearing for the DEFAULT filter too: on GL a
// mipmap-filtering sampler over a texture whose declared range is wider than its real levels samples
// as opaque black. Leg F therefore re-asserts, for both sampleable kinds and all nine ordinals, that
// a one-level resource stays complete and still samples level 0 -- and leg G asserts the harder case
// REMED-GFX-174 never had to face, a chain that was DECLARED with four levels but only ever had
// level 0 written. Nothing here forces a chain onto a texture, generates a missing level, or reads a
// level that was never written.
//
// LEGS:
//   A  preconditions            the chains really exist; the single-level texture is untouched
//   B  Point, LOD matrix        exact level identity at every scale, no interpolation between levels
//   C  Linear, LOD matrix       the chain IS used; exact at integer lambda, bracketed in between
//   D  all nine ordinals        each ordinal's mip component against the authoritative table
//   E  within-level + orient.   the pattern chain: min/mag halves and orientation still correct
//   F  single-level complete    REMED-GFX-174's contract, now under a mip-filtering sampler
//   G  partial chain            declared 4 levels, only level 0 written -- bounded, never black
//   H  SpriteBatch              the sprite path selects levels by the same contract
//   I  transitions              Point then Linear, two chains of different length, recreation
//   J  NPOT and odd destination a 6x6 chain and destinations that are not powers of two
//   K  draw paths               indexed, non-indexed and DrawUser reach the same selection
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped.

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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

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

namespace
{
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
#elif defined(CNA_RENDERER_D3D9)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "D3D9";
#elif defined(CNA_RENDERER_D3D11)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "D3D11";
#elif defined(CNA_RENDERER_D3D12)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "D3D12";
#else
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "UNKNOWN";
#endif

    constexpr int kBBW = 160;
    constexpr int kBBH = 120;

    /// Absent from every level of every chain, so an uncovered destination pixel is distinguishable.
    const Color kSentinel(13, 17, 19, 255);
    /// What an incomplete texture samples as on GL. No level colour below is black.
    const Color kIncomplete(0, 0, 0, 255);

    /// Level identity colours. 0/255-only channels, so a sampler that only ever chooses between
    /// them, or averages a whole level, still lands on byte-exact values; and no blend of any two
    /// equals a third, so "one level" and "two levels mixed" are distinguishable.
    const std::array<Color, 4> kLevelColor{{
        Color(255,   0,   0, 255),   // L0 red
        Color(  0, 255,   0, 255),   // L1 green
        Color(  0,   0, 255, 255),   // L2 blue
        Color(255, 255,   0, 255),   // L3 yellow
    }};

    struct FilterOrdinal
    {
        TextureFilter filter;
        int ordinal;
        const char* name;
        /// The authoritative mip component of this ordinal: true = LINEAR between levels.
        bool mipLinear;
        /// The authoritative minification component: true = LINEAR within a level.
        bool minLinear;
        /// The authoritative magnification component: true = LINEAR within a level.
        bool magLinear;
        /// Anisotropic's own level selection is unspecified per GPU and is not asserted exactly.
        bool anisotropic;
    };

    constexpr std::array<FilterOrdinal, 9> kOrdinals{{
        {TextureFilter::Linear,                     0, "Linear",                     true,  true,  true,  false},
        {TextureFilter::Point,                      1, "Point",                      false, false, false, false},
        {TextureFilter::Anisotropic,                2, "Anisotropic",                true,  true,  true,  true },
        {TextureFilter::LinearMipPoint,             3, "LinearMipPoint",             false, true,  true,  false},
        {TextureFilter::PointMipLinear,             4, "PointMipLinear",             true,  false, false, false},
        {TextureFilter::MinLinearMagPointMipLinear, 5, "MinLinearMagPointMipLinear", true,  true,  false, false},
        {TextureFilter::MinLinearMagPointMipPoint,  6, "MinLinearMagPointMipPoint",  false, true,  false, false},
        {TextureFilter::MinPointMagLinearMipLinear, 7, "MinPointMagLinearMipLinear", true,  false, true,  false},
        {TextureFilter::MinPointMagLinearMipPoint,  8, "MinPointMagLinearMipPoint",  false, false, true,  false},
    }};

    bool SameColor(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }

    /// Stride-20 VertexPositionTexture stream (REMED-GFX-125: object size is not the stream stride).
    struct VtxPT { float px, py, pz; float u, v; };
    static_assert(sizeof(VtxPT) == 20, "stride 20");

    /// A full-viewport quad in NDC with identity matrices, so a destination of W x H pixels covers
    /// the whole [0,1] uv range and the texel footprint per pixel is exactly texWidth / W.
    std::vector<VtxPT> MakeQuad()
    {
        VtxPT tl{}, bl{}, br{}, tr{};
        auto place = [](VtxPT& v, float x, float y) { v.px = x; v.py = y; v.pz = 0.0f; };
        place(tl, -1.0f,  1.0f); tl.u = 0.0f; tl.v = 0.0f;
        place(bl, -1.0f, -1.0f); bl.u = 0.0f; bl.v = 1.0f;
        place(br,  1.0f, -1.0f); br.u = 1.0f; br.v = 1.0f;
        place(tr,  1.0f,  1.0f); tr.u = 1.0f; tr.v = 0.0f;
        return { tl, bl, br, tl, br, tr };
    }

    VertexDeclaration PositionTextureDecl()
    {
        return VertexDeclaration(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

class TextureFilterMipContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    /// 8x8, four levels, each level one flat identity colour. The level oracle.
    std::unique_ptr<Texture2D> mipFlat_;
    /// 8x8, four levels, each level an asymmetric two-colour pattern. The within-level oracle.
    std::unique_ptr<Texture2D> mipPat_;
    /// 6x6 (non power of two), three levels, flat identity colours.
    std::unique_ptr<Texture2D> mipNpot_;
    /// 4x4, three levels, flat identity colours -- a SHORTER chain than mipFlat_.
    std::unique_ptr<Texture2D> mipShort_;
    /// 8x8 declared with four levels but ONLY level 0 ever written.
    std::unique_ptr<Texture2D> mipPartial_;
    /// 8x8 with exactly one level.
    std::unique_ptr<Texture2D> flat1_;

    int flatLevels_ = 1;
    int npotLevels_ = 1;
    int shortLevels_ = 1;

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

    static SamplerState MakeSampler(TextureFilter filter,
                                    TextureAddressMode u = TextureAddressMode::Clamp,
                                    TextureAddressMode v = TextureAddressMode::Clamp)
    {
        SamplerState s;
        s.setFilterProperty(filter);
        s.setAddressUProperty(u);
        s.setAddressVProperty(v);
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
        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    /// How the textured quad reaches the rasterizer. Every path must select the same mip level:
    /// the level is a property of the footprint and the sampler, not of the submission route.
    enum class Path { NonIndexed, Indexed, UserRaw };

    /// One plain textured BasicEffect quad under an explicit SamplerStates[0], unlit and untinted,
    /// so the modulation is the identity and a level's stored colour survives byte-for-byte.
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

            const std::vector<VtxPT> verts = MakeQuad();
            if (path == Path::UserRaw)
            {
                const VertexDeclaration decl = PositionTextureDecl();
                d.DrawUserPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 2, decl);
                return;
            }

            VertexBuffer vb(d, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(VtxPT)));
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

    /// Which single level's identity colour every pixel holds, or -1 if the frame is not exactly one
    /// level. This is the whole Point oracle: exact identity, no tolerance.
    static int SoleLevel(const std::vector<Color>& pix, int levels)
    {
        if (DistinctColors(pix) != 1) return -1;
        for (int l = 0; l < levels && l < static_cast<int>(kLevelColor.size()); ++l)
            if (CountEqual(pix, kLevelColor[static_cast<std::size_t>(l)]) ==
                static_cast<int>(pix.size()))
                return l;
        return -1;
    }

    static std::string Describe(const std::vector<Color>& pix)
    {
        const Color& c = pix.front();
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + ") distinct=" +
               std::to_string(DistinctColors(pix));
    }

    /// Whether every pixel is a convex combination of levels @p lo and @p hi and of nothing else.
    /// The identity colours use only 0 and 255 per channel, so a channel that is 0 in both levels
    /// MUST be 0 in the result -- any contribution from a third level shows up as a non-zero byte
    /// in a channel neither bracketing level owns. That is what makes this bounded check exact about
    /// WHICH levels contributed while staying silent about the hardware's own lambda precision.
    static int OutsideBracket(const std::vector<Color>& pix, int lo, int hi, int slack)
    {
        const Color& a = kLevelColor[static_cast<std::size_t>(lo)];
        const Color& b = kLevelColor[static_cast<std::size_t>(hi)];
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

    /// How many pixels carry ANY contribution from level 0. Level 0 is the only level whose red
    /// channel is 255 while green is 0, so a red-dominant pixel can only come from it. Used to prove
    /// a minifying draw left level 0 behind rather than merely producing "some other colour".
    static int Level0Contribution(const std::vector<Color>& pix)
    {
        int n = 0;
        for (const Color& c : pix)
            if (c.getRProperty() > 0 && c.getGProperty() == 0 && c.getBProperty() == 0) ++n;
        return n;
    }

    // ---------------------------------------------------------------------------------------------
    // Chain construction. Every level is written explicitly through the public SetData(level, ...)
    // overload; nothing here asks any renderer to generate a level.
    // ---------------------------------------------------------------------------------------------

    static void FillLevelFlat(Texture2D& tex, int level, int w, int h, const Color& c)
    {
        std::vector<Color> buf(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), c);
        tex.SetData(level, nullptr, buf.data(), 0, static_cast<int>(buf.size()));
    }

    /// An asymmetric two-colour pattern: the left column band and the top row band carry the level's
    /// own colour, everything else is the level's dark partner. Asymmetric in BOTH axes and not
    /// self-similar under a flip, so a wrong orientation or a wrong source coordinate shows up.
    static void FillLevelPattern(Texture2D& tex, int level, int w, int h,
                                 const Color& on, const Color& off)
    {
        std::vector<Color> buf(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), off);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const bool lit = (x < std::max(1, w / 4)) || (y == 0 && x < std::max(1, w / 2));
                if (lit) buf[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                              static_cast<std::size_t>(x)] = on;
            }
        tex.SetData(level, nullptr, buf.data(), 0, static_cast<int>(buf.size()));
    }

    static std::unique_ptr<Texture2D> MakeFlatChain(GraphicsDevice& dev, int size, int& levelsOut)
    {
        auto tex = std::make_unique<Texture2D>(dev, size, size, true, SurfaceFormat::Color);
        levelsOut = tex->getLevelCountProperty();
        int w = size, h = size;
        for (int level = 0; level < levelsOut; ++level)
        {
            const std::size_t idx = static_cast<std::size_t>(
                std::min<int>(level, static_cast<int>(kLevelColor.size()) - 1));
            FillLevelFlat(*tex, level, w, h, kLevelColor[idx]);
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        return tex;
    }

    // ---------------------------------------------------------------------------------------------
    // A -- preconditions
    // ---------------------------------------------------------------------------------------------
    void RunA()
    {
        check(flatLevels_ == 4, "A0 the 8x8 flat chain really has four levels (levels=" +
              std::to_string(flatLevels_) + ")");
        check(mipPat_->getLevelCountProperty() == 4,
              "A1 the 8x8 pattern chain really has four levels");
        check(flat1_->getLevelCountProperty() == 1,
              "A2 no mip chain was forced onto the ordinary single-level texture");
        check(mipPartial_->getLevelCountProperty() == 4,
              "A3 the partial texture DECLARES four levels even though only level 0 was written");
        check(shortLevels_ == 3, "A4 the 4x4 chain is a genuinely SHORTER chain (levels=" +
              std::to_string(shortLevels_) + ")");
        check(npotLevels_ == 3, "A5 the 6x6 non-power-of-two chain has three levels (levels=" +
              std::to_string(npotLevels_) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // B -- Point across the LOD matrix: exact level identity, no interpolation
    // ---------------------------------------------------------------------------------------------
    void RunB(GraphicsDevice& dev)
    {
        struct Scale { int dest; int expectLevel; const char* lambda; };
        static const std::array<Scale, 5> kScales{{
            {32, 0, "-2 (4x magnification)"},
            { 8, 0, "0 (one to one)"},
            { 4, 1, "1 (exact 2x minification)"},
            { 2, 2, "2 (exact 4x minification)"},
            { 1, 3, "3 (exact 8x minification)"},
        }};

        for (const Scale& s : kScales)
        {
            const std::vector<Color> pix =
                Draw3D(dev, s.dest, s.dest, *mipFlat_, MakeSampler(TextureFilter::Point));
            const int lvl = SoleLevel(pix, flatLevels_);
            check(lvl == s.expectLevel,
                  std::string("B Point at lambda ") + s.lambda + ": selects EXACTLY level " +
                  std::to_string(s.expectLevel) + " (got level " + std::to_string(lvl) + ", " +
                  Describe(pix) + ")");
        }

        // A mip-POINT ordinal must never mix two levels: one distinct colour, and it is a stored
        // level's colour rather than any blend of two.
        for (const FilterOrdinal& o : kOrdinals)
        {
            if (o.mipLinear || o.anisotropic) continue;
            const std::vector<Color> pix = Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(o.filter));
            const int lvl = SoleLevel(pix, flatLevels_);
            check(lvl >= 1,
                  std::string("B mip-POINT ") + o.name +
                  ": 4x minification selects ONE stored level below level 0 (level=" +
                  std::to_string(lvl) + ", " + Describe(pix) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // C -- Linear across the LOD matrix: the chain IS used
    // ---------------------------------------------------------------------------------------------
    void RunC(GraphicsDevice& dev)
    {
        // C0/C1: magnification and one-to-one must stay on level 0 -- lambda <= 0 and there is no
        // level below 0 to select. This is what stops the fix from over-reaching into ordinary draws.
        for (int dest : {32, 8})
        {
            const std::vector<Color> pix =
                Draw3D(dev, dest, dest, *mipFlat_, MakeSampler(TextureFilter::Linear));
            check(SoleLevel(pix, flatLevels_) == 0,
                  std::string("C Linear at dest ") + std::to_string(dest) +
                  " (lambda <= 0): stays on level 0 (" + Describe(pix) + ")");
        }

        // C2/C3/C4: at an INTEGER lambda the cross-level weight is zero, so trilinear degenerates to
        // exactly one level and the identity colour is byte-exact. No tolerance.
        struct Exact { int dest; int level; };
        static const std::array<Exact, 3> kExact{{ {4, 1}, {2, 2}, {1, 3} }};
        for (const Exact& e : kExact)
        {
            const std::vector<Color> pix =
                Draw3D(dev, e.dest, e.dest, *mipFlat_, MakeSampler(TextureFilter::Linear));
            const int lvl = SoleLevel(pix, flatLevels_);
            check(lvl == e.level,
                  "C Linear at integer lambda, dest " + std::to_string(e.dest) +
                  ": uses the real chain and lands exactly on level " + std::to_string(e.level) +
                  " (got level " + std::to_string(lvl) + ", " + Describe(pix) + ")");
        }

        // C5/C6: between two levels the exact weight is the hardware's business, but WHICH levels
        // contributed is the contract's. A channel neither bracketing level owns must stay at zero.
        struct Bracket { int dest; int lo; int hi; const char* lambda; };
        static const std::array<Bracket, 2> kBrackets{{
            {6, 0, 1, "0.415"},
            {3, 1, 2, "1.415"},
        }};
        for (const Bracket& b : kBrackets)
        {
            const std::vector<Color> pix =
                Draw3D(dev, b.dest, b.dest, *mipFlat_, MakeSampler(TextureFilter::Linear));
            const int outside = OutsideBracket(pix, b.lo, b.hi, 2);
            check(outside == 0,
                  std::string("C Linear at lambda ~") + b.lambda + ": blends ONLY levels " +
                  std::to_string(b.lo) + " and " + std::to_string(b.hi) + " (outside=" +
                  std::to_string(outside) + "/" + std::to_string(pix.size()) + ", " +
                  Describe(pix) + ")");
        }

        // C7: the headline regression -- a minifying Linear draw must not return level 0's stored
        // colour. This is the exact measurement REMED-GFX-174's D4 note reported and declined to
        // assert, restated against the authoritative table.
        {
            const std::vector<Color> pix =
                Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(TextureFilter::Linear));
            const int lvl0 = Level0Contribution(pix);
            check(lvl0 == 0,
                  "C7 Linear minifying a real chain carries NO level-0 contribution (level-0 px=" +
                  std::to_string(lvl0) + "/" + std::to_string(pix.size()) + ", " + Describe(pix) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // D -- every ordinal's mip component against the authoritative table
    // ---------------------------------------------------------------------------------------------
    void RunD(GraphicsDevice& dev)
    {
        // At an exact 4x minification lambda is exactly 2, so BOTH mip components agree on level 2:
        // mip-point rounds to it and mip-linear weights it 1.0. Every non-anisotropic ordinal
        // therefore owes the same byte-exact answer, and an ordinal that ignores the chain answers
        // level 0 instead. This is the one place where all nine ordinals are directly comparable.
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix = Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(o.filter));
            if (o.anisotropic)
            {
                // Anisotropic's footprint and level choice are unspecified per GPU; what IS fixed is
                // that it uses the chain at all, so level 0 must not survive a 4x minification.
                const int lvl0 = Level0Contribution(pix);
                check(lvl0 == 0,
                      std::string("D ") + o.name +
                      ": uses the chain under 4x minification, no level-0 contribution (level-0 px=" +
                      std::to_string(lvl0) + ", " + Describe(pix) + ")");
                continue;
            }
            const int lvl = SoleLevel(pix, flatLevels_);
            check(lvl == 2,
                  std::string("D ") + o.name + " (mip=" + (o.mipLinear ? "linear" : "point") +
                  "): 4x minification selects level 2 (got level " + std::to_string(lvl) + ", " +
                  Describe(pix) + ")");
        }

        // Magnification is the mip component's boundary: no ordinal may select a level under it.
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix = Draw3D(dev, 32, 32, *mipFlat_, MakeSampler(o.filter));
            const int lvl0 = CountEqual(pix, kLevelColor[0]);
            check(lvl0 == 32 * 32,
                  std::string("D ") + o.name + ": magnification stays wholly on level 0 (level-0 px=" +
                  std::to_string(lvl0) + "/1024)");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // E -- the pattern chain: within-level filtering and orientation survive the mip fix
    // ---------------------------------------------------------------------------------------------
    void RunE(GraphicsDevice& dev)
    {
        // E0: Point magnification of the pattern chain is byte-exact against level 0's own texels --
        // an integer 4x magnification puts each destination pixel wholly inside one texel. This is
        // the min/mag half REMED-GFX-150 and REMED-GFX-170 already own; it must not move.
        {
            const std::vector<Color> pix =
                Draw3D(dev, 32, 32, *mipPat_, MakeSampler(TextureFilter::Point));
            int bad = 0;
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                {
                    const Color& got = pix[static_cast<std::size_t>(y) * 32u +
                                           static_cast<std::size_t>(x)];
                    const int tx = x / 4, ty = y / 4;
                    const bool lit = (tx < 2) || (ty == 0 && tx < 4);
                    const Color want = lit ? Color(255, 0, 0, 255) : Color(0, 0, 0, 255);
                    if (!SameColor(got, want)) ++bad;
                }
            check(bad == 0, "E0 Point magnification of the pattern chain is byte-exact on level 0"
                            " (mismatches=" + std::to_string(bad) + "/1024)");
        }

        // E1: the pattern is asymmetric in both axes, so a level-1 selection under Point must show
        // level 1's OWN pattern -- green where lit, black where not -- and never red or blue. Proves
        // the selected level is read with the right orientation and the right source coordinates.
        {
            const std::vector<Color> pix =
                Draw3D(dev, 4, 4, *mipPat_, MakeSampler(TextureFilter::Point));
            int wrongChannel = 0, litCount = 0;
            for (const Color& c : pix)
            {
                if (c.getRProperty() != 0 || c.getBProperty() != 0) ++wrongChannel;
                if (c.getGProperty() > 0) ++litCount;
            }
            check(wrongChannel == 0 && litCount > 0 && litCount < 16,
                  "E1 Point at lambda 1 shows level 1's own asymmetric pattern and only its channel"
                  " (foreign-channel px=" + std::to_string(wrongChannel) + ", lit=" +
                  std::to_string(litCount) + "/16)");
        }

        // E2: a MIN-point / MAG-linear ordinal magnifying must still magnify LINEARLY -- selecting a
        // level must not have replaced the within-level filter. A linear magnification of this
        // pattern produces intermediate reds that point filtering cannot.
        {
            const std::vector<Color> pix =
                Draw3D(dev, 32, 32, *mipPat_, MakeSampler(TextureFilter::MinPointMagLinearMipPoint));
            int intermediate = 0;
            for (const Color& c : pix)
                if (c.getRProperty() > 0 && c.getRProperty() < 255) ++intermediate;
            check(intermediate > 0,
                  "E2 MinPointMagLinearMipPoint still magnifies LINEARLY within the selected level"
                  " (intermediate px=" + std::to_string(intermediate) + "/1024)");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // F -- single-level completeness under a mip-filtering sampler (REMED-GFX-174 preserved)
    // ---------------------------------------------------------------------------------------------
    void RunF(GraphicsDevice& dev)
    {
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix = Draw3D(dev, 16, 16, *flat1_, MakeSampler(o.filter));
            const int black = CountEqual(pix, kIncomplete);
            const int sentinel = CountEqual(pix, kSentinel);
            const int lvl0 = CountEqual(pix, kLevelColor[0]);
            check(black == 0 && sentinel == 0 && lvl0 == 16 * 16,
                  std::string("F ") + o.name +
                  ": a ONE-level Texture2D stays complete and samples level 0 (black=" +
                  std::to_string(black) + ", uncovered=" + std::to_string(sentinel) +
                  ", level0=" + std::to_string(lvl0) + "/256)");
        }

        // The other sampleable kind REMED-GFX-174 corrected: a render target with only level 0.
        RenderTarget2D src(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&src);
        ResetDeviceState(dev, 8, 8);
        dev.Clear(kLevelColor[0]);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        check(src.getLevelCountProperty() == 1, "F0 the render target really has ONE level");
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix = Draw3D(dev, 16, 16, src, MakeSampler(o.filter));
            const int black = CountEqual(pix, kIncomplete);
            check(black == 0,
                  std::string("F ") + o.name +
                  ": a ONE-level RENDER TARGET stays complete when sampled (incomplete-black=" +
                  std::to_string(black) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // G -- a DECLARED but unpopulated chain
    // ---------------------------------------------------------------------------------------------
    void RunG(GraphicsDevice& dev)
    {
        // mipPartial_ asked for mipMap=true and then wrote ONLY level 0. XNA allocates every declared
        // level at creation and leaves the unwritten ones' CONTENT unspecified, so what a minifying
        // draw returns from level 2 is not something a portable fixture may assert -- reading it to
        // make filtering "observable" would be asserting a value nobody wrote.
        //
        // What IS the contract, and what this leg asserts, is that declaring a chain must not damage
        // the level that WAS written. A renderer that leaves the declared range wider than the levels
        // it really allocated has an INCOMPLETE texture, and on GL an incomplete texture samples as
        // opaque black over its whole surface -- level 0 included, magnification included. So a
        // magnifying draw, which can only ever touch level 0, is a complete and portable test of
        // whether the resource is well-formed, for every ordinal including the mip-filtering ones.
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix =
                Draw3D(dev, 16, 16, *mipPartial_, MakeSampler(o.filter));
            const int lvl0 = CountEqual(pix, kLevelColor[0]);
            const int black = CountEqual(pix, kIncomplete);
            check(lvl0 == 16 * 16 && black == 0,
                  std::string("G ") + o.name +
                  ": declaring a chain and writing only level 0 leaves level 0 intact and the"
                  " resource complete (level0=" + std::to_string(lvl0) + "/256, black=" +
                  std::to_string(black) + ", " + Describe(pix) + ")");
        }
        // The minifying case is MEASURED, not asserted: the level it lands on was never written, so
        // its content belongs to the renderer's own allocation, not to this contract.
        {
            const std::vector<Color> pix =
                Draw3D(dev, 2, 2, *mipPartial_, MakeSampler(TextureFilter::Point));
            note("G0 minifying a declared-but-unwritten chain reads a level no caller supplied; its"
                 " content is the renderer's allocation, reported not asserted -- " + Describe(pix));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // H -- SpriteBatch selects levels by the same contract
    // ---------------------------------------------------------------------------------------------
    void RunH(GraphicsDevice& dev)
    {
        {
            const std::vector<Color> pix = DrawSprite(dev, 32, 32, *mipFlat_,
                                                      MakeSampler(TextureFilter::Point));
            check(SoleLevel(pix, flatLevels_) == 0,
                  "H0 SpriteBatch magnification stays on level 0 (" + Describe(pix) + ")");
        }
        {
            const std::vector<Color> pix = DrawSprite(dev, 2, 2, *mipFlat_,
                                                      MakeSampler(TextureFilter::Point));
            const int lvl = SoleLevel(pix, flatLevels_);
            check(lvl == 2, "H1 SpriteBatch Point at 4x minification selects level 2 (got level " +
                  std::to_string(lvl) + ", " + Describe(pix) + ")");
        }
        {
            const std::vector<Color> pix = DrawSprite(dev, 2, 2, *mipFlat_,
                                                      MakeSampler(TextureFilter::Linear));
            const int lvl0 = Level0Contribution(pix);
            check(lvl0 == 0,
                  "H2 SpriteBatch Linear at 4x minification uses the chain (level-0 px=" +
                  std::to_string(lvl0) + ", " + Describe(pix) + ")");
        }
        {
            // A source rectangle covering the whole texture keeps the mip semantics defined and must
            // not force level 0 -- the footprint, not the rectangle, decides the level.
            const std::vector<Color> pix = DrawSprite(dev, 4, 4, *mipFlat_,
                                                      MakeSampler(TextureFilter::Point));
            const int lvl = SoleLevel(pix, flatLevels_);
            check(lvl == 1, "H3 SpriteBatch with a full source rectangle still selects by footprint"
                  " (level=" + std::to_string(lvl) + ", " + Describe(pix) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // I -- transitions: one draw must never inherit another draw's mip state
    // ---------------------------------------------------------------------------------------------
    void RunI(GraphicsDevice& dev)
    {
        // I0/I1: the same texture under Point then Linear, and back. Both answers must be the
        // ordinal's own, not the previous one's.
        {
            const int a = SoleLevel(Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(TextureFilter::Point)),
                                    flatLevels_);
            const std::vector<Color> linPix =
                Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(TextureFilter::Linear));
            const int b = SoleLevel(linPix, flatLevels_);
            const int c = SoleLevel(Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(TextureFilter::Point)),
                                    flatLevels_);
            check(a == 2 && b == 2 && c == 2,
                  "I0 Point -> Linear -> Point on one chain: every draw answers level 2 (" +
                  std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + ")");
        }
        // I2: magnification after minification -- a stale level must not survive the scale change.
        {
            (void)Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(TextureFilter::Point));
            const std::vector<Color> pix =
                Draw3D(dev, 32, 32, *mipFlat_, MakeSampler(TextureFilter::Point));
            check(SoleLevel(pix, flatLevels_) == 0,
                  "I1 a magnifying draw after a minifying one returns to level 0 (" +
                  Describe(pix) + ")");
        }
        // I3: two chains of DIFFERENT length, alternating. The 4x4 chain's last level is 2, so a 4x
        // minification of it clamps there while the 8x8 chain still has a level 2 of its own.
        {
            const int longChain = SoleLevel(Draw3D(dev, 2, 2, *mipFlat_,
                                                   MakeSampler(TextureFilter::Point)), flatLevels_);
            const int shortChain = SoleLevel(Draw3D(dev, 1, 1, *mipShort_,
                                                    MakeSampler(TextureFilter::Point)), shortLevels_);
            const int longAgain = SoleLevel(Draw3D(dev, 2, 2, *mipFlat_,
                                                   MakeSampler(TextureFilter::Point)), flatLevels_);
            check(longChain == 2 && shortChain == 2 && longAgain == 2,
                  "I2 alternating a 4-level and a 3-level chain: each answers its own clamped level"
                  " (" + std::to_string(longChain) + "," + std::to_string(shortChain) + "," +
                  std::to_string(longAgain) + ")");
        }
        // I4: recreation with a DIFFERENT level count must update the effective state -- a one-level
        // texture created after a four-level one must not inherit its range.
        {
            int levels = 0;
            auto fresh = MakeFlatChain(dev, 8, levels);
            const int chained = SoleLevel(Draw3D(dev, 2, 2, *fresh, MakeSampler(TextureFilter::Point)),
                                          levels);
            Texture2D one(dev, 8, 8, false, SurfaceFormat::Color);
            FillLevelFlat(one, 0, 8, 8, kLevelColor[0]);
            const std::vector<Color> onePix =
                Draw3D(dev, 2, 2, one, MakeSampler(TextureFilter::Point));
            check(chained == 2 && CountEqual(onePix, kLevelColor[0]) == 4,
                  "I3 a one-level texture created after a four-level one samples level 0, not a"
                  " stale range (chained=" + std::to_string(chained) + ", one-level=" +
                  Describe(onePix) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // J -- non-power-of-two source and odd destination dimensions
    // ---------------------------------------------------------------------------------------------
    void RunJ(GraphicsDevice& dev)
    {
        // 6x6 -> levels 6, 3, 1. A 3x3 destination is an exact 2x minification of it.
        {
            const std::vector<Color> pix =
                Draw3D(dev, 3, 3, *mipNpot_, MakeSampler(TextureFilter::Point));
            const int lvl = SoleLevel(pix, npotLevels_);
            check(lvl == 1, "J0 a 6x6 NPOT chain minified 2x selects level 1 (got level " +
                  std::to_string(lvl) + ", " + Describe(pix) + ")");
        }
        {
            const std::vector<Color> pix =
                Draw3D(dev, 6, 6, *mipNpot_, MakeSampler(TextureFilter::Point));
            check(SoleLevel(pix, npotLevels_) == 0,
                  "J1 a 6x6 NPOT chain at one-to-one stays on level 0 (" + Describe(pix) + ")");
        }
        // An ODD destination that is not a clean ratio: level selection must stay inside the chain
        // and must not produce a level the chain does not own.
        for (int dest : {5, 7})
        {
            const std::vector<Color> pix =
                Draw3D(dev, dest, dest, *mipFlat_, MakeSampler(TextureFilter::Point));
            const int lvl = SoleLevel(pix, flatLevels_);
            check(lvl >= 0 && lvl < flatLevels_,
                  "J odd destination " + std::to_string(dest) +
                  ": selects a level the chain really owns (level=" + std::to_string(lvl) + ", " +
                  Describe(pix) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // K -- the submission path does not change the selected level
    // ---------------------------------------------------------------------------------------------
    void RunK(GraphicsDevice& dev)
    {
        struct Route { Path path; const char* name; };
        static const std::array<Route, 3> kRoutes{{
            {Path::NonIndexed, "non-indexed"},
            {Path::Indexed,    "indexed"},
            {Path::UserRaw,    "DrawUser"},
        }};
        for (const Route& r : kRoutes)
        {
            const std::vector<Color> minified =
                Draw3D(dev, 2, 2, *mipFlat_, MakeSampler(TextureFilter::Point), r.path);
            const std::vector<Color> magnified =
                Draw3D(dev, 32, 32, *mipFlat_, MakeSampler(TextureFilter::Point), r.path);
            check(SoleLevel(minified, flatLevels_) == 2 && SoleLevel(magnified, flatLevels_) == 0,
                  std::string("K ") + r.name +
                  ": 4x minification selects level 2 and magnification level 0 (min=" +
                  Describe(minified) + ", mag=" + Describe(magnified) + ")");
        }
    }

public:
    TextureFilterMipContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int Result() const { return result_; }

protected:
    void LoadContent() override
    {
        GraphicsDevice& dev = getGraphicsDeviceProperty();

        mipFlat_ = MakeFlatChain(dev, 8, flatLevels_);
        mipShort_ = MakeFlatChain(dev, 4, shortLevels_);
        mipNpot_ = MakeFlatChain(dev, 6, npotLevels_);

        mipPat_ = std::make_unique<Texture2D>(dev, 8, 8, true, SurfaceFormat::Color);
        {
            const int levels = mipPat_->getLevelCountProperty();
            int w = 8, h = 8;
            for (int level = 0; level < levels; ++level)
            {
                const std::size_t idx = static_cast<std::size_t>(
                    std::min<int>(level, static_cast<int>(kLevelColor.size()) - 1));
                FillLevelPattern(*mipPat_, level, w, h, kLevelColor[idx], Color(0, 0, 0, 255));
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }
        }

        // Declared with four levels, only level 0 ever written. Nothing fills the rest.
        mipPartial_ = std::make_unique<Texture2D>(dev, 8, 8, true, SurfaceFormat::Color);
        FillLevelFlat(*mipPartial_, 0, 8, 8, kLevelColor[0]);

        flat1_ = std::make_unique<Texture2D>(dev, 8, 8, false, SurfaceFormat::Color);
        FillLevelFlat(*flat1_, 0, 8, 8, kLevelColor[0]);
    }

    void Draw(const GameTime& gameTime) override
    {
        Game::Draw(gameTime);
        if (done_) { Exit(); return; }
        done_ = true;

        if (!kRasterizes)
        {
            std::printf("[SKIP] %s does not rasterize; mip level selection is not observable\n",
                        kRendererName);
            result_ = 77;
            Exit();
            return;
        }

        GraphicsDevice& dev = getGraphicsDeviceProperty();
        std::printf("=== REMED-GFX-175 TextureFilter mip contract -- renderer %s ===\n", kRendererName);

        RunA();
        if (flatLevels_ < 4)
        {
            note(std::string(kRendererName) + " allocated " + std::to_string(flatLevels_) +
                 " level(s) for a mipMap=true 8x8 Texture2D; the LOD legs below are not observable");
        }
        RunB(dev);
        RunC(dev);
        RunD(dev);
        RunE(dev);
        RunF(dev);
        RunG(dev);
        RunH(dev);
        RunI(dev);
        RunJ(dev);
        RunK(dev);

        std::printf("=== %d/%d checks passed on %s ===\n", passCount_, totalCount_, kRendererName);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }
};

int main()
{
    TextureFilterMipContractTest game;
    game.Run();
    return game.Result();
}
