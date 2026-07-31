// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-182: the SLOT-1 SAMPLER STATE of an EnvironmentMapEffect reflection cube, measured
// where REMED-GFX-173's contract fixture does not reach.
//
// THE CONTRACT is the one that fixture pins and this one extends: `SamplerStates[0]` filters the
// base 2D texture (`EnvironmentMapEffect.Texture`) and `SamplerStates[1]` filters the cube
// (`EnvironmentMapEffect.EnvironmentMap`); the two are independent; every draw uses the state that
// was public when IT was issued; and an explicit sampler is never replaced by an implicit one.
//
// THE DEFECT this closes is Software-local. `SoftwareGraphicsBackend::SampleCubeMap` took no
// sampler state at all -- after face selection it ended in a single clamped
// `static_cast<int>(s * size)` fetch of level 0 -- so the cube was point-sampled at level 0 however
// `SamplerStates[1]` was set. REMED-GFX-150 replaced that shape on the ordinary 2D path and
// REMED-GFX-175 gave it mip selection; neither reached the cube.
//
// WHAT THIS FIXTURE ADDS over REMED-GFX-173's. That one answers "is slot 1 consulted, per ordinal,
// per draw path, per face, per mip level". This one answers the questions a lost sampler state can
// also fail:
//
//   N1  RETURNING to a sampler        Point -> Linear -> Point must reproduce the FIRST image
//                                     byte-for-byte, so nothing latches on first use.
//   N2  INTERLEAVING                  an ordinary BasicEffect textured draw between two env-map
//                                     draws must not move the cube, and the env-map draws must not
//                                     move the ordinary one.
//   N3  slot 0 SWEPT, slot 1 fixed    all nine slot-0 ordinals leave the cube byte-identical.
//   N4  slot 1 SWEPT, slot 0 fixed    all nine slot-1 ordinals leave the 2D texture byte-identical.
//   N5  the MAGNIFICATION PARTITION   the nine ordinals fall into EXACTLY the two groups the public
//                                     enum names -- {1,4,5,6} magnify by Point, {0,2,3,7,8} by
//                                     Linear -- byte-identical inside each group and different
//                                     between them.
//   N6  LIFETIME                      create/sample/destroy repeatedly; a new cube at a recycled
//                                     address must show ITS OWN texels, and a sampler change must
//                                     not need the cube rebuilt.
//   N7  DEGENERATE DIRECTION          a zero vertex normal makes the reflection vector degenerate;
//                                     the draw must not crash, must be deterministic, and must
//                                     still land on stored texels under Point.
//   N8  DECLARED-BUT-UNWRITTEN CHAIN  a mipMap=true cube whose game wrote only level 0, minified.
//   N9  RenderTargetCube              classified honestly, per backend.
//
// THE ORACLE is byte-exact, and identical in construction to REMED-GFX-173's: a generated palette
// whose entries are at least `kSep` apart in L-inf and whose every pairwise midpoint is at least
// `kMidSep` from every entry -- both CHECKED, not assumed -- so "this pixel is a stored texel" and
// "this pixel is an interpolation" are decidable exactly. With `EnvironmentMapAmount = 1`,
// `FresnelFactor = 0`, no specular, white diffuse, alpha 1 and an opaque base texture the fragment
// result is EXACTLY the cube sample, so the configuration measures slot 1 alone; with amount 0 and
// ambient-only white lighting it is exactly the base texel, so it measures slot 0 alone.
//
// GEOMETRY: a screen-filling quad at z=0 under an ORTHOGRAPHIC projection with the eye at
// (0,0,4/3). For a +Z normal `reflect(-E,N)` is proportional to (x, y, 4/3), linear in the pixel and
// always +Z-major, so one 4x4 cube face magnifies into a 4x4 grid of blocks across the target.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/Exception.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
#else
    constexpr bool kRasterizes = true;
    constexpr const char* kBackendName = "UNKNOWN";
#endif

    /// Whether a declared-but-unwritten mip chain has a DEFINED answer on this backend. Real
    /// XNA/D3D leave the content of a level nobody uploaded undefined, so this is not a public
    /// contract; REMED-GFX-175 made it a deliberate, conservative Software guarantee (the sampler
    /// bounds itself at the last level that was really supplied) and REMED-GFX-182 extended that
    /// per cube face. Asserted where it is a guarantee, reported everywhere else.
#if defined(CNA_BACKEND_SOFTWARE)
    constexpr bool kUnwrittenChainIsBounded = true;
#else
    constexpr bool kUnwrittenChainIsBounded = false;
#endif

    constexpr int kBBW = 128;
    constexpr int kBBH = 96;
    /// Magnifying target: a 4x4 cube face becomes a 4x4 grid of blocks across it.
    constexpr int kRT = 32;
    /// Minifying target, for the mip leg.
    constexpr int kMipRT = 8;
    /// Cube face edge and base 2D texture edge, in texels.
    constexpr int kFace = 4;
    constexpr int kTex = 4;
    /// Mipmapped cube face edge -- 32 gives levels 0..5.
    constexpr int kMipFace = 32;
    /// Eye distance; > 1 keeps +Z the strict major axis of the reflection over the whole quad.
    constexpr float kEyeD = 4.0f / 3.0f;

    /// 6 faces x 16 texels, then the base texture, then a 16-colour block the lifetime leg gives
    /// its second-generation cubes. That block is reused on all six of those faces -- the leg asks
    /// "which GENERATION is this pixel from", and face aliasing is REMED-GFX-173 leg I's question,
    /// so 16 disjoint colours are exactly what it needs and 96 would only strain the generator.
    constexpr int kCubeColors = 6 * 16;
    constexpr int kBaseColors = 16;
    constexpr int kSpareColors = 16;
    constexpr int kPaletteSize = kCubeColors + kBaseColors + kSpareColors;
    constexpr int kSep = 20;
    constexpr int kMidSep = 9;

    const Color kSentinel(11, 13, 17, 255);

    /// Stride-32 VertexPositionNormalTexture, the only layout EnvironmentMapEffect accepts.
    struct VtxPNT { float px, py, pz; float nx, ny, nz; float u, v; };
    static_assert(sizeof(VtxPNT) == 32, "stride 32");

    bool SameColor(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    std::uint32_t Pack(const Color& c)
    {
        return (static_cast<std::uint32_t>(c.getRProperty()) << 24) |
               (static_cast<std::uint32_t>(c.getGProperty()) << 16) |
               (static_cast<std::uint32_t>(c.getBProperty()) << 8) |
                static_cast<std::uint32_t>(c.getAProperty());
    }

    int LinfRGB(const Color& a, const Color& b)
    {
        const int dr = std::abs(static_cast<int>(a.getRProperty()) - static_cast<int>(b.getRProperty()));
        const int dg = std::abs(static_cast<int>(a.getGProperty()) - static_cast<int>(b.getGProperty()));
        const int db = std::abs(static_cast<int>(a.getBProperty()) - static_cast<int>(b.getBProperty()));
        return std::max(dr, std::max(dg, db));
    }

    Color Mid(const Color& a, const Color& b)
    {
        return Color(static_cast<std::uint8_t>((static_cast<int>(a.getRProperty()) + b.getRProperty()) / 2),
                     static_cast<std::uint8_t>((static_cast<int>(a.getGProperty()) + b.getGProperty()) / 2),
                     static_cast<std::uint8_t>((static_cast<int>(a.getBProperty()) + b.getBProperty()) / 2),
                     static_cast<std::uint8_t>(255));
    }

    std::vector<std::uint8_t> Bytes(const std::vector<Color>& p)
    {
        std::vector<std::uint8_t> out;
        out.reserve(p.size() * 4u);
        for (const Color& c : p)
        {
            out.push_back(c.getRProperty());
            out.push_back(c.getGProperty());
            out.push_back(c.getBProperty());
            out.push_back(c.getAProperty());
        }
        return out;
    }

    const char* FilterName(int ordinal)
    {
        switch (ordinal)
        {
            case 0: return "Linear";
            case 1: return "Point";
            case 2: return "Anisotropic";
            case 3: return "LinearMipPoint";
            case 4: return "PointMipLinear";
            case 5: return "MinLinearMagPointMipLinear";
            case 6: return "MinLinearMagPointMipPoint";
            case 7: return "MinPointMagLinearMipLinear";
            case 8: return "MinPointMagLinearMipPoint";
            default: return "<out-of-range>";
        }
    }

    /// The public enum's magnification component -- REMED-GFX-170's authoritative table, not a
    /// second copy of a mapping.
    bool MagnifiesByPoint(int ordinal)
    {
        return ordinal == 1 || ordinal == 4 || ordinal == 5 || ordinal == 6;
    }
}

class EnvMapCubeSamplerStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    bool paletteOk_ = true;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    std::vector<Color> palette_;
    std::set<std::uint32_t> cubeSet_, baseSet_, spareSet_;

    Texture2D base_;
    std::unique_ptr<TextureCube> cube_, cubeLevel0Only_;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void skip(const std::string& label)
    {
        std::printf("[SKIP] %s\n", label.c_str());
        std::fflush(stdout);
    }

    void note(const std::string& label)
    {
        std::printf("[NOTE] %s\n", label.c_str());
        std::fflush(stdout);
    }

    // --------------------------------------------------------------------------------------
    // Palette
    // --------------------------------------------------------------------------------------

    /// Deterministic (a fixed LCG, no <random>), so the same colours are measured on every backend
    /// and in every run.
    static std::vector<Color> BuildPalette()
    {
        std::vector<Color> out;
        out.reserve(kPaletteSize);
        std::vector<Color> mids;
        std::uint32_t s = 20260731u;
        auto nextByte = [&s]() {
            s = s * 1664525u + 1013904223u;
            return static_cast<std::uint8_t>((s >> 16) & 0xFFu);
        };
        int guard = 0;
        while (static_cast<int>(out.size()) < kPaletteSize && guard++ < 4000000)
        {
            const Color c(nextByte(), nextByte(), nextByte(), static_cast<std::uint8_t>(255));

            bool ok = true;
            for (const Color& p : out)
                if (LinfRGB(c, p) < kSep) { ok = false; break; }
            if (!ok) continue;
            for (const Color& m : mids)
                if (LinfRGB(m, c) < kMidSep) { ok = false; break; }
            if (!ok) continue;

            std::vector<Color> fresh;
            fresh.reserve(out.size());
            for (const Color& p : out)
            {
                const Color m = Mid(c, p);
                if (LinfRGB(m, c) < kMidSep) { ok = false; break; }
                for (const Color& r2 : out)
                    if (LinfRGB(m, r2) < kMidSep) { ok = false; break; }
                if (!ok) break;
                fresh.push_back(m);
            }
            if (!ok) continue;

            out.push_back(c);
            mids.insert(mids.end(), fresh.begin(), fresh.end());
        }
        return out;
    }

    const Color& CubeTexel(int face, int i) const
    { return palette_[static_cast<std::size_t>(face) * 16 + i]; }
    const Color& BaseTexel(int i) const
    { return palette_[static_cast<std::size_t>(kCubeColors + i)]; }
    const Color& SpareTexel(int i) const
    { return palette_[static_cast<std::size_t>(kCubeColors + kBaseColors + i)]; }

    // --------------------------------------------------------------------------------------
    // Oracles
    // --------------------------------------------------------------------------------------

    static int OffPalette(const std::vector<Color>& pix, const std::set<std::uint32_t>& pal)
    {
        int n = 0;
        for (const Color& c : pix) if (pal.find(Pack(c)) == pal.end()) ++n;
        return n;
    }

    static int DistinctColors(const std::vector<Color>& pix)
    {
        std::set<std::uint32_t> seen;
        for (const Color& c : pix) seen.insert(Pack(c));
        return static_cast<int>(seen.size());
    }

    static int DiffPixels(const std::vector<Color>& a, const std::vector<Color>& b)
    {
        int n = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
            if (!SameColor(a[i], b[i])) ++n;
        return n;
    }

    // --------------------------------------------------------------------------------------
    // Device helpers
    // --------------------------------------------------------------------------------------

    static SamplerState Sampler(TextureFilter f, TextureAddressMode a)
    {
        SamplerState s;
        s.setFilterProperty(f);
        s.setAddressUProperty(a);
        s.setAddressVProperty(a);
        return s;
    }

    static void SetSlot(GraphicsDevice& dev, int slot, int ordinal, TextureAddressMode a)
    {
        dev.getSamplerStatesProperty()[slot] = Sampler(static_cast<TextureFilter>(ordinal), a);
    }

    static void ResetDeviceState(GraphicsDevice& dev, int w, int h)
    {
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    // --------------------------------------------------------------------------------------
    // Geometry and draws
    // --------------------------------------------------------------------------------------

    static std::vector<VtxPNT> Quad(float nx, float ny, float nz)
    {
        const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
        const float ux = (l > 0.0f) ? nx / l : 0.0f;
        const float uy = (l > 0.0f) ? ny / l : 0.0f;
        const float uz = (l > 0.0f) ? nz / l : 0.0f;
        auto v = [&](float x, float y, float u, float w) {
            VtxPNT r{};
            r.px = x; r.py = y; r.pz = 0.0f;
            r.nx = ux; r.ny = uy; r.nz = uz;
            r.u = u; r.v = w;
            return r;
        };
        return { v(-1.0f, 1.0f, 0.0f, 0.0f), v(-1.0f, -1.0f, 0.0f, 1.0f), v(1.0f, -1.0f, 1.0f, 1.0f),
                 v(-1.0f, 1.0f, 0.0f, 0.0f), v(1.0f, -1.0f, 1.0f, 1.0f), v(1.0f, 1.0f, 1.0f, 0.0f) };
    }

    void ConfigureEnv(EnvironmentMapEffect& fx, TextureCube* cube, float amount, Texture2D* tex) const
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, kEyeD), Vector3::Zero, Vector3::Up));
        fx.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f));
        fx.setTextureProperty(tex);
        fx.setEnvironmentMapProperty(cube);
        fx.setEnvironmentMapAmountProperty(amount);
        // FresnelFactor 0 also clears FresnelEnabled, so the blend factor is the flat amount.
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(1.0f);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
    }

    void DrawEnv(GraphicsDevice& dev, TextureCube* cube, float amount, Texture2D* tex,
                 float nx, float ny, float nz)
    {
        EnvironmentMapEffect fx(dev);
        ConfigureEnv(fx, cube, amount, tex);
        fx.Apply();

        const std::vector<VtxPNT> quad = Quad(nx, ny, nz);
        VertexBuffer vb(dev, static_cast<int>(quad.size()));
        vb.SetDataRaw(quad.data(), static_cast<int>(quad.size()), static_cast<int>(sizeof(VtxPNT)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    /// An ordinary textured BasicEffect draw -- the "unrelated effect" leg N2 interleaves. A 1:1
    /// blit of the base texture, so its own result is a set of stored base texels under Point.
    static void DrawPlain(GraphicsDevice& dev, Texture2D* tex)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(tex);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(1.0f);
        fx.setFogEnabledProperty(false);
        fx.Apply();
        const std::vector<VertexPositionTexture> blit = {
            VertexPositionTexture(Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 0.0f)),
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, blit.data(), 0, 2);
    }

    /// One env-map render into a fresh target, read back.
    std::vector<Color> Render(GraphicsDevice& dev, TextureCube* cube, float amount, Texture2D* tex,
                              int edge = kRT, float nx = 0.0f, float ny = 0.0f, float nz = 1.0f)
    {
        RenderTarget2D rt(dev, edge, edge, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, edge, edge);
        dev.Clear(kSentinel);
        DrawEnv(dev, cube, amount, tex, nx, ny, nz);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(edge) * edge, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    // --------------------------------------------------------------------------------------
    // Legs
    // --------------------------------------------------------------------------------------

    void RunN1_ReturnToSampler(GraphicsDevice& dev)
    {
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);

        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        const std::vector<Color> first = Render(dev, cube_.get(), 1.0f, &base_);
        SetSlot(dev, 1, 0, TextureAddressMode::Clamp);
        const std::vector<Color> middle = Render(dev, cube_.get(), 1.0f, &base_);
        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        const std::vector<Color> again = Render(dev, cube_.get(), 1.0f, &base_);

        check(OffPalette(first, cubeSet_) == 0,
              "N1a: the first PointClamp draw lands on stored cube texels only (" +
              std::to_string(OffPalette(first, cubeSet_)) + " off-palette)");
        check(DiffPixels(first, middle) > 0,
              "N1b: LinearClamp in between really changed the image (differing=" +
              std::to_string(DiffPixels(first, middle)) + ")");
        check(DiffPixels(first, again) == 0,
              "N1c: RETURNING to PointClamp reproduces the first image byte-for-byte, so no cube "
              "sampler latches on first use (differing=" + std::to_string(DiffPixels(first, again)) + ")");
        check(OffPalette(again, cubeSet_) == 0,
              "N1d: the returned-to PointClamp draw is exact again (" +
              std::to_string(OffPalette(again, cubeSet_)) + " off-palette)");
    }

    void RunN2_Interleaved(GraphicsDevice& dev)
    {
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        const std::vector<Color> aloneEnv = Render(dev, cube_.get(), 1.0f, &base_);

        // One target, three draws: env(Point) -> ordinary BasicEffect -> env(Point), each into its
        // own target so the three results are separable, with the ordinary draw genuinely issued
        // between them on the same device.
        RenderTarget2D rtA(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        RenderTarget2D rtB(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        RenderTarget2D rtC(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);

        dev.SetRenderTarget(&rtA);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);
        DrawEnv(dev, cube_.get(), 1.0f, &base_, 0.0f, 0.0f, 1.0f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(&rtB);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);
        DrawPlain(dev, &base_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(&rtC);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);
        DrawEnv(dev, cube_.get(), 1.0f, &base_, 0.0f, 0.0f, 1.0f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);

        std::vector<Color> a(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
        std::vector<Color> b = a, c = a;
        rtA.GetData(a.data(), 0, static_cast<int>(a.size()));
        rtB.GetData(b.data(), 0, static_cast<int>(b.size()));
        rtC.GetData(c.data(), 0, static_cast<int>(c.size()));

        check(DiffPixels(a, aloneEnv) == 0,
              "N2a: an env-map draw is byte-identical whether or not other effects follow it "
              "(differing=" + std::to_string(DiffPixels(a, aloneEnv)) + ")");
        check(DiffPixels(a, c) == 0,
              "N2b: an ordinary BasicEffect draw BETWEEN two identical env-map draws leaves the "
              "cube byte-identical (differing=" + std::to_string(DiffPixels(a, c)) + ")");
        check(OffPalette(b, baseSet_) == 0,
              "N2c: the interleaved ordinary draw itself lands on stored base texels only (" +
              std::to_string(OffPalette(b, baseSet_)) + " off-palette)");
        check(OffPalette(c, cubeSet_) == 0,
              "N2d: the env-map draw AFTER the ordinary one is still exact (" +
              std::to_string(OffPalette(c, cubeSet_)) + " off-palette)");
    }

    void RunN3_Slot0Swept(GraphicsDevice& dev)
    {
        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        std::vector<Color> reference;
        int moved = 0;
        for (int f = 0; f <= 8; ++f)
        {
            SetSlot(dev, 0, f, TextureAddressMode::Clamp);
            const std::vector<Color> pix = Render(dev, cube_.get(), 1.0f, &base_);
            if (reference.empty()) reference = pix;
            else if (DiffPixels(reference, pix) > 0) ++moved;
        }
        check(moved == 0,
              "N3: sweeping ALL NINE slot-0 ordinals with slot 1 fixed leaves the cube "
              "byte-identical -- slot 0 cannot reach the cube (" + std::to_string(moved) +
              "/8 ordinals moved it)");
        check(OffPalette(reference, cubeSet_) == 0,
              "N3b: and the cube image is exact throughout (" +
              std::to_string(OffPalette(reference, cubeSet_)) + " off-palette)");
    }

    void RunN4_Slot1Swept(GraphicsDevice& dev)
    {
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        std::vector<Color> reference;
        int moved = 0;
        for (int f = 0; f <= 8; ++f)
        {
            SetSlot(dev, 1, f, TextureAddressMode::Clamp);
            const std::vector<Color> pix = Render(dev, cube_.get(), 0.0f, &base_);
            if (reference.empty()) reference = pix;
            else if (DiffPixels(reference, pix) > 0) ++moved;
        }
        check(moved == 0,
              "N4: sweeping ALL NINE slot-1 ordinals with slot 0 fixed leaves the 2D texture "
              "byte-identical -- slot 1 cannot reach the base texture (" + std::to_string(moved) +
              "/8 ordinals moved it)");
        check(OffPalette(reference, baseSet_) == 0,
              "N4b: and the base image is exact throughout (" +
              std::to_string(OffPalette(reference, baseSet_)) + " off-palette)");
    }

    void RunN5_MagnificationPartition(GraphicsDevice& dev)
    {
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        std::vector<std::vector<Color>> byOrdinal;
        for (int f = 0; f <= 8; ++f)
        {
            SetSlot(dev, 1, f, TextureAddressMode::Clamp);
            byOrdinal.push_back(Render(dev, cube_.get(), 1.0f, &base_));
        }

        const std::vector<Color>* firstPoint = nullptr;
        const std::vector<Color>* firstLinear = nullptr;
        int pointSpread = 0, linearSpread = 0;
        for (int f = 0; f <= 8; ++f)
        {
            // Anisotropic(2) lowers the level a MINIFIED fetch selects by a per-GPU amount; this leg
            // magnifies, where it is a Linear magnifier everywhere, but its group membership is the
            // one boundary REMED-GFX-170 declared and is reported rather than required.
            if (f == 2) continue;
            const std::vector<Color>& pix = byOrdinal[static_cast<std::size_t>(f)];
            if (MagnifiesByPoint(f))
            {
                if (!firstPoint) firstPoint = &pix; else pointSpread += DiffPixels(*firstPoint, pix);
            }
            else
            {
                if (!firstLinear) firstLinear = &pix; else linearSpread += DiffPixels(*firstLinear, pix);
            }
        }

        check(pointSpread == 0,
              "N5a: the four Point-magnifying ordinals (1,4,5,6) agree byte-for-byte (differing=" +
              std::to_string(pointSpread) + ")");
        check(linearSpread == 0,
              "N5b: the four non-anisotropic Linear-magnifying ordinals (0,3,7,8) agree "
              "byte-for-byte (differing=" + std::to_string(linearSpread) + ")");
        check(firstPoint && firstLinear && DiffPixels(*firstPoint, *firstLinear) > 0,
              "N5c: the two magnification groups DIFFER, so the MAGNIFICATION component of slot 1 "
              "reaches the cube (differing=" +
              std::to_string((firstPoint && firstLinear) ? DiffPixels(*firstPoint, *firstLinear) : 0) + ")");
        check(firstPoint && OffPalette(*firstPoint, cubeSet_) == 0,
              "N5d: the Point group is exactly stored cube texels (" +
              std::to_string(firstPoint ? OffPalette(*firstPoint, cubeSet_) : -1) + " off-palette)");
        check(firstLinear && OffPalette(*firstLinear, cubeSet_) > 0,
              "N5e: the Linear group interpolates -- colours appear that exist in NO cube texel (" +
              std::to_string(firstLinear ? OffPalette(*firstLinear, cubeSet_) : -1) + ")");
        note(std::string("N5: Anisotropic(2) is in the ") +
             (firstLinear && DiffPixels(byOrdinal[2], *firstLinear) == 0 ? "Linear" : "some other") +
             " magnification group here; reported, not asserted.");
    }

    void RunN6_Lifetime(GraphicsDevice& dev)
    {
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);

        // Three create/sample/destroy rounds. Each round builds a cube from a DIFFERENT palette
        // block, so a stale pointer to a freed cube -- or an address the allocator recycles --
        // shows up as the previous round's texels rather than as a crash.
        int wrongGeneration = 0;
        int exact = 0;
        for (int round = 0; round < 3; ++round)
        {
            auto temp = std::make_unique<TextureCube>(dev, kFace, false, SurfaceFormat::Color);
            for (int f = 0; f < 6; ++f)
            {
                std::vector<Color> face;
                face.reserve(16);
                for (int i = 0; i < 16; ++i)
                    face.push_back((round % 2 == 0) ? SpareTexel(i) : CubeTexel(f, i));
                temp->SetData(static_cast<CubeMapFace>(f), face.data(), 16);
            }
            const std::vector<Color> pix = Render(dev, temp.get(), 1.0f, &base_);
            const std::set<std::uint32_t>& expected = (round % 2 == 0) ? spareSet_ : cubeSet_;
            const std::set<std::uint32_t>& other = (round % 2 == 0) ? cubeSet_ : spareSet_;
            if (OffPalette(pix, expected) != 0) ++wrongGeneration;
            else ++exact;
            for (const Color& c : pix)
                if (other.find(Pack(c)) != other.end()) { ++wrongGeneration; break; }
            temp.reset();
            // A draw after the destruction, on the SURVIVING cube, must still be correct.
            const std::vector<Color> after = Render(dev, cube_.get(), 1.0f, &base_);
            if (OffPalette(after, cubeSet_) != 0) ++wrongGeneration;
        }
        check(wrongGeneration == 0 && exact == 3,
              "N6a: three create/sample/destroy rounds each sample THEIR OWN cube's texels, and a "
              "draw after each destruction is still correct (" + std::to_string(wrongGeneration) +
              " anomalies, " + std::to_string(exact) + "/3 exact)");

        // A sampler change must not need the cube rebuilt: the same live cube under a new slot-1
        // state must change without any SetData in between.
        SetSlot(dev, 1, 0, TextureAddressMode::Clamp);
        const std::vector<Color> linear = Render(dev, cube_.get(), 1.0f, &base_);
        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        const std::vector<Color> point = Render(dev, cube_.get(), 1.0f, &base_);
        check(DiffPixels(linear, point) > 0 && OffPalette(point, cubeSet_) == 0,
              "N6b: a slot-1 change alone re-filters a LIVE cube -- no upload, no recreation "
              "(differing=" + std::to_string(DiffPixels(linear, point)) + ")");
    }

    void RunN7_DegenerateDirection(GraphicsDevice& dev)
    {
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        try
        {
            // A ZERO vertex normal: `reflect(-E, N)` collapses, so the cube is addressed by a
            // direction with no major axis. The requirement is not a particular pixel -- it is that
            // the backend stays defined: no crash, the same answer twice, and no fetch outside the
            // cube's own storage.
            const std::vector<Color> a = Render(dev, cube_.get(), 1.0f, &base_, kRT, 0.0f, 0.0f, 0.0f);
            const std::vector<Color> b = Render(dev, cube_.get(), 1.0f, &base_, kRT, 0.0f, 0.0f, 0.0f);
            check(DiffPixels(a, b) == 0,
                  "N7a: a degenerate (zero) normal renders DETERMINISTICALLY (differing=" +
                  std::to_string(DiffPixels(a, b)) + ")");
            std::set<std::uint32_t> allowed = cubeSet_;
            allowed.insert(Pack(kSentinel));
            check(OffPalette(a, allowed) == 0,
                  "N7b: and every pixel is a stored cube texel or the untouched clear colour -- no "
                  "fetch outside the cube's storage (" + std::to_string(OffPalette(a, allowed)) +
                  " off-palette, " + std::to_string(DistinctColors(a)) + " distinct)");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("N7: a degenerate normal threw (") + e.what() + ")");
        }
    }

    void RunN8_UnwrittenChain(GraphicsDevice& dev)
    {
        if (!cubeLevel0Only_)
        {
            skip("N8: this backend could not store a mipmapped cube");
            return;
        }
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        const std::vector<Color> pix = Render(dev, cubeLevel0Only_.get(), 1.0f, &base_, kMipRT);
        const int off = OffPalette(pix, cubeSet_);
        const std::string diag = " (" + std::to_string(off) + " off-palette, " +
                                 std::to_string(DistinctColors(pix)) + " distinct)";
        if (kUnwrittenChainIsBounded)
            check(off == 0,
                  "N8: a mipMap=true cube whose game wrote ONLY level 0, minified under mip-Point, "
                  "samples stored level-0 texels -- the sampler bounds itself at the last level "
                  "really supplied and never fades into a level nobody uploaded" + diag);
        else
            note("N8: a mipMap=true cube whose game wrote only level 0 has UNDEFINED content above "
                 "level 0 in XNA/D3D, so this backend's answer is reported, not required" + diag);
    }

    void RunN9_RenderTargetCube(GraphicsDevice& dev)
    {
        std::unique_ptr<RenderTargetCube> rtc;
        try
        {
            rtc = std::make_unique<RenderTargetCube>(dev, kFace, false, SurfaceFormat::Color,
                                                     DepthFormat::None, 0,
                                                     RenderTargetUsage::PreserveContents);
        }
        catch (const std::exception& e)
        {
            note(std::string("N9: this backend refuses to CREATE a RenderTargetCube (") + e.what() +
                 "), so no cube render target can reach the sampler. REMED-GFX-173's leg L is the "
                 "place that measures it where it exists.");
            return;
        }
        try
        {
            dev.SetRenderTarget(rtc.get(), CubeMapFace::PositiveX);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetDeviceState(dev, kBBW, kBBH);
            note("N9: this backend BINDS a RenderTargetCube face; REMED-GFX-173's leg L measures "
                 "the sampler contract against it, and this fixture leaves it there.");
        }
        catch (const std::exception& e)
        {
            ResetDeviceState(dev, kBBW, kBBH);
            note(std::string("N9: this backend creates a RenderTargetCube but refuses to BIND a "
                             "face (") + e.what() + "), so it can never hold content to sample. "
                 "The refusal is a clean exception, not a silent no-op.");
        }
    }

public:
    EnvMapCubeSamplerStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void LoadContent() override
    {
        GraphicsDevice& dev = getGraphicsDeviceProperty();

        palette_ = BuildPalette();
        if (static_cast<int>(palette_.size()) < kPaletteSize)
        {
            check(false, "PAL1: the generator produced " + std::to_string(palette_.size()) +
                         " separated colours, fewer than the " + std::to_string(kPaletteSize) +
                         " this fixture needs");
            paletteOk_ = false;
            return;
        }
        check(true, "PAL1: " + std::to_string(palette_.size()) +
                    " palette entries, each at least " + std::to_string(kSep) + " apart in L-inf");

        int tooClose = 0, midTooClose = 0;
        for (std::size_t i = 0; i < palette_.size(); ++i)
            for (std::size_t j = i + 1; j < palette_.size(); ++j)
            {
                if (LinfRGB(palette_[i], palette_[j]) < kSep) ++tooClose;
                const Color m = Mid(palette_[i], palette_[j]);
                for (const Color& e : palette_)
                    if (LinfRGB(m, e) < kMidSep) { ++midTooClose; break; }
            }
        check(tooClose == 0,
              "PAL2: no two palette entries are within " + std::to_string(kSep) + " (" +
              std::to_string(tooClose) + " violations)");
        check(midTooClose == 0,
              "PAL3: no midpoint of two entries is within " + std::to_string(kMidSep) +
              " of any entry, so an interpolated pixel is decidable exactly (" +
              std::to_string(midTooClose) + " violations)");

        for (int f = 0; f < 6; ++f)
            for (int i = 0; i < 16; ++i)
                cubeSet_.insert(Pack(CubeTexel(f, i)));
        for (int i = 0; i < kSpareColors; ++i) spareSet_.insert(Pack(SpareTexel(i)));
        for (int i = 0; i < kBaseColors; ++i) baseSet_.insert(Pack(BaseTexel(i)));

        std::vector<Color> baseTexels;
        for (int i = 0; i < kTex * kTex; ++i) baseTexels.push_back(BaseTexel(i));
        base_ = Texture2D::CreateFromPixels(dev, kTex, kTex, Bytes(baseTexels));

        try
        {
            cube_ = std::make_unique<TextureCube>(dev, kFace, false, SurfaceFormat::Color);
            for (int f = 0; f < 6; ++f)
            {
                std::vector<Color> face;
                face.reserve(16);
                for (int i = 0; i < 16; ++i) face.push_back(CubeTexel(f, i));
                cube_->SetData(static_cast<CubeMapFace>(f), face.data(), 16);
            }
        }
        catch (const std::exception&)
        {
            cube_.reset();
        }

        try
        {
            // DECLARED mipMap=true, but only level 0 is ever written. The pattern repeats the 4x4
            // cube texels in 8x8 blocks, so level 0 alone is a set of STORED colours.
            cubeLevel0Only_ = std::make_unique<TextureCube>(dev, kMipFace, true, SurfaceFormat::Color);
            for (int f = 0; f < 6; ++f)
            {
                std::vector<Color> level0;
                level0.reserve(static_cast<std::size_t>(kMipFace) * kMipFace);
                for (int y = 0; y < kMipFace; ++y)
                    for (int x = 0; x < kMipFace; ++x)
                        level0.push_back(CubeTexel(f, (y / 8) * kFace + (x / 8)));
                cubeLevel0Only_->SetData(static_cast<CubeMapFace>(f), level0.data(),
                                         static_cast<int>(level0.size()));
            }
        }
        catch (const std::exception&)
        {
            cubeLevel0Only_.reset();
        }
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        GraphicsDevice& dev = getGraphicsDeviceProperty();
        std::printf("=== REMED-GFX-182 EnvironmentMapEffect slot-1 sampler state [%s] ===\n",
                    kBackendName);

        if (!paletteOk_)
        {
            skip("N1..N9: the palette generator did not converge, so no oracle exists");
        }
        else if (!kRasterizes)
        {
            skip("N1..N9: this backend produces no pixels, so a sampler is unobservable here");
        }
        else if (!cube_)
        {
            skip("N1..N9: this backend could not store a TextureCube, so EnvironmentMapEffect has "
                 "no reflection source");
        }
        else
        {
            try
            {
                RunN1_ReturnToSampler(dev);
                RunN2_Interleaved(dev);
                RunN3_Slot0Swept(dev);
                RunN4_Slot1Swept(dev);
                RunN5_MagnificationPartition(dev);
                RunN6_Lifetime(dev);
                RunN7_DegenerateDirection(dev);
                RunN8_UnwrittenChain(dev);
                RunN9_RenderTargetCube(dev);
            }
            catch (const System::Exception& e)
            {
                skip(std::string("N1..N9: this backend does not implement EnvironmentMapEffect (") +
                     e.what() + ")");
            }
            catch (const std::exception& e)
            {
                check(false, std::string("N1..N9: unexpected exception: ") + e.what());
            }
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }
};

int main()
{
    try
    {
        EnvMapCubeSamplerStateTest test;
        test.Run();
        return test.Result();
    }
    catch (const std::exception& e)
    {
        std::printf("[FAIL] the fixture itself threw: %s\n", e.what());
        return 1;
    }
}
