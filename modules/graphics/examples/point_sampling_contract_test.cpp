// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-150: TextureFilter::Point must select exactly ONE texel, and TextureAddressMode must
// decide which one, on every textured draw path.
//
// THE PUBLIC CONTRACT, stated as the arithmetic a conforming renderer performs. A destination pixel
// is sampled at its CENTRE, (x + 0.5, y + 0.5). That centre is mapped back through the sprite's
// (or the primitive's) geometry to a coordinate in TEXEL space, `s`, whose origin is the texture's
// top-left corner and whose unit is one texel. Point filtering then selects
//
//     texel = AddressMode( floor(s), size )
//
// and returns that texel's stored bytes UNCHANGED. Three consequences the legs below all lean on:
//
//   * `floor(s)`, not `floor(s - 0.5)`. `s - 0.5` is the LINEAR path's lower neighbour -- it is
//     where bilinear filtering starts, because it converts a texel-centre-relative position into
//     an index pair. Point filtering must never apply that half-texel shift: a texel COVERS
//     [i, i+1) in texel space, so the texel containing `s` is `floor(s)`.
//   * A coordinate exactly on a texel boundary (`s` integral) selects the HIGHER texel, the one
//     that boundary opens. This is the tie rule; every exactness leg here is deliberately built so
//     no pixel centre lands on a tie (see the TIE GUARD below), so the legs measure the contract
//     rather than the tie.
//   * The address mode is applied to the INTEGER index, after the floor -- not to a normalized
//     coordinate before it. Clamp reaches the first and last texel and blends with nothing;
//     Wrap tiles; Mirror tiles and flips.
//
// THE DEFECT (Software). `SoftwareRenderer::ApplySamplerState(int slot, int, int, int, int)`
// named none of its parameters -- filter, addressU, addressV and maxAnisotropy were all discarded,
// and `SoftwareSpriteBatchRenderer`'s `textureFilter_`/`addressU_`/`addressV_` were dead stores that
// nothing ever read. One function, `SampleBilinear`, served every textured fragment, so every draw
// was LinearClamp regardless of the SamplerState the game selected:
//
//     tx = u * texW - 0.5;  x0 = clamp(floor(tx)); x1 = clamp(x0 + 1); fx = tx - floor(tx);
//     result = lerp(lerp(t[x0,y0], t[x1,y0], fx), lerp(t[x0,y1], t[x1,y1], fx), fy)
//
// Measured on the 8x4 -> 16x8 PointClamp magnification that isolated this (leg C below):
// 4/128 destination pixels correct on SOFTWARE, 128/128 on EASYGL. At destination (2,0) the
// contract selects texel (1,0) = (50,25,190,200); Software returned (42,25,152,213), which is
// exactly 0.75*(50,25,190,200) + 0.25*(20,25,40,255) -- texel (1,0) blended with its left
// neighbour (0,0) at the weight `fx = 0.75` that `u*texW - 0.5` produces there. The interpolation
// is horizontal AND vertical; the only pixels that came out right were the ones where clamping
// happened to collapse both interpolation endpoints onto one texel, i.e. columns 0 and 15 crossed
// with rows 0 and 7 -- 2*2 = the 4 that passed.
//
// THE ORACLE. Every exactness leg compares the COMPLETE destination image, pixel for pixel, against
// an expectation computed from the contract above by inverting the sprite/primitive geometry in
// double precision -- never by sampling representative points, never against a hash, and never with
// a tolerance. A mismatch reports the destination coordinate, the texel the contract selects, that
// texel's RGBA and the RGBA actually produced. Pixels the geometry does not cover must still hold
// the clear sentinel, so a draw that spills or falls short fails too.
//
// TIE GUARD. Before comparing, each exactness leg checks that no covered pixel's texel coordinate
// lands within 1e-3 of an integer. A fixture whose expected values depend on the tie rule would be
// measuring float rounding rather than the contract, so that condition is itself a reported check
// and the leg is not scored if it trips.
//
// PATTERNS are asymmetric by construction: no two rows and no two columns are equal, every texel is
// unique, each carries 0 and 255 channels plus mid-tones, and alpha varies independently of RGB. A
// horizontal mirror, a vertical mirror, a transpose, a 180-degree rotation, an off-by-one texel, a
// uniform fill and a stale buffer therefore all fail distinguishably.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   A  2x2, integer 4x magnification            the minimal magnification case
//   B  3x3, integer 7x magnification            odd texture, odd scale
//   C  8x4 -> 16x8                              the exact case that isolated GFX-150
//   D  non-integer and odd destination scales   3x3 -> 10x10, 8x4 -> 21x13
//   E  1:1                                      point and linear must agree here (and this is
//                                               precisely why a 1:1 fixture cannot see the defect)
//   F  minification                             8x4 -> 4x2
//   G  interior source rectangle                a subregion is addressed, not the whole texture
//   H  1-texel-wide and 1-texel-high source     a degenerate rect must still select that texel
//   I  fractional destination placement         via the SpriteBatch transform's translation
//   J  transform translation and scaling        composition with the sprite's own scale
//   K  SpriteEffects flips                      H, V and both, composing rather than cancelling
//   L  rotation and origin                      180 degrees about the sprite centre
//   M  PointClamp vs PointWrap vs PointMirror   an oversized source rect drives u,v outside [0,1]
//   N  address-mode boundaries                  u,v exactly 0 and 1, just outside, and far outside
//   O  PointClamp vs LinearClamp                the differential -- linear MUST still interpolate
//   P  LinearWrap                               interpolation AND wrapping survive together
//   Q  sampler transitions                      Point -> Linear -> Point restores the exact image,
//                                               and no state leaks across Begin/End
//   R  two textures, two sizes, one batch       per-draw texture dimensions, one sampler
//   S  RenderTarget2D as the SOURCE             classified separately from ordinary Texture2D
//   T  alpha and blend                          alpha 0/128/255 texels, Opaque and AlphaBlend
//   U  3D textured primitive                    the device SamplerStates[0] path, not SpriteBatch,
//                                              and the one leg on INTEGER pixel centres -- XNA
//                                              uses a different convention here than for sprites
//   V  3D wrap                                  UVs beyond [0,1] on the 3D path
//   W  custom viewport                          point selection is unaffected by viewport placement
//   X  the device default sampler                XNA's SamplerStateCollection defaults every slot to
//                                                LinearWrap, so a full-texture quad's far edge blends
//                                                with the WRAPPED texel -- this is what makes the
//                                                address mode observable without any explicit sampler
//   Y  the TextureFilter ordinal table            Anisotropic filters like Linear, and the four
//                                                ordinals that name DIFFERENT minification and
//                                                magnification filters use the right half
//   Z  extreme texture coordinates                a source rectangle far outside the texture, and
//                                                UVs large enough to overflow, must address a real
//                                                texel deterministically rather than crash
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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/Exception.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
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
#elif defined(CNA_RENDERER_LLGL)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "LLGL";
#else
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "UNKNOWN";
#endif

    constexpr int kBBW = 160;
    constexpr int kBBH = 120;

    /// The clear colour of every destination. Deliberately absent from every pattern, so a pixel the
    /// geometry should not cover is distinguishable from one that was sampled.
    const Color kSentinel(13, 17, 19, 255);

    // ---------------------------------------------------------------------------------------------
    // Patterns
    // ---------------------------------------------------------------------------------------------

    struct Pattern
    {
        int w = 0;
        int h = 0;
        std::vector<Color> texels;
        std::string name;

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

    /// 2x2, four distinct colours: opaque red, opaque blue, a mid-tone with mid alpha, and an
    /// opaque yellow-green. No row and no column repeats, and alpha varies independently of RGB.
    Pattern Make2x2()
    {
        return Pattern{2, 2,
            {Color(255, 0, 0, 255),   Color(0, 0, 255, 255),
             Color(90, 140, 30, 128), Color(200, 255, 0, 255)},
            "2x2"};
    }

    /// 3x3 with no symmetric row or column and no repeated texel; centre carries alpha 0 so a
    /// fully transparent texel is addressed like any other.
    Pattern Make3x3()
    {
        return Pattern{3, 3,
            {Color(255,   0,   0, 255), Color(  0, 255,   0, 255), Color(  0,   0, 255, 255),
             Color(255, 255,   0, 200), Color( 96,  96,  96,   0), Color(255,   0, 255, 255),
             Color(  0, 255, 255, 255), Color(130,  40, 210, 255), Color(255, 255, 255,  64)},
            "3x3"};
    }

    /// 3x2, non-square so a transpose cannot masquerade as a pass.
    Pattern Make3x2()
    {
        return Pattern{3, 2,
            {Color(255,   0,   0, 255), Color( 17, 200,  60, 255), Color(  0,   0, 255, 255),
             Color(240, 240,   0, 128), Color(  8,   8,   8, 255), Color(120,   0, 200, 255)},
            "3x2"};
    }

    /// The 8x4 pattern GFX-150 was measured on: every one of its 32 texels is unique, R ramps with
    /// x, G ramps with y, B adds a third asymmetry, A carries four distinct values. Texel (1,0) is
    /// (50,25,190,200) and texel (0,0) is (20,25,40,255) -- the pair whose 0.75/0.25 blend the
    /// pre-fix renderer returned at destination (2,0).
    Pattern Make8x4()
    {
        Pattern p{8, 4, {}, "8x4"};
        p.texels.reserve(32);
        static const int kBlue[8] = {40, 190, 15, 235, 70, 160, 5, 205};
        static const int kAlpha[4] = {255, 200, 128, 64};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x)
                p.texels.emplace_back(static_cast<std::uint8_t>(20 + x * 30),
                                      static_cast<std::uint8_t>(25 + y * 70),
                                      static_cast<std::uint8_t>(kBlue[x] ^ (y * 3)),
                                      static_cast<std::uint8_t>(kAlpha[y]));
        return p;
    }

    /// 4x4 used by the address-mode legs: each texel is unique and the first/last row and column
    /// differ strongly, so Clamp's edge extension, Wrap's tiling and Mirror's reflection produce
    /// visibly different images rather than three similar ones.
    Pattern Make4x4()
    {
        Pattern p{4, 4, {}, "4x4"};
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
    // The contract
    // ---------------------------------------------------------------------------------------------

    /// How far a coordinate is from the nearest texel boundary.
    double BoundaryDistance(double v) { return std::abs(v - std::round(v)); }

    /// The address mode applied to an already-floored integer texel index -- the second half of
    /// `AddressMode(floor(s), size)`. `long long` because an oversized source rectangle or an
    /// extreme texture coordinate can legitimately produce an index far outside the texture.
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

    /// The complete geometry of one SpriteBatch draw, in the same terms `SpriteBatch::Draw` takes
    /// them, plus the `SpriteBatch.Begin` transform. The expectation below inverts exactly this.
    struct SpriteGeom
    {
        Rectangle dest{0, 0, 0, 0};
        Rectangle src{0, 0, 0, 0};
        bool flipH = false;
        bool flipV = false;
        float rotation = 0.0f;
        Vector2 origin{0.0f, 0.0f};
        // SpriteBatch.Begin's transformMatrix, restricted to the 2D scale+translate it is used with
        // here (a general matrix would need a general inverse; these legs do not need one).
        double transScaleX = 1.0;
        double transScaleY = 1.0;
        double transTx = 0.0;
        double transTy = 0.0;
        int viewportX = 0;
        int viewportY = 0;
    };

    /// Where destination pixel (x, y) reads from, in TEXEL space, or "not covered".
    ///
    /// Inverts the sprite construction: screen -> undo the viewport origin -> undo the transform ->
    /// undo the destination translation -> undo the rotation -> undo the destination/source scale ->
    /// re-apply the origin -> source-local texels -> texture-space texels (honouring the flips).
    struct SourceCoord
    {
        bool covered = false;
        double sx = 0.0;
        double sy = 0.0;
    };

    SourceCoord SpriteSourceCoord(const SpriteGeom& g, int x, int y)
    {
        const double sw = std::max(1, g.src.Width);
        const double sh = std::max(1, g.src.Height);
        const double scaleX = static_cast<double>(g.dest.Width) / sw;
        const double scaleY = static_cast<double>(g.dest.Height) / sh;
        if (scaleX == 0.0 || scaleY == 0.0) return {};

        double px = static_cast<double>(x) + 0.5 - static_cast<double>(g.viewportX);
        double py = static_cast<double>(y) + 0.5 - static_cast<double>(g.viewportY);

        // Undo SpriteBatch.Begin's transformMatrix.
        px = (px - g.transTx) / g.transScaleX;
        py = (py - g.transTy) / g.transScaleY;

        // Undo the destination translation, then the rotation.
        const double rx = px - static_cast<double>(g.dest.X);
        const double ry = py - static_cast<double>(g.dest.Y);
        const double c = std::cos(static_cast<double>(g.rotation));
        const double s = std::sin(static_cast<double>(g.rotation));
        const double ex = rx * c + ry * s;
        const double ey = -rx * s + ry * c;

        // Undo the destination/source scale and re-apply the origin, giving source-local texels.
        const double lx = ex / scaleX + static_cast<double>(g.origin.X);
        const double ly = ey / scaleY + static_cast<double>(g.origin.Y);

        if (lx < 0.0 || lx > sw || ly < 0.0 || ly > sh) return {};

        SourceCoord out;
        out.covered = true;
        out.sx = g.flipH ? (static_cast<double>(g.src.X) + sw - lx) : (static_cast<double>(g.src.X) + lx);
        out.sy = g.flipV ? (static_cast<double>(g.src.Y) + sh - ly) : (static_cast<double>(g.src.Y) + ly);
        return out;
    }

    /// The point-filtered texel the contract selects for a texel-space coordinate.
    void PointTexel(double sx, double sy, int texW, int texH,
                    TextureAddressMode addrU, TextureAddressMode addrV, int& tx, int& ty)
    {
        tx = AddressTexel(static_cast<long long>(std::floor(sx)), texW, addrU);
        ty = AddressTexel(static_cast<long long>(std::floor(sy)), texH, addrV);
    }

    /// How close to a texel boundary a coordinate may land before the two texels it separates become
    /// indistinguishable. The contract's tie rule (an integral coordinate selects the HIGHER texel)
    /// is exact in real arithmetic, but a renderer interpolates u,v in single precision across the
    /// primitive, so at an exact boundary either neighbour is a legitimate answer. Anything that is
    /// neither -- a blend -- still fails.
    constexpr double kTieEpsilon = 1e-4;

    /// The texel indices admissible for a coordinate: one, or two when it sits on a boundary.
    struct Admissible
    {
        int count = 1;
        int idx[2] = {0, 0};
    };

    Admissible AdmissibleTexels(double s, int size, TextureAddressMode mode)
    {
        Admissible a;
        const long long lo = static_cast<long long>(std::floor(s));
        a.idx[0] = AddressTexel(lo, size, mode);
        if (BoundaryDistance(s) < kTieEpsilon)
        {
            const long long other = (s - std::floor(s) < 0.5) ? (lo - 1) : (lo + 1);
            const int mapped = AddressTexel(other, size, mode);
            if (mapped != a.idx[0]) { a.idx[1] = mapped; a.count = 2; }
        }
        return a;
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

}

class PointSamplingContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    Pattern p2x2_ = Make2x2();
    Pattern p3x3_ = Make3x3();
    Pattern p3x2_ = Make3x2();
    Pattern p8x4_ = Make8x4();
    Pattern p4x4_ = Make4x4();

    Texture2D t2x2_, t3x3_, t3x2_, t8x4_, t4x4_;

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

    static SamplerState MakeSampler(TextureFilter filter, TextureAddressMode u, TextureAddressMode v)
    {
        SamplerState s;
        s.setFilterProperty(filter);
        s.setAddressUProperty(u);
        s.setAddressVProperty(v);
        return s;
    }

    /// Renders one sprite into a freshly cleared target and returns the whole target.
    std::vector<Color> RenderSprite(GraphicsDevice& dev, RenderTarget2D& rt, int rtW, int rtH,
                                    const Texture2D& tex, const SpriteGeom& g,
                                    SamplerState sampler, const Color& tint,
                                    BlendState blend = BlendState::Opaque)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev, rtW, rtH);
        dev.Clear(kSentinel);
        if (g.viewportX != 0 || g.viewportY != 0)
            dev.setViewportProperty(Viewport(g.viewportX, g.viewportY,
                                             rtW - g.viewportX, rtH - g.viewportY));

        SpriteEffects fx = SpriteEffects::None;
        if (g.flipH) fx = static_cast<SpriteEffects>(static_cast<int>(fx) |
                                                     static_cast<int>(SpriteEffects::FlipHorizontally));
        if (g.flipV) fx = static_cast<SpriteEffects>(static_cast<int>(fx) |
                                                     static_cast<int>(SpriteEffects::FlipVertically));

        Matrix m = Matrix::CreateScale(static_cast<float>(g.transScaleX),
                                       static_cast<float>(g.transScaleY), 1.0f) *
                   Matrix::CreateTranslation(static_cast<float>(g.transTx),
                                             static_cast<float>(g.transTy), 0.0f);

        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Deferred, blend, &sampler, nullptr, nullptr, nullptr, m);
        sb.Draw(tex, g.dest, g.src, tint, g.rotation, g.origin, fx, 0.0f);
        sb.End();

        ResetState(dev, rtW, rtH);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);

        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    /// The full-image exactness comparison. Returns the number of mismatching pixels and prints the
    /// first few with the destination coordinate, the selected texel, its RGBA and the actual RGBA.
    int CompareExact(const std::vector<Color>& pix, int rtW, int rtH,
                     const Pattern& pat, const SpriteGeom& g,
                     TextureAddressMode addrU, TextureAddressMode addrV,
                     const std::string& label, int& coveredOut, int& tiesOut)
    {
        int mismatches = 0;
        int covered = 0;
        int ties = 0;
        int reported = 0;
        for (int y = 0; y < rtH; ++y)
        {
            for (int x = 0; x < rtW; ++x)
            {
                const Color& actual = pix[static_cast<std::size_t>(y) * static_cast<std::size_t>(rtW) +
                                          static_cast<std::size_t>(x)];
                const SourceCoord sc = SpriteSourceCoord(g, x, y);
                if (!sc.covered)
                {
                    if (!SameColor(actual, kSentinel))
                    {
                        ++mismatches;
                        if (reported < 6)
                        {
                            ++reported;
                            std::printf("        %s dest(%d,%d) NOT COVERED expected sentinel %s actual %s\n",
                                        label.c_str(), x, y, Str(kSentinel).c_str(), Str(actual).c_str());
                        }
                    }
                    continue;
                }
                ++covered;
                const Admissible ax = AdmissibleTexels(sc.sx, pat.w, addrU);
                const Admissible ay = AdmissibleTexels(sc.sy, pat.h, addrV);
                if (ax.count > 1 || ay.count > 1) ++ties;
                bool ok = false;
                for (int i = 0; i < ax.count && !ok; ++i)
                    for (int j = 0; j < ay.count && !ok; ++j)
                        if (SameColor(actual, pat.At(ax.idx[i], ay.idx[j]))) ok = true;
                if (!ok)
                {
                    ++mismatches;
                    if (reported < 6)
                    {
                        ++reported;
                        std::printf("        %s dest(%d,%d) s=(%.4f,%.4f) texel(%d,%d) expected %s actual %s\n",
                                    label.c_str(), x, y, sc.sx, sc.sy, ax.idx[0], ay.idx[0],
                                    Str(pat.At(ax.idx[0], ay.idx[0])).c_str(), Str(actual).c_str());
                    }
                }
            }
        }
        coveredOut = covered;
        tiesOut = ties;
        return mismatches;
    }

    /// One complete exactness leg: draw, guard against ties, compare every pixel.
    void ExactLeg(GraphicsDevice& dev, const std::string& label, const Pattern& pat,
                  const Texture2D& tex, int rtW, int rtH, const SpriteGeom& g,
                  TextureAddressMode addrU = TextureAddressMode::Clamp,
                  TextureAddressMode addrV = TextureAddressMode::Clamp,
                  bool requireNoTies = true)
    {
        RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const std::vector<Color> pix =
            RenderSprite(dev, rt, rtW, rtH, tex, g, MakeSampler(TextureFilter::Point, addrU, addrV),
                         Color(255, 255, 255, 255));

        int covered = 0;
        int ties = 0;
        const int bad = CompareExact(pix, rtW, rtH, pat, g, addrU, addrV, label, covered, ties);

        check(covered > 0, label + ": geometry covers destination pixels (covered=" +
                           std::to_string(covered) + ")");
        if (requireNoTies)
            check(ties == 0,
                  label + ": TIE GUARD -- no pixel centre lands on a texel boundary, so the leg "
                  "measures the contract rather than the tie rule (ties=" + std::to_string(ties) + ")");
        else
            note(label + ": " + std::to_string(ties) + " of " + std::to_string(covered) +
                 " covered pixels sit on a texel boundary; at those the two neighbouring texels are "
                 "both accepted, a blend of them is not");
        check(bad == 0, label + ": every one of the " + std::to_string(rtW * rtH) +
                        " destination pixels equals the point-selected texel (mismatches=" +
                        std::to_string(bad) + ")");
    }

    /// Draws a full-viewport textured 3D quad (BasicEffect + VertexPositionTexture), so the device
    /// SamplerStates[0] path is measured instead of SpriteBatch's own sampler.
    std::vector<Color> Render3DQuad(GraphicsDevice& dev, RenderTarget2D& rt, int rtW, int rtH,
                                    Texture2D& tex, float uMax, float vMax, SamplerState sampler)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev, rtW, rtH);
        // A raw DrawUserPrimitives runs under whatever device state is current, and the previous
        // SpriteBatch::Begin left CullCounterClockwise behind -- which culls this quad's winding
        // (REMED-GFX-160: XNA's front face is clockwise AS DISPLAYED). Without this the leg would
        // draw nothing and its "no interpolated colour" companion check would pass vacuously.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(kSentinel);

        const std::array<VertexPositionTexture, 6> quad{
            VertexPositionTexture(Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, vMax)),
            VertexPositionTexture(Vector3( 1.0f, -1.0f, 0.0f), Vector2(uMax, vMax)),
            VertexPositionTexture(Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3( 1.0f, -1.0f, 0.0f), Vector2(uMax, vMax)),
            VertexPositionTexture(Vector3( 1.0f,  1.0f, 0.0f), Vector2(uMax, 0.0f)),
        };

        dev.getSamplerStatesProperty()[0] = sampler;
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.Apply();
        dev.SetVertexBuffer(nullptr);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);
        dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;

        std::vector<Color> pix(static_cast<std::size_t>(rtW) * static_cast<std::size_t>(rtH),
                               Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    /// Exactness for the 3D quad: the quad fills the viewport, so destination pixel x reads texel
    /// coordinate (x + 0.5) / rtW * uMax * texW.
    /// @param tieFree Whether this leg's magnification keeps every pixel centre off a texel
    ///                boundary. Only a NON-INTEGER magnification can, once the sample point is
    ///                XNA's integer pixel centre: at an exact 2x, `x/rtW * texw` is a whole number
    ///                for every other x, so the boundary is where the centres land by construction.
    ///                A tie-free leg measures the contract; a tied one measures that the tie is
    ///                resolved to one of the two admissible texels, which is still a real check.
    void Exact3DLeg(GraphicsDevice& dev, const std::string& label, const Pattern& pat,
                    Texture2D& tex, int rtW, int rtH, float uMax, float vMax,
                    TextureAddressMode addrU, TextureAddressMode addrV, bool tieFree)
    {
        RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const std::vector<Color> pix =
            Render3DQuad(dev, rt, rtW, rtH, tex, uMax, vMax,
                         MakeSampler(TextureFilter::Point, addrU, addrV));

        int bad = 0;
        int ties = 0;
        int reported = 0;
        for (int y = 0; y < rtH; ++y)
        {
            for (int x = 0; x < rtW; ++x)
            {
                // The 3D path addresses pixel centres at INTEGER window coordinates, which is
                // Direct3D 9's convention and so XNA's. The SpriteBatch legs above land on
                // half-integers instead, because XNA's SpriteEffect offsets by half a pixel and
                // CNA's sprite path reproduces that. Both were measured on the real XNA 4.0
                // runtime at this exact magnification: the 3D quad agrees with integer centres
                // 100/100 and with half-integer 81/100, and SpriteBatch agrees the other way
                // round, also 100/100. See spikes/xna-pixel-center-spike/ and REMED-GFX-238.
                //
                // Do not "simplify" this back to (x + 0.5): that is the OpenGL/Direct3D 10
                // convention, it is what this leg used to assert, and asserting it here marks
                // XNA-correct output wrong on exactly the pixels where the two disagree.
                const double sx = static_cast<double>(x) / rtW * uMax * pat.w;
                const double sy = static_cast<double>(y) / rtH * vMax * pat.h;
                const Admissible ax = AdmissibleTexels(sx, pat.w, addrU);
                const Admissible ay = AdmissibleTexels(sy, pat.h, addrV);
                if (ax.count > 1 || ay.count > 1) ++ties;
                const Color& actual = pix[static_cast<std::size_t>(y) * static_cast<std::size_t>(rtW) +
                                          static_cast<std::size_t>(x)];
                bool ok = false;
                for (int i = 0; i < ax.count && !ok; ++i)
                    for (int j = 0; j < ay.count && !ok; ++j)
                        if (SameColor(actual, pat.At(ax.idx[i], ay.idx[j]))) ok = true;
                if (!ok)
                {
                    ++bad;
                    if (reported < 6)
                    {
                        ++reported;
                        std::printf("        %s dest(%d,%d) s=(%.4f,%.4f) texel(%d,%d) expected %s actual %s\n",
                                    label.c_str(), x, y, sx, sy, ax.idx[0], ay.idx[0],
                                    Str(pat.At(ax.idx[0], ay.idx[0])).c_str(), Str(actual).c_str());
                    }
                }
            }
        }
        if (tieFree)
        {
            check(ties == 0, label + ": TIE GUARD -- no pixel centre lands on a texel boundary, so "
                             "the leg measures the contract rather than the tie rule (ties=" +
                                 std::to_string(ties) + ")");
        }
        else
        {
            // Asserted rather than merely reported: if the sample point is ever moved back to the
            // half-integer (OpenGL) convention this drops to zero, so the leg notices the reversion
            // instead of quietly continuing to pass. REMED-GFX-238.
            check(ties > 0, label + ": TIE GUARD -- an integer magnification is structurally tied "
                            "under XNA's integer pixel centres, and this leg checks the tie resolves "
                            "to an admissible texel (ties=" + std::to_string(ties) + ")");
        }
        check(bad == 0, label + ": every one of the " + std::to_string(rtW * rtH) +
                        " destination pixels equals the point-selected texel (mismatches=" +
                        std::to_string(bad) + ")");
    }

    /// True when every pixel of @p pix is byte-identical to some texel of @p pat (or the sentinel) --
    /// i.e. nothing was blended into existence. This is what separates point filtering from linear
    /// filtering without assuming a particular interpolation weight.
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

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        t2x2_ = Texture2D::CreateFromPixels(dev, p2x2_.w, p2x2_.h, p2x2_.Bytes());
        t3x3_ = Texture2D::CreateFromPixels(dev, p3x3_.w, p3x3_.h, p3x3_.Bytes());
        t3x2_ = Texture2D::CreateFromPixels(dev, p3x2_.w, p3x2_.h, p3x2_.Bytes());
        t8x4_ = Texture2D::CreateFromPixels(dev, p8x4_.w, p8x4_.h, p8x4_.Bytes());
        t4x4_ = Texture2D::CreateFromPixels(dev, p4x4_.w, p4x4_.h, p4x4_.Bytes());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        std::printf("=== REMED-GFX-150 point-sampling contract on %s ===\n", kRendererName);

        if (!kRasterizes)
        {
            RunNonRasterizing(dev);
            std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
            result_ = (passCount_ == totalCount_) ? 0 : 1;
            Exit();
            return;
        }

        RunA_IntegerMagnification(dev);
        RunB_OddIntegerMagnification(dev);
        RunC_TicketCase(dev);
        RunD_NonIntegerScale(dev);
        RunE_OneToOne(dev);
        RunF_Minification(dev);
        RunG_SourceRectangle(dev);
        RunH_DegenerateSourceRects(dev);
        RunI_FractionalPlacement(dev);
        RunJ_Transform(dev);
        RunK_Flips(dev);
        RunL_RotationOrigin(dev);
        RunM_AddressModes(dev);
        RunN_AddressBoundaries(dev);
        RunO_PointVsLinear(dev);
        RunP_LinearWrap(dev);
        RunQ_SamplerTransitions(dev);
        RunR_TwoTexturesOneBatch(dev);
        RunS_RenderTargetSource(dev);
        RunT_AlphaAndBlend(dev);
        RunU_3DPath(dev);
        RunV_3DWrap(dev);
        RunW_CustomViewport(dev);
        RunX_DefaultSamplerIsLinearWrap(dev);
        RunY_FilterOrdinals(dev);
        RunZ_ExtremeCoordinates(dev);

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

private:
    void RunNonRasterizing(GraphicsDevice& dev)
    {
        // HEADLESS performs no rasterization, so REMED-GFX-127's contract makes the readback reject
        // deterministically. There is no sampled pixel to measure; the honest assertion is that the
        // request is refused rather than fabricated.
        RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        std::vector<Color> pix(64, Color(0, 0, 0, 0));
        bool threw = false;
        try { rt.GetData(pix.data(), 0, static_cast<int>(pix.size())); }
        catch (const System::Exception&) { threw = true; }
        catch (const std::exception&) { threw = true; }
        check(threw, "Z1: a non-rasterizing renderer rejects the readback instead of fabricating a "
                     "sampled image");
    }

    SpriteGeom Geom(const Rectangle& dest, const Rectangle& src)
    {
        SpriteGeom g;
        g.dest = dest;
        g.src = src;
        return g;
    }

    // A -----------------------------------------------------------------------------------------
    void RunA_IntegerMagnification(GraphicsDevice& dev)
    {
        // 2x2 -> 8x8: each texel must become an exact 4x4 block of one colour.
        const SpriteGeom g = Geom(Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2));
        ExactLeg(dev, "A1 2x2 magnified 4x", p2x2_, t2x2_, 8, 8, g);

        // The block structure itself, stated independently of the per-pixel comparison: exactly four
        // distinct colours, 16 pixels each.
        RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const std::vector<Color> pix = RenderSprite(dev, rt, 8, 8, t2x2_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp),
            Color(255, 255, 255, 255));
        int counts[4] = {0, 0, 0, 0};
        for (const Color& c : pix)
            for (int i = 0; i < 4; ++i)
                if (SameColor(c, p2x2_.texels[static_cast<std::size_t>(i)])) ++counts[i];
        check(counts[0] == 16 && counts[1] == 16 && counts[2] == 16 && counts[3] == 16,
              "A2: each of the four texels covers exactly a 16-pixel block (" +
              std::to_string(counts[0]) + "/" + std::to_string(counts[1]) + "/" +
              std::to_string(counts[2]) + "/" + std::to_string(counts[3]) + ")");
        check(CountForeignColors(pix, p2x2_) == 0,
              "A3: no colour outside the texture's own palette was produced (point filtering "
              "invents no value)");
    }

    // B -----------------------------------------------------------------------------------------
    void RunB_OddIntegerMagnification(GraphicsDevice& dev)
    {
        const SpriteGeom g = Geom(Rectangle(0, 0, 21, 21), Rectangle(0, 0, 3, 3));
        ExactLeg(dev, "B1 3x3 magnified 7x", p3x3_, t3x3_, 21, 21, g);

        const SpriteGeom g2 = Geom(Rectangle(0, 0, 15, 10), Rectangle(0, 0, 3, 2));
        ExactLeg(dev, "B2 3x2 magnified 5x", p3x2_, t3x2_, 15, 10, g2);
    }

    // C -----------------------------------------------------------------------------------------
    void RunC_TicketCase(GraphicsDevice& dev)
    {
        const SpriteGeom g = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        ExactLeg(dev, "C1 8x4 -> 16x8 (the GFX-150 case)", p8x4_, t8x4_, 16, 8, g);

        // The single pixel the ticket recorded, named explicitly so a regression reports the same
        // coordinate the finding did.
        RenderTarget2D rt(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const std::vector<Color> pix = RenderSprite(dev, rt, 16, 8, t8x4_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp),
            Color(255, 255, 255, 255));
        const Color& at20 = pix[2];
        const Color expected = p8x4_.At(1, 0);
        check(SameColor(at20, expected),
              "C2: destination (2,0) is texel (1,0) " + Str(expected) + ", not a blend with texel "
              "(0,0) " + Str(p8x4_.At(0, 0)) + " -- actual " + Str(at20));
        check(CountForeignColors(pix, p8x4_) == 0,
              "C3: the whole 16x8 image is drawn from the texture's own 32 texels, with no "
              "interpolated colour anywhere");
    }

    // D -----------------------------------------------------------------------------------------
    void RunD_NonIntegerScale(GraphicsDevice& dev)
    {
        ExactLeg(dev, "D1 3x3 -> 10x10 (non-integer 3.33x)", p3x3_, t3x3_, 10, 10,
                 Geom(Rectangle(0, 0, 10, 10), Rectangle(0, 0, 3, 3)));
        ExactLeg(dev, "D2 8x4 -> 21x13 (odd, non-integer, different per axis)", p8x4_, t8x4_, 21, 13,
                 Geom(Rectangle(0, 0, 21, 13), Rectangle(0, 0, 8, 4)),
                 TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
        ExactLeg(dev, "D3 3x2 -> 23x9 within a larger target", p3x2_, t3x2_, 32, 16,
                 Geom(Rectangle(4, 3, 23, 9), Rectangle(0, 0, 3, 2)),
                 TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
    }

    // E -----------------------------------------------------------------------------------------
    void RunE_OneToOne(GraphicsDevice& dev)
    {
        const SpriteGeom g = Geom(Rectangle(0, 0, 8, 4), Rectangle(0, 0, 8, 4));
        ExactLeg(dev, "E1 8x4 at 1:1", p8x4_, t8x4_, 8, 4, g);

        // At 1:1 every pixel centre lands on a texel centre, so point and linear agree exactly.
        // That is precisely why a 1:1 fixture cannot see GFX-150 -- recorded here as a measured
        // fact rather than left as an assumption.
        RenderTarget2D rtP(dev, 8, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        RenderTarget2D rtL(dev, 8, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        const std::vector<Color> point = RenderSprite(dev, rtP, 8, 4, t8x4_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp),
            Color(255, 255, 255, 255));
        const std::vector<Color> linear = RenderSprite(dev, rtL, 8, 4, t8x4_, g,
            MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp, TextureAddressMode::Clamp),
            Color(255, 255, 255, 255));
        int same = 0;
        for (std::size_t i = 0; i < point.size(); ++i)
            if (SameColor(point[i], linear[i])) ++same;
        check(same == static_cast<int>(point.size()),
              "E2: at 1:1 Point and Linear produce the identical image (" + std::to_string(same) +
              "/" + std::to_string(point.size()) + ") -- a 1:1 fixture is blind to this defect");
    }

    // F -----------------------------------------------------------------------------------------
    void RunF_Minification(GraphicsDevice& dev)
    {
        // An exact integer minification puts EVERY pixel centre on a texel boundary (s = 2x+1), so
        // this leg is scored with the tie rule rather than against it.
        ExactLeg(dev, "F1 8x4 -> 4x2 (2x minification)", p8x4_, t8x4_, 4, 2,
                 Geom(Rectangle(0, 0, 4, 2), Rectangle(0, 0, 8, 4)),
                 TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
        ExactLeg(dev, "F2 3x3 -> 2x2 (non-integer minification)", p3x3_, t3x3_, 2, 2,
                 Geom(Rectangle(0, 0, 2, 2), Rectangle(0, 0, 3, 3)));
    }

    // G -----------------------------------------------------------------------------------------
    void RunG_SourceRectangle(GraphicsDevice& dev)
    {
        ExactLeg(dev, "G1 interior source rect (2,1,4,2) of 8x4 magnified 5x", p8x4_, t8x4_, 20, 10,
                 Geom(Rectangle(0, 0, 20, 10), Rectangle(2, 1, 4, 2)));
        ExactLeg(dev, "G2 source rect touching the far edge (5,2,3,2)", p8x4_, t8x4_, 21, 14,
                 Geom(Rectangle(0, 0, 21, 14), Rectangle(5, 2, 3, 2)));
        ExactLeg(dev, "G3 interior source rect at non-integer scale", p3x3_, t3x3_, 13, 13,
                 Geom(Rectangle(0, 0, 13, 13), Rectangle(1, 1, 2, 2)),
                 TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
    }

    // H -----------------------------------------------------------------------------------------
    void RunH_DegenerateSourceRects(GraphicsDevice& dev)
    {
        ExactLeg(dev, "H1 one-texel-wide source rect (3,0,1,4)", p8x4_, t8x4_, 9, 12,
                 Geom(Rectangle(0, 0, 9, 12), Rectangle(3, 0, 1, 4)));
        ExactLeg(dev, "H2 one-texel-high source rect (0,2,8,1)", p8x4_, t8x4_, 24, 7,
                 Geom(Rectangle(0, 0, 24, 7), Rectangle(0, 2, 8, 1)));
        ExactLeg(dev, "H3 single-texel source rect (6,3,1,1)", p8x4_, t8x4_, 11, 9,
                 Geom(Rectangle(0, 0, 11, 9), Rectangle(6, 3, 1, 1)));
    }

    // I -----------------------------------------------------------------------------------------
    void RunI_FractionalPlacement(GraphicsDevice& dev)
    {
        // SpriteBatch's destination rectangle is integral, so fractional placement is expressed the
        // way a game expresses it: through Begin's transform. 0.25/0.75 keep every pixel centre off
        // a texel boundary (the tie guard proves it).
        SpriteGeom g = Geom(Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2));
        g.transTx = 0.25;
        g.transTy = 0.75;
        ExactLeg(dev, "I1 fractional placement (+0.25,+0.75)", p2x2_, t2x2_, 12, 12, g);

        SpriteGeom g2 = Geom(Rectangle(2, 1, 21, 13), Rectangle(0, 0, 8, 4));
        g2.transTx = 0.375;
        g2.transTy = 0.125;
        ExactLeg(dev, "I2 fractional placement of a non-integer scale", p8x4_, t8x4_, 28, 18, g2,
                 TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
    }

    // J -----------------------------------------------------------------------------------------
    void RunJ_Transform(GraphicsDevice& dev)
    {
        SpriteGeom g = Geom(Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2));
        g.transScaleX = 2.0;
        g.transScaleY = 2.0;
        ExactLeg(dev, "J1 transform scale 2x on top of a 4x sprite scale", p2x2_, t2x2_, 16, 16, g);

        SpriteGeom g2 = Geom(Rectangle(1, 2, 9, 6), Rectangle(0, 0, 3, 2));
        g2.transScaleX = 1.5;
        g2.transScaleY = 2.5;
        g2.transTx = 3.0;
        g2.transTy = 1.0;
        ExactLeg(dev, "J2 anisotropic transform scale + translation", p3x2_, t3x2_, 28, 20, g2,
                 TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
    }

    // K -----------------------------------------------------------------------------------------
    void RunK_Flips(GraphicsDevice& dev)
    {
        SpriteGeom h = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        h.flipH = true;
        ExactLeg(dev, "K1 FlipHorizontally", p8x4_, t8x4_, 16, 8, h);

        SpriteGeom v = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        v.flipV = true;
        ExactLeg(dev, "K2 FlipVertically", p8x4_, t8x4_, 16, 8, v);

        SpriteGeom b = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        b.flipH = true;
        b.flipV = true;
        ExactLeg(dev, "K3 both flips compose (not cancel)", p8x4_, t8x4_, 16, 8, b);

        SpriteGeom sh = Geom(Rectangle(0, 0, 20, 10), Rectangle(2, 1, 4, 2));
        sh.flipH = true;
        ExactLeg(dev, "K4 FlipHorizontally with an interior source rect", p8x4_, t8x4_, 20, 10, sh);
    }

    // L -----------------------------------------------------------------------------------------
    void RunL_RotationOrigin(GraphicsDevice& dev)
    {
        // 180 degrees about the source-rect centre: deterministic, keeps the quad axis-aligned, and
        // lands it back on the pixel grid, so point selection stays exactly measurable.
        SpriteGeom g = Geom(Rectangle(8, 4, 16, 8), Rectangle(0, 0, 8, 4));
        g.rotation = 3.14159265358979323846f;
        g.origin = Vector2(4.0f, 2.0f);
        ExactLeg(dev, "L1 rotation 180 degrees about the sprite centre", p8x4_, t8x4_, 32, 16, g);

        // Origin alone (no rotation) shifts the destination by origin*scale.
        SpriteGeom o = Geom(Rectangle(10, 6, 16, 8), Rectangle(0, 0, 8, 4));
        o.origin = Vector2(2.0f, 1.0f);
        ExactLeg(dev, "L2 origin offset without rotation", p8x4_, t8x4_, 32, 16, o);
    }

    // M -----------------------------------------------------------------------------------------
    void RunM_AddressModes(GraphicsDevice& dev)
    {
        // An oversized source rectangle is the public way to drive u,v outside [0,1]: the 4x4
        // texture is asked for an 8x8 region, so texel indices 0..7 are produced on both axes and
        // the address mode decides what 4..7 mean.
        const SpriteGeom g = Geom(Rectangle(0, 0, 16, 16), Rectangle(0, 0, 8, 8));
        ExactLeg(dev, "M1 PointClamp with an oversized source rect (edge extension)", p4x4_, t4x4_,
                 16, 16, g, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
        ExactLeg(dev, "M2 PointWrap with an oversized source rect (tiling)", p4x4_, t4x4_, 16, 16, g,
                 TextureAddressMode::Wrap, TextureAddressMode::Wrap);
        ExactLeg(dev, "M3 PointMirror with an oversized source rect (reflection)", p4x4_, t4x4_,
                 16, 16, g, TextureAddressMode::Mirror, TextureAddressMode::Mirror);
        ExactLeg(dev, "M4 mixed address modes (U wraps, V clamps)", p4x4_, t4x4_, 16, 16, g,
                 TextureAddressMode::Wrap, TextureAddressMode::Clamp);

        // The three modes must genuinely differ, so "all three passed" cannot mean "all three were
        // clamped".
        RenderTarget2D rtC(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        RenderTarget2D rtW(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        RenderTarget2D rtM(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        const Color white(255, 255, 255, 255);
        const std::vector<Color> clamp = RenderSprite(dev, rtC, 16, 16, t4x4_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp), white);
        const std::vector<Color> wrap = RenderSprite(dev, rtW, 16, 16, t4x4_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Wrap, TextureAddressMode::Wrap), white);
        const std::vector<Color> mirror = RenderSprite(dev, rtM, 16, 16, t4x4_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Mirror, TextureAddressMode::Mirror), white);
        int cw = 0, cm = 0, wm = 0;
        for (std::size_t i = 0; i < clamp.size(); ++i)
        {
            if (!SameColor(clamp[i], wrap[i])) ++cw;
            if (!SameColor(clamp[i], mirror[i])) ++cm;
            if (!SameColor(wrap[i], mirror[i])) ++wm;
        }
        check(cw > 0 && cm > 0 && wm > 0,
              "M5: Clamp, Wrap and Mirror are three genuinely different images (differing pixels "
              "C/W=" + std::to_string(cw) + " C/M=" + std::to_string(cm) + " W/M=" +
              std::to_string(wm) + ")");
    }

    // N -----------------------------------------------------------------------------------------
    void RunN_AddressBoundaries(GraphicsDevice& dev)
    {
        // Coordinates far outside [0,1] on both sides, including negative ones.
        ExactLeg(dev, "N1 PointWrap over a source rect starting at -8", p4x4_, t4x4_, 32, 32,
                 Geom(Rectangle(0, 0, 32, 32), Rectangle(-8, -8, 16, 16)),
                 TextureAddressMode::Wrap, TextureAddressMode::Wrap);
        ExactLeg(dev, "N2 PointClamp over a source rect starting at -8", p4x4_, t4x4_, 32, 32,
                 Geom(Rectangle(0, 0, 32, 32), Rectangle(-8, -8, 16, 16)),
                 TextureAddressMode::Clamp, TextureAddressMode::Clamp);
        ExactLeg(dev, "N3 PointMirror over a source rect starting at -8", p4x4_, t4x4_, 32, 32,
                 Geom(Rectangle(0, 0, 32, 32), Rectangle(-8, -8, 16, 16)),
                 TextureAddressMode::Mirror, TextureAddressMode::Mirror);

        // Clamp must be able to reach BOTH the first and the last texel, and must never mix in a
        // border colour that does not exist in the texture.
        RenderTarget2D rt(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const SpriteGeom g = Geom(Rectangle(0, 0, 16, 16), Rectangle(-4, -4, 12, 12));
        const std::vector<Color> pix = RenderSprite(dev, rt, 16, 16, t4x4_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp),
            Color(255, 255, 255, 255));
        bool sawFirst = false, sawLast = false;
        for (const Color& c : pix)
        {
            if (SameColor(c, p4x4_.At(0, 0))) sawFirst = true;
            if (SameColor(c, p4x4_.At(3, 3))) sawLast = true;
        }
        check(sawFirst && sawLast,
              "N4: Clamp reaches both the first texel (0,0) and the last texel (3,3)");
        check(CountForeignColors(pix, p4x4_) == 0,
              "N5: Clamp introduces no border colour that is not a texel of the texture");
    }

    // O -----------------------------------------------------------------------------------------
    void RunO_PointVsLinear(GraphicsDevice& dev)
    {
        const SpriteGeom g = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        const Color white(255, 255, 255, 255);
        RenderTarget2D rtP(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        RenderTarget2D rtL(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        const std::vector<Color> point = RenderSprite(dev, rtP, 16, 8, t8x4_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp), white);
        const std::vector<Color> linear = RenderSprite(dev, rtL, 16, 8, t8x4_, g,
            MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp, TextureAddressMode::Clamp), white);

        check(CountForeignColors(point, p8x4_) == 0,
              "O1: PointClamp produces only stored texel values (no interpolated colour)");
        const int foreignLinear = CountForeignColors(linear, p8x4_);
        check(foreignLinear > 0,
              "O2: LinearClamp still interpolates -- it produces " + std::to_string(foreignLinear) +
              " pixels whose colour is not any stored texel (the fix must not turn Linear into Point)");

        int differing = 0;
        for (std::size_t i = 0; i < point.size(); ++i)
            if (!SameColor(point[i], linear[i])) ++differing;
        check(differing > 0,
              "O3: Point and Linear are different images under magnification (differing=" +
              std::to_string(differing) + ")");

        // At the pre-fix failure pixel, Linear must land strictly BETWEEN the two texels it blends
        // -- proof that the linear path still weighs neighbours rather than snapping.
        const Color& lin = linear[2];
        const Color a = p8x4_.At(0, 0);
        const Color b = p8x4_.At(1, 0);
        const int lo = std::min(a.getRProperty(), b.getRProperty());
        const int hi = std::max(a.getRProperty(), b.getRProperty());
        check(lin.getRProperty() > lo && lin.getRProperty() < hi,
              "O4: at destination (2,0) Linear blends texels (0,0) and (1,0) -- red " +
              std::to_string(static_cast<int>(lin.getRProperty())) + " lies strictly between " +
              std::to_string(lo) + " and " + std::to_string(hi));
    }

    // P -----------------------------------------------------------------------------------------
    void RunP_LinearWrap(GraphicsDevice& dev)
    {
        // Linear must keep interpolating AND keep wrapping: the fix must not convert the linear
        // path's address handling into the old unconditional clamp either.
        const SpriteGeom g = Geom(Rectangle(0, 0, 16, 16), Rectangle(0, 0, 8, 8));
        const Color white(255, 255, 255, 255);
        RenderTarget2D rtLC(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::DiscardContents);
        RenderTarget2D rtLW(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::DiscardContents);
        const std::vector<Color> lc = RenderSprite(dev, rtLC, 16, 16, t4x4_, g,
            MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp, TextureAddressMode::Clamp), white);
        const std::vector<Color> lw = RenderSprite(dev, rtLW, 16, 16, t4x4_, g,
            MakeSampler(TextureFilter::Linear, TextureAddressMode::Wrap, TextureAddressMode::Wrap), white);

        check(CountForeignColors(lw, p4x4_) > 0, "P1: LinearWrap still interpolates");
        int differing = 0;
        for (std::size_t i = 0; i < lc.size(); ++i)
            if (!SameColor(lc[i], lw[i])) ++differing;
        check(differing > 0,
              "P2: LinearWrap and LinearClamp differ -- the address mode reaches the linear path too "
              "(differing=" + std::to_string(differing) + ")");
    }

    // Q -----------------------------------------------------------------------------------------
    void RunQ_SamplerTransitions(GraphicsDevice& dev)
    {
        const SpriteGeom g = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        const Color white(255, 255, 255, 255);
        const SamplerState point =
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
        const SamplerState linear =
            MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp, TextureAddressMode::Clamp);

        RenderTarget2D r1(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        RenderTarget2D r2(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        RenderTarget2D r3(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const std::vector<Color> first = RenderSprite(dev, r1, 16, 8, t8x4_, g, point, white);
        const std::vector<Color> middle = RenderSprite(dev, r2, 16, 8, t8x4_, g, linear, white);
        const std::vector<Color> third = RenderSprite(dev, r3, 16, 8, t8x4_, g, point, white);

        int same = 0;
        for (std::size_t i = 0; i < first.size(); ++i)
            if (SameColor(first[i], third[i])) ++same;
        check(same == static_cast<int>(first.size()),
              "Q1: Point -> Linear -> Point restores the byte-identical point image (" +
              std::to_string(same) + "/" + std::to_string(first.size()) + ")");
        check(CountForeignColors(middle, p8x4_) > 0,
              "Q2: the intervening Linear batch really did interpolate (so Q1 is not three identical "
              "point batches)");

        // Two batches in ONE frame with different samplers, drawn into disjoint halves of one
        // target: the second Begin must not inherit the first's sampler, and the first must not be
        // retroactively changed by the second.
        RenderTarget2D rt(dev, 32, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetState(dev, 32, 8);
        dev.Clear(kSentinel);
        {
            SamplerState p = point;
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &p, nullptr, nullptr);
            sb.Draw(t8x4_, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255));
            sb.End();
        }
        {
            SamplerState l = linear;
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &l, nullptr, nullptr);
            sb.Draw(t8x4_, Rectangle(16, 0, 16, 8), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255));
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);
        std::vector<Color> both(32u * 8u, Color(0, 0, 0, 0));
        rt.GetData(both.data(), 0, static_cast<int>(both.size()));

        int leftBad = 0, rightForeign = 0;
        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const SpriteGeom lg = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
                const SourceCoord sc = SpriteSourceCoord(lg, x, y);
                int tx = 0, ty = 0;
                PointTexel(sc.sx, sc.sy, p8x4_.w, p8x4_.h,
                           TextureAddressMode::Clamp, TextureAddressMode::Clamp, tx, ty);
                if (!SameColor(both[static_cast<std::size_t>(y) * 32u + static_cast<std::size_t>(x)],
                               p8x4_.At(tx, ty)))
                    ++leftBad;
            }
            for (int x = 16; x < 32; ++x)
            {
                const Color& c = both[static_cast<std::size_t>(y) * 32u + static_cast<std::size_t>(x)];
                bool stored = false;
                for (const Color& t : p8x4_.texels)
                    if (SameColor(c, t)) { stored = true; break; }
                if (!stored) ++rightForeign;
            }
        }
        check(leftBad == 0,
              "Q3: the PointClamp batch stays exact when a LinearClamp batch follows it in the same "
              "frame (mismatches=" + std::to_string(leftBad) + ")");
        check(rightForeign > 0,
              "Q4: the following LinearClamp batch really used Linear (interpolated pixels=" +
              std::to_string(rightForeign) + "), so Q3 is not two point batches");
    }

    // R -----------------------------------------------------------------------------------------
    void RunR_TwoTexturesOneBatch(GraphicsDevice& dev)
    {
        // One Begin/End, one sampler, two textures of different dimensions. Each draw must address
        // ITS OWN texture's size.
        RenderTarget2D rt(dev, 32, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetState(dev, 32, 16);
        dev.Clear(kSentinel);
        {
            SamplerState p = MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                                         TextureAddressMode::Clamp);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &p, nullptr, nullptr);
            sb.Draw(t8x4_, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255));
            sb.Draw(t3x3_, Rectangle(16, 0, 15, 15), Rectangle(0, 0, 3, 3), Color(255, 255, 255, 255));
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);
        std::vector<Color> pix(32u * 16u, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));

        int badA = 0, badB = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const SourceCoord sc =
                    SpriteSourceCoord(Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4)), x, y);
                int tx = 0, ty = 0;
                PointTexel(sc.sx, sc.sy, p8x4_.w, p8x4_.h, TextureAddressMode::Clamp,
                           TextureAddressMode::Clamp, tx, ty);
                if (!SameColor(pix[static_cast<std::size_t>(y) * 32u + static_cast<std::size_t>(x)],
                               p8x4_.At(tx, ty))) ++badA;
            }
        for (int y = 0; y < 15; ++y)
            for (int x = 16; x < 31; ++x)
            {
                const SourceCoord sc =
                    SpriteSourceCoord(Geom(Rectangle(16, 0, 15, 15), Rectangle(0, 0, 3, 3)), x, y);
                int tx = 0, ty = 0;
                PointTexel(sc.sx, sc.sy, p3x3_.w, p3x3_.h, TextureAddressMode::Clamp,
                           TextureAddressMode::Clamp, tx, ty);
                if (!SameColor(pix[static_cast<std::size_t>(y) * 32u + static_cast<std::size_t>(x)],
                               p3x3_.At(tx, ty))) ++badB;
            }
        check(badA == 0 && badB == 0,
              "R1: two textures of different sizes in ONE batch are each addressed with their own "
              "dimensions (8x4 mismatches=" + std::to_string(badA) + ", 3x3 mismatches=" +
              std::to_string(badB) + ")");
    }

    // S -----------------------------------------------------------------------------------------
    void RunS_RenderTargetSource(GraphicsDevice& dev)
    {
        // A RenderTarget2D carrying the 8x4 pattern 1:1, then magnified 2x through PointClamp. The
        // source is produced by rendering, so this classifies render-target sampling separately from
        // ordinary Texture2D sampling -- and the pattern is asymmetric, so a vertical flip could not
        // be mistaken for a filtering failure.
        RenderTarget2D src(dev, 8, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(&src);
        ResetState(dev, 8, 4);
        dev.Clear(kSentinel);
        {
            SamplerState p = MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                                         TextureAddressMode::Clamp);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &p, nullptr, nullptr);
            sb.Draw(t8x4_, Rectangle(0, 0, 8, 4), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255));
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);

        std::vector<Color> srcPix(32, Color(0, 0, 0, 0));
        src.GetData(srcPix.data(), 0, static_cast<int>(srcPix.size()));
        int srcBad = 0;
        for (int i = 0; i < 32; ++i)
            if (!SameColor(srcPix[static_cast<std::size_t>(i)], p8x4_.texels[static_cast<std::size_t>(i)]))
                ++srcBad;
        check(srcBad == 0,
              "S1: the render target holds the 8x4 pattern exactly before it is sampled "
              "(mismatches=" + std::to_string(srcBad) + ")");

        RenderTarget2D dst(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev, 16, 8);
        dev.Clear(kSentinel);
        {
            SamplerState p = MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                                         TextureAddressMode::Clamp);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &p, nullptr, nullptr);
            sb.Draw(src, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4), Color(255, 255, 255, 255));
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev, kBBW, kBBH);

        std::vector<Color> pix(16u * 8u, Color(0, 0, 0, 0));
        dst.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        int covered = 0;
        int ties = 0;
        const SpriteGeom g = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        const int bad = CompareExact(pix, 16, 8, p8x4_, g, TextureAddressMode::Clamp,
                                     TextureAddressMode::Clamp, "S2", covered, ties);
        check(bad == 0,
              "S2: a RenderTarget2D magnified 2x under PointClamp selects one texel per pixel, "
              "exactly like an ordinary Texture2D (mismatches=" + std::to_string(bad) + ")");
        check(CountForeignColors(pix, p8x4_) == 0,
              "S3: no interpolated colour appears when the source is a render target");
    }

    // T -----------------------------------------------------------------------------------------
    void RunT_AlphaAndBlend(GraphicsDevice& dev)
    {
        // SurfaceFormat::Color with alpha 0, 64, 128, 200 and 255 texels. Under Opaque the sampled
        // texel must reach the destination byte-for-byte INCLUDING its alpha -- the sampler must
        // select the stored texel before any blending decision.
        ExactLeg(dev, "T1 alpha 0/64/128/200/255 texels survive point selection under Opaque",
                 p3x3_, t3x3_, 15, 15, Geom(Rectangle(0, 0, 15, 15), Rectangle(0, 0, 3, 3)));
        ExactLeg(dev, "T2 the 8x4 pattern's four alpha bands survive point selection",
                 p8x4_, t8x4_, 24, 12, Geom(Rectangle(0, 0, 24, 12), Rectangle(0, 0, 8, 4)));

        // Under AlphaBlend the RGB that reaches the blender must still come from ONE texel: a fully
        // opaque texel writes its own colour, so a blend cannot hide a filtering error there.
        RenderTarget2D rt(dev, 12, 12, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        SpriteGeom g = Geom(Rectangle(0, 0, 12, 12), Rectangle(0, 0, 3, 3));
        const std::vector<Color> pix = RenderSprite(dev, rt, 12, 12, t3x3_, g,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp),
            Color(255, 255, 255, 255), BlendState::AlphaBlend);
        int opaqueBad = 0, opaqueSeen = 0;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
            {
                const SourceCoord sc = SpriteSourceCoord(g, x, y);
                if (!sc.covered) continue;
                int tx = 0, ty = 0;
                PointTexel(sc.sx, sc.sy, p3x3_.w, p3x3_.h, TextureAddressMode::Clamp,
                           TextureAddressMode::Clamp, tx, ty);
                const Color t = p3x3_.At(tx, ty);
                if (t.getAProperty() != 255) continue;
                ++opaqueSeen;
                const Color& a = pix[static_cast<std::size_t>(y) * 12u + static_cast<std::size_t>(x)];
                if (a.getRProperty() != t.getRProperty() || a.getGProperty() != t.getGProperty() ||
                    a.getBProperty() != t.getBProperty())
                    ++opaqueBad;
            }
        check(opaqueSeen > 0 && opaqueBad == 0,
              "T3: under AlphaBlend, every fully opaque texel still writes its own exact RGB "
              "(checked=" + std::to_string(opaqueSeen) + ", mismatches=" +
              std::to_string(opaqueBad) + ")");
    }

    // U -----------------------------------------------------------------------------------------
    void RunU_3DPath(GraphicsDevice& dev)
    {
        // The device SamplerStates[0] path (BasicEffect + a textured primitive), not SpriteBatch:
        // GFX-150's root cause discarded the filter here too, so this is a second public entry
        // point into the same defect rather than a variation of the first.
        Exact3DLeg(dev, "U1 3D textured quad, 8x4 magnified onto 16x8", p8x4_, t8x4_, 16, 8,
                   1.0f, 1.0f, TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
        Exact3DLeg(dev, "U2 3D textured quad, 3x3 magnified onto 10x10 (non-integer)", p3x3_, t3x3_,
                   10, 10, 1.0f, 1.0f, TextureAddressMode::Clamp, TextureAddressMode::Clamp, true);

        RenderTarget2D rt(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        const std::vector<Color> pix = Render3DQuad(dev, rt, 16, 8, t8x4_, 1.0f, 1.0f,
            MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp, TextureAddressMode::Clamp));
        check(CountForeignColors(pix, p8x4_) == 0,
              "U3: the 3D path under PointClamp produces only stored texel values");
    }

    // V -----------------------------------------------------------------------------------------
    void RunV_3DWrap(GraphicsDevice& dev)
    {
        Exact3DLeg(dev, "V1 3D quad with UVs to 2.0 under PointWrap", p4x4_, t4x4_, 16, 16,
                   2.0f, 2.0f, TextureAddressMode::Wrap, TextureAddressMode::Wrap, false);
        Exact3DLeg(dev, "V2 3D quad with UVs to 2.0 under PointClamp", p4x4_, t4x4_, 16, 16,
                   2.0f, 2.0f, TextureAddressMode::Clamp, TextureAddressMode::Clamp, false);
    }

    // W -----------------------------------------------------------------------------------------
    void RunW_CustomViewport(GraphicsDevice& dev)
    {
        SpriteGeom g = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));
        g.viewportX = 5;
        g.viewportY = 3;
        ExactLeg(dev, "W1 point selection under a custom viewport origin", p8x4_, t8x4_, 32, 16, g);
    }

    // X -----------------------------------------------------------------------------------------
    void RunX_DefaultSamplerIsLinearWrap(GraphicsDevice& dev)
    {
        // XNA's SamplerStateCollection defaults every slot to SamplerState.LinearWrap, so a draw
        // that sets no sampler at all still has an address mode -- and at the far edge of a
        // full-texture quad the linear filter's upper neighbour WRAPS round to texel 0 instead of
        // being clamped to the last texel. Software returned the clamped answer for both modes,
        // which is why a fixture that leaves SamplerState implicit could assert the pure edge texel
        // and pass. Measured here as a public differential, not asserted from the enum's name.
        RenderTarget2D rtDefault(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
        RenderTarget2D rtClamp(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
        const std::vector<Color> wrapped = Render3DQuad(dev, rtDefault, 16, 16, t2x2_, 1.0f, 1.0f,
            MakeSampler(TextureFilter::Linear, TextureAddressMode::Wrap, TextureAddressMode::Wrap));
        const std::vector<Color> clamped = Render3DQuad(dev, rtClamp, 16, 16, t2x2_, 1.0f, 1.0f,
            MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp, TextureAddressMode::Clamp));

        const Color& farWrapped = wrapped[15u * 16u + 15u];
        const Color& farClamped = clamped[15u * 16u + 15u];
        const Color last = p2x2_.At(1, 1);
        check(SameColor(farClamped, last),
              "X1: under LinearClamp the far corner collapses onto the last texel " + Str(last) +
              " -- actual " + Str(farClamped));
        check(!SameColor(farWrapped, last),
              "X2: under LinearWrap the SAME corner blends with the wrapped texel instead of "
              "clamping -- expected something other than " + Str(last) + ", actual " +
              Str(farWrapped));
    }

    // Y -----------------------------------------------------------------------------------------
    void RunY_FilterOrdinals(GraphicsDevice& dev)
    {
        const Color white(255, 255, 255, 255);
        const SpriteGeom mag = Geom(Rectangle(0, 0, 16, 8), Rectangle(0, 0, 8, 4));

        // Anisotropic names Linear for both halves -- it must interpolate, not snap.
        {
            RenderTarget2D rt(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 16, 8, t8x4_, mag,
                MakeSampler(TextureFilter::Anisotropic, TextureAddressMode::Clamp,
                            TextureAddressMode::Clamp), white);
            check(CountForeignColors(pix, p8x4_) > 0,
                  "Y1: TextureFilter::Anisotropic filters like Linear under magnification");
        }

        // The four ordinals that name a DIFFERENT minification and magnification filter. Only the
        // MAGNIFICATION half is asserted cross-renderer: on a GL-family renderer these four also
        // select a mipmap minification filter, and a texture with no mip chain is then incomplete,
        // so their minification half is not a portable measurement. Software's own minification
        // half is covered by Y6/Y7 below, which use ordinals whose two halves agree.
        struct MagCase { TextureFilter filter; bool point; const char* name; };
        const MagCase magCases[] = {
            {TextureFilter::MinLinearMagPointMipLinear, true,  "MinLinearMagPointMipLinear"},
            {TextureFilter::MinLinearMagPointMipPoint,  true,  "MinLinearMagPointMipPoint"},
            {TextureFilter::MinPointMagLinearMipLinear, false, "MinPointMagLinearMipLinear"},
            {TextureFilter::MinPointMagLinearMipPoint,  false, "MinPointMagLinearMipPoint"},
        };
        for (const MagCase& c : magCases)
        {
            RenderTarget2D rt(dev, 16, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 16, 8, t8x4_,
                mag, MakeSampler(c.filter, TextureAddressMode::Clamp, TextureAddressMode::Clamp),
                white);
            const int foreign = CountForeignColors(pix, p8x4_);
            if (c.point)
                check(foreign == 0, std::string("Y2 ") + c.name +
                      ": magnifies with POINT -- only stored texel values (interpolated=" +
                      std::to_string(foreign) + ")");
            else
                check(foreign > 0, std::string("Y2 ") + c.name +
                      ": magnifies with LINEAR -- genuinely interpolates (interpolated=" +
                      std::to_string(foreign) + ")");
        }

        // Minification, using the two ordinals whose halves agree so no mip chain is implied.
        const SpriteGeom min = Geom(Rectangle(0, 0, 3, 2), Rectangle(0, 0, 8, 4));
        {
            RenderTarget2D rt(dev, 3, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 3, 2, t8x4_, min,
                MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                            TextureAddressMode::Clamp), white);
            check(CountForeignColors(pix, p8x4_) == 0,
                  "Y6: TextureFilter::Point minifies with point selection too -- one texel per "
                  "pixel, no averaging");
        }
        {
            RenderTarget2D rt(dev, 3, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 3, 2, t8x4_, min,
                MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp,
                            TextureAddressMode::Clamp), white);
            check(CountForeignColors(pix, p8x4_) > 0,
                  "Y7: TextureFilter::Linear still interpolates when minifying");
        }
    }

    // Z -----------------------------------------------------------------------------------------
    void RunZ_ExtremeCoordinates(GraphicsDevice& dev)
    {
        const Color white(255, 255, 255, 255);

        // A source rectangle a million texels outside the texture. Wrap must still land on a real
        // texel, and nothing may be read outside the texture's storage.
        {
            RenderTarget2D rt(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 16, 16, t4x4_,
                Geom(Rectangle(0, 0, 16, 16), Rectangle(1000000, 1000000, 8, 8)),
                MakeSampler(TextureFilter::Point, TextureAddressMode::Wrap,
                            TextureAddressMode::Wrap), white);
            check(CountForeignColors(pix, p4x4_) == 0,
                  "Z1: PointWrap over a source rectangle a million texels away still addresses a "
                  "stored texel");
        }

        // A source rectangle spanning a million texels, so the per-pixel step is enormous.
        {
            RenderTarget2D rt(dev, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            const std::vector<Color> pix = RenderSprite(dev, rt, 16, 16, t4x4_,
                Geom(Rectangle(0, 0, 16, 16), Rectangle(0, 0, 1 << 20, 1 << 20)),
                MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                            TextureAddressMode::Clamp), white);
            check(CountForeignColors(pix, p4x4_) == 0,
                  "Z2: PointClamp over a million-texel source rectangle stays inside the texture");
        }

        // UVs large enough that u * width overflows to infinity on the 3D path. The requirement is
        // that this is DEFINED -- a real texel, no crash, no out-of-bounds read -- not that a
        // particular texel is chosen, which no renderer specifies.
        {
            RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            bool threw = false;
            std::vector<Color> pix;
            try
            {
                pix = Render3DQuad(dev, rt, 8, 8, t4x4_, 1.0e30f, 1.0e30f,
                    MakeSampler(TextureFilter::Point, TextureAddressMode::Wrap,
                                TextureAddressMode::Wrap));
            }
            catch (const std::exception&) { threw = true; }
            check(!threw && !pix.empty(),
                  "Z3: a texture coordinate large enough to overflow the texel index is handled "
                  "deterministically instead of crashing or throwing");
        }
    }

public:
    PointSamplingContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    PointSamplingContractTest game;
    game.Run();
    return game.getResult();
}
