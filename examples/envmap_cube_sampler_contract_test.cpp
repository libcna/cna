// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-173: EnvironmentMapEffect samples TWO independently observable resources -- an ordinary
// 2D texture and a reflection cube -- and each must be filtered by the sampler assigned to ITS OWN
// public GraphicsDevice.SamplerStates slot.
//
// THE CONTRACT. `SamplerStates[0]` filters the base 2D texture (`EnvironmentMapEffect.Texture`) and
// `SamplerStates[1]` filters the cube (`EnvironmentMapEffect.EnvironmentMap`). This is not a summary
// taken on trust: `env_map3d.frag.glsl` declares `binding = 0 sampler2D uTexture` and
// `binding = 1 samplerCube uEnvMap`, and Vulkan's `GetOrCreateEnvMapDescSet` -- corrected under
// REMED-GFX-169 and the reference this fixture measures every other backend against -- writes
// `slotSamplers_[0]` into binding 0 and `slotSamplers_[1]` into binding 1. The two slots are
// INDEPENDENT: changing one may not alter what the other resource looks like, both must be captured
// when the draw is issued so a later public change cannot reach back into a queued command, and an
// explicit sampler must never be replaced by a fixed constant. An unset slot uses the documented
// device default (`SamplerState::LinearWrap`).
//
// THE DEFECT this reproduces is SDL_GPU-local. `SdlGpuGraphicsBackend::IssueEnvMapDraw` binds
// `samplerBindings[1].sampler = GetOrCreateSampler(0, 1, 1, ...)` -- a literal LinearClamp -- for
// the cube, and `QueueEnvMapDraw` captures only `samplerSlots_[0]`, so `EnvMapDrawCommand` has no
// field in which slot 1 could survive to replay even if the constant were removed. Both halves are
// therefore required: a capture without a binding change would still bind the constant, and a
// binding change without a capture would read live state at replay and break the deferred-snapshot
// contract. `DualTextureEffect` is the positive control living three functions above it -- its
// command carries `texture1Filter/AddressU/AddressV/MaxAnisotropy` and `IssueDualTextureDraw` binds
// slot 1 from them, which is exactly the shape the env-map path is missing.
//
// THE ORACLE is byte-exact for every Point mode and strictly differential for every Linear mode,
// and it never depends on where a particular texel lands on screen -- so cube-face orientation
// conventions, which differ across backends and are settled elsewhere (REMED-GFX-134), cannot make
// it pass or fail.
//
//   * A PALETTE built at start-up guarantees, and CHECKS, two properties: every entry is at least
//     `kSep` apart from every other in L-inf, and the midpoint of ANY two entries is at least
//     `kMidSep` away from EVERY entry. So "this pixel is a stored texel" is decidable exactly, and
//     "this pixel is an interpolation of two stored texels" is decidable exactly too.
//   * With `EnvironmentMapAmount = 1`, `FresnelFactor = 0`, `EnvironmentMapSpecular = 0`,
//     `DiffuseColor = White`, `Alpha = 1` and an all-opaque base texture, the fragment shader's
//     `mix(baseColor, envSample.rgb * combinedAlpha, 1.0)` is EXACTLY `envSample.rgb`: the base
//     texture cannot influence one bit of the output, so this configuration measures slot 1 alone.
//   * With `EnvironmentMapAmount = 0` and ambient-only white lighting, the same expression is
//     exactly `texColor.rgb`: the cube cannot influence one bit, so it measures slot 0 alone.
//
// So slot independence is asserted as BYTE-IDENTICAL vs BYTE-DIFFERENT images rather than as a
// tolerance, and "explicit sampler replaced by LinearClamp" shows up as the Point image containing
// colours that exist in NO texel of the cube.
//
// THE GEOMETRY. A screen-filling quad at z=0 spanning [-1,1]^2 is drawn with an ORTHOGRAPHIC
// projection, so the rasterised position does not depend on the eye distance, while the view matrix
// places the eye at (0,0,D). For a quad normal of +Z the shader's `reflect(-E, N)` reduces to a
// direction proportional to (x, y, D), which is linear in the pixel coordinate and always has +Z as
// its major axis for D > 1. D = 4/3 therefore magnifies exactly the middle 0.75 of one cube face
// across the target -- a 4x4 face becomes a 4x4 grid of blocks. Tilting the normal aims the same
// sweep at a different face, which is how the face legs are driven without touching UVs or shaders.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   A  slot 1 is consulted at all          the red-first check: Point on the cube must be exact
//   B  slot 0 still works                  REMED-GFX-176's result, re-asserted as a regression gate
//   C  independence                        one slot fixed, the other changed, byte-identical or not
//   D  filter ordinals 0..8 on the cube    the complete public enum, magnification partition
//   E  cache identity                      same cube two samplers; two cubes one sampler; one frame
//   F  deferred snapshot                   queued draws keep the state public at THEIR call
//   G  draw paths                          static/dynamic, indexed 16/32-bit, ranges, DrawUser
//   H  repeated frames                     steady state, no first-frame-only correctness
//   I  faces                               six normals, six faces, no aliasing
//   J  mipmapped cube                      the MIP component of slot 1's ordinal, isolated
//   K  address modes                       Clamp/Wrap/Mirror classified honestly on a cube
//   L  RenderTargetCube                    the other concrete cube class reaching the same binding
//   M  device default                      an unset slot 1 keeps the documented default
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
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
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

    constexpr int kBBW = 128;
    constexpr int kBBH = 96;

    /// Magnifying target: a 4x4 cube face becomes a 4x4 grid of blocks across it.
    constexpr int kRT = 32;
    /// Cube face edge, in texels, for every non-mipmapped cube.
    constexpr int kFace = 4;
    /// Base 2D texture edge, in texels.
    constexpr int kTex = 4;
    /// Minifying target for the mip leg.
    constexpr int kMipRT = 8;
    /// Mipmapped cube face edge -- 32 gives levels 0..5 and a level-2-ish lambda at kMipRT.
    constexpr int kMipFace = 32;
    /// Eye distance. > 1 keeps +Z the strict major axis of the reflection over the whole quad;
    /// 4/3 sweeps exactly the middle 0.75 of the face across the target.
    constexpr float kEyeD = 4.0f / 3.0f;

    /// Palette layout. 6 faces x 16 texels, then the base texture, then one flat marker per
    /// (face, mip level 0..5).
    constexpr int kCubeColors = 6 * 16;
    constexpr int kBaseColors = 16;
    constexpr int kMipColors = 6 * 6;
    constexpr int kPaletteSize = kCubeColors + kBaseColors + kMipColors;
    /// Minimum L-inf separation between two palette entries.
    constexpr int kSep = 20;
    /// Minimum L-inf distance from the midpoint of any two entries to every entry.
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

    std::string Str(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
                     std::to_string(static_cast<int>(c.getGProperty())) + "," +
                     std::to_string(static_cast<int>(c.getBProperty())) + "," +
                     std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    std::uint32_t Pack(const Color& c)
    {
        return (static_cast<std::uint32_t>(c.getRProperty()) << 24) |
               (static_cast<std::uint32_t>(c.getGProperty()) << 16) |
               (static_cast<std::uint32_t>(c.getBProperty()) << 8) |
                static_cast<std::uint32_t>(c.getAProperty());
    }

    /// RGB only -- alpha is 255 everywhere by construction and the shader's own alpha path is not
    /// what this fixture measures.
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

    /// The public enum's magnification component, from TextureFilter.hpp's own table (the
    /// authoritative translation REMED-GFX-170 established; NOT a second copy of a mapping).
    bool MagnifiesByPoint(int ordinal)
    {
        return ordinal == 1 || ordinal == 4 || ordinal == 5 || ordinal == 6;
    }

    /// The public enum's mipmap component.
    bool MipsByPoint(int ordinal)
    {
        return ordinal == 1 || ordinal == 3 || ordinal == 6 || ordinal == 8;
    }

    /// Which cube face the shader's reflect(-E, N) aims at, for the normals this fixture uses.
    struct NormalFace { float nx, ny, nz; CubeMapFace face; const char* name; };
}

class EnvMapCubeSamplerContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    std::vector<Color> palette_;
    std::set<std::uint32_t> cubeSet_, baseSet_, mipSet_;

    Texture2D base_;
    std::unique_ptr<TextureCube> cubeA_, cubeB_, cubeMip_, cubeFlat_;

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

    // ----------------------------------------------------------------------------------------
    // Palette
    // ----------------------------------------------------------------------------------------

    /// Builds a palette in which no two entries are closer than kSep and no midpoint of two
    /// entries is within kMidSep of any entry. Deterministic (a fixed LCG, no <random>), so the
    /// same colours are measured on every backend and in every run.
    static std::vector<Color> BuildPalette()
    {
        std::vector<Color> out;
        out.reserve(kPaletteSize);
        std::vector<Color> mids;
        std::uint32_t s = 12345u;
        auto nextByte = [&s]() {
            s = s * 1664525u + 1013904223u;
            return static_cast<std::uint8_t>((s >> 16) & 0xFFu);
        };
        int guard = 0;
        while (static_cast<int>(out.size()) < kPaletteSize && guard++ < 4000000)
        {
            const std::uint8_t r = nextByte();
            const std::uint8_t g = nextByte();
            const std::uint8_t b = nextByte();
            const Color c(r, g, b, static_cast<std::uint8_t>(255));

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

    const Color& CubeTexel(int face, int i) const { return palette_[static_cast<std::size_t>(face) * 16 + i]; }
    const Color& BaseTexel(int i) const { return palette_[static_cast<std::size_t>(kCubeColors + i)]; }
    /// level is 0..5. EVERY level of the mipmapped cube is a flat per-(face, level) colour, so the
    /// MINIFICATION filter is invisible inside a level at every lambda and only the ordinal's
    /// MIPMAP component can change the answer -- the same isolation REMED-GFX-175's fixture uses.
    const Color& MipMarker(int face, int level) const
    {
        return palette_[static_cast<std::size_t>(kCubeColors + kBaseColors + face * 6 + level)];
    }

    // ----------------------------------------------------------------------------------------
    // Oracles
    // ----------------------------------------------------------------------------------------

    static int OffPalette(const std::vector<Color>& pix, const std::set<std::uint32_t>& pal)
    {
        int n = 0;
        for (const Color& c : pix) if (pal.find(Pack(c)) == pal.end()) ++n;
        return n;
    }

    static Color FirstOffPalette(const std::vector<Color>& pix, const std::set<std::uint32_t>& pal)
    {
        for (const Color& c : pix) if (pal.find(Pack(c)) == pal.end()) return c;
        return Color(0, 0, 0, 0);
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

    /// How many of the image's pixels come from face `face`'s own 16 texels.
    int PixelsFromFace(const std::vector<Color>& pix, int face) const
    {
        std::set<std::uint32_t> f;
        for (int i = 0; i < 16; ++i) f.insert(Pack(CubeTexel(face, i)));
        int n = 0;
        for (const Color& c : pix) if (f.find(Pack(c)) != f.end()) ++n;
        return n;
    }

    // ----------------------------------------------------------------------------------------
    // Device helpers
    // ----------------------------------------------------------------------------------------

    static SamplerState Sampler(TextureFilter f, TextureAddressMode a)
    {
        SamplerState s;
        s.setFilterProperty(f);
        s.setAddressUProperty(a);
        s.setAddressVProperty(a);
        return s;
    }

    static void SetSlot(GraphicsDevice& dev, int slot, TextureFilter f, TextureAddressMode a)
    {
        dev.getSamplerStatesProperty()[slot] = Sampler(f, a);
    }

    static void SetSlot(GraphicsDevice& dev, int slot, int ordinal, TextureAddressMode a)
    {
        SetSlot(dev, slot, static_cast<TextureFilter>(ordinal), a);
    }

    static void ResetDeviceState(GraphicsDevice& dev, int w, int h)
    {
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    // ----------------------------------------------------------------------------------------
    // Geometry and the one env-map draw
    // ----------------------------------------------------------------------------------------

    static std::vector<VtxPNT> Quad(float nx, float ny, float nz)
    {
        const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
        const float ux = nx / l, uy = ny / l, uz = nz / l;
        auto v = [&](float x, float y, float u, float w) {
            VtxPNT r{};
            r.px = x; r.py = y; r.pz = 0.0f;
            r.nx = ux; r.ny = uy; r.nz = uz;
            r.u = u; r.v = w;
            return r;
        };
        const VtxPNT tl = v(-1.0f,  1.0f, 0.0f, 0.0f);
        const VtxPNT bl = v(-1.0f, -1.0f, 0.0f, 1.0f);
        const VtxPNT br = v( 1.0f, -1.0f, 1.0f, 1.0f);
        const VtxPNT tr = v( 1.0f,  1.0f, 1.0f, 0.0f);
        return { tl, bl, br, tl, br, tr };
    }

    static std::vector<VertexPositionNormalTexture> QuadXna(float nx, float ny, float nz)
    {
        std::vector<VertexPositionNormalTexture> out;
        for (const VtxPNT& s : Quad(nx, ny, nz))
            out.emplace_back(Vector3(s.px, s.py, s.pz), Vector3(s.nx, s.ny, s.nz), Vector2(s.u, s.v));
        return out;
    }

    /// Which public draw entry point carries the queued command.
    enum class Path
    {
        StaticNonIndexed,
        StaticIndexed16,
        StaticIndexed32,
        StaticIndexedRange,   ///< nonzero startIndex AND nonzero baseVertex
        UserNonIndexed,
        UserIndexed16,
        UserIndexed32,
        DynamicNonIndexed,
    };

    static const char* PathName(Path p)
    {
        switch (p)
        {
            case Path::StaticNonIndexed:  return "DrawPrimitives";
            case Path::StaticIndexed16:   return "DrawIndexedPrimitives/16-bit";
            case Path::StaticIndexed32:   return "DrawIndexedPrimitives/32-bit";
            case Path::StaticIndexedRange:return "DrawIndexedPrimitives/startIndex+baseVertex";
            case Path::UserNonIndexed:    return "DrawUserPrimitives";
            case Path::UserIndexed16:     return "DrawUserIndexedPrimitives/16-bit";
            case Path::UserIndexed32:     return "DrawUserIndexedPrimitives/32-bit";
            case Path::DynamicNonIndexed: return "DrawPrimitives/dynamic buffer";
        }
        return "?";
    }

    struct Cfg
    {
        TextureCube* cube = nullptr;
        float amount = 1.0f;           ///< 1 = the cube alone decides the pixel, 0 = the 2D texture alone
        float nx = 0.0f, ny = 0.0f, nz = 1.0f;
        Path path = Path::StaticNonIndexed;
    };

    void Configure(EnvironmentMapEffect& fx, const Cfg& cfg) const
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, kEyeD), Vector3::Zero, Vector3::Up));
        fx.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f));
        fx.setTextureProperty(const_cast<Texture2D*>(&base_));
        fx.setEnvironmentMapProperty(cfg.cube);
        fx.setEnvironmentMapAmountProperty(cfg.amount);
        // FresnelFactor 0 also clears FresnelEnabled, so the blend factor is the flat
        // EnvironmentMapAmount and pow() never sees 0^0 at the quad centre.
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(1.0f);
        fx.setEmissiveColorProperty(Vector3::Zero);
        // Ambient-only white lighting makes the base modulation the identity, so the amount=0
        // configuration reads back the stored 2D texel exactly.
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
    }

    void DrawEnv(GraphicsDevice& dev, const Cfg& cfg)
    {
        EnvironmentMapEffect fx(dev);
        Configure(fx, cfg);
        fx.Apply();

        const std::vector<VtxPNT> quad = Quad(cfg.nx, cfg.ny, cfg.nz);
        switch (cfg.path)
        {
            case Path::StaticNonIndexed:
            {
                VertexBuffer vb(dev, static_cast<int>(quad.size()));
                vb.SetDataRaw(quad.data(), static_cast<int>(quad.size()), static_cast<int>(sizeof(VtxPNT)));
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                break;
            }
            case Path::DynamicNonIndexed:
            {
                // A dynamic buffer written twice, so the draw reads the SECOND write -- a backend
                // that shadows vertex data at creation time cannot pass this by accident.
                std::vector<VtxPNT> decoy(quad.size(), quad.front());
                VertexBuffer vb(dev, static_cast<int>(quad.size()));
                vb.SetDataRaw(decoy.data(), static_cast<int>(decoy.size()), static_cast<int>(sizeof(VtxPNT)));
                vb.SetDataRaw(quad.data(), static_cast<int>(quad.size()), static_cast<int>(sizeof(VtxPNT)));
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                break;
            }
            case Path::StaticIndexed16:
            {
                VertexBuffer vb(dev, 4);
                const VtxPNT corners[4] = { quad[0], quad[1], quad[2], quad[5] };
                vb.SetDataRaw(corners, 4, static_cast<int>(sizeof(VtxPNT)));
                const std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
                IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
                ib.SetData(idx, 6);
                dev.SetVertexBuffer(&vb);
                dev.SetIndexBuffer(&ib);
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                dev.SetIndexBuffer(nullptr);
                dev.SetVertexBuffer(nullptr);
                break;
            }
            case Path::StaticIndexed32:
            {
                VertexBuffer vb(dev, 4);
                const VtxPNT corners[4] = { quad[0], quad[1], quad[2], quad[5] };
                vb.SetDataRaw(corners, 4, static_cast<int>(sizeof(VtxPNT)));
                const std::uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
                IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::WriteOnly);
                ib.SetData(idx, 6);
                dev.SetVertexBuffer(&vb);
                dev.SetIndexBuffer(&ib);
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                dev.SetIndexBuffer(nullptr);
                dev.SetVertexBuffer(nullptr);
                break;
            }
            case Path::StaticIndexedRange:
            {
                // Two decoy vertices in front and three decoy indices in front, so the draw is only
                // correct when BOTH baseVertex and startIndex are honoured (REMED-GFX-117).
                VtxPNT decoy = quad[0];
                decoy.px = decoy.py = 0.0f;
                std::vector<VtxPNT> verts = { decoy, decoy, quad[0], quad[1], quad[2], quad[5] };
                VertexBuffer vb(dev, static_cast<int>(verts.size()));
                vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(VtxPNT)));
                const std::uint16_t idx[9] = { 0, 0, 0, 0, 1, 2, 0, 2, 3 };
                IndexBuffer ib(dev, IndexElementSize::SixteenBits, 9, BufferUsage::WriteOnly);
                ib.SetData(idx, 9);
                dev.SetVertexBuffer(&vb);
                dev.SetIndexBuffer(&ib);
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 2, 0, 4, 3, 2);
                dev.SetIndexBuffer(nullptr);
                dev.SetVertexBuffer(nullptr);
                break;
            }
            case Path::UserNonIndexed:
            {
                const std::vector<VertexPositionNormalTexture> v = QuadXna(cfg.nx, cfg.ny, cfg.nz);
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, v.data(), 0, 2);
                break;
            }
            case Path::UserIndexed16:
            {
                const std::vector<VertexPositionNormalTexture> all = QuadXna(cfg.nx, cfg.ny, cfg.nz);
                const VertexPositionNormalTexture v[4] = { all[0], all[1], all[2], all[5] };
                const std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
                dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, 0, 4, idx, 0, 2);
                break;
            }
            case Path::UserIndexed32:
            {
                const std::vector<VertexPositionNormalTexture> all = QuadXna(cfg.nx, cfg.ny, cfg.nz);
                const VertexPositionNormalTexture v[4] = { all[0], all[1], all[2], all[5] };
                const std::uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
                dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, 0, 4, idx, 0, 2);
                break;
            }
        }
    }

    /// One draw into a fresh target of the given edge, returned whole.
    std::vector<Color> Render(GraphicsDevice& dev, const Cfg& cfg, int edge = kRT)
    {
        RenderTarget2D rt(dev, edge, edge, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, edge, edge);
        dev.Clear(kSentinel);
        DrawEnv(dev, cfg);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(edge) * edge, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    /// Two draws in ONE frame, one per horizontal half, with `between` run after the first is
    /// queued -- this is what separates "captured at the public call" from "read at replay".
    std::vector<Color> RenderTwo(GraphicsDevice& dev, const Cfg& a, const Cfg& b,
                                 const std::function<void(GraphicsDevice&)>& between,
                                 const std::function<void(GraphicsDevice&)>& after)
    {
        RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);
        dev.setViewportProperty(Viewport(0, 0, kRT, kRT / 2));
        DrawEnv(dev, a);
        if (between) between(dev);
        dev.setViewportProperty(Viewport(0, kRT / 2, kRT, kRT / 2));
        DrawEnv(dev, b);
        if (after) after(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    static std::vector<Color> TopHalf(const std::vector<Color>& pix)
    {
        return std::vector<Color>(pix.begin(),
                                  pix.begin() + static_cast<std::ptrdiff_t>(kRT) * (kRT / 2));
    }

    static std::vector<Color> BottomHalf(const std::vector<Color>& pix)
    {
        return std::vector<Color>(pix.begin() + static_cast<std::ptrdiff_t>(kRT) * (kRT / 2), pix.end());
    }

    // ----------------------------------------------------------------------------------------
    // Legs
    // ----------------------------------------------------------------------------------------

    void RunPalette()
    {
        check(static_cast<int>(palette_.size()) == kPaletteSize,
              "PAL1: the palette generator produced all " + std::to_string(kPaletteSize) +
              " entries (got " + std::to_string(palette_.size()) + ")");
        if (static_cast<int>(palette_.size()) != kPaletteSize) return;

        int minSep = 1000;
        for (std::size_t i = 0; i < palette_.size(); ++i)
            for (std::size_t j = i + 1; j < palette_.size(); ++j)
                minSep = std::min(minSep, LinfRGB(palette_[i], palette_[j]));
        check(minSep >= kSep,
              "PAL2: every pair of stored colours is at least " + std::to_string(kSep) +
              " apart in L-inf, so 'this pixel is a stored texel' is exact (min=" +
              std::to_string(minSep) + ")");

        int minMid = 1000;
        std::set<std::uint32_t> all;
        for (const Color& c : palette_) all.insert(Pack(c));
        for (std::size_t i = 0; i < palette_.size(); ++i)
            for (std::size_t j = i + 1; j < palette_.size(); ++j)
            {
                const Color m = Mid(palette_[i], palette_[j]);
                for (const Color& r : palette_) minMid = std::min(minMid, LinfRGB(m, r));
            }
        check(minMid >= kMidSep,
              "PAL3: the midpoint of any two stored colours is at least " + std::to_string(kMidSep) +
              " from EVERY stored colour, so 'this pixel is an interpolation' is exact (min=" +
              std::to_string(minMid) + ")");

        int worstAdjacent = 1000;
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < kFace; ++y)
                for (int x = 0; x < kFace; ++x)
                {
                    const int i = y * kFace + x;
                    if (x + 1 < kFace) worstAdjacent = std::min(worstAdjacent, LinfRGB(CubeTexel(f, i), CubeTexel(f, i + 1)));
                    if (y + 1 < kFace) worstAdjacent = std::min(worstAdjacent, LinfRGB(CubeTexel(f, i), CubeTexel(f, i + kFace)));
                }
        check(worstAdjacent >= kSep,
              "PAL4: orthogonally adjacent cube texels differ strongly, so an interpolation between "
              "two of them is far from both (min=" + std::to_string(worstAdjacent) + ")");

        int midTones = 0;
        for (const Color& c : palette_)
            if ((c.getRProperty() >= 96 && c.getRProperty() <= 160) ||
                (c.getGProperty() >= 96 && c.getGProperty() <= 160) ||
                (c.getBProperty() >= 96 && c.getBProperty() <= 160)) ++midTones;
        check(midTones > 0,
              "PAL5: the palette contains mid-tones, so a saturating or clamping path cannot pass "
              "by accident (" + std::to_string(midTones) + " entries)");

        // No face is a transform of another, and no face is symmetric under either reflection.
        bool anySymmetric = false, anyFacePairEqual = false;
        for (int f = 0; f < 6; ++f)
        {
            for (int y = 0; y < kFace; ++y)
                for (int x = 0; x < kFace / 2; ++x)
                {
                    if (SameColor(CubeTexel(f, y * kFace + x), CubeTexel(f, y * kFace + (kFace - 1 - x)))) anySymmetric = true;
                    if (SameColor(CubeTexel(f, x * kFace + y), CubeTexel(f, (kFace - 1 - x) * kFace + y))) anySymmetric = true;
                }
            for (int g = f + 1; g < 6; ++g)
            {
                bool same = true;
                for (int i = 0; i < 16; ++i) if (!SameColor(CubeTexel(f, i), CubeTexel(g, i))) { same = false; break; }
                if (same) anyFacePairEqual = true;
            }
        }
        check(!anySymmetric,
              "PAL6: no cube face is symmetric under a horizontal or vertical reflection");
        check(!anyFacePairEqual,
              "PAL7: no two cube faces carry the same content");
    }

    void RunA_CubeSlotIsConsulted(GraphicsDevice& dev)
    {
        Cfg cfg; cfg.cube = cubeA_.get(); cfg.amount = 1.0f;

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> point = Render(dev, cfg);

        const int distinct = DistinctColors(point);
        check(distinct >= 6,
              "A1: the draw produced a real magnified image of the cube (" +
              std::to_string(distinct) + " distinct colours)");
        const int off = OffPalette(point, cubeSet_);
        check(off == 0,
              "A2: with SamplerStates[1] = PointClamp every pixel is a STORED cube texel -- an "
              "explicit Point sampler was not replaced by a filtered one (" + std::to_string(off) +
              " off-palette, first " + Str(FirstOffPalette(point, cubeSet_)) + ")");
        check(distinct <= kFace * kFace,
              "A3: PointClamp yields at most " + std::to_string(kFace * kFace) +
              " distinct colours -- one per reachable cube texel (got " + std::to_string(distinct) + ")");

        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        const std::vector<Color> linear = Render(dev, cfg);
        check(DiffPixels(point, linear) > 0,
              "A4: changing SamplerStates[1] from Point to Linear changes the image -- the cube "
              "slot's sampler is consulted at all (differing=" + std::to_string(DiffPixels(point, linear)) + ")");
        check(OffPalette(linear, cubeSet_) > 0,
              "A5: LinearClamp on the cube interpolates -- colours appear that exist in NO cube "
              "texel (" + std::to_string(OffPalette(linear, cubeSet_)) + ")");
        check(DistinctColors(linear) > kFace * kFace,
              "A6: LinearClamp yields more colours than the cube has reachable texels (" +
              std::to_string(DistinctColors(linear)) + ")");
    }

    void RunB_BaseSlotStillWorks(GraphicsDevice& dev)
    {
        Cfg cfg; cfg.cube = cubeA_.get(); cfg.amount = 0.0f;

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> point = Render(dev, cfg);
        check(OffPalette(point, baseSet_) == 0,
              "B1: with EnvironmentMapAmount = 0 and SamplerStates[0] = PointClamp every pixel is a "
              "STORED 2D texel (" + std::to_string(OffPalette(point, baseSet_)) + " off-palette, first " +
              Str(FirstOffPalette(point, baseSet_)) + ")");
        check(DistinctColors(point) <= kTex * kTex,
              "B2: the base slot magnifies to at most " + std::to_string(kTex * kTex) +
              " colours (got " + std::to_string(DistinctColors(point)) + ")");

        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Clamp);
        const std::vector<Color> linear = Render(dev, cfg);
        check(DiffPixels(point, linear) > 0,
              "B3: SamplerStates[0] still decides the base texture's filtering (differing=" +
              std::to_string(DiffPixels(point, linear)) + ")");
        check(OffPalette(linear, baseSet_) > 0,
              "B4: LinearClamp on slot 0 interpolates (" + std::to_string(OffPalette(linear, baseSet_)) + ")");
    }

    void RunC_Independence(GraphicsDevice& dev)
    {
        Cfg env; env.cube = cubeA_.get(); env.amount = 1.0f;
        Cfg base; base.cube = cubeA_.get(); base.amount = 0.0f;

        // C1/C2: one slot fixed, the other swept -- the fixed resource must be BYTE-IDENTICAL.
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> envPP = Render(dev, env);
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Wrap);
        const std::vector<Color> envLP = Render(dev, env);
        check(DiffPixels(envPP, envLP) == 0,
              "C1: changing SamplerStates[0] does not alter the CUBE's contribution (differing=" +
              std::to_string(DiffPixels(envPP, envLP)) + ")");

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> basePP = Render(dev, base);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Wrap);
        const std::vector<Color> basePL = Render(dev, base);
        check(DiffPixels(basePP, basePL) == 0,
              "C2: changing SamplerStates[1] does not alter the 2D TEXTURE's contribution "
              "(differing=" + std::to_string(DiffPixels(basePP, basePL)) + ")");

        // C3: the two slots are not aliased onto one another. Slot0=Point/Slot1=Linear and
        // Slot0=Linear/Slot1=Point must give OPPOSITE answers on the cube-only configuration.
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        const std::vector<Color> pl = Render(dev, env);
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> lp = Render(dev, env);
        check(OffPalette(pl, cubeSet_) > 0,
              "C3: slot0=Point slot1=Linear -- the cube really is filtered LINEARLY (" +
              std::to_string(OffPalette(pl, cubeSet_)) + " interpolated pixels)");
        check(OffPalette(lp, cubeSet_) == 0,
              "C4: slot0=Linear slot1=Point -- the cube really is filtered by POINT, so slot 0 "
              "did not leak into slot 1 (" + std::to_string(OffPalette(lp, cubeSet_)) + " off-palette)");
        check(DiffPixels(pl, lp) > 0,
              "C5: swapping the two slots' samplers changes the image (differing=" +
              std::to_string(DiffPixels(pl, lp)) + ")");

        // C6: both Linear -- neither resource is stored-exact.
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        const std::vector<Color> ll = Render(dev, env);
        check(OffPalette(ll, cubeSet_) > 0,
              "C6: both slots Linear -- the cube is interpolated (" +
              std::to_string(OffPalette(ll, cubeSet_)) + ")");
    }

    void RunD_FilterOrdinals(GraphicsDevice& dev)
    {
        Cfg cfg; cfg.cube = cubeA_.get(); cfg.amount = 1.0f;
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);

        std::vector<std::vector<Color>> byOrdinal;
        for (int f = 0; f <= 8; ++f)
        {
            SetSlot(dev, 1, f, TextureAddressMode::Clamp);
            std::vector<Color> pix = Render(dev, cfg);
            byOrdinal.push_back(pix);

            const int off = OffPalette(pix, cubeSet_);
            const int distinct = DistinctColors(pix);
            const std::string tag = "D" + std::to_string(f) + ": slot 1 = " +
                                    std::to_string(f) + "(" + FilterName(f) + ") ";
            if (MagnifiesByPoint(f))
                check(off == 0 && distinct <= kFace * kFace,
                      tag + "magnifies by POINT -- every pixel is a stored cube texel (off=" +
                      std::to_string(off) + ", distinct=" + std::to_string(distinct) + ")");
            else
                check(off > 0,
                      tag + "magnifies LINEARLY -- interpolated colours appear (off=" +
                      std::to_string(off) + ", distinct=" + std::to_string(distinct) + ")");
        }

        // Every one-level texture makes the minification and mipmap components unobservable, so the
        // four Point-magnifying ordinals must agree byte-for-byte, and so must the linear ones --
        // EXCEPT Anisotropic(2), whose tap pattern is a per-GPU choice and is a declared boundary
        // (the same one REMED-GFX-170 declared).
        int pointDiff = 0, linearDiff = 0;
        const std::vector<Color>* firstPoint = nullptr;
        const std::vector<Color>* firstLinear = nullptr;
        for (int f = 0; f <= 8; ++f)
        {
            if (f == 2) continue;
            if (MagnifiesByPoint(f))
            {
                if (!firstPoint) firstPoint = &byOrdinal[static_cast<std::size_t>(f)];
                else pointDiff += DiffPixels(*firstPoint, byOrdinal[static_cast<std::size_t>(f)]);
            }
            else
            {
                if (!firstLinear) firstLinear = &byOrdinal[static_cast<std::size_t>(f)];
                else linearDiff += DiffPixels(*firstLinear, byOrdinal[static_cast<std::size_t>(f)]);
            }
        }
        check(pointDiff == 0,
              "D9: on a ONE-LEVEL cube all four Point-magnifying ordinals (1,4,5,6) agree "
              "byte-for-byte (differing=" + std::to_string(pointDiff) + ")");
        check(linearDiff == 0,
              "D10: on a ONE-LEVEL cube all four non-anisotropic Linear-magnifying ordinals "
              "(0,3,7,8) agree byte-for-byte (differing=" + std::to_string(linearDiff) + ")");
        note("D: TextureFilter::Anisotropic(2) is excluded from the byte-identity grouping -- its "
             "tap pattern is a per-GPU choice; only its LINEAR magnification class is required.");
    }

    void RunE_CacheIdentity(GraphicsDevice& dev)
    {
        Cfg a; a.cube = cubeA_.get(); a.amount = 1.0f;
        Cfg b = a;

        // E1: ONE frame, SAME cube, different slot-1 samplers. A binding cache that does not carry
        // the cube's sampler in its key hands the second draw the first draw's sampler.
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        std::vector<Color> two = RenderTwo(dev, a, b,
            [](GraphicsDevice& d) { SetSlot(d, 1, TextureFilter::Linear, TextureAddressMode::Clamp); },
            nullptr);
        check(OffPalette(TopHalf(two), cubeSet_) == 0,
              "E1: same frame, same cube -- the half drawn under PointClamp stayed exact (" +
              std::to_string(OffPalette(TopHalf(two), cubeSet_)) + " off-palette)");
        check(OffPalette(BottomHalf(two), cubeSet_) > 0,
              "E2: same frame, same cube -- the half drawn under LinearClamp is interpolated (" +
              std::to_string(OffPalette(BottomHalf(two), cubeSet_)) + ")");

        // E3: same resources, same sampler -- one cache entry is legitimate and both halves agree.
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        two = RenderTwo(dev, a, b, nullptr, nullptr);
        check(DiffPixels(TopHalf(two), BottomHalf(two)) == 0,
              "E3: same cube and same sampler twice -- both halves are identical, so reuse is "
              "still correct (differing=" + std::to_string(DiffPixels(TopHalf(two), BottomHalf(two))) + ")");

        // E4: two DIFFERENT cubes under one sampler must not share a binding.
        if (cubeB_)
        {
            Cfg other = a; other.cube = cubeB_.get();
            two = RenderTwo(dev, a, other, nullptr, nullptr);
            check(DiffPixels(TopHalf(two), BottomHalf(two)) > 0,
                  "E4: two different cube textures under ONE sampler produce different halves "
                  "(differing=" + std::to_string(DiffPixels(TopHalf(two), BottomHalf(two))) + ")");
            check(OffPalette(two, cubeSet_) == 0,
                  "E5: both cubes still sample exactly under Point (" +
                  std::to_string(OffPalette(two, cubeSet_)) + " off-palette)");
        }
        else
        {
            skip("E4/E5: the second cube texture is unavailable on this backend");
        }

        // E6: swapping which slot holds which sampler must not alias one binding onto the other.
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Wrap);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> lp = Render(dev, a);
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Wrap);
        const std::vector<Color> pl = Render(dev, a);
        check(OffPalette(lp, cubeSet_) == 0 && OffPalette(pl, cubeSet_) > 0,
              "E6: swapping the samplers between slot 0 and slot 1 swaps which resource is exact, "
              "so no binding aliases the other (lp off=" + std::to_string(OffPalette(lp, cubeSet_)) +
              ", pl off=" + std::to_string(OffPalette(pl, cubeSet_)) + ")");
    }

    void RunF_DeferredSnapshot(GraphicsDevice& dev)
    {
        Cfg a; a.cube = cubeA_.get(); a.amount = 1.0f;

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        // Queue A under Point, change to Linear, queue B, then change AGAIN so the last public
        // state matches neither queued draw.
        const std::vector<Color> two = RenderTwo(dev, a, a,
            [](GraphicsDevice& d) { SetSlot(d, 1, TextureFilter::Linear, TextureAddressMode::Clamp); },
            [](GraphicsDevice& d) { SetSlot(d, 1, TextureFilter::Point, TextureAddressMode::Wrap); });
        check(OffPalette(TopHalf(two), cubeSet_) == 0,
              "F1: a draw queued under PointClamp keeps it after SamplerStates[1] changes twice (" +
              std::to_string(OffPalette(TopHalf(two), cubeSet_)) + " off-palette)");
        check(OffPalette(BottomHalf(two), cubeSet_) > 0,
              "F2: the draw queued under LinearClamp kept ITS sampler too (" +
              std::to_string(OffPalette(BottomHalf(two), cubeSet_)) + ")");

        // F3: coming back to Point after Linear reproduces the original image exactly.
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> back = Render(dev, a);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        (void)Render(dev, a);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> again = Render(dev, a);
        check(DiffPixels(back, again) == 0,
              "F3: returning to PointClamp after LinearClamp reproduces the Point image exactly "
              "(differing=" + std::to_string(DiffPixels(back, again)) + ")");
    }

    void RunG_DrawPaths(GraphicsDevice& dev)
    {
        static const Path kPaths[] = {
            Path::StaticNonIndexed, Path::StaticIndexed16, Path::StaticIndexed32,
            Path::StaticIndexedRange, Path::UserNonIndexed, Path::UserIndexed16,
            Path::UserIndexed32, Path::DynamicNonIndexed,
        };

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        Cfg ref; ref.cube = cubeA_.get(); ref.amount = 1.0f;
        const std::vector<Color> reference = Render(dev, ref);

        for (Path p : kPaths)
        {
            Cfg cfg = ref; cfg.path = p;
            const std::string tag = std::string("G[") + PathName(p) + "] ";
            std::vector<Color> point;
            SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
            try
            {
                point = Render(dev, cfg);
            }
            catch (const std::exception& e)
            {
                skip(tag + "this backend does not implement the path (" + std::string(e.what()) + ")");
                continue;
            }
            check(OffPalette(point, cubeSet_) == 0,
                  tag + "PointClamp on the cube is exact (" +
                  std::to_string(OffPalette(point, cubeSet_)) + " off-palette)");
            check(DiffPixels(point, reference) == 0,
                  tag + "produces the same image as DrawPrimitives (differing=" +
                  std::to_string(DiffPixels(point, reference)) + ")");

            SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
            const std::vector<Color> linear = Render(dev, cfg);
            check(OffPalette(linear, cubeSet_) > 0,
                  tag + "LinearClamp on the cube interpolates (" +
                  std::to_string(OffPalette(linear, cubeSet_)) + ")");
        }
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
    }

    void RunH_RepeatedFrames(GraphicsDevice& dev)
    {
        Cfg cfg; cfg.cube = cubeA_.get(); cfg.amount = 1.0f;

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        std::vector<std::vector<Color>> points, linears;
        for (int i = 0; i < 4; ++i)
        {
            SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
            points.push_back(Render(dev, cfg));
            SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
            linears.push_back(Render(dev, cfg));
        }
        int pd = 0, ld = 0;
        for (std::size_t i = 1; i < points.size(); ++i) pd += DiffPixels(points[0], points[i]);
        for (std::size_t i = 1; i < linears.size(); ++i) ld += DiffPixels(linears[0], linears[i]);
        check(pd == 0, "H1: four alternating frames -- every PointClamp frame is identical "
                       "(differing=" + std::to_string(pd) + ")");
        check(ld == 0, "H2: four alternating frames -- every LinearClamp frame is identical "
                       "(differing=" + std::to_string(ld) + ")");
        check(OffPalette(points[3], cubeSet_) == 0,
              "H3: the LAST Point frame is still exact, so correctness is a steady state and not a "
              "first-frame accident (" + std::to_string(OffPalette(points[3], cubeSet_)) + ")");
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
    }

    void RunI_Faces(GraphicsDevice& dev)
    {
        static const NormalFace kNormals[] = {
            { 0.0f,  0.0f, 1.0f, CubeMapFace::PositiveZ, "+Z" },
            { 1.0f,  0.0f, 1.0f, CubeMapFace::PositiveX, "+X" },
            {-1.0f,  0.0f, 1.0f, CubeMapFace::NegativeX, "-X" },
            { 0.0f,  1.0f, 1.0f, CubeMapFace::PositiveY, "+Y" },
            { 0.0f, -1.0f, 1.0f, CubeMapFace::NegativeY, "-Y" },
            { 1.0f,  0.0f, 0.0f, CubeMapFace::NegativeZ, "-Z" },
        };

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);

        std::vector<std::vector<Color>> images;
        for (const NormalFace& n : kNormals)
        {
            Cfg cfg; cfg.cube = cubeA_.get(); cfg.amount = 1.0f;
            cfg.nx = n.nx; cfg.ny = n.ny; cfg.nz = n.nz;
            std::vector<Color> pix = Render(dev, cfg);
            images.push_back(pix);
            const int fromExpected = PixelsFromFace(pix, static_cast<int>(n.face));
            check(OffPalette(pix, cubeSet_) == 0,
                  std::string("I[") + n.name + "]: Point sampling of the aimed face is exact -- no "
                  "invalid border and no interpolation across a seam (" +
                  std::to_string(OffPalette(pix, cubeSet_)) + " off-palette)");
            note(std::string("I[") + n.name + "]: " + std::to_string(fromExpected) + "/" +
                 std::to_string(pix.size()) + " pixels come from the " + n.name +
                 " face's own texels (orientation is settled by REMED-GFX-134, not here)");
        }

        int identicalPairs = 0;
        for (std::size_t i = 0; i < images.size(); ++i)
            for (std::size_t j = i + 1; j < images.size(); ++j)
                if (DiffPixels(images[i], images[j]) == 0) ++identicalPairs;
        check(identicalPairs == 0,
              "I7: the six aimed directions produce six DIFFERENT images -- no face aliases another "
              "(" + std::to_string(identicalPairs) + " identical pairs)");

        std::set<int> facesSeen;
        for (const std::vector<Color>& pix : images)
            for (int f = 0; f < 6; ++f)
                if (PixelsFromFace(pix, f) > 0) facesSeen.insert(f);
        check(facesSeen.size() >= 4,
              "I8: at least four of the six stored faces are actually reached (" +
              std::to_string(facesSeen.size()) + ")");
    }

    void RunJ_MippedCube(GraphicsDevice& dev)
    {
        if (!cubeMip_ || !cubeFlat_)
        {
            skip("J: this backend could not store a mipmapped cube");
            return;
        }
        check(cubeMip_->getLevelCountProperty() == 6,
              "J1: a 32x32 mipMap=true TextureCube reports LevelCount 6 (got " +
              std::to_string(cubeMip_->getLevelCountProperty()) + ")");
        check(cubeFlat_->getLevelCountProperty() == 1,
              "J2: a 32x32 mipMap=false TextureCube reports LevelCount 1 (got " +
              std::to_string(cubeFlat_->getLevelCountProperty()) + ")");

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);

        // Every level above 0 is a FLAT per-face marker colour, so the MIN filter is invisible
        // inside a level and the ordinal's MIPMAP component is the only thing that can change the
        // answer. The oracle is set membership, not flatness: a cube-map LOD is derived from the
        // derivative of a NORMALISED direction vector, so lambda varies slightly across the quad
        // even though the face coordinate is linear in the pixel -- which is a property of cube
        // sampling, not of the sampler under test.
        std::set<std::uint32_t> markers;
        for (int f = 0; f < 6; ++f)
            for (int l = 1; l <= 5; ++l) markers.insert(Pack(MipMarker(f, l)));
        const std::set<std::uint32_t>& stored = mipSet_;   // every level, 0 included

        std::vector<std::vector<Color>> mipByOrdinal;
        for (int f = 0; f <= 8; ++f)
        {
            SetSlot(dev, 1, f, TextureAddressMode::Clamp);
            Cfg cfg; cfg.cube = cubeMip_.get(); cfg.amount = 1.0f;
            std::vector<Color> pix = Render(dev, cfg, kMipRT);
            mipByOrdinal.push_back(pix);

            int fromMarker = 0;
            for (const Color& c : pix) if (markers.find(Pack(c)) != markers.end()) ++fromMarker;
            const int off = OffPalette(pix, stored);
            const std::string tag = "J[" + std::to_string(f) + " " + FilterName(f) + "] ";
            const std::string diag = " (off-stored=" + std::to_string(off) + ", from a level>0 "
                                     "marker=" + std::to_string(fromMarker) + "/" +
                                     std::to_string(pix.size()) + ", distinct=" +
                                     std::to_string(DistinctColors(pix)) + ")";
            if (f == 2)
            {
                // Anisotropic filtering exists precisely to LOWER the level a minified fetch
                // selects, and by how much is a per-GPU choice -- the same boundary leg D declares.
                // Its result is reported, not required.
                note(tag + "anisotropy changes which level is selected by a per-GPU amount; "
                     "reported, not asserted" + diag);
                continue;
            }
            if (MipsByPoint(f))
                check(off == 0 && fromMarker > 0,
                      tag + "mip-POINT minification reads STORED levels only, and reaches a level "
                      "above 0" + diag);
            else
                check(off > 0,
                      tag + "mip-LINEAR minification BLENDS two stored levels, so colours appear "
                      "that are stored at no level" + diag);
        }

        // The differential: the mip-Point ordinals must all agree byte-for-byte, the mip-Linear
        // ones must too, and the two answers must DIFFER -- which a hardcoded cube sampler, giving
        // one answer for all nine, cannot do.
        int pointDiff = 0, linearSpread = 0;
        const std::vector<Color>* firstPoint = nullptr;
        const std::vector<Color>* firstLinear = nullptr;
        for (int f = 0; f <= 8; ++f)
        {
            if (f == 2) continue;   // anisotropy: declared boundary, see leg D
            const std::vector<Color>& pix = mipByOrdinal[static_cast<std::size_t>(f)];
            if (MipsByPoint(f))
            {
                if (!firstPoint) firstPoint = &pix; else pointDiff += DiffPixels(*firstPoint, pix);
            }
            else
            {
                if (!firstLinear) firstLinear = &pix; else linearSpread += DiffPixels(*firstLinear, pix);
            }
        }
        check(pointDiff == 0,
              "J12: all four mip-POINT ordinals (1,3,6,8) select the same levels byte-for-byte "
              "(differing=" + std::to_string(pointDiff) + ")");

        // Each mip-LINEAR ordinal INDEPENDENTLY has to differ from the mip-POINT answer -- that is
        // the check a hardcoded cube sampler, giving one answer for all nine ordinals, fails.
        int linearVsPoint = 0;
        for (int f = 0; f <= 8; ++f)
        {
            if (f == 2 || MipsByPoint(f)) continue;
            if (firstPoint && DiffPixels(*firstPoint, mipByOrdinal[static_cast<std::size_t>(f)]) > 0)
                ++linearVsPoint;
        }
        check(linearVsPoint == 4,
              "J13: EACH of the four non-anisotropic mip-LINEAR ordinals (0,4,5,7) differs from the "
              "mip-POINT answer, so the MIPMAP component is read per ordinal (" +
              std::to_string(linearVsPoint) + "/4)");
        check(firstPoint && firstLinear && DiffPixels(*firstPoint, *firstLinear) > 0,
              "J14: mip-POINT and mip-LINEAR give DIFFERENT images, so the MIPMAP component of "
              "SamplerStates[1] reaches the cube (differing=" +
              std::to_string((firstPoint && firstLinear) ? DiffPixels(*firstPoint, *firstLinear) : 0) + ")");
        note("J: the four mip-LINEAR ordinals are NOT required to agree byte-for-byte and here "
             "spread over " + std::to_string(linearSpread) + " pixel comparisons. Flat levels make "
             "min/mag invisible INSIDE a face, but a cube is filtered seamlessly ACROSS face edges, "
             "where the neighbouring face's own flat colour is a different value -- so the min/mag "
             "components remain observable at the boundary taps. That is a property of cube "
             "sampling, not of the sampler under test, and it is why only the MIPMAP differential "
             "above is asserted.");
        check(firstPoint && OffPalette(*firstPoint, markers) < static_cast<int>(firstPoint->size()),
              "J15: mip-POINT actually landed on stored level markers rather than staying on "
              "level 0");

        // A single-level cube minified the same way cannot mip at all, so it must differ from both.
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        Cfg one; one.cube = cubeFlat_.get(); one.amount = 1.0f;
        const std::vector<Color> single = Render(dev, one, kMipRT);
        check(OffPalette(single, cubeSet_) == 0,
              "J16: a ONE-LEVEL cube minified under mip-Point still samples stored level-0 texels (" +
              std::to_string(OffPalette(single, cubeSet_)) + " off-palette)");

        // One face versus another at the same level: different markers.
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        Cfg zface; zface.cube = cubeMip_.get(); zface.amount = 1.0f;
        Cfg xface = zface; xface.nx = 1.0f; xface.nz = 1.0f;
        const std::vector<Color> zpix = Render(dev, zface, kMipRT);
        const std::vector<Color> xpix = Render(dev, xface, kMipRT);
        check(DiffPixels(zpix, xpix) > 0,
              "J17: two different aimed faces select different stored level markers -- mip levels "
              "are per-face, not aliased (differing=" + std::to_string(DiffPixels(zpix, xpix)) + ")");
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
    }

    void RunK_AddressModes(GraphicsDevice& dev)
    {
        static const struct { TextureAddressMode mode; const char* name; } kModes[] = {
            { TextureAddressMode::Clamp,  "Clamp"  },
            { TextureAddressMode::Wrap,   "Wrap"   },
            { TextureAddressMode::Mirror, "Mirror" },
        };

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        Cfg cfg; cfg.cube = cubeA_.get(); cfg.amount = 1.0f;

        std::vector<std::vector<Color>> images;
        for (const auto& m : kModes)
        {
            SetSlot(dev, 1, TextureFilter::Point, m.mode);
            const std::vector<Color> pix = Render(dev, cfg);
            images.push_back(pix);
            check(OffPalette(pix, cubeSet_) == 0,
                  std::string("K[") + m.name + "]: a cube sampled under Point stays on stored texels "
                  "-- no border colour and no out-of-face fetch (" +
                  std::to_string(OffPalette(pix, cubeSet_)) + " off-palette)");
        }
        note("K: a cube map is addressed by a DIRECTION, so U/V/W address modes are only reachable "
             "at face edges and a driver with seamless cube filtering may render all three "
             "identically. Clamp vs Wrap differing pixels = " +
             std::to_string(DiffPixels(images[0], images[1])) + ", Clamp vs Mirror = " +
             std::to_string(DiffPixels(images[0], images[2])) +
             ". This fixture asserts only that no mode produces an invalid fetch; it does NOT "
             "assert a cube address-mode convention.");

        // A direction that grazes a face boundary: the reflection is aimed almost exactly at the
        // +X/+Z edge, so any out-of-face fetch would show up as an off-palette pixel.
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        Cfg edge = cfg; edge.nx = 0.4142f; edge.nz = 1.0f;   // tan(22.5 deg) -> reflection near 45 deg
        const std::vector<Color> nearSeam = Render(dev, edge);
        check(OffPalette(nearSeam, cubeSet_) == 0,
              "K4: a reflection aimed at a face BOUNDARY still lands on stored texels under Point (" +
              std::to_string(OffPalette(nearSeam, cubeSet_)) + " off-palette)");
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
    }

    void RunL_RenderTargetCube(GraphicsDevice& dev)
    {
        std::unique_ptr<RenderTargetCube> rtc;
        try
        {
            rtc = std::make_unique<RenderTargetCube>(dev, kFace, false, SurfaceFormat::Color,
                                                     DepthFormat::None, 0,
                                                     RenderTargetUsage::PreserveContents);
            // Each face is PAINTED, not cleared: a 1:1 textured blit of the asymmetric 4x4 base
            // pattern. A flat-filled face would be filter-invisible and would let a hardcoded cube
            // sampler pass this leg, which is exactly the false positive this fixture is auditing
            // for elsewhere.
            SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
            const std::vector<VertexPositionTexture> blit = {
                VertexPositionTexture(Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)),
                VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
                VertexPositionTexture(Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
                VertexPositionTexture(Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)),
                VertexPositionTexture(Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
                VertexPositionTexture(Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 0.0f)),
            };
            for (int f = 0; f < 6; ++f)
            {
                dev.SetRenderTarget(rtc.get(), static_cast<CubeMapFace>(f));
                ResetDeviceState(dev, kFace, kFace);
                dev.Clear(kSentinel);
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setLightingEnabledProperty(false);
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(&base_);
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setAlphaProperty(1.0f);
                fx.setFogEnabledProperty(false);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, blit.data(), 0, 2);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetDeviceState(dev, kBBW, kBBH);
            }
        }
        catch (const std::exception& e)
        {
            skip(std::string("L: this backend does not support RenderTargetCube (") + e.what() + ")");
            return;
        }

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        Cfg cfg; cfg.cube = rtc.get(); cfg.amount = 1.0f;
        std::vector<Color> pix;
        try
        {
            pix = Render(dev, cfg);
        }
        catch (const std::exception& e)
        {
            skip(std::string("L: this backend does not accept a RenderTargetCube as an "
                             "EnvironmentMap (") + e.what() + ")");
            return;
        }

        // The faces carry the base pattern, so the same exact/differential oracle applies: this
        // proves the OTHER concrete cube class reaches the same binding under the SAME captured
        // sampler, and is not silently replaced by a default.
        check(OffPalette(pix, baseSet_) == 0,
              "L1: a RenderTargetCube used as the EnvironmentMap samples STORED texels under "
              "PointClamp (" + std::to_string(OffPalette(pix, baseSet_)) + " off-palette, first " +
              Str(FirstOffPalette(pix, baseSet_)) + ")");
        check(DistinctColors(pix) > 1,
              "L2: the RenderTargetCube's aimed face really carries a pattern, so L1 is not a "
              "flat-fill accident (" + std::to_string(DistinctColors(pix)) + " distinct colours)");

        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        const std::vector<Color> linear = Render(dev, cfg);
        check(OffPalette(linear, baseSet_) > 0,
              "L3: LinearClamp interpolates a RenderTargetCube too, so slot 1 is honoured for both "
              "concrete cube classes (" + std::to_string(OffPalette(linear, baseSet_)) + ")");
        check(DiffPixels(pix, linear) > 0,
              "L4: Point and Linear give different images on a RenderTargetCube (differing=" +
              std::to_string(DiffPixels(pix, linear)) + ")");
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
    }

    void RunM_DeviceDefault(GraphicsDevice& dev)
    {
        // A device-fresh SamplerStateCollection is LinearWrap on every slot. Restoring that must
        // give the documented default's behaviour on the cube -- a filtered fetch -- and must NOT
        // silently become Point.
        dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        dev.getSamplerStatesProperty()[1] = SamplerState::LinearWrap;
        Cfg cfg; cfg.cube = cubeA_.get(); cfg.amount = 1.0f;
        const std::vector<Color> def = Render(dev, cfg);
        check(OffPalette(def, cubeSet_) > 0,
              "M1: the documented device default (LinearWrap) filters the cube (" +
              std::to_string(OffPalette(def, cubeSet_)) + " interpolated pixels)");

        // Setting slot 1 EXPLICITLY to the documented default must be a no-op, so "unset" and
        // "explicitly default" cannot diverge.
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Wrap);
        const std::vector<Color> explicitDefault = Render(dev, cfg);
        check(DiffPixels(def, explicitDefault) == 0,
              "M2: an explicitly assigned LinearWrap reproduces the device default exactly "
              "(differing=" + std::to_string(DiffPixels(def, explicitDefault)) + ")");
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        const std::vector<Color> linearClamp = Render(dev, cfg);
        note("M: LinearWrap vs LinearClamp on a direction-addressed cube differ in " +
             std::to_string(DiffPixels(def, linearClamp)) + " pixels -- see leg K's boundary.");

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
    }

    void Finish()
    {
        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        palette_ = BuildPalette();
        if (static_cast<int>(palette_.size()) != kPaletteSize) return;

        for (int f = 0; f < 6; ++f)
            for (int i = 0; i < 16; ++i) cubeSet_.insert(Pack(CubeTexel(f, i)));
        for (int i = 0; i < kBaseColors; ++i) baseSet_.insert(Pack(BaseTexel(i)));
        for (int f = 0; f < 6; ++f)
            for (int l = 0; l <= 5; ++l) mipSet_.insert(Pack(MipMarker(f, l)));

        std::vector<Color> basePattern;
        for (int i = 0; i < kTex * kTex; ++i) basePattern.push_back(BaseTexel(i));
        base_ = Texture2D::CreateFromPixels(dev, kTex, kTex, Bytes(basePattern));

        // A non-rasterizing backend rejects cube uploads by REMED-GFX-130/135's contract; the run
        // must still reach its honest-rejection leg rather than abort in LoadContent.
        try
        {
            cubeA_ = std::make_unique<TextureCube>(dev, kFace, false, SurfaceFormat::Color);
            for (int f = 0; f < 6; ++f)
            {
                std::vector<Color> face;
                for (int i = 0; i < 16; ++i) face.push_back(CubeTexel(f, i));
                cubeA_->SetData(static_cast<CubeMapFace>(f), face.data(), static_cast<int>(face.size()));
            }
            // The same palette rotated onto different faces: a genuinely different texture whose
            // texels are still all in cubeSet_, so "two cubes, one sampler" stays byte-exact.
            cubeB_ = std::make_unique<TextureCube>(dev, kFace, false, SurfaceFormat::Color);
            for (int f = 0; f < 6; ++f)
            {
                std::vector<Color> face;
                for (int i = 0; i < 16; ++i) face.push_back(CubeTexel((f + 3) % 6, 15 - i));
                cubeB_->SetData(static_cast<CubeMapFace>(f), face.data(), static_cast<int>(face.size()));
            }
        }
        catch (const std::exception&)
        {
            cubeA_.reset();
            cubeB_.reset();
        }

        try
        {
            // The mipmapped cube is FLAT PER (face, level) at every level including 0. A cube-map
            // lambda varies slightly across the quad because it is derived from the derivative of a
            // NORMALISED direction, so a patterned level 0 would let the MINIFICATION filter show
            // through wherever lambda dips under 1 -- and the mip leg would then be measuring two
            // components at once. The one-level control keeps a real pattern, which is what makes
            // its own stored-texel check meaningful.
            //
            // Levels above 0 are written AFTER every level-0 upload, because a backend that
            // regenerates the chain on a full level-0 write would otherwise overwrite them.
            cubeMip_ = std::make_unique<TextureCube>(dev, kMipFace, true, SurfaceFormat::Color);
            cubeFlat_ = std::make_unique<TextureCube>(dev, kMipFace, false, SurfaceFormat::Color);
            for (int f = 0; f < 6; ++f)
            {
                std::vector<Color> pattern;
                pattern.reserve(static_cast<std::size_t>(kMipFace) * kMipFace);
                for (int y = 0; y < kMipFace; ++y)
                    for (int x = 0; x < kMipFace; ++x)
                        pattern.push_back(CubeTexel(f, (y / 8) * kFace + (x / 8)));
                cubeFlat_->SetData(static_cast<CubeMapFace>(f), pattern.data(), static_cast<int>(pattern.size()));

                std::vector<Color> level0(static_cast<std::size_t>(kMipFace) * kMipFace, MipMarker(f, 0));
                cubeMip_->SetData(static_cast<CubeMapFace>(f), level0.data(), static_cast<int>(level0.size()));
            }
            for (int f = 0; f < 6; ++f)
                for (int l = 1; l <= 5; ++l)
                {
                    const int edge = std::max(1, kMipFace >> l);
                    std::vector<Color> flat(static_cast<std::size_t>(edge) * edge, MipMarker(f, l));
                    cubeMip_->SetData(static_cast<CubeMapFace>(f), l, nullptr, flat.data(), 0,
                                      static_cast<int>(flat.size()));
                }
        }
        catch (const std::exception&)
        {
            cubeMip_.reset();
            cubeFlat_.reset();
        }
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        std::printf("=== REMED-GFX-173 EnvironmentMapEffect cube-sampler contract on %s ===\n",
                    kBackendName);

        RunPalette();

        if (!kRasterizes)
        {
            RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            std::vector<Color> pix(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
            bool threw = false;
            try { rt.GetData(pix.data(), 0, static_cast<int>(pix.size())); }
            catch (const System::Exception&) { threw = true; }
            catch (const std::exception&) { threw = true; }
            check(threw, "Z1: a non-rasterizing backend rejects the readback instead of fabricating "
                         "a sampled image");
            Finish();
            return;
        }

        if (!cubeA_)
        {
            skip("A..M: this backend could not store a TextureCube, so EnvironmentMapEffect has no "
                 "reflection resource to measure");
            Finish();
            return;
        }

        try
        {
            Cfg probe; probe.cube = cubeA_.get(); probe.amount = 1.0f;
            SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
            SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
            (void)Render(dev, probe);
        }
        catch (const std::exception& e)
        {
            skip(std::string("A..M: this backend does not implement EnvironmentMapEffect (") +
                 e.what() + ")");
            Finish();
            return;
        }

        RunA_CubeSlotIsConsulted(dev);
        RunB_BaseSlotStillWorks(dev);
        RunC_Independence(dev);
        RunD_FilterOrdinals(dev);
        RunE_CacheIdentity(dev);
        RunF_DeferredSnapshot(dev);
        RunG_DrawPaths(dev);
        RunH_RepeatedFrames(dev);
        RunI_Faces(dev);
        RunJ_MippedCube(dev);
        RunK_AddressModes(dev);
        RunL_RenderTargetCube(dev);
        RunM_DeviceDefault(dev);
        Finish();
    }

public:
    EnvMapCubeSamplerContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    EnvMapCubeSamplerContractTest game;
    game.Run();
    return game.getResult();
}
