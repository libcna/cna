// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-172: DualTextureEffect samples TWO independently observable resources -- an ordinary
// 2D texture and a second 2D overlay layer -- and each must be filtered by the sampler assigned to
// ITS OWN public GraphicsDevice.SamplerStates slot.
//
// THE CONTRACT, read from the authoritative source rather than assumed to match the neighbouring
// EnvironmentMapEffect numbering: FNA's `DualTextureEffect.fx` declares `DECLARE_TEXTURE(Texture, 0)`
// and `DECLARE_TEXTURE(Texture2, 1)`, and CNA's `DualTextureEffect::FillGpuDrawParams` assigns
// `Texture -> params.texture0` and `Texture2 -> params.texture1`. So `SamplerStates[0]` filters
// `DualTextureEffect.Texture` and `SamplerStates[1]` filters `DualTextureEffect.Texture2`. The two
// slots are INDEPENDENT: changing one may not alter what the other resource looks like, both must be
// captured when the draw is issued so a later public change cannot reach back into a queued command,
// and an explicit sampler must never be replaced by a fixed constant or by the other slot's value.
// An unset slot uses the documented device default (`SamplerState::LinearWrap`).
//
// THE DEFECT this reproduces is WebGPU-local, and it is a SHADER and BIND-GROUP-LAYOUT shape rather
// than a filter-ordinal mapping (which is REMED-GFX-170, already correct here). `dual_texture3d.wgsl`
// declares ONE `@group(1) @binding(0) var texSampler: sampler` and calls `textureSample(tex0,
// texSampler, uv)` AND `textureSample(tex1, texSampler, uv)` through it; `dualTextureBindGroupLayout_`
// has three entries (one sampler, two texture views); and `DualTextureDrawCommand` carries ONE
// `textureFilter/addressU/addressV/maxAnisotropy` group, filled from `slotSamplers_[0]`. So slot 1 is
// inexpressible at THREE layers at once and slot 1 silently inherits slot 0's sampler. All three must
// change together: a capture without a binding has nowhere to go, a binding without a shader
// declaration fails bind-group-layout validation, and a shader change without a capture would read
// live state at replay and break the deferred-snapshot contract REMED-GFX-116/146/159 established.
//
// THE ORACLE never needs the two contributions separated by configuration -- it observes BOTH IN THE
// SAME IMAGE, which is what makes "changing one sampler affects only its own resource" a direct
// measurement instead of an inference:
//
//   * `colTex_` (slot 0) varies along X ONLY -- four column colours, constant down each column.
//   * `rowTex_` (slot 1) varies along Y ONLY -- four row colours, constant across each row.
//   * The shader computes `(tex0.rgb*2) * tex1.rgb * tint`. With `tint = 0.5` that is exactly
//     `A(x) * B(y)`: a separable product whose X dependence comes from slot 0 ALONE and whose Y
//     dependence comes from slot 1 ALONE.
//   * Magnified 8x, each texel becomes an 8x8 block. Inside one block a POINT-filtered axis is
//     constant and a LINEAR-filtered axis is not. So `XEdgeVariation` counts slot 0's magnification
//     filter and `YEdgeVariation` counts slot 1's, simultaneously, with no cross-talk possible.
//
// A second, byte-exact oracle backs it up where an exact answer is available: with the OTHER slot's
// texture set to pure white, `(2*A)*1*0.5 = A` and `(2*1)*B*0.5 = B` are both exact in 8-bit, so
// "this pixel is a stored texel" is decidable exactly and "an interpolated colour appeared" is too.
// A PALETTE built at start-up guarantees, and CHECKS, that every entry is at least `kSep` apart in
// L-inf from every other and that the midpoint of any two entries is at least `kMidSep` from every
// entry, so neither decision has a tolerance.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   PAL palette validity                 the oracle's own preconditions
//   A   slot 1 is consulted at all       the red-first check
//   B   slot 0 still works               regression gate, and slot 0 did not move to slot 1
//   C   independence                     the full 2x2 matrix, change-one, swap, and return
//   D   filter ordinals 0..8             the complete public enum, on EACH slot, both partitions
//   E   resource identity vs sampler     same resources/different samplers, and the converse
//   F   deferred snapshot                queued draws keep the state public at THEIR call
//   G   draw paths                       static/dynamic, indexed 16/32-bit, ranges, DrawUser
//   H   repeated frames                  steady state, no first-frame-only correctness
//   I   interleaving                     another textured family between two DualTexture draws
//   J   mip                              the MIPMAP component of each slot's ordinal, isolated
//   K   address modes                    Clamp/Wrap/Mirror per slot, independently
//   L   vertex-colour variant            stride 24 is a SECOND shader module and pipeline
//   M   device default                   an unset slot 1 keeps the documented default
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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
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

    constexpr int kBBW = 128;
    constexpr int kBBH = 96;

    /// Magnifying target: a 4x4 texture becomes a 4x4 grid of kBlock-sized blocks across it.
    constexpr int kRT = 32;
    /// Texture edge, in texels, for every non-mipmapped texture here.
    constexpr int kTex = 4;
    /// One texel's extent on the magnifying target.
    constexpr int kBlock = kRT / kTex;
    /// Minifying target for the mip leg.
    constexpr int kMipRT = 8;
    /// Mipmapped texture edge -- 8 gives levels 0..3.
    constexpr int kMipTex = 8;

    /// Palette layout: 4 column colours (slot 0's separable axis), 4 row colours (slot 1's),
    /// two independent 4x4 grids for the byte-exact isolation legs, then flat per-level markers
    /// for two mip chains.
    constexpr int kColColors = 4;
    constexpr int kRowColors = 4;
    constexpr int kGridColors = 16;
    constexpr int kMipLevels = 4;
    constexpr int kPaletteSize = kColColors + kRowColors + 2 * kGridColors + 2 * kMipLevels;
    /// Minimum L-inf separation between two palette entries.
    constexpr int kSep = 20;
    /// Minimum L-inf distance from the midpoint of any two entries to every entry.
    constexpr int kMidSep = 9;
    /// Channel range every palette entry is confined to. The low bound keeps the separable product
    /// A*B away from zero (where a filter difference would be invisible) and the high bound keeps
    /// it away from the 1.0 clamp.
    constexpr int kChanLo = 72;
    constexpr int kChanHi = 248;

    const Color kSentinel(11, 13, 17, 255);
    const Color kWhite(255, 255, 255, 255);

    /// Stride-20 VertexPositionTexture, used by the interleaved BasicEffect draw.
    struct VtxPT { float px, py, pz; float u, v; };
    static_assert(sizeof(VtxPT) == 20, "stride 20");

    /// DualTextureEffect vertex with independent TextureCoordinate0/1 channels.
    struct VtxDualPT { float px, py, pz; float u0, v0; float u1, v1; };
    static_assert(sizeof(VtxDualPT) == 28, "stride 28");

    /// Vertex-colour DualTextureEffect vertex with independent TextureCoordinate0/1 channels.
    struct VtxDualPCT { float px, py, pz; std::uint32_t rgba; float u0, v0; float u1, v1; };
    static_assert(sizeof(VtxDualPCT) == 32, "stride 32");

    const VertexDeclaration kDualDeclaration(
        28,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        });

    const VertexDeclaration kDualColorDeclaration(
        32,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        });

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

    /// RGB only -- alpha is 255 everywhere by construction.
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

    const char* AddressName(TextureAddressMode m)
    {
        switch (m)
        {
            case TextureAddressMode::Wrap:   return "Wrap";
            case TextureAddressMode::Clamp:  return "Clamp";
            case TextureAddressMode::Mirror: return "Mirror";
        }
        return "?";
    }
}

class DualTextureSlotSamplerContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    std::vector<Color> palette_;
    std::set<std::uint32_t> colSet_, rowSet_, gridASet_, gridBSet_, mipASet_, mipBSet_;

    std::unique_ptr<Texture2D> white_, colTex_, rowTex_, gridA_, gridB_, mipA_, mipB_;
    int mipLevels_ = 0;

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
    /// entries is within kMidSep of any entry, with every channel inside [kChanLo, kChanHi].
    /// Deterministic (a fixed LCG, no <random>), so the same colours are measured on every renderer
    /// and in every run.
    static std::vector<Color> BuildPalette()
    {
        std::vector<Color> out;
        out.reserve(kPaletteSize);
        std::vector<Color> mids;
        std::uint32_t s = 24680u;
        auto nextChan = [&s]() {
            s = s * 1664525u + 1013904223u;
            return static_cast<std::uint8_t>(kChanLo + ((s >> 16) % (kChanHi - kChanLo + 1)));
        };
        int guard = 0;
        while (static_cast<int>(out.size()) < kPaletteSize && guard++ < 8000000)
        {
            const std::uint8_t r = nextChan();
            const std::uint8_t g = nextChan();
            const std::uint8_t b = nextChan();
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

    const Color& ColColor(int x) const { return palette_[static_cast<std::size_t>(x)]; }
    const Color& RowColor(int y) const { return palette_[static_cast<std::size_t>(kColColors + y)]; }
    const Color& GridATexel(int i) const
    {
        return palette_[static_cast<std::size_t>(kColColors + kRowColors + i)];
    }
    const Color& GridBTexel(int i) const
    {
        return palette_[static_cast<std::size_t>(kColColors + kRowColors + kGridColors + i)];
    }
    const Color& MipAMarker(int level) const
    {
        return palette_[static_cast<std::size_t>(kColColors + kRowColors + 2 * kGridColors + level)];
    }
    const Color& MipBMarker(int level) const
    {
        return palette_[static_cast<std::size_t>(kColColors + kRowColors + 2 * kGridColors +
                                                 kMipLevels + level)];
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

    /// How many blocks vary along X between their first and last column. With the separable
    /// colTex_/rowTex_ pair this counts SLOT 0's magnification filter and nothing else: slot 1's
    /// texture is constant along X, so it multiplies every pixel of a block row by the same value.
    ///
    /// The block extent is derived from the image's OWN width and height rather than assumed
    /// square: the deferred-snapshot and interleaving legs draw into a half-height viewport, where
    /// one texel covers w/kTex columns but only h/kTex rows, and comparing a fixed 8 rows apart
    /// there would straddle a block boundary and report variation that POINT filtering never
    /// produced.
    static int XEdgeVariation(const std::vector<Color>& pix, int w = kRT, int h = kRT)
    {
        const int bw = std::max(1, w / kTex);
        int n = 0;
        for (int y = 0; y < h; ++y)
            for (int bx = 0; bx < kTex; ++bx)
            {
                const std::size_t left  = static_cast<std::size_t>(y) * w + bx * bw;
                const std::size_t right = left + static_cast<std::size_t>(bw) - 1u;
                if (right < pix.size() && !SameColor(pix[left], pix[right])) ++n;
            }
        return n;
    }

    /// The transpose: counts SLOT 1's magnification filter and nothing else.
    static int YEdgeVariation(const std::vector<Color>& pix, int w = kRT, int h = kRT)
    {
        const int bh = std::max(1, h / kTex);
        int n = 0;
        for (int x = 0; x < w; ++x)
            for (int by = 0; by < kTex; ++by)
            {
                const std::size_t top    = static_cast<std::size_t>(by * bh) * w + x;
                const std::size_t bottom = static_cast<std::size_t>(by * bh + bh - 1) * w + x;
                if (bottom < pix.size() && !SameColor(pix[top], pix[bottom])) ++n;
            }
        return n;
    }

    /// True when every one-texel block is one flat colour -- the signature of BOTH slots
    /// magnifying by POINT.
    static bool BlockUniform(const std::vector<Color>& pix, std::string& why,
                             int w = kRT, int h = kRT)
    {
        const int bw = std::max(1, w / kTex);
        const int bh = std::max(1, h / kTex);
        for (int by = 0; by < kTex; ++by)
            for (int bx = 0; bx < kTex; ++bx)
            {
                const Color first = pix[static_cast<std::size_t>(by * bh) * w + bx * bw];
                for (int y = 0; y < bh; ++y)
                    for (int x = 0; x < bw; ++x)
                    {
                        const std::size_t i = static_cast<std::size_t>(by * bh + y) * w + bx * bw + x;
                        if (i >= pix.size()) continue;
                        if (!SameColor(pix[i], first))
                        {
                            why = "block(" + std::to_string(bx) + "," + std::to_string(by) +
                                  ") has " + Str(first) + " and " + Str(pix[i]);
                            return false;
                        }
                    }
            }
        why.clear();
        return true;
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

    /// The two slots in one call, so a leg's intent reads as a PAIR.
    static void SetPair(GraphicsDevice& dev, int f0, int f1,
                        TextureAddressMode a = TextureAddressMode::Clamp)
    {
        SetSlot(dev, 0, f0, a);
        SetSlot(dev, 1, f1, a);
    }

    static void ResetDeviceState(GraphicsDevice& dev, int w, int h)
    {
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    // ----------------------------------------------------------------------------------------
    // Textures
    // ----------------------------------------------------------------------------------------

    static std::unique_ptr<Texture2D> MakeTexture(GraphicsDevice& dev, const std::vector<Color>& texels)
    {
        auto t = std::make_unique<Texture2D>(dev, kTex, kTex);
        t->SetData(texels.data(), static_cast<int>(texels.size()));
        return t;
    }

    void BuildTextures(GraphicsDevice& dev)
    {
        std::vector<Color> whiteTexels(static_cast<std::size_t>(kTex) * kTex, kWhite);
        white_ = MakeTexture(dev, whiteTexels);

        std::vector<Color> col(static_cast<std::size_t>(kTex) * kTex, kWhite);
        std::vector<Color> row(static_cast<std::size_t>(kTex) * kTex, kWhite);
        std::vector<Color> ga(static_cast<std::size_t>(kTex) * kTex, kWhite);
        std::vector<Color> gb(static_cast<std::size_t>(kTex) * kTex, kWhite);
        for (int y = 0; y < kTex; ++y)
            for (int x = 0; x < kTex; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * kTex + x;
                col[i] = ColColor(x);
                row[i] = RowColor(y);
                ga[i]  = GridATexel(static_cast<int>(i));
                gb[i]  = GridBTexel(static_cast<int>(i));
            }
        colTex_ = MakeTexture(dev, col);
        rowTex_ = MakeTexture(dev, row);
        gridA_  = MakeTexture(dev, ga);
        gridB_  = MakeTexture(dev, gb);

        for (int x = 0; x < kTex; ++x) colSet_.insert(Pack(ColColor(x)));
        for (int y = 0; y < kTex; ++y) rowSet_.insert(Pack(RowColor(y)));
        for (int i = 0; i < kGridColors; ++i) gridASet_.insert(Pack(GridATexel(i)));
        for (int i = 0; i < kGridColors; ++i) gridBSet_.insert(Pack(GridBTexel(i)));

        // Two flat-per-level chains. Every level is one colour, so the MINIFICATION filter is
        // invisible inside a level at every lambda and only the ordinal's MIPMAP component can
        // change the answer -- the isolation REMED-GFX-175's fixture uses.
        mipA_ = std::make_unique<Texture2D>(dev, kMipTex, kMipTex, true, SurfaceFormat::Color);
        mipB_ = std::make_unique<Texture2D>(dev, kMipTex, kMipTex, true, SurfaceFormat::Color);
        mipLevels_ = mipA_->getLevelCountProperty();
        int w = kMipTex, h = kMipTex;
        for (int level = 0; level < mipLevels_; ++level)
        {
            const int idx = std::min(level, kMipLevels - 1);
            std::vector<Color> a(static_cast<std::size_t>(w) * h, MipAMarker(idx));
            std::vector<Color> b(static_cast<std::size_t>(w) * h, MipBMarker(idx));
            mipA_->SetData(level, nullptr, a.data(), 0, static_cast<int>(a.size()));
            mipB_->SetData(level, nullptr, b.data(), 0, static_cast<int>(b.size()));
            mipASet_.insert(Pack(MipAMarker(idx)));
            mipBSet_.insert(Pack(MipBMarker(idx)));
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
    }

    // ----------------------------------------------------------------------------------------
    // Geometry and the one dual-texture draw
    // ----------------------------------------------------------------------------------------

    /// UV span. 1.0 covers the texture exactly once (the magnifying default); larger values are
    /// what leg K uses to push the sample outside [0,1] and make the address mode decide.
    static std::vector<VtxDualPT> Quad(float uvMax = 1.0f)
    {
        auto v = [](float x, float y, float u, float w) {
            VtxDualPT r{};
            r.px = x; r.py = y; r.pz = 0.0f;
            r.u0 = u; r.v0 = w;
            r.u1 = u; r.v1 = w;
            return r;
        };
        const VtxDualPT tl = v(-1.0f,  1.0f, 0.0f,   0.0f);
        const VtxDualPT bl = v(-1.0f, -1.0f, 0.0f,   uvMax);
        const VtxDualPT br = v( 1.0f, -1.0f, uvMax,  uvMax);
        const VtxDualPT tr = v( 1.0f,  1.0f, uvMax,  0.0f);
        return { tl, bl, br, tl, br, tr };
    }

    static std::vector<VtxDualPCT> QuadColored(float uvMax = 1.0f)
    {
        std::vector<VtxDualPCT> out;
        for (const VtxDualPT& s : Quad(uvMax))
        {
            VtxDualPCT r{};
            r.px = s.px; r.py = s.py; r.pz = s.pz;
            r.rgba = 0xFFFFFFFFu;   ///< white, so the tint is exactly DiffuseColor
            r.u0 = s.u0; r.v0 = s.v0;
            r.u1 = s.u1; r.v1 = s.v1;
            out.push_back(r);
        }
        return out;
    }

    static std::vector<VtxPT> QuadBasic(float uvMax = 1.0f)
    {
        std::vector<VtxPT> out;
        for (const VtxDualPT& s : Quad(uvMax))
            out.push_back({s.px, s.py, s.pz, s.u0, s.v0});
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
            case Path::StaticNonIndexed:   return "DrawPrimitives";
            case Path::StaticIndexed16:    return "DrawIndexedPrimitives/16-bit";
            case Path::StaticIndexed32:    return "DrawIndexedPrimitives/32-bit";
            case Path::StaticIndexedRange: return "DrawIndexedPrimitives/startIndex+baseVertex";
            case Path::UserNonIndexed:     return "DrawUserPrimitives";
            case Path::UserIndexed16:      return "DrawUserIndexedPrimitives/16-bit";
            case Path::UserIndexed32:      return "DrawUserIndexedPrimitives/32-bit";
            case Path::DynamicNonIndexed:  return "DrawPrimitives/dynamic buffer";
        }
        return "?";
    }

    struct Cfg
    {
        Texture2D* tex0 = nullptr;   ///< DualTextureEffect.Texture  -- SamplerStates[0]
        Texture2D* tex1 = nullptr;   ///< DualTextureEffect.Texture2 -- SamplerStates[1]
        float uvMax = 1.0f;
        bool vertexColor = false;    ///< the second, vertex-colour DualTexture shader module
        Path path = Path::StaticNonIndexed;
    };

    void Configure(DualTextureEffect& fx, const Cfg& cfg) const
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(cfg.tex0);
        fx.setTexture2Property(cfg.tex1);
        // The shader is (tex0.rgb*2) * tex1.rgb * tint. tint = 0.5 makes that exactly tex0*tex1,
        // so a white partner leaves the other layer's stored texel byte-exact.
        fx.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setAlphaProperty(1.0f);
        fx.setVertexColorEnabledProperty(cfg.vertexColor);
        fx.setFogEnabledProperty(false);
    }

    void DrawDual(GraphicsDevice& dev, const Cfg& cfg)
    {
        DualTextureEffect fx(dev);
        Configure(fx, cfg);
        fx.Apply();

        if (cfg.vertexColor)
        {
            const std::vector<VtxDualPCT> quad = QuadColored(cfg.uvMax);
            VertexBuffer vb(dev, kDualColorDeclaration, static_cast<int>(quad.size()), BufferUsage::None);
            vb.SetDataRaw(quad.data(), static_cast<int>(quad.size()), static_cast<int>(sizeof(VtxDualPCT)));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            return;
        }

        const std::vector<VtxDualPT> quad = Quad(cfg.uvMax);
        switch (cfg.path)
        {
            case Path::StaticNonIndexed:
            {
                VertexBuffer vb(dev, kDualDeclaration, static_cast<int>(quad.size()), BufferUsage::None);
                vb.SetDataRaw(quad.data(), static_cast<int>(quad.size()), static_cast<int>(sizeof(VtxDualPT)));
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                break;
            }
            case Path::DynamicNonIndexed:
            {
                // A dynamic buffer written twice, so the draw reads the SECOND write -- a renderer
                // that shadows vertex data at creation time cannot pass this by accident.
                std::vector<VtxDualPT> decoy(quad.size(), quad.front());
                VertexBuffer vb(dev, kDualDeclaration, static_cast<int>(quad.size()), BufferUsage::None);
                vb.SetDataRaw(decoy.data(), static_cast<int>(decoy.size()), static_cast<int>(sizeof(VtxDualPT)));
                vb.SetDataRaw(quad.data(), static_cast<int>(quad.size()), static_cast<int>(sizeof(VtxDualPT)));
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                break;
            }
            case Path::StaticIndexed16:
            {
                VertexBuffer vb(dev, kDualDeclaration, 4, BufferUsage::None);
                const VtxDualPT corners[4] = { quad[0], quad[1], quad[2], quad[5] };
                vb.SetDataRaw(corners, 4, static_cast<int>(sizeof(VtxDualPT)));
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
                VertexBuffer vb(dev, kDualDeclaration, 4, BufferUsage::None);
                const VtxDualPT corners[4] = { quad[0], quad[1], quad[2], quad[5] };
                vb.SetDataRaw(corners, 4, static_cast<int>(sizeof(VtxDualPT)));
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
                VtxDualPT decoy = quad[0];
                decoy.px = decoy.py = 0.0f;
                std::vector<VtxDualPT> verts = { decoy, decoy, quad[0], quad[1], quad[2], quad[5] };
                VertexBuffer vb(dev, kDualDeclaration, static_cast<int>(verts.size()), BufferUsage::None);
                vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(VtxDualPT)));
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
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2,
                                       kDualDeclaration);
                break;
            }
            case Path::UserIndexed16:
            {
                const VtxDualPT v[4] = { quad[0], quad[1], quad[2], quad[5] };
                const std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
                dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, 0, 4, idx, 0, 2,
                                              kDualDeclaration);
                break;
            }
            case Path::UserIndexed32:
            {
                const VtxDualPT v[4] = { quad[0], quad[1], quad[2], quad[5] };
                const std::uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
                dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, 0, 4, idx, 0, 2,
                                              kDualDeclaration);
                break;
            }
        }
    }

    /// A single ordinary textured draw -- the OTHER effect family leg I interleaves with.
    void DrawBasicTextured(GraphicsDevice& dev, Texture2D* tex)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(tex);
        fx.setLightingEnabledProperty(false);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(1.0f);
        fx.setFogEnabledProperty(false);
        fx.Apply();
        const std::vector<VtxPT> quad = QuadBasic(1.0f);
        VertexBuffer vb(dev, static_cast<int>(quad.size()));
        vb.SetDataRaw(quad.data(), static_cast<int>(quad.size()), static_cast<int>(sizeof(VtxPT)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    /// One draw into a fresh target of the given edge, returned whole.
    std::vector<Color> Render(GraphicsDevice& dev, const Cfg& cfg, int edge = kRT)
    {
        RenderTarget2D rt(dev, edge, edge, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, edge, edge);
        dev.Clear(kSentinel);
        DrawDual(dev, cfg);
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
        DrawDual(dev, a);
        if (between) between(dev);
        dev.setViewportProperty(Viewport(0, kRT / 2, kRT, kRT / 2));
        DrawDual(dev, b);
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

    /// The separable configuration every independence leg uses: slot 0 sees an X-only texture and
    /// slot 1 a Y-only one, so XEdgeVariation and YEdgeVariation read one slot each.
    Cfg Sep(Path p = Path::StaticNonIndexed) const
    {
        Cfg c;
        c.tex0 = colTex_.get();
        c.tex1 = rowTex_.get();
        c.path = p;
        return c;
    }

    /// Slot 0's texture alone, byte-exact (slot 1 sees pure white).
    Cfg OnlySlot0(Texture2D* t) const { Cfg c; c.tex0 = t; c.tex1 = white_.get(); return c; }
    /// Slot 1's texture alone, byte-exact (slot 0 sees pure white).
    Cfg OnlySlot1(Texture2D* t) const { Cfg c; c.tex0 = white_.get(); c.tex1 = t; return c; }

    // ----------------------------------------------------------------------------------------
    // PAL -- the oracle's own preconditions
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

        int lo = 255, hi = 0;
        for (const Color& c : palette_)
        {
            lo = std::min({lo, static_cast<int>(c.getRProperty()), static_cast<int>(c.getGProperty()),
                           static_cast<int>(c.getBProperty())});
            hi = std::max({hi, static_cast<int>(c.getRProperty()), static_cast<int>(c.getGProperty()),
                           static_cast<int>(c.getBProperty())});
        }
        check(lo >= kChanLo && hi <= kChanHi,
              "PAL4: every channel is inside [" + std::to_string(kChanLo) + "," +
              std::to_string(kChanHi) + "], so the separable product A*B is far from both 0 and the "
              "1.0 clamp and a filter difference cannot be squeezed out of it (min=" +
              std::to_string(lo) + ", max=" + std::to_string(hi) + ")");

        int worstCol = 1000, worstRow = 1000;
        for (int i = 0; i + 1 < kColColors; ++i) worstCol = std::min(worstCol, LinfRGB(ColColor(i), ColColor(i + 1)));
        for (int i = 0; i + 1 < kRowColors; ++i) worstRow = std::min(worstRow, LinfRGB(RowColor(i), RowColor(i + 1)));
        check(worstCol >= kSep && worstRow >= kSep,
              "PAL5: adjacent column colours and adjacent row colours both differ strongly, so an "
              "interpolation across a block boundary is far from both neighbours (col=" +
              std::to_string(worstCol) + ", row=" + std::to_string(worstRow) + ")");
    }

    // ----------------------------------------------------------------------------------------
    // A -- slot 1 is consulted at all (the red-first leg)
    // ----------------------------------------------------------------------------------------

    void RunA(GraphicsDevice& dev)
    {
        SetPair(dev, 1, 1);
        const std::vector<Color> pp = Render(dev, Sep());
        SetPair(dev, 1, 0);
        const std::vector<Color> pl = Render(dev, Sep());

        check(DiffPixels(pp, pl) > 0,
              "A1: changing SamplerStates[1] from Point to Linear changes the image -- the second "
              "texture layer's sampler is consulted at all (differing=" +
              std::to_string(DiffPixels(pp, pl)) + ")");
        check(YEdgeVariation(pl) > 0,
              "A2: with slot 1 LINEAR the image varies DOWN each block, so Texture2 really is "
              "filtered linearly (y-variation=" + std::to_string(YEdgeVariation(pl)) + ")");
        check(XEdgeVariation(pl) == 0,
              "A3: and it does NOT vary ACROSS a block, so slot 1's Linear did not leak into slot 0 "
              "(x-variation=" + std::to_string(XEdgeVariation(pl)) + ")");

        // The byte-exact half: slot 1 alone, with white on slot 0.
        SetPair(dev, 1, 1);
        const std::vector<Color> exact = Render(dev, OnlySlot1(gridB_.get()));
        check(OffPalette(exact, gridBSet_) == 0,
              "A4: slot 1 POINT reproduces Texture2's stored texels byte-exactly, so the oracle's "
              "own arithmetic is exact (off-palette=" + std::to_string(OffPalette(exact, gridBSet_)) +
              ", first=" + Str(FirstOffPalette(exact, gridBSet_)) + ")");
        SetPair(dev, 1, 0);
        const std::vector<Color> interp = Render(dev, OnlySlot1(gridB_.get()));
        check(OffPalette(interp, gridBSet_) > 0,
              "A5: slot 1 LINEAR makes colours appear that are stored in NO texel of Texture2 (off-"
              "palette=" + std::to_string(OffPalette(interp, gridBSet_)) + ")");
    }

    // ----------------------------------------------------------------------------------------
    // B -- slot 0 still works, and did not move to slot 1
    // ----------------------------------------------------------------------------------------

    void RunB(GraphicsDevice& dev)
    {
        SetPair(dev, 0, 1);
        const std::vector<Color> lp = Render(dev, Sep());
        check(XEdgeVariation(lp) > 0,
              "B1: with slot 0 LINEAR the image varies ACROSS each block, so Texture is still "
              "filtered by its own slot (x-variation=" + std::to_string(XEdgeVariation(lp)) + ")");
        check(YEdgeVariation(lp) == 0,
              "B2: and it does NOT vary DOWN a block, so slot 0's Linear did not reach Texture2 "
              "(y-variation=" + std::to_string(YEdgeVariation(lp)) + ")");

        SetPair(dev, 1, 1);
        const std::vector<Color> exact = Render(dev, OnlySlot0(gridA_.get()));
        check(OffPalette(exact, gridASet_) == 0,
              "B3: slot 0 POINT reproduces Texture's stored texels byte-exactly (off-palette=" +
              std::to_string(OffPalette(exact, gridASet_)) + ", first=" +
              Str(FirstOffPalette(exact, gridASet_)) + ")");
        SetPair(dev, 0, 1);
        const std::vector<Color> interp = Render(dev, OnlySlot0(gridA_.get()));
        check(OffPalette(interp, gridASet_) > 0,
              "B4: slot 0 LINEAR makes colours appear that are stored in NO texel of Texture (off-"
              "palette=" + std::to_string(OffPalette(interp, gridASet_)) + ")");
    }

    // ----------------------------------------------------------------------------------------
    // C -- the independence matrix
    // ----------------------------------------------------------------------------------------

    void RunC(GraphicsDevice& dev)
    {
        SetPair(dev, 1, 1);
        const std::vector<Color> pp = Render(dev, Sep());
        SetPair(dev, 0, 1);
        const std::vector<Color> lp = Render(dev, Sep());
        SetPair(dev, 1, 0);
        const std::vector<Color> pl = Render(dev, Sep());
        SetPair(dev, 0, 0);
        const std::vector<Color> ll = Render(dev, Sep());

        std::string why;
        check(BlockUniform(pp, why),
              "C1: both slots PointClamp -- every block is one flat colour. " + why);
        check(XEdgeVariation(pp) == 0 && YEdgeVariation(pp) == 0,
              "C2: both slots Point -- neither axis varies inside a block (x=" +
              std::to_string(XEdgeVariation(pp)) + ", y=" + std::to_string(YEdgeVariation(pp)) + ")");
        check(XEdgeVariation(ll) > 0 && YEdgeVariation(ll) > 0,
              "C3: both slots Linear -- BOTH axes vary inside a block (x=" +
              std::to_string(XEdgeVariation(ll)) + ", y=" + std::to_string(YEdgeVariation(ll)) + ")");
        check(XEdgeVariation(lp) > 0 && YEdgeVariation(lp) == 0,
              "C4: slot0=Linear slot1=Point -- ONLY the x axis varies (x=" +
              std::to_string(XEdgeVariation(lp)) + ", y=" + std::to_string(YEdgeVariation(lp)) + ")");
        check(XEdgeVariation(pl) == 0 && YEdgeVariation(pl) > 0,
              "C5: slot0=Point slot1=Linear -- ONLY the y axis varies (x=" +
              std::to_string(XEdgeVariation(pl)) + ", y=" + std::to_string(YEdgeVariation(pl)) + ")");

        // Change ONE slot at a time from the both-Point baseline.
        check(DiffPixels(pp, lp) > 0,
              "C6: changing ONLY slot 0 changes the image (differing=" +
              std::to_string(DiffPixels(pp, lp)) + ")");
        check(DiffPixels(pp, pl) > 0,
              "C7: changing ONLY slot 1 changes the image (differing=" +
              std::to_string(DiffPixels(pp, pl)) + ")");
        check(DiffPixels(lp, pl) > 0,
              "C8: swapping the two slots' states changes the image, so the two bindings are not "
              "aliases of one another (differing=" + std::to_string(DiffPixels(lp, pl)) + ")");

        // Return to an earlier pair -- byte-identical, so nothing latched.
        SetPair(dev, 1, 1);
        const std::vector<Color> ppAgain = Render(dev, Sep());
        check(DiffPixels(pp, ppAgain) == 0,
              "C9: returning to an earlier sampler pair reproduces the earlier image byte-for-byte, "
              "so no sampler state latched (differing=" + std::to_string(DiffPixels(pp, ppAgain)) + ")");

        // Byte-exact independence: with white on slot 1, slot 1's own filter must not change one
        // bit of the answer, and the converse.
        SetPair(dev, 1, 1);
        const std::vector<Color> s0p = Render(dev, OnlySlot0(gridA_.get()));
        SetPair(dev, 1, 0);
        const std::vector<Color> s0pl = Render(dev, OnlySlot0(gridA_.get()));
        check(DiffPixels(s0p, s0pl) == 0,
              "C10: a flat white Texture2 filtered Point or Linear gives the same white, so changing "
              "slot 1 alone cannot alter Texture's contribution (differing=" +
              std::to_string(DiffPixels(s0p, s0pl)) + ")");
        SetPair(dev, 1, 1);
        const std::vector<Color> s1p = Render(dev, OnlySlot1(gridB_.get()));
        SetPair(dev, 0, 1);
        const std::vector<Color> s1lp = Render(dev, OnlySlot1(gridB_.get()));
        check(DiffPixels(s1p, s1lp) == 0,
              "C11: and changing slot 0 alone cannot alter Texture2's contribution -- THE decisive "
              "check, since a shared sampler makes slot 0's Linear reach the second layer "
              "(differing=" + std::to_string(DiffPixels(s1p, s1lp)) + ")");
    }

    // ----------------------------------------------------------------------------------------
    // D -- the complete filter ordinal enum, on EACH slot
    // ----------------------------------------------------------------------------------------

    void RunD(GraphicsDevice& dev)
    {
        for (int o = 0; o <= 8; ++o)
        {
            SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
            SetSlot(dev, 1, o, TextureAddressMode::Clamp);
            const std::vector<Color> pix = Render(dev, Sep());
            const int yv = YEdgeVariation(pix);
            const int xv = XEdgeVariation(pix);
            const bool wantPoint = MagnifiesByPoint(o);
            check(wantPoint ? (yv == 0) : (yv > 0),
                  "D1[" + std::to_string(o) + " " + FilterName(o) + "] slot 1 magnifies by " +
                  (wantPoint ? "POINT" : "LINEAR") + " (y-variation=" + std::to_string(yv) + ")");
            check(xv == 0,
                  "D2[" + std::to_string(o) + " " + FilterName(o) + "] slot 0 stayed POINT while "
                  "slot 1 swept the enum, so the ordinal reached slot 1 alone (x-variation=" +
                  std::to_string(xv) + ")");
        }

        for (int o = 0; o <= 8; ++o)
        {
            SetSlot(dev, 0, o, TextureAddressMode::Clamp);
            SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
            const std::vector<Color> pix = Render(dev, Sep());
            const int xv = XEdgeVariation(pix);
            const int yv = YEdgeVariation(pix);
            const bool wantPoint = MagnifiesByPoint(o);
            check(wantPoint ? (xv == 0) : (xv > 0),
                  "D3[" + std::to_string(o) + " " + FilterName(o) + "] slot 0 magnifies by " +
                  (wantPoint ? "POINT" : "LINEAR") + " (x-variation=" + std::to_string(xv) + ")");
            check(yv == 0,
                  "D4[" + std::to_string(o) + " " + FilterName(o) + "] slot 1 stayed POINT while "
                  "slot 0 swept the enum (y-variation=" + std::to_string(yv) + ")");
        }
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // E -- resource identity and sampler identity are independent binding inputs
    // ----------------------------------------------------------------------------------------

    void RunE(GraphicsDevice& dev)
    {
        // Same resources, four sampler pairs, all four images distinct.
        SetPair(dev, 1, 1); const std::vector<Color> a = Render(dev, Sep());
        SetPair(dev, 0, 1); const std::vector<Color> b = Render(dev, Sep());
        SetPair(dev, 1, 0); const std::vector<Color> c = Render(dev, Sep());
        SetPair(dev, 0, 0); const std::vector<Color> d = Render(dev, Sep());
        const bool allDistinct = DiffPixels(a, b) > 0 && DiffPixels(a, c) > 0 && DiffPixels(a, d) > 0 &&
                                 DiffPixels(b, c) > 0 && DiffPixels(b, d) > 0 && DiffPixels(c, d) > 0;
        check(allDistinct,
              "E1: the SAME two textures under four different sampler pairs give four different "
              "images, so no cache collapses two binding states into one");

        // One resource changed while the samplers stay fixed.
        SetPair(dev, 1, 1);
        Cfg one = Sep(); one.tex1 = gridB_.get();
        const std::vector<Color> r1 = Render(dev, one);
        Cfg two = Sep(); two.tex1 = gridA_.get();
        const std::vector<Color> r2 = Render(dev, two);
        check(DiffPixels(r1, r2) > 0,
              "E2: changing ONLY the second texture, with both samplers fixed, changes the image, so "
              "resource identity is keyed independently of sampler identity (differing=" +
              std::to_string(DiffPixels(r1, r2)) + ")");

        Cfg swap0 = Sep(); swap0.tex0 = gridA_.get();
        const std::vector<Color> r3 = Render(dev, swap0);
        check(DiffPixels(r1, r3) > 0,
              "E3: changing ONLY the first texture does too (differing=" +
              std::to_string(DiffPixels(r1, r3)) + ")");

        // Two draws in ONE frame under different sampler pairs -- the halves must disagree.
        const std::vector<Color> two1 = RenderTwo(
            dev, Sep(), Sep(),
            [](GraphicsDevice& g) { SetPair(g, 0, 0); },
            [](GraphicsDevice& g) { SetPair(g, 1, 1); });
        // The first half was queued under the pair set before RenderTwo, the second under (0,0).
        const std::vector<Color> top = TopHalf(two1);
        const std::vector<Color> bottom = BottomHalf(two1);
        check(XEdgeVariation(top, kRT, kRT / 2) == 0 || YEdgeVariation(top, kRT, kRT / 2) == 0 ||
              DiffPixels(top, bottom) > 0,
              "E4: two draws in one frame under different sampler pairs do not produce identical "
              "halves (differing=" + std::to_string(DiffPixels(top, bottom)) + ")");
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // F -- the deferred snapshot
    // ----------------------------------------------------------------------------------------

    void RunF(GraphicsDevice& dev)
    {
        // Draw A queued under (Point, Point); the public pair is then changed twice before the
        // frame is flushed. A renderer that resolves samplers at replay shows the LAST pair on both.
        SetPair(dev, 1, 1);
        const std::vector<Color> pix = RenderTwo(
            dev, Sep(), Sep(),
            [](GraphicsDevice& g) { SetPair(g, 0, 0); },
            [](GraphicsDevice& g) { SetPair(g, 1, 0); });
        const std::vector<Color> top = TopHalf(pix);
        const std::vector<Color> bottom = BottomHalf(pix);

        check(XEdgeVariation(top, kRT, kRT / 2) == 0 && YEdgeVariation(top, kRT, kRT / 2) == 0,
              "F1: the draw queued under (Point,Point) kept BOTH of its samplers after the public "
              "pair was changed twice (x=" + std::to_string(XEdgeVariation(top, kRT, kRT / 2)) +
              ", y=" + std::to_string(YEdgeVariation(top, kRT, kRT / 2)) + ")");
        check(XEdgeVariation(bottom, kRT, kRT / 2) > 0 && YEdgeVariation(bottom, kRT, kRT / 2) > 0,
              "F2: and the draw queued under (Linear,Linear) kept ITS pair too (x=" +
              std::to_string(XEdgeVariation(bottom, kRT, kRT / 2)) + ", y=" +
              std::to_string(YEdgeVariation(bottom, kRT, kRT / 2)) + ")");

        // The asymmetric case: the two queued draws differ in slot 1 ONLY.
        SetPair(dev, 1, 1);
        const std::vector<Color> pix2 = RenderTwo(
            dev, Sep(), Sep(),
            [](GraphicsDevice& g) { SetPair(g, 1, 0); },
            [](GraphicsDevice& g) { SetPair(g, 0, 0); });
        check(YEdgeVariation(TopHalf(pix2), kRT, kRT / 2) == 0 &&
              YEdgeVariation(BottomHalf(pix2), kRT, kRT / 2) > 0,
              "F3: two queued draws differing in slot 1 ALONE keep their own slot-1 samplers "
              "(top y=" + std::to_string(YEdgeVariation(TopHalf(pix2), kRT, kRT / 2)) + ", bottom y=" +
              std::to_string(YEdgeVariation(BottomHalf(pix2), kRT, kRT / 2)) + ")");
        check(XEdgeVariation(TopHalf(pix2), kRT, kRT / 2) == 0 &&
              XEdgeVariation(BottomHalf(pix2), kRT, kRT / 2) == 0,
              "F4: and neither of them picked up the slot-0 Linear set AFTER both were queued "
              "(top x=" + std::to_string(XEdgeVariation(TopHalf(pix2), kRT, kRT / 2)) + ", bottom x=" +
              std::to_string(XEdgeVariation(BottomHalf(pix2), kRT, kRT / 2)) + ")");
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // G -- every public draw path
    // ----------------------------------------------------------------------------------------

    void RunG(GraphicsDevice& dev)
    {
        const Path paths[] = {
            Path::StaticNonIndexed, Path::StaticIndexed16, Path::StaticIndexed32,
            Path::StaticIndexedRange, Path::UserNonIndexed, Path::UserIndexed16,
            Path::UserIndexed32, Path::DynamicNonIndexed,
        };
        for (Path p : paths)
        {
            SetPair(dev, 1, 0);
            const std::vector<Color> pl = Render(dev, Sep(p));
            SetPair(dev, 0, 1);
            const std::vector<Color> lp = Render(dev, Sep(p));
            check(YEdgeVariation(pl) > 0 && XEdgeVariation(pl) == 0,
                  std::string("G1[") + PathName(p) + "] slot 1 Linear varies DOWN only (x=" +
                  std::to_string(XEdgeVariation(pl)) + ", y=" + std::to_string(YEdgeVariation(pl)) + ")");
            check(XEdgeVariation(lp) > 0 && YEdgeVariation(lp) == 0,
                  std::string("G2[") + PathName(p) + "] slot 0 Linear varies ACROSS only (x=" +
                  std::to_string(XEdgeVariation(lp)) + ", y=" + std::to_string(YEdgeVariation(lp)) + ")");
        }
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // H -- repeated frames
    // ----------------------------------------------------------------------------------------

    void RunH(GraphicsDevice& dev)
    {
        SetPair(dev, 1, 0);
        std::vector<Color> first = Render(dev, Sep());
        bool stable = true;
        for (int i = 0; i < 4; ++i)
        {
            const std::vector<Color> again = Render(dev, Sep());
            if (DiffPixels(first, again) != 0) stable = false;
        }
        check(stable,
              "H1: five consecutive frames under the same sampler pair are byte-identical, so the "
              "answer is a steady state and not a first-frame accident");
        check(YEdgeVariation(first) > 0 && XEdgeVariation(first) == 0,
              "H2: and the steady state is still the correct one");
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // I -- interleaving with another textured effect family
    // ----------------------------------------------------------------------------------------

    void RunI(GraphicsDevice& dev)
    {
        // DualTexture(Point,Linear) | BasicEffect textured | DualTexture(Linear,Point), in ONE
        // frame, in three non-overlapping bands. A sampler that leaks across a family transition
        // shows up as the wrong variation axis in band 3. Each band is kBandH rows so that a whole
        // number of one-texel block rows fits inside it and the two variation oracles stay exact.
        constexpr int kBandH = kRT / 4;
        RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);

        SetPair(dev, 1, 0);
        dev.setViewportProperty(Viewport(0, 0, kRT, kBandH));
        DrawDual(dev, Sep());

        SetPair(dev, 0, 0);
        dev.setViewportProperty(Viewport(0, kBandH, kRT, kBandH));
        DrawBasicTextured(dev, gridA_.get());

        SetPair(dev, 0, 1);
        dev.setViewportProperty(Viewport(0, 2 * kBandH, kRT, kBandH));
        DrawDual(dev, Sep());

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));

        const std::vector<Color> topBand(pix.begin(),
                                         pix.begin() + static_cast<std::ptrdiff_t>(kRT) * kBandH);
        const std::vector<Color> bottomBand(
            pix.begin() + static_cast<std::ptrdiff_t>(kRT) * 2 * kBandH,
            pix.begin() + static_cast<std::ptrdiff_t>(kRT) * 3 * kBandH);
        check(YEdgeVariation(topBand, kRT, kBandH) > 0 && XEdgeVariation(topBand, kRT, kBandH) == 0,
              "I1: the DualTexture draw queued BEFORE another textured family kept its (Point,Linear) "
              "pair (x=" + std::to_string(XEdgeVariation(topBand, kRT, kBandH)) + ", y=" +
              std::to_string(YEdgeVariation(topBand, kRT, kBandH)) + ")");
        check(XEdgeVariation(bottomBand, kRT, kBandH) > 0 &&
              YEdgeVariation(bottomBand, kRT, kBandH) == 0,
              "I2: and the one queued AFTER it kept its own (Linear,Point) pair, so no sampler "
              "leaked across the family transition (x=" +
              std::to_string(XEdgeVariation(bottomBand, kRT, kBandH)) + ", y=" +
              std::to_string(YEdgeVariation(bottomBand, kRT, kBandH)) + ")");
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // J -- the MIPMAP component of each slot's ordinal
    // ----------------------------------------------------------------------------------------

    void RunJ(GraphicsDevice& dev)
    {
        check(mipLevels_ >= 3,
              "J0: the 8x8 mip chains really carry several levels (levels=" +
              std::to_string(mipLevels_) + ")");
        if (mipLevels_ < 3) return;

        // Minify: an 8x8 chain drawn into an 8x8 target forces a lambda of log2(uvMax). uvMax is 3,
        // NOT a power of two, deliberately -- at an integer lambda mip-POINT and mip-LINEAR select
        // and blend the same single level and agree by definition, which would make the mip legs
        // pass on a renderer that ignores the mipmap component entirely.
        Cfg m1 = OnlySlot1(mipB_.get());
        m1.uvMax = 3.0f;
        Cfg m0 = OnlySlot0(mipA_.get());
        m0.uvMax = 3.0f;

        SetPair(dev, 1, 1, TextureAddressMode::Wrap);
        const std::vector<Color> s1Point = Render(dev, m1, kMipRT);
        SetSlot(dev, 1, 4, TextureAddressMode::Wrap);   // PointMipLinear: mag POINT, mip LINEAR
        const std::vector<Color> s1MipLin = Render(dev, m1, kMipRT);
        check(OffPalette(s1Point, mipBSet_) == 0,
              "J1: slot 1 mip-POINT reads ONE stored level of Texture2, byte-exactly (off-stored=" +
              std::to_string(OffPalette(s1Point, mipBSet_)) + ", first=" +
              Str(FirstOffPalette(s1Point, mipBSet_)) + ")");
        check(OffPalette(s1MipLin, mipBSet_) > 0 || DiffPixels(s1Point, s1MipLin) > 0,
              "J2: slot 1 mip-LINEAR gives a different answer, so the MIPMAP component of "
              "SamplerStates[1] reaches Texture2 (off-stored=" +
              std::to_string(OffPalette(s1MipLin, mipBSet_)) + ", differing=" +
              std::to_string(DiffPixels(s1Point, s1MipLin)) + ")");

        SetPair(dev, 1, 1, TextureAddressMode::Wrap);
        const std::vector<Color> s0Point = Render(dev, m0, kMipRT);
        SetSlot(dev, 0, 4, TextureAddressMode::Wrap);
        const std::vector<Color> s0MipLin = Render(dev, m0, kMipRT);
        check(OffPalette(s0Point, mipASet_) == 0,
              "J3: slot 0 mip-POINT reads ONE stored level of Texture, byte-exactly (off-stored=" +
              std::to_string(OffPalette(s0Point, mipASet_)) + ")");
        check(OffPalette(s0MipLin, mipASet_) > 0 || DiffPixels(s0Point, s0MipLin) > 0,
              "J4: slot 0 mip-LINEAR gives a different answer too (off-stored=" +
              std::to_string(OffPalette(s0MipLin, mipASet_)) + ", differing=" +
              std::to_string(DiffPixels(s0Point, s0MipLin)) + ")");

        // Independence of the MIPMAP component: change slot 0's mip term while slot 1 carries the
        // only visible resource.
        SetPair(dev, 1, 1, TextureAddressMode::Wrap);
        const std::vector<Color> base = Render(dev, m1, kMipRT);
        SetSlot(dev, 0, 4, TextureAddressMode::Wrap);
        const std::vector<Color> other = Render(dev, m1, kMipRT);
        check(DiffPixels(base, other) == 0,
              "J5: changing slot 0's MIPMAP component leaves Texture2's contribution byte-identical "
              "(differing=" + std::to_string(DiffPixels(base, other)) + ")");

        // Every non-anisotropic ordinal's declared mip component, on slot 1.
        int mismatches = 0;
        for (int o = 0; o <= 8; ++o)
        {
            if (o == 2) continue;   // anisotropy's mip behaviour is per-device, declared elsewhere
            SetSlot(dev, 0, 1, TextureAddressMode::Wrap);
            SetSlot(dev, 1, o, TextureAddressMode::Wrap);
            const std::vector<Color> pix = Render(dev, m1, kMipRT);
            const bool exactLevel = OffPalette(pix, mipBSet_) == 0;
            if (exactLevel != MipsByPoint(o)) ++mismatches;
        }
        check(mismatches == 0,
              "J6: EVERY non-anisotropic ordinal's declared MIPMAP component is what slot 1 does to "
              "Texture2 -- mip-POINT lands on a stored level, mip-LINEAR blends two (mismatches=" +
              std::to_string(mismatches) + ")");
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // K -- address modes, per slot, independently
    // ----------------------------------------------------------------------------------------

    void RunK(GraphicsDevice& dev)
    {
        // uvMax 2 sweeps the texture twice, so Wrap repeats, Mirror reflects and Clamp smears the
        // edge -- three visibly different answers for the SAME resource.
        Cfg c = Sep();
        c.uvMax = 2.0f;

        SetSlot(dev, 1, 1, TextureAddressMode::Clamp);
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        const std::vector<Color> clampBoth = Render(dev, c);
        SetSlot(dev, 1, 1, TextureAddressMode::Wrap);
        const std::vector<Color> wrap1 = Render(dev, c);
        SetSlot(dev, 1, 1, TextureAddressMode::Mirror);
        const std::vector<Color> mirror1 = Render(dev, c);

        check(DiffPixels(clampBoth, wrap1) > 0,
              "K1: slot 1's own AddressV decides how Texture2 repeats (Clamp vs Wrap differing=" +
              std::to_string(DiffPixels(clampBoth, wrap1)) + ")");
        check(DiffPixels(wrap1, mirror1) > 0,
              "K2: and Mirror differs from Wrap on slot 1 (differing=" +
              std::to_string(DiffPixels(wrap1, mirror1)) + ")");

        // Slot 0's address mode, changed alone, must not alter the y structure Texture2 owns.
        SetSlot(dev, 1, 1, TextureAddressMode::Wrap);
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        const std::vector<Color> a = Render(dev, c);
        SetSlot(dev, 0, 1, TextureAddressMode::Wrap);
        const std::vector<Color> b = Render(dev, c);
        check(DiffPixels(a, b) > 0,
              "K3: slot 0's own AddressU decides how Texture repeats, independently (differing=" +
              std::to_string(DiffPixels(a, b)) + ")");

        // Byte-exact: with a white partner, an address mode on the OTHER slot changes nothing.
        Cfg only1 = OnlySlot1(gridB_.get());
        only1.uvMax = 2.0f;
        SetSlot(dev, 1, 1, TextureAddressMode::Wrap);
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        const std::vector<Color> o1 = Render(dev, only1);
        SetSlot(dev, 0, 1, TextureAddressMode::Mirror);
        const std::vector<Color> o2 = Render(dev, only1);
        check(DiffPixels(o1, o2) == 0,
              "K4: changing slot 0's address mode leaves Texture2's contribution byte-identical "
              "(differing=" + std::to_string(DiffPixels(o1, o2)) + ")");
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // L -- the vertex-colour variant (a SECOND shader module and pipeline)
    // ----------------------------------------------------------------------------------------

    void RunL(GraphicsDevice& dev)
    {
        Cfg vc = Sep();
        vc.vertexColor = true;

        SetPair(dev, 1, 1);
        std::vector<Color> pp;
        try
        {
            pp = Render(dev, vc);
        }
        catch (const std::exception& e)
        {
            skip(std::string("L: this renderer does not implement the vertex-colour DualTexture "
                             "variant (") + e.what() + ")");
            SetPair(dev, 0, 0);
            return;
        }
        SetPair(dev, 1, 0);
        const std::vector<Color> pl = Render(dev, vc);
        SetPair(dev, 0, 1);
        const std::vector<Color> lp = Render(dev, vc);

        std::string why;
        check(BlockUniform(pp, why),
              "L1: the vertex-colour shader, both slots Point -- every block flat. " + why);
        check(YEdgeVariation(pl) > 0 && XEdgeVariation(pl) == 0,
              "L2: it honours slot 1 independently too (x=" + std::to_string(XEdgeVariation(pl)) +
              ", y=" + std::to_string(YEdgeVariation(pl)) + ")");
        check(XEdgeVariation(lp) > 0 && YEdgeVariation(lp) == 0,
              "L3: and slot 0 independently (x=" + std::to_string(XEdgeVariation(lp)) + ", y=" +
              std::to_string(YEdgeVariation(lp)) + ")");
        SetPair(dev, 0, 0);
    }

    // ----------------------------------------------------------------------------------------
    // M -- the documented device default on an unset slot
    // ----------------------------------------------------------------------------------------

    void RunM(GraphicsDevice& dev)
    {
        // Only slot 0 is written; slot 1 keeps whatever the device documents as its default. XNA's
        // documented default is LinearWrap, so the second layer must magnify LINEARLY.
        dev.getSamplerStatesProperty()[1] = SamplerState::LinearWrap;
        SetSlot(dev, 0, 1, TextureAddressMode::Clamp);
        const std::vector<Color> deflt = Render(dev, Sep());
        check(YEdgeVariation(deflt) > 0,
              "M1: the documented device default (LinearWrap) filters Texture2 (y-variation=" +
              std::to_string(YEdgeVariation(deflt)) + ")");
        check(XEdgeVariation(deflt) == 0,
              "M2: and slot 0's explicit Point is still honoured next to it (x-variation=" +
              std::to_string(XEdgeVariation(deflt)) + ")");

        SetSlot(dev, 1, 0, TextureAddressMode::Wrap);
        const std::vector<Color> explicitSame = Render(dev, Sep());
        check(DiffPixels(deflt, explicitSame) == 0,
              "M3: an explicitly assigned LinearWrap on slot 1 reproduces the device default exactly "
              "(differing=" + std::to_string(DiffPixels(deflt, explicitSame)) + ")");
        SetPair(dev, 0, 0);
    }

public:
    DualTextureSlotSamplerContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }

private:
    void Finish()
    {
        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        GraphicsDevice& dev = getGraphicsDeviceProperty();

        std::printf("=== REMED-GFX-172 DualTextureEffect independent slot samplers on %s ===\n",
                    kRendererName);
        std::fflush(stdout);

        palette_ = BuildPalette();
        RunPalette();
        if (static_cast<int>(palette_.size()) != kPaletteSize) { Finish(); return; }

        if (!kRasterizes)
        {
            RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            std::vector<Color> pix(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
            bool threw = false;
            try { rt.GetData(pix.data(), 0, static_cast<int>(pix.size())); }
            catch (const System::Exception&) { threw = true; }
            catch (const std::exception&) { threw = true; }
            check(threw, "Z1: a non-rasterizing renderer rejects the readback instead of fabricating "
                         "a sampled image");
            Finish();
            return;
        }

        try
        {
            BuildTextures(dev);
            SetPair(dev, 1, 1);
            (void)Render(dev, Sep());
        }
        catch (const std::exception& e)
        {
            skip(std::string("A..M: this renderer does not implement DualTextureEffect (") +
                 e.what() + ")");
            Finish();
            return;
        }

        try
        {
            RunA(dev);
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
            RunL(dev);
            RunM(dev);
        }
        catch (const System::Exception& e)
        {
            std::printf("[FAIL] unexpected CNA exception: %s\n", e.what());
            std::fflush(stdout);
            ++totalCount_;
        }
        catch (const std::exception& e)
        {
            std::printf("[FAIL] unexpected exception: %s\n", e.what());
            std::fflush(stdout);
            ++totalCount_;
        }

        Finish();
    }
};

int main()
{
    DualTextureSlotSamplerContractTest game;
    game.Run();
    return game.getResult();
}
