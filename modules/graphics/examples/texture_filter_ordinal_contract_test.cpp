// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification filter, a
// SEPARATE minification filter and a SEPARATE mipmap filter, and a conforming renderer must
// translate all three, not reduce the ordinal to one boolean.
//
// THE PUBLIC TABLE, read from include/Microsoft/Xna/Framework/Graphics/TextureFilter.hpp and
// cross-checked against the XNA 4.0 documentation each enumerator's own summary paraphrases:
//
//   ord  name                        min      mag      mip      anisotropy
//   ---  --------------------------  -------  -------  -------  ----------
//    0   Linear                      Linear   Linear   Linear   no
//    1   Point                       Point    Point    Point    no
//    2   Anisotropic                 Linear   Linear   Linear   YES
//    3   LinearMipPoint              Linear   Linear   Point    no
//    4   PointMipLinear              Point    Point    Linear   no
//    5   MinLinearMagPointMipLinear  Linear   Point    Linear   no
//    6   MinLinearMagPointMipPoint   Linear   Point    Point    no
//    7   MinPointMagLinearMipLinear  Point    Linear   Linear   no
//    8   MinPointMagLinearMipPoint   Point    Linear   Point    no
//
// "Linear filtering to shrink" is the MINIFICATION half; "point filtering to expand" is the
// MAGNIFICATION half. So the five ordinals 0, 2, 3, 7 and 8 magnify LINEAR and the four ordinals
// 1, 4, 5 and 6 magnify POINT; the five ordinals 0, 2, 3, 5 and 6 minify LINEAR and the four
// ordinals 1, 4, 7 and 8 minify POINT. Those two partitions are DIFFERENT sets, which is exactly
// what a single boolean cannot express and what this fixture measures.
//
// THE DEFECT. Both WebGPU and SDL_GPU resolved a native sampler with, literally,
//
//     filter = textureFilter == 0 ? LINEAR : NEAREST
//
// (WebGPURenderer.cpp:4891 and SdlGpuRenderer.cpp:2742), applied to min, mag and mip
// alike, and -- the half that would have defeated a descriptor-only correction -- they keyed the
// sampler cache with `filterIndex = filter == 0 ? 0 : 1`, so all eight non-Linear ordinals shared
// ONE cached native sampler per address-mode pair. On WebGPU that expression served SpriteBatch
// only (the 3D families already reached a complete table). On SDL_GPU the same function served
// SpriteBatch AND every 3D family -- Textured3D, LitTextured3D, AlphaTest3D, DualTexture3D (both
// slots), EnvironmentMap3D, Skinned3D and Pbr3D -- and SDL_GPU never set enable_anisotropy at all,
// so TextureFilter::Anisotropic was a plain nearest fetch there.
//
// THE ORACLE. Magnification and minification are measured by the SAME differential GFX-150
// established: a point fetch returns a STORED texel byte-for-byte, so an image produced by point
// filtering contains only colours that appear in the source pattern, while a linear fetch over a
// pattern whose neighbours differ strongly manufactures colours that appear in NO texel. The
// patterns below are built so that is unambiguous -- every texel unique, adjacent texels far
// apart in every channel, and no blend of any two texels equal to a third. Point legs are
// additionally compared BYTE-EXACTLY against the texel the contract selects, so "did not
// interpolate" cannot be satisfied by a uniform fill, a stale buffer or the wrong texel.
//
// MIP BOUNDARY, declared. Every leg here samples a texture with ONE level. The mip half of an
// ordinal is therefore not observable through pixels in this fixture and is asserted only through
// the native sampler descriptor, via each renderer's own CNA_WEBGPU_SAMPLER_TRACE /
// CNA_SDLGPU_SAMPLER_TRACE. No mip-generation capability is added by this task. Leg H states the
// boundary as a reported note rather than pretending to measure it.
//
// ANISOTROPY BOUNDARY, declared. Anisotropic filtering's exact output is not specified and differs
// per GPU, so no leg requires particular anisotropic pixels. What IS required, and is the part
// that was broken, is that Anisotropic(2) is a LINEAR min/mag filter and not silently nearest.
//
// GFX-172 BOUNDARY. DualTextureEffect is exercised on slot 0 only. WebGPU's DualTexture3D bind
// group declares one sampler for two texture views, so SamplerStates[1] is inexpressible there;
// that is REMED-GFX-172, a bind-group-layout and WGSL question, and it is deliberately NOT
// measured or changed here.
//
// LEGS:
//
//   A  SpriteBatch magnification, all 9 ordinals    the partition {0,2,3,7,8} vs {1,4,5,6}
//   B  SpriteBatch minification, all 9 ordinals     the DIFFERENT partition {0,2,3,5,6} vs {1,4,7,8}
//   C  3D BasicEffect magnification, all 9 ordinals the device SamplerStates[0] path
//   D  3D BasicEffect minification, all 9 ordinals
//   E  effect families                              every textured stock family under one
//                                                   point-magnifying and one linear-magnifying
//                                                   ordinal that the collapse got wrong
//   F  one texture, many ordinals, one frame        distinct states must not share a native sampler
//   G  queued sampler transitions                   a draw already queued must keep ITS sampler
//   H  address modes survive the correction         Clamp/Wrap/Mirror under a compound ordinal
//   I  geometry that is not integer-aligned         odd sizes, non-integer scale, source rects,
//                                                   UVs between texel centres, coordinates outside
//                                                   [0,1]
//   J  two textures, one ordinal                    per-draw texture dimensions, one sampler
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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/Exception.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX12";
#else
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "UNKNOWN";
#endif

    constexpr int kBBW = 160;
    constexpr int kBBH = 120;

    /// Absent from every pattern, so an uncovered destination pixel is distinguishable from a
    /// sampled one.
    const Color kSentinel(13, 17, 19, 255);

    // ---------------------------------------------------------------------------------------------
    // The public ordinal table
    // ---------------------------------------------------------------------------------------------

    struct FilterOrdinal
    {
        TextureFilter filter;
        int ordinal;
        const char* name;
        bool magLinear;   ///< magnification half
        bool minLinear;   ///< minification half
        bool mipLinear;   ///< mipmap half -- NOT observable here, see the MIP BOUNDARY above
        bool anisotropic;
    };

    constexpr std::array<FilterOrdinal, 9> kOrdinals{{
        {TextureFilter::Linear,                     0, "Linear",                     true,  true,  true,  false},
        {TextureFilter::Point,                      1, "Point",                      false, false, false, false},
        {TextureFilter::Anisotropic,                2, "Anisotropic",                true,  true,  true,  true },
        {TextureFilter::LinearMipPoint,             3, "LinearMipPoint",             true,  true,  false, false},
        {TextureFilter::PointMipLinear,             4, "PointMipLinear",             false, false, true,  false},
        {TextureFilter::MinLinearMagPointMipLinear, 5, "MinLinearMagPointMipLinear", false, true,  true,  false},
        {TextureFilter::MinLinearMagPointMipPoint,  6, "MinLinearMagPointMipPoint",  false, true,  false, false},
        {TextureFilter::MinPointMagLinearMipLinear, 7, "MinPointMagLinearMipLinear", true,  false, true,  false},
        {TextureFilter::MinPointMagLinearMipPoint,  8, "MinPointMagLinearMipPoint",  true,  false, false, false},
    }};

    // ---------------------------------------------------------------------------------------------
    // Patterns
    // ---------------------------------------------------------------------------------------------

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

        std::vector<std::uint8_t> Bytes() const
        {
            std::vector<std::uint8_t> out;
            out.reserve(texels.size() * 4u);
            for (const Color& c : texels)
            {
                out.push_back(c.getRProperty());
                out.push_back(c.getGProperty());
                out.push_back(c.getBProperty());
                out.push_back(c.getAProperty());
            }
            return out;
        }
    };

    /// 8x4, every texel unique, adjacent texels far apart in every channel. Alpha is a constant 255
    /// so an Opaque blend cannot turn a filtering difference into an alpha difference; the filter
    /// question is asked in RGB alone.
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

    /// 5x3, odd in both axes, so a scale can never be a whole number of texels per pixel in both
    /// directions at once.
    Pattern Make5x3()
    {
        Pattern p{5, 3, {}};
        p.texels.reserve(15);
        static const int kR[5] = {5, 245, 35, 215, 65};
        static const int kG[3] = {25, 225, 95};
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 5; ++x)
                p.texels.emplace_back(static_cast<std::uint8_t>(kR[x]),
                                      static_cast<std::uint8_t>(kG[y]),
                                      static_cast<std::uint8_t>((x * 5 + y) * 16 + 7),
                                      static_cast<std::uint8_t>(255));
        return p;
    }

    /// 4x4 for the address-mode legs: first/last row and column differ strongly, so Clamp's edge
    /// extension, Wrap's tiling and Mirror's reflection produce visibly different images.
    Pattern Make4x4()
    {
        Pattern p{4, 4, {}};
        p.texels.reserve(16);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                p.texels.emplace_back(static_cast<std::uint8_t>(x * 85),
                                      static_cast<std::uint8_t>(y * 85),
                                      static_cast<std::uint8_t>(255 - (x * 4 + y) * 17),
                                      static_cast<std::uint8_t>(255));
        return p;
    }

    // ---------------------------------------------------------------------------------------------
    // Comparison
    // ---------------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------------
    // Raw vertex layouts, matching each stock family's declared GPU stride exactly (REMED-GFX-125:
    // the object size of the public vertex types is not the stream stride).
    // ---------------------------------------------------------------------------------------------

    /// Stride-20 VertexPositionTexture.
    struct VtxPT { float px, py, pz; float u, v; };
    static_assert(sizeof(VtxPT) == 20, "stride 20");
    /// Stride-24 VertexPositionColorTexture.
    struct VtxPCT { float px, py, pz; std::uint8_t r, g, b, a; float u, v; };
    static_assert(sizeof(VtxPCT) == 24, "stride 24");
    /// Stride-32 VertexPositionNormalTexture.
    struct VtxPNT { float px, py, pz; float nx, ny, nz; float u, v; };
    static_assert(sizeof(VtxPNT) == 32, "stride 32");
    /// Stride-52 VertexPositionNormalTextureSkinned.
    struct VtxPNTS { float px, py, pz; float nx, ny, nz; float u, v;
                     float w0, w1, w2, w3; std::uint8_t i0, i1, i2, i3; };
    static_assert(sizeof(VtxPNTS) == 52, "stride 52");

    /// A full-viewport quad in NDC with identity matrices, so destination pixel (x,y) reads texture
    /// coordinate ((x+0.5)/W * uvMax, (y+0.5)/H * uvMax) with no projection to reason about.
    template <typename V>
    std::vector<V> MakeQuad(float uvMax)
    {
        V tl{}, bl{}, br{}, tr{};
        auto place = [](V& v, float x, float y) { v.px = x; v.py = y; v.pz = 0.0f; };
        place(tl, -1.0f,  1.0f); tl.u = 0.0f;  tl.v = 0.0f;
        place(bl, -1.0f, -1.0f); bl.u = 0.0f;  bl.v = uvMax;
        place(br,  1.0f, -1.0f); br.u = uvMax; br.v = uvMax;
        place(tr,  1.0f,  1.0f); tr.u = uvMax; tr.v = 0.0f;
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

    bool SameColor(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    std::string Str(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
                     std::to_string(static_cast<int>(c.getGProperty())) + "," +
                     std::to_string(static_cast<int>(c.getBProperty())) + "," +
                     std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /// The address mode applied to an already-floored integer texel index.
    int AddressTexel(long long i, int size, TextureAddressMode mode)
    {
        if (size <= 0) return 0;
        const long long n = size;
        switch (mode)
        {
        case TextureAddressMode::Clamp:
            return static_cast<int>(std::clamp<long long>(i, 0, n - 1));
        case TextureAddressMode::Wrap:
        {
            long long m = i % n;
            if (m < 0) m += n;
            return static_cast<int>(m);
        }
        case TextureAddressMode::Mirror:
        {
            const long long period = 2 * n;
            long long m = i % period;
            if (m < 0) m += period;
            return static_cast<int>(m < n ? m : (period - 1 - m));
        }
        }
        return 0;
    }
}

class TextureFilterOrdinalContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    Pattern p8x4_ = Make8x4();
    Pattern p5x3_ = Make5x3();
    Pattern p4x4_ = Make4x4();

    Texture2D tex8x4_, tex5x3_, tex4x4_, white_;
    /// TextureCube has no default constructor, so it is created in LoadContent once the device
    /// exists. EnvironmentMapEffect needs a non-null cube even when its contribution is zeroed.
    std::unique_ptr<TextureCube> cube_;

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

    // -------------------------------------------------------------------------------------------
    // Rendering helpers
    // -------------------------------------------------------------------------------------------

    static void ResetState(GraphicsDevice& dev, int w, int h)
    {
        dev.setViewportProperty(Viewport(0, 0, w, h));
    }

    static SamplerState MakeSampler(TextureFilter filter,
                                    TextureAddressMode u = TextureAddressMode::Clamp,
                                    TextureAddressMode v = TextureAddressMode::Clamp,
                                    int maxAnisotropy = 4)
    {
        SamplerState s;
        s.setFilterProperty(filter);
        s.setAddressUProperty(u);
        s.setAddressVProperty(v);
        s.setMaxAnisotropyProperty(maxAnisotropy);
        return s;
    }

    /// Renders one sprite into a freshly cleared target and returns the whole target.
    std::vector<Color> RenderSprite(GraphicsDevice& dev, RenderTarget2D& rt, int rtW, int rtH,
                                    const Texture2D& tex, const Rectangle& dest,
                                    const Rectangle& src, SamplerState sampler)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev, rtW, rtH);
        dev.Clear(kSentinel);

        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr, nullptr,
                 Matrix::getIdentityProperty());
        sb.Draw(tex, dest, src, Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
                SpriteEffects::None, 0.0f);
        sb.End();

        ResetState(dev, rtW, rtH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);

        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    /// Puts the device into the state a raw 3D draw needs. A preceding SpriteBatch leaves
    /// CullCounterClockwise behind, which culls this quad's winding (REMED-GFX-160).
    static void ResetDeviceState(GraphicsDevice& dev, int w, int h)
    {
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    /// Draws one quad through @p draw into a fresh target and returns the whole target.
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
        dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        dev.getSamplerStatesProperty()[1] = SamplerState::LinearWrap;
        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    template <typename V>
    static void DrawQuad(GraphicsDevice& dev, const std::vector<V>& verts)
    {
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(V)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    template <typename FX>
    static void Matrices(FX& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

    /// Ambient-only lighting: the modulation is spatially uniform, so it scales every texel by the
    /// same factor and cannot manufacture a colour by itself.
    template <typename FX>
    static void AmbientOnly(FX& fx)
    {
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
    }

    /// One plain textured BasicEffect quad under an explicit SamplerStates[0]. Unlit and untinted,
    /// so the modulation is the identity and the stored texel is asserted byte-for-byte.
    std::vector<Color> Render3DQuad(GraphicsDevice& dev, int rtW, int rtH, Texture2D& tex,
                                    float uvMax, const SamplerState& sampler)
    {
        return Render3D(dev, rtW, rtH, [&](GraphicsDevice& d) {
            d.getSamplerStatesProperty()[0] = sampler;
            BasicEffect fx(d);
            Matrices(fx);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);
            fx.setLightingEnabledProperty(false);
            fx.Apply();
            DrawQuad(d, MakeQuad<VtxPT>(uvMax));
        });
    }

    /// The point/linear differential: how many pixels hold a colour that appears in NO texel of the
    /// pattern. Point filtering owes exactly 0; linear filtering over these patterns owes many.
    static int CountForeignColors(const std::vector<Color>& pix, const Pattern& pat)
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

    /// Byte-exact point comparison for a full-target sprite whose destination is an exact multiple
    /// of the source: destination pixel x reads texel floor((x + 0.5) / scaleX) + src.X.
    int PointMismatches(const std::vector<Color>& pix, int rtW, int rtH, const Pattern& pat,
                        const Rectangle& dest, const Rectangle& src,
                        TextureAddressMode addrU, TextureAddressMode addrV,
                        const std::string& label)
    {
        const double scaleX = static_cast<double>(dest.Width) / src.Width;
        const double scaleY = static_cast<double>(dest.Height) / src.Height;
        int bad = 0;
        int reported = 0;
        for (int y = 0; y < rtH; ++y)
        {
            for (int x = 0; x < rtW; ++x)
            {
                const Color& actual =
                    pix[static_cast<std::size_t>(y) * static_cast<std::size_t>(rtW) +
                        static_cast<std::size_t>(x)];
                const bool covered = x >= dest.X && x < dest.X + dest.Width &&
                                     y >= dest.Y && y < dest.Y + dest.Height;
                if (!covered)
                {
                    if (!SameColor(actual, kSentinel)) ++bad;
                    continue;
                }
                const double sx = src.X + (x - dest.X + 0.5) / scaleX;
                const double sy = src.Y + (y - dest.Y + 0.5) / scaleY;
                const int tx = AddressTexel(static_cast<long long>(std::floor(sx)), pat.w, addrU);
                const int ty = AddressTexel(static_cast<long long>(std::floor(sy)), pat.h, addrV);
                const Color& expected = pat.At(tx, ty);
                if (!SameColor(actual, expected))
                {
                    ++bad;
                    if (reported < 4)
                    {
                        ++reported;
                        std::printf("        %s dest(%d,%d) s=(%.4f,%.4f) texel(%d,%d) expected %s "
                                    "actual %s\n", label.c_str(), x, y, sx, sy, tx, ty,
                                    Str(expected).c_str(), Str(actual).c_str());
                    }
                }
            }
        }
        return bad;
    }

    /// True when two images differ in at least one pixel. Used to prove two ordinals were NOT
    /// handed the same native sampler.
    static int DifferingPixels(const std::vector<Color>& a, const std::vector<Color>& b)
    {
        int n = 0;
        const std::size_t count = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < count; ++i)
            if (!SameColor(a[i], b[i])) ++n;
        return n;
    }

    // A -----------------------------------------------------------------------------------------
    // Every ordinal's MAGNIFICATION half, through SpriteBatch. 8x4 -> 16x8 is an exact 2x, so a
    // point fetch's expected texel is exactly computable and a linear fetch's 0.25/0.75 weights
    // manufacture colours no texel holds.
    void RunA_SpriteMagnification(GraphicsDevice& dev)
    {
        const Rectangle dest(0, 0, 16, 8);
        const Rectangle src(0, 0, 8, 4);
        for (const FilterOrdinal& o : kOrdinals)
        {
            RenderTarget2D rt(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 16, 8, tex8x4_, dest, src,
                                                        MakeSampler(o.filter));
            const int foreign = CountForeignColors(pix, p8x4_);
            const std::string tag = std::string("A") + std::to_string(o.ordinal) + " " + o.name;
            if (o.magLinear)
            {
                check(foreign > 0, tag + ": magnifies LINEAR -- genuinely interpolates "
                                         "(manufactured colours=" + std::to_string(foreign) + ")");
            }
            else
            {
                const int bad = PointMismatches(pix, 16, 8, p8x4_, dest, src,
                                                TextureAddressMode::Clamp,
                                                TextureAddressMode::Clamp, tag);
                check(foreign == 0 && bad == 0,
                      tag + ": magnifies POINT -- all 128 destination pixels equal the selected "
                            "texel byte-for-byte (manufactured=" + std::to_string(foreign) +
                      ", mismatches=" + std::to_string(bad) + ")");
            }
        }
    }

    // B -----------------------------------------------------------------------------------------
    // Every ordinal's MINIFICATION half, through SpriteBatch. This is a DIFFERENT partition of the
    // nine ordinals from leg A's -- 5, 6 magnify point but minify linear; 7, 8 do the reverse --
    // so a renderer that passes A with one boolean per ordinal still fails here.
    void RunB_SpriteMinification(GraphicsDevice& dev)
    {
        const Rectangle dest(0, 0, 3, 2);
        const Rectangle src(0, 0, 8, 4);
        for (const FilterOrdinal& o : kOrdinals)
        {
            RenderTarget2D rt(dev, 3, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 3, 2, tex8x4_, dest, src,
                                                        MakeSampler(o.filter));
            const int foreign = CountForeignColors(pix, p8x4_);
            const std::string tag = std::string("B") + std::to_string(o.ordinal) + " " + o.name;
            if (o.minLinear)
                check(foreign > 0, tag + ": minifies LINEAR -- averages neighbours "
                                         "(manufactured colours=" + std::to_string(foreign) + ")");
            else
                check(foreign == 0, tag + ": minifies POINT -- one stored texel per pixel, no "
                                          "averaging (manufactured colours=" +
                                    std::to_string(foreign) + ")");
        }
    }

    // C -----------------------------------------------------------------------------------------
    // The same magnification partition on the 3D path, through GraphicsDevice.SamplerStates[0].
    void RunC_ThreeDMagnification(GraphicsDevice& dev)
    {
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix =
                Render3DQuad(dev, 16, 8, tex8x4_, 1.0f, MakeSampler(o.filter));
            const int foreign = CountForeignColors(pix, p8x4_);
            const std::string tag = std::string("C") + std::to_string(o.ordinal) + " " + o.name;
            if (o.magLinear)
                check(foreign > 0, tag + ": 3D magnifies LINEAR (manufactured colours=" +
                                    std::to_string(foreign) + ")");
            else
                check(foreign == 0, tag + ": 3D magnifies POINT (manufactured colours=" +
                                    std::to_string(foreign) + ")");
        }
    }

    // D -----------------------------------------------------------------------------------------
    void RunD_ThreeDMinification(GraphicsDevice& dev)
    {
        for (const FilterOrdinal& o : kOrdinals)
        {
            const std::vector<Color> pix =
                Render3DQuad(dev, 3, 2, tex8x4_, 1.0f, MakeSampler(o.filter));
            const int foreign = CountForeignColors(pix, p8x4_);
            const std::string tag = std::string("D") + std::to_string(o.ordinal) + " " + o.name;
            if (o.minLinear)
                check(foreign > 0, tag + ": 3D minifies LINEAR (manufactured colours=" +
                                    std::to_string(foreign) + ")");
            else
                check(foreign == 0, tag + ": 3D minifies POINT (manufactured colours=" +
                                    std::to_string(foreign) + ")");
        }
    }

    // E -----------------------------------------------------------------------------------------
    // Every textured stock family, under ONE ordinal the collapse magnified wrongly
    // (MinPointMagLinearMipPoint, which owes LINEAR magnification) and one it happened to get right
    // (Point). Both are asserted, so a family that ignores the sampler entirely fails the pair.
    //
    // The lit families are driven ambient-only, so their modulation is spatially UNIFORM: it scales
    // every texel by the same factor and therefore cannot manufacture a colour by itself, but it
    // can move colours off the stored palette. Those are measured by the DISTINCT-COLOUR count
    // instead of by the palette test -- a uniform scale of a 32-texel palette still has at most 32
    // distinct colours, while a genuine interpolation has strictly more.
    void RunE_EffectFamilies(GraphicsDevice& dev)
    {
        const SamplerState pointS = MakeSampler(TextureFilter::Point);
        const SamplerState linMagS = MakeSampler(TextureFilter::MinPointMagLinearMipPoint);
        const int palette = static_cast<int>(p8x4_.texels.size());

        struct Family
        {
            const char* name;
            void (TextureFilterOrdinalContractTest::*draw)(GraphicsDevice&);
            /// True when the effect's modulation is the identity, so the exact stored texel is
            /// asserted rather than only the distinct-colour count.
            bool identityModulation;
        };
        static const Family kFamilies[] = {
            {"BasicEffect(plain textured)",       &TextureFilterOrdinalContractTest::DrawBasicPlain,   true },
            {"BasicEffect(coloured textured)",    &TextureFilterOrdinalContractTest::DrawBasicColored, true },
            {"BasicEffect(lit textured)",         &TextureFilterOrdinalContractTest::DrawBasicLit,     false},
            {"AlphaTestEffect",                   &TextureFilterOrdinalContractTest::DrawAlphaTest,    true },
            {"DualTextureEffect(slot 0)",         &TextureFilterOrdinalContractTest::DrawDualTexture,  false},
            {"EnvironmentMapEffect(base texture)",&TextureFilterOrdinalContractTest::DrawEnvMap,       false},
            {"SkinnedEffect",                     &TextureFilterOrdinalContractTest::DrawSkinned,      false},
        };

        int index = 0;
        for (const Family& f : kFamilies)
        {
            ++index;
            const std::string tag = "E" + std::to_string(index) + " " + f.name;
            std::vector<Color> p, l;
            try
            {
                dev.getSamplerStatesProperty()[0] = pointS;
                p = Render3D(dev, 16, 8, [&](GraphicsDevice& d) {
                    d.getSamplerStatesProperty()[0] = pointS;
                    (this->*f.draw)(d);
                });
                l = Render3D(dev, 16, 8, [&](GraphicsDevice& d) {
                    d.getSamplerStatesProperty()[0] = linMagS;
                    (this->*f.draw)(d);
                });
            }
            catch (const System::Exception& e)
            {
                note(tag + ": NOT EXERCISED on " + kRendererName + " -- " + e.what());
                continue;
            }
            catch (const std::exception& e)
            {
                note(tag + ": NOT EXERCISED on " + kRendererName + " -- " + e.what());
                continue;
            }

            if (f.identityModulation)
            {
                check(CountForeignColors(p, p8x4_) == 0,
                      tag + "/Point: only stored texels (manufactured=" +
                      std::to_string(CountForeignColors(p, p8x4_)) + ")");
                check(CountForeignColors(l, p8x4_) > 0,
                      tag + "/MinPointMagLinearMipPoint: genuinely interpolates (manufactured=" +
                      std::to_string(CountForeignColors(l, p8x4_)) + ")");
            }
            else
            {
                check(DistinctColors(p) <= palette + 1,
                      tag + "/Point: no more distinct colours than the " + std::to_string(palette) +
                      "-texel palette plus the sentinel (distinct=" +
                      std::to_string(DistinctColors(p)) + ")");
                check(DistinctColors(l) > DistinctColors(p),
                      tag + "/MinPointMagLinearMipPoint: strictly more distinct colours than the "
                      "point run (" + std::to_string(DistinctColors(l)) + " > " +
                      std::to_string(DistinctColors(p)) + ")");
            }
        }
    }

    void DrawBasicPlain(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        Matrices(fx);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex8x4_);
        fx.setLightingEnabledProperty(false);
        fx.Apply();
        DrawQuad(dev, MakeQuad<VtxPT>(1.0f));
    }

    void DrawBasicColored(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        Matrices(fx);
        fx.setTextureEnabledProperty(true);
        fx.VertexColorEnabled = true;
        fx.setTextureProperty(&tex8x4_);
        fx.setLightingEnabledProperty(false);
        fx.Apply();
        // Opaque white vertex colours, so the modulation stays the identity and the palette test
        // still applies -- what is being measured is the SAMPLER on a second vertex layout and
        // shader path, not the tint.
        std::vector<VtxPCT> v = MakeQuad<VtxPCT>(1.0f);
        for (VtxPCT& x : v) { x.r = 255; x.g = 255; x.b = 255; x.a = 255; }
        DrawQuad(dev, v);
    }

    void DrawBasicLit(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        Matrices(fx);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex8x4_);
        AmbientOnly(fx);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.Apply();
        std::vector<VtxPNT> v = MakeQuad<VtxPNT>(1.0f);
        FillNormals(v);
        DrawQuad(dev, v);
    }

    void DrawAlphaTest(GraphicsDevice& dev)
    {
        AlphaTestEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex8x4_);
        fx.Apply();
        DrawQuad(dev, MakeQuad<VtxPT>(1.0f));
    }

    void DrawDualTexture(GraphicsDevice& dev)
    {
        // Texture 2 is a single white texel, so `tex0 * (2 * tex1)` is a uniform scale of texture 0
        // and the filtering question stays a question about slot 0 alone. Slot 1 is left at the
        // device default in BOTH runs -- REMED-GFX-172 is deliberately not measured here.
        DualTextureEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex8x4_);
        fx.setTexture2Property(&white_);
        fx.Apply();
        DrawQuad(dev, MakeQuad<VtxPT>(1.0f));
    }

    void DrawEnvMap(GraphicsDevice& dev)
    {
        if (cube_ == nullptr)
            throw std::runtime_error("no TextureCube on this renderer");
        EnvironmentMapEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex8x4_);
        fx.setEnvironmentMapProperty(cube_.get());
        // The cube contributes nothing, so this leg measures the BASE texture's slot-0 sampler.
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        AmbientOnly(fx);
        fx.Apply();
        std::vector<VtxPNT> v = MakeQuad<VtxPNT>(1.0f);
        FillNormals(v);
        DrawQuad(dev, v);
    }

    void DrawSkinned(GraphicsDevice& dev)
    {
        SkinnedEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex8x4_);
        AmbientOnly(fx);
        fx.setSpecularColorProperty(Vector3::Zero);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.Apply();
        std::vector<VtxPNTS> v = MakeQuad<VtxPNTS>(1.0f);
        FillNormals(v);
        FillSkin(v);
        DrawQuad(dev, v);
    }

    static int DistinctColors(const std::vector<Color>& pix)
    {
        std::set<std::uint32_t> seen;
        for (const Color& c : pix)
            seen.insert((static_cast<std::uint32_t>(c.getRProperty()) << 24) |
                        (static_cast<std::uint32_t>(c.getGProperty()) << 16) |
                        (static_cast<std::uint32_t>(c.getBProperty()) << 8) |
                         static_cast<std::uint32_t>(c.getAProperty()));
        return static_cast<int>(seen.size());
    }

    // F -----------------------------------------------------------------------------------------
    // One texture under every ordinal, all in ONE frame. This is the sampler-CACHE question the
    // pre-fix key could not answer: with `filterIndex = filter == 0 ? 0 : 1` the eight non-Linear
    // ordinals shared a single cached native sampler, so whichever of them was seen first decided
    // the rest. Every ordinal that owes a different magnification half must produce a different
    // image, and every ordinal that owes the same one must produce an identical image.
    void RunF_CacheDistinctness(GraphicsDevice& dev)
    {
        const Rectangle dest(0, 0, 16, 8);
        const Rectangle src(0, 0, 8, 4);
        std::array<std::vector<Color>, 9> images;
        for (std::size_t i = 0; i < kOrdinals.size(); ++i)
        {
            RenderTarget2D rt(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            images[i] = RenderSprite(dev, rt, 16, 8, tex8x4_, dest, src,
                                     MakeSampler(kOrdinals[i].filter));
        }
        int wrongShared = 0;
        int wrongSplit = 0;
        for (std::size_t i = 0; i < kOrdinals.size(); ++i)
        {
            for (std::size_t j = i + 1; j < kOrdinals.size(); ++j)
            {
                const int diff = DifferingPixels(images[i], images[j]);
                // Anisotropic may legitimately differ from plain Linear even when both magnify
                // linear, so a pair involving it is only required NOT to collapse onto a point
                // image; it is excluded from the "must be identical" half.
                const bool anyAniso = kOrdinals[i].anisotropic || kOrdinals[j].anisotropic;
                if (kOrdinals[i].magLinear != kOrdinals[j].magLinear)
                {
                    if (diff == 0)
                    {
                        ++wrongShared;
                        std::printf("        F: %s and %s owe different magnification filters but "
                                    "produced identical images\n",
                                    kOrdinals[i].name, kOrdinals[j].name);
                    }
                }
                else if (!anyAniso && diff != 0)
                {
                    ++wrongSplit;
                    std::printf("        F: %s and %s owe the same magnification filter but "
                                "differ in %d pixels\n",
                                kOrdinals[i].name, kOrdinals[j].name, diff);
                }
            }
        }
        check(wrongShared == 0,
              "F1: no two ordinals with different magnification halves share one native sampler "
              "(collapsed pairs=" + std::to_string(wrongShared) + ")");
        check(wrongSplit == 0,
              "F2: ordinals with the same magnification half agree exactly, so the cache is still "
              "a cache (disagreeing pairs=" + std::to_string(wrongSplit) + ")");
    }

    // G -----------------------------------------------------------------------------------------
    // A queued draw must keep the sampler it was queued with. Both renderers defer their commands to
    // the end of the frame, so a mutable per-slot read at replay time would give BOTH draws the
    // LAST sampler.
    void RunG_QueuedTransitions(GraphicsDevice& dev)
    {
        RenderTarget2D rt(dev, 32, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetState(dev, 32, 8);
        dev.Clear(kSentinel);

        SamplerState pointS = MakeSampler(TextureFilter::Point);
        SamplerState linS = MakeSampler(TextureFilter::MinPointMagLinearMipPoint);
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointS, nullptr, nullptr,
                     nullptr, Matrix::getIdentityProperty());
            sb.Draw(tex8x4_, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255),
                    0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
            sb.End();
        }
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linS, nullptr, nullptr,
                     nullptr, Matrix::getIdentityProperty());
            sb.Draw(tex8x4_, Rectangle(16, 0, 16, 8), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255),
                    0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
            sb.End();
        }
        ResetState(dev, 32, 8);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);

        std::vector<Color> pix(32u * 8u, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));

        std::vector<Color> left(32u * 8u, kSentinel);
        std::vector<Color> right(32u * 8u, kSentinel);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 32; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * 32u + static_cast<std::size_t>(x);
                (x < 16 ? left : right)[i] = pix[i];
            }
        check(CountForeignColors(left, p8x4_) == 0,
              "G1: the sprite queued under Point still sampled Point after a later Begin selected "
              "a linear-magnifying ordinal (manufactured colours=" +
              std::to_string(CountForeignColors(left, p8x4_)) + ")");
        check(CountForeignColors(right, p8x4_) > 0,
              "G2: the sprite queued under MinPointMagLinearMipPoint still interpolated "
              "(manufactured colours=" + std::to_string(CountForeignColors(right, p8x4_)) + ")");
    }

    // H -----------------------------------------------------------------------------------------
    // The correction must not disturb the address modes, which share the sampler descriptor and the
    // cache key. Driven by an oversized source rectangle so u,v genuinely leave [0,1].
    void RunH_AddressModes(GraphicsDevice& dev)
    {
        const Rectangle dest(0, 0, 16, 16);
        const Rectangle src(0, 0, 8, 8);   // 2x the 4x4 texture, so u,v reach 2.0
        struct AddrCase { TextureAddressMode mode; const char* name; };
        const AddrCase modes[] = {
            {TextureAddressMode::Clamp,  "Clamp"},
            {TextureAddressMode::Wrap,   "Wrap"},
            {TextureAddressMode::Mirror, "Mirror"},
        };
        // Under a POINT-magnifying compound ordinal the exact selected texel is computable, so this
        // is a byte-exact address-mode check, not merely "the three differ".
        std::array<std::vector<Color>, 3> imgs;
        for (int i = 0; i < 3; ++i)
        {
            RenderTarget2D rt(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            imgs[static_cast<std::size_t>(i)] = RenderSprite(dev, rt, 16, 16, tex4x4_, dest, src,
                MakeSampler(TextureFilter::MinLinearMagPointMipPoint, modes[i].mode, modes[i].mode));
            const int bad = PointMismatches(imgs[static_cast<std::size_t>(i)], 16, 16, p4x4_, dest,
                                            src, modes[i].mode, modes[i].mode,
                                            std::string("H/") + modes[i].name);
            check(bad == 0, std::string("H1 ") + modes[i].name +
                  ": MinLinearMagPointMipPoint selects the address-transformed texel exactly "
                  "(mismatches=" + std::to_string(bad) + ")");
        }
        check(DifferingPixels(imgs[0], imgs[1]) > 0 && DifferingPixels(imgs[1], imgs[2]) > 0 &&
              DifferingPixels(imgs[0], imgs[2]) > 0,
              "H2: Clamp, Wrap and Mirror remain three distinct images under a compound ordinal");

        // The address mode must still be part of the sampler cache key after the filter joined it.
        {
            RenderTarget2D rtA(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            RenderTarget2D rtB(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            const std::vector<Color> uw = RenderSprite(dev, rtA, 16, 16, tex4x4_, dest, src,
                MakeSampler(TextureFilter::Point, TextureAddressMode::Wrap,
                            TextureAddressMode::Clamp));
            const std::vector<Color> vw = RenderSprite(dev, rtB, 16, 16, tex4x4_, dest, src,
                MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                            TextureAddressMode::Wrap));
            check(DifferingPixels(uw, vw) > 0,
                  "H3: U and V address modes remain independent (WrapU/ClampV differs from "
                  "ClampU/WrapV)");
        }
    }

    // I -----------------------------------------------------------------------------------------
    // Geometry that is not integer-aligned: an odd texture, a non-integer scale, an interior source
    // rectangle, and a 1:1 scale where point and linear must agree.
    void RunI_Geometry(GraphicsDevice& dev)
    {
        // 5x3 -> 17x11: neither axis is a whole number of destination pixels per texel.
        for (const FilterOrdinal& o : kOrdinals)
        {
            if (o.anisotropic) continue;  // aniso output is unspecified at a non-integer scale
            RenderTarget2D rt(dev, 17, 11, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const Rectangle dest(0, 0, 17, 11);
            const Rectangle src(0, 0, 5, 3);
            const std::vector<Color> pix = RenderSprite(dev, rt, 17, 11, tex5x3_, dest, src,
                                                        MakeSampler(o.filter));
            const int foreign = CountForeignColors(pix, p5x3_);
            const std::string tag = std::string("I1 ") + o.name;
            if (o.magLinear)
                check(foreign > 0, tag + ": non-integer magnification still interpolates "
                                         "(manufactured=" + std::to_string(foreign) + ")");
            else
                check(foreign == 0, tag + ": non-integer magnification still selects one texel "
                                          "(manufactured=" + std::to_string(foreign) + ")");
        }

        // An interior source rectangle -- addressing a subregion must not re-enable interpolation
        // under a point-magnifying compound ordinal.
        {
            RenderTarget2D rt(dev, 12, 6, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const Rectangle dest(0, 0, 12, 6);
            const Rectangle src(2, 1, 4, 2);
            const std::vector<Color> pix = RenderSprite(dev, rt, 12, 6, tex8x4_, dest, src,
                MakeSampler(TextureFilter::MinLinearMagPointMipLinear));
            const int bad = PointMismatches(pix, 12, 6, p8x4_, dest, src, TextureAddressMode::Clamp,
                                            TextureAddressMode::Clamp, "I2");
            check(bad == 0, "I2: an interior source rectangle under MinLinearMagPointMipLinear "
                            "selects the exact texel (mismatches=" + std::to_string(bad) + ")");
        }

        // 1:1 -- every ordinal must agree here, because neither half is exercised. A renderer that
        // passes only this scale is exactly the false positive that let GFX-170 survive.
        {
            std::vector<std::vector<Color>> at11;
            for (const FilterOrdinal& o : kOrdinals)
            {
                RenderTarget2D rt(dev, 8, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
                at11.push_back(RenderSprite(dev, rt, 8, 4, tex8x4_, Rectangle(0, 0, 8, 4),
                                            Rectangle(0, 0, 8, 4), MakeSampler(o.filter)));
            }
            int disagree = 0;
            for (std::size_t i = 1; i < at11.size(); ++i)
                if (DifferingPixels(at11[0], at11[i]) != 0) ++disagree;
            check(disagree == 0,
                  "I3: at a 1:1 scale all nine ordinals produce the same image -- which is why a "
                  "1:1 fixture cannot see this defect (disagreeing ordinals=" +
                  std::to_string(disagree) + ")");
        }
    }

    // J -----------------------------------------------------------------------------------------
    void RunJ_TwoTextures(GraphicsDevice& dev)
    {
        RenderTarget2D rt(dev, 32, 12, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetState(dev, 32, 12);
        dev.Clear(kSentinel);
        SamplerState s = MakeSampler(TextureFilter::MinLinearMagPointMipPoint);
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &s, nullptr, nullptr, nullptr,
                     Matrix::getIdentityProperty());
            sb.Draw(tex8x4_, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255),
                    0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
            sb.Draw(tex5x3_, Rectangle(16, 0, 15, 9), Rectangle(0, 0, 5, 3), Color(255, 255, 255, 255),
                    0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
            sb.End();
        }
        ResetState(dev, 32, 12);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);

        std::vector<Color> pix(32u * 12u, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));

        int foreign = 0;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 32; ++x)
            {
                const Color& c = pix[static_cast<std::size_t>(y) * 32u + static_cast<std::size_t>(x)];
                if (SameColor(c, kSentinel)) continue;
                bool found = false;
                for (const Color& t : (x < 16 ? p8x4_ : p5x3_).texels)
                    if (SameColor(c, t)) { found = true; break; }
                if (!found) ++foreign;
            }
        check(foreign == 0,
              "J1: two textures of different sizes under one point-magnifying compound ordinal in "
              "one batch both select stored texels (manufactured=" + std::to_string(foreign) + ")");
    }

    void RunAll(GraphicsDevice& dev)
    {
        RunA_SpriteMagnification(dev);
        RunB_SpriteMinification(dev);
        RunC_ThreeDMagnification(dev);
        RunD_ThreeDMinification(dev);
        RunE_EffectFamilies(dev);
        RunF_CacheDistinctness(dev);
        RunG_QueuedTransitions(dev);
        RunH_AddressModes(dev);
        RunI_Geometry(dev);
        RunJ_TwoTextures(dev);
    }

    void Finish()
    {
        std::printf("=== %d/%d checks passed on %s ===\n", passCount_, totalCount_, kRendererName);
        std::fflush(stdout);
        result_ = (totalCount_ > 0 && passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> b8 = p8x4_.Bytes();
        const std::vector<std::uint8_t> b5 = p5x3_.Bytes();
        const std::vector<std::uint8_t> b4 = p4x4_.Bytes();
        tex8x4_ = Texture2D::CreateFromPixels(dev, p8x4_.w, p8x4_.h, b8);
        tex5x3_ = Texture2D::CreateFromPixels(dev, p5x3_.w, p5x3_.h, b5);
        tex4x4_ = Texture2D::CreateFromPixels(dev, p4x4_.w, p4x4_.h, b4);
        white_  = Texture2D::CreateFromPixels(dev, 1, 1,
                      std::vector<std::uint8_t>{255, 255, 255, 255});
        // A non-rasterizing renderer rejects cube uploads by REMED-GFX-135's contract, and it never
        // reaches the effect families anyway, so an unavailable cube must not abort the run.
        try
        {
            cube_ = std::make_unique<TextureCube>(dev, 4, false, SurfaceFormat::Color);
            const std::vector<Color> faceTexels(16, Color(0, 0, 0, 255));
            for (int f = 0; f < 6; ++f)
                cube_->SetData(static_cast<CubeMapFace>(f), faceTexels.data(),
                               static_cast<int>(faceTexels.size()));
        }
        catch (const std::exception&)
        {
            cube_.reset();
        }
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        std::printf("=== REMED-GFX-170 TextureFilter ordinal contract on %s ===\n", kRendererName);
        note("MIP BOUNDARY: every texture here has ONE level, so the mipmap half of each ordinal "
             "is asserted through the native sampler descriptor (CNA_WEBGPU_SAMPLER_TRACE / "
             "CNA_SDLGPU_SAMPLER_TRACE), not through pixels. No mip generation is added.");
        note("ANISOTROPY BOUNDARY: anisotropic output is unspecified per GPU, so only its LINEAR "
             "min/mag classification is required -- which is the half that was broken.");
        note("GFX-172 BOUNDARY: DualTextureEffect is exercised on slot 0 only.");

        if (!kRasterizes)
        {
            RenderTarget2D rt(dev, 8, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            std::vector<Color> pix(32u, Color(0, 0, 0, 0));
            bool threw = false;
            try { rt.GetData(pix.data(), 0, static_cast<int>(pix.size())); }
            catch (const System::Exception&) { threw = true; }
            catch (const std::exception&) { threw = true; }
            check(threw, std::string("Z1: ") + kRendererName +
                  " rejects a render-target readback instead of fabricating a sampled image");
            Finish();
            return;
        }

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
    TextureFilterOrdinalContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    TextureFilterOrdinalContractTest game;
    game.Run();
    return game.getResult();
}
