// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-177: a renderer's descriptor (or view, or binding-slot) bookkeeping must be a function of
// what is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a
// monotonic bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw
// `CBV/SRV/UAV descriptor heap exhausted` even when six were alive -- REMED-GFX-175's contract
// fixture reached exactly 64 checks on D3D12 and aborted, having created and destroyed one
// RenderTarget2D per measurement.
//
// WHAT THIS FIXTURE PINS. Three properties that no existing fixture in this tree asserted, all of
// them through the public XNA API only, so every renderer runs the same file as a control:
//
//   1. CARDINALITY OVER TIME. Creating and destroying resources in a loop must not consume a finite
//      pool. Leg E runs 256 create/draw/read/destroy cycles with at most two resources live.
//
//   2. CARDINALITY AT AN INSTANT. More than 64 simultaneously live textures, samplers, render
//      targets and texture/sampler pairs must all work. Legs A/B/C walk 1, 2, 31, 32, 63, 64, 65,
//      66, 96, 128 and 256 -- the thresholds around the observed boundary, and well past it.
//
//   3. IDENTITY UNDER PRESSURE. Growing or recycling a binding pool must never let one resource be
//      sampled through another one's descriptor. Every draw here is checked against a SELF-
//      IDENTIFYING oracle, not against "some plausible colour".
//
// THE ORACLE, and why it is stronger than a colour comparison. Texture i is 2x2 RGBA whose twelve
// 0/255-only colour channels spell out i in binary (texel t, channel c carries bit t*3+c). Drawn
// one-to-one into a 2x2 render target under a Point sampler, the readback is byte-exact on every
// renderer and DECODES BACK TO AN INTEGER. So a check does not merely ask "is this the right shade";
// it recovers which texture was actually sampled and names it. An off-by-one in a recycled slot, a
// stale descriptor left pointing at a destroyed resource, or an index aliased after a heap grew all
// report the wrong integer rather than a near-miss colour. 0/255-only channels also keep the
// comparison byte-exact under any sRGB or filtering convention, and 12 bits addresses 4096
// identities, far past anything this fixture creates.
//
// WHAT THIS FIXTURE MUST NOT DO. It must not stay under any renderer's capacity to pass: shrinking
// the number of variants is the failure mode this file exists to prevent, so the thresholds are
// fixed constants and the high-cardinality legs are unconditional wherever the feature is
// supported at all.
//
// LEGS:
//   A  distinct textures            1..256 live at once, each decoded back to its own identity
//   B  distinct sampler states      27 distinct (filter, addressU, addressV) combinations
//   C  distinct textures x samplers pairs, so neither pool alone explains a pass
//   D  repeated identical state     256 draws of the SAME texture and sampler
//   E  create/dispose cycles        256 cycles, never more than two resources live
//   F  render targets as textures   65 live render targets, each sampled by identity
//   G  two texture slots            DualTextureEffect over many distinct pairs
//   H  effect families              Basic/AlphaTest/DualTexture/EnvironmentMap at high cardinality
//   I  SpriteBatch                  many distinct textures within and across batches
//   J  draw paths                   indexed, non-indexed and DrawUser reach the same identity
//   K  bulk acquire/release rounds   40 live per round, 8 rounds, released together
//   L  handle reuse                 destroy then recreate, and prove no stale content survives
//   M  cube and volume textures     where the device reports support for them
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
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
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

    /// The thresholds around the observed D3D12 boundary, plus two well past it. Fixed on purpose:
    /// lowering any of these would make the fixture pass by avoiding the defect.
    constexpr std::array<int, 11> kThresholds{{1, 2, 31, 32, 63, 64, 65, 66, 96, 128, 256}};

    /// The largest threshold, reused as the loop count wherever a leg wants "well past capacity".
    constexpr int kMaxN = 256;

    /// Never produced by the identity encoding (which only ever writes 0 or 255), so an untouched
    /// or partially covered destination pixel is distinguishable from every valid identity.
    const Color kSentinel(13, 17, 19, 255);

    /// Encodes @p id into a 2x2 RGBA image, one bit per colour channel, 0 or 255 only.
    std::array<Color, 4> EncodeIdentity(int id)
    {
        std::array<Color, 4> texels{{Color(0, 0, 0, 255), Color(0, 0, 0, 255),
                                     Color(0, 0, 0, 255), Color(0, 0, 0, 255)}};
        for (int t = 0; t < 4; ++t)
        {
            const int r = ((id >> (t * 3 + 0)) & 1) ? 255 : 0;
            const int g = ((id >> (t * 3 + 1)) & 1) ? 255 : 0;
            const int b = ((id >> (t * 3 + 2)) & 1) ? 255 : 0;
            texels[static_cast<std::size_t>(t)] = Color(r, g, b, 255);
        }
        return texels;
    }

    /// Recovers the identity a 2x2 readback carries, or -1 if any channel is not a clean 0/255 --
    /// which is itself the answer when a draw was dropped, blended or left uncovered.
    int DecodeIdentity(const std::vector<Color>& pix)
    {
        if (pix.size() != 4) return -1;
        int id = 0;
        for (int t = 0; t < 4; ++t)
        {
            const Color& c = pix[static_cast<std::size_t>(t)];
            const std::array<int, 3> ch{{c.getRProperty(), c.getGProperty(), c.getBProperty()}};
            for (int k = 0; k < 3; ++k)
            {
                if (ch[static_cast<std::size_t>(k)] == 255) id |= (1 << (t * 3 + k));
                else if (ch[static_cast<std::size_t>(k)] != 0) return -1;
            }
        }
        return id;
    }

    /// Stride-20 VertexPositionTexture stream (REMED-GFX-125: object size is not the stream stride).
    struct VtxPT { float px, py, pz; float u, v; };
    static_assert(sizeof(VtxPT) == 20, "stride 20");

    /// Stride-32 position + normal + texture coordinate, the shape EnvironmentMapEffect needs.
    struct VtxPNT { float px, py, pz; float nx, ny, nz; float u, v; };
    static_assert(sizeof(VtxPNT) == 32, "stride 32");

    /// A full-viewport quad in NDC with identity matrices, so a 2x2 destination maps one texel to
    /// one pixel and the identity encoding survives byte-for-byte.
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

    std::vector<VtxPNT> MakeQuadNormalTexture()
    {
        std::vector<VtxPNT> out;
        for (const VtxPT& v : MakeQuad())
            out.push_back(VtxPNT{v.px, v.py, v.pz, 0.0f, 0.0f, 1.0f, v.u, v.v});
        return out;
    }

    VertexDeclaration PositionTextureDecl()
    {
        return VertexDeclaration(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    VertexDeclaration PositionNormalTextureDecl()
    {
        return VertexDeclaration(32, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    /// The nine public TextureFilter ordinals, swept against every address mode below so the
    /// distinct-sampler-state count is well past D3D12's own starting sampler-heap capacity of 16.
    constexpr std::array<TextureFilter, 9> kFilters{{
        TextureFilter::Linear,
        TextureFilter::Point,
        TextureFilter::Anisotropic,
        TextureFilter::LinearMipPoint,
        TextureFilter::PointMipLinear,
        TextureFilter::MinLinearMagPointMipLinear,
        TextureFilter::MinLinearMagPointMipPoint,
        TextureFilter::MinPointMagLinearMipLinear,
        TextureFilter::MinPointMagLinearMipPoint,
    }};

    /// XNA 4.0 defines exactly three address modes, so the sweep below is 9 x 3 = 27 distinct
    /// sampler states -- still comfortably past D3D12's own starting sampler-heap capacity of 16.
    constexpr std::array<TextureAddressMode, 3> kAddressModes{{
        TextureAddressMode::Clamp,
        TextureAddressMode::Wrap,
        TextureAddressMode::Mirror,
    }};
}

class DescriptorCapacityContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    /// Set once by a real construct-and-write probe: CNA::GraphicsCapability has no TextureCube
    /// entry, so "does this renderer have cube textures" is answered by trying, not by asking.
    bool cubeSupported_ = false;

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

    /// Runs one leg and turns an escaping exception into a FAIL rather than an aborted process.
    /// A leg that throws is exactly the pre-fix D3D12 symptom, so it must be REPORTED, not
    /// swallowed -- and the remaining legs must still be measured instead of being lost with it.
    void runLeg(const char* name, const std::function<void()>& leg)
    {
        try
        {
            leg();
        }
        catch (const std::exception& e)
        {
            check(false, std::string("leg ") + name + " threw: " + e.what());
        }
        catch (...)
        {
            check(false, std::string("leg ") + name + " threw a non-std exception");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------------------------

    static SamplerState MakeSampler(TextureFilter filter = TextureFilter::Point,
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

    static std::unique_ptr<Texture2D> MakeIdentityTexture(GraphicsDevice& dev, int id)
    {
        auto tex = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> texels = EncodeIdentity(id);
        tex->SetData(texels.data(), 4);
        return tex;
    }

    /// Draws into a caller-supplied 2x2 target and returns its four pixels.
    std::vector<Color> RenderInto(GraphicsDevice& dev, RenderTarget2D& rt,
                                  const std::function<void(GraphicsDevice&)>& draw)
    {
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, 2, 2);
        dev.Clear(kSentinel);
        draw(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(4, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, 4);
        return pix;
    }

    static std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev)
    {
        return std::make_unique<RenderTarget2D>(dev, 2, 2, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    enum class Path { NonIndexed, Indexed, UserRaw };

    /// One unlit, untinted BasicEffect quad, so the modulation is the identity and the encoded
    /// texels survive byte-for-byte.
    static void DrawBasic(GraphicsDevice& d, Texture2D& tex, const SamplerState& sampler,
                          Path path = Path::NonIndexed)
    {
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
    }

    int DrawAndDecode(GraphicsDevice& dev, RenderTarget2D& rt, Texture2D& tex,
                      const SamplerState& sampler, Path path = Path::NonIndexed)
    {
        return DecodeIdentity(RenderInto(dev, rt, [&](GraphicsDevice& d) {
            DrawBasic(d, tex, sampler, path);
        }));
    }

    // ---------------------------------------------------------------------------------------------
    // A -- distinct textures, all live at once
    // ---------------------------------------------------------------------------------------------

    void RunA(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev);
        const SamplerState sampler = MakeSampler();

        for (int n : kThresholds)
        {
            std::vector<std::unique_ptr<Texture2D>> textures;
            textures.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) textures.push_back(MakeIdentityTexture(dev, i));

            int wrong = 0;
            int firstWrongAt = -1;
            int firstWrongGot = -1;
            for (int i = 0; i < n; ++i)
            {
                const int got = DrawAndDecode(dev, *rt, *textures[static_cast<std::size_t>(i)], sampler);
                if (got != i)
                {
                    ++wrong;
                    if (firstWrongAt < 0) { firstWrongAt = i; firstWrongGot = got; }
                }
            }
            check(wrong == 0,
                  "A" + std::to_string(n) + ": " + std::to_string(n) +
                      " simultaneously live textures each sample as themselves (wrong=" +
                      std::to_string(wrong) + "/" + std::to_string(n) +
                      (firstWrongAt < 0 ? std::string()
                                        : ", first at " + std::to_string(firstWrongAt) + " decoded " +
                                              std::to_string(firstWrongGot)) +
                      ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // B -- distinct sampler states
    // ---------------------------------------------------------------------------------------------

    void RunB(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev);
        auto tex = MakeIdentityTexture(dev, 1365); // 0b010101010101 -- every channel exercised

        int wrong = 0;
        int combos = 0;
        for (TextureFilter f : kFilters)
        {
            for (TextureAddressMode u : kAddressModes)
            {
                // One address mode per filter keeps this at 36 distinct states without exploding
                // the draw count; the u/v pair still varies independently across the sweep.
                const TextureAddressMode v = kAddressModes[static_cast<std::size_t>(
                    (static_cast<int>(combos) + 1) % static_cast<int>(kAddressModes.size()))];
                ++combos;
                if (DrawAndDecode(dev, *rt, *tex, MakeSampler(f, u, v)) != 1365) ++wrong;
            }
        }
        check(combos == 27 && wrong == 0,
              "B1: " + std::to_string(combos) +
                  " distinct sampler states all sample the same texture correctly (wrong=" +
                  std::to_string(wrong) + ")");

        // The states must be genuinely DISTINCT, not collapsed onto one cached descriptor: at a 4x
        // magnification Point reproduces exactly two source colours per row while Linear does not.
        RenderTarget2D big(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        auto twoTone = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> tone{{Color(255, 0, 0, 255), Color(0, 0, 255, 255),
                                         Color(0, 0, 255, 255), Color(255, 0, 0, 255)}};
        twoTone->SetData(tone.data(), 4);

        auto distinctColours = [&](const SamplerState& s) {
            dev.SetRenderTarget(&big);
            ResetDeviceState(dev, 8, 8);
            dev.Clear(kSentinel);
            DrawBasic(dev, *twoTone, s);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetDeviceState(dev, kBBW, kBBH);
            std::vector<Color> pix(64, Color(0, 0, 0, 0));
            big.GetData(pix.data(), 0, 64);
            int intermediate = 0;
            for (const Color& c : pix)
                if (c.getRProperty() != 0 && c.getRProperty() != 255) ++intermediate;
            return intermediate;
        };
        const int pointIntermediate = distinctColours(MakeSampler(TextureFilter::Point));
        const int linearIntermediate = distinctColours(MakeSampler(TextureFilter::Linear));
        check(pointIntermediate == 0 && linearIntermediate > 0,
              "B2: the sampler states really differ after the sweep (Point intermediate px=" +
                  std::to_string(pointIntermediate) + ", Linear intermediate px=" +
                  std::to_string(linearIntermediate) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // C -- distinct textures AND distinct samplers together
    // ---------------------------------------------------------------------------------------------

    void RunC(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev);
        std::vector<std::unique_ptr<Texture2D>> textures;
        for (int i = 0; i < kMaxN; ++i) textures.push_back(MakeIdentityTexture(dev, i));

        int wrong = 0;
        for (int i = 0; i < kMaxN; ++i)
        {
            const TextureFilter f = kFilters[static_cast<std::size_t>(i % kFilters.size())];
            const TextureAddressMode u = kAddressModes[static_cast<std::size_t>(i % kAddressModes.size())];
            const TextureAddressMode v =
                kAddressModes[static_cast<std::size_t>((i / 4) % kAddressModes.size())];
            if (DrawAndDecode(dev, *rt, *textures[static_cast<std::size_t>(i)], MakeSampler(f, u, v)) != i)
                ++wrong;
        }
        check(wrong == 0,
              "C1: " + std::to_string(kMaxN) +
                  " live textures each paired with a rotating sampler state (wrong=" +
                  std::to_string(wrong) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // D -- repeated identical state
    // ---------------------------------------------------------------------------------------------

    void RunD(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev);
        auto tex = MakeIdentityTexture(dev, 2730); // 0b101010101010, the complement of B's
        const SamplerState sampler = MakeSampler();

        int wrong = 0;
        for (int i = 0; i < kMaxN; ++i)
            if (DrawAndDecode(dev, *rt, *tex, sampler) != 2730) ++wrong;
        check(wrong == 0,
              "D1: " + std::to_string(kMaxN) +
                  " draws of the SAME texture and sampler stay correct (wrong=" +
                  std::to_string(wrong) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // E -- create/dispose cycles; never more than two resources live
    // ---------------------------------------------------------------------------------------------

    void RunE(GraphicsDevice& dev)
    {
        const SamplerState sampler = MakeSampler();
        int wrong = 0;
        int firstWrongAt = -1;
        for (int i = 0; i < kMaxN; ++i)
        {
            auto tex = MakeIdentityTexture(dev, i);
            auto rt = MakeTarget(dev);
            if (DrawAndDecode(dev, *rt, *tex, sampler) != i)
            {
                ++wrong;
                if (firstWrongAt < 0) firstWrongAt = i;
            }
            // Both go out of scope here: at most two resources are ever live at once, so a pool
            // sized for live demand never grows and a pool that leaks is exhausted.
        }
        check(wrong == 0,
              "E1: " + std::to_string(kMaxN) +
                  " create/draw/read/destroy cycles with at most two live resources (wrong=" +
                  std::to_string(wrong) +
                  (firstWrongAt < 0 ? std::string() : ", first at " + std::to_string(firstWrongAt)) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // F -- many live render targets, each sampled as a texture
    // ---------------------------------------------------------------------------------------------

    void RunF(GraphicsDevice& dev)
    {
        constexpr int kTargets = 65; // one past D3D12's own starting capacity
        std::vector<std::unique_ptr<Texture2D>> sources;
        std::vector<std::unique_ptr<RenderTarget2D>> targets;
        const SamplerState sampler = MakeSampler();

        for (int i = 0; i < kTargets; ++i)
        {
            sources.push_back(MakeIdentityTexture(dev, i));
            targets.push_back(MakeTarget(dev));
        }

        // Produce: each target receives its own source's identity.
        for (int i = 0; i < kTargets; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            RenderInto(dev, *targets[idx], [&](GraphicsDevice& d) { DrawBasic(d, *sources[idx], sampler); });
        }

        // Consume: sample every target back through a separate destination, so each target has to
        // be a live, correctly-bound shader resource at the same time as all the others.
        auto dest = MakeTarget(dev);
        int wrong = 0;
        for (int i = 0; i < kTargets; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            const int got = DecodeIdentity(RenderInto(dev, *dest, [&](GraphicsDevice& d) {
                DrawBasic(d, *targets[idx], sampler);
            }));
            if (got != i) ++wrong;
        }
        check(wrong == 0,
              "F1: " + std::to_string(kTargets) +
                  " simultaneously live render targets each sample back as themselves (wrong=" +
                  std::to_string(wrong) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // G -- two texture slots at once
    // ---------------------------------------------------------------------------------------------

    void RunG(GraphicsDevice& dev)
    {
        constexpr int kPairs = 96;
        auto rt = MakeTarget(dev);

        // Slot 0 carries the identity; slot 1 is a constant WHITE, so DualTextureEffect's modulate
        // leaves the identity byte-exact and a swapped or aliased second slot destroys it.
        auto white = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> whitePixels{{Color(255, 255, 255, 255), Color(255, 255, 255, 255),
                                                Color(255, 255, 255, 255), Color(255, 255, 255, 255)}};
        white->SetData(whitePixels.data(), 4);

        std::vector<std::unique_ptr<Texture2D>> textures;
        for (int i = 0; i < kPairs; ++i) textures.push_back(MakeIdentityTexture(dev, i));

        const SamplerState sampler = MakeSampler();
        // Stride 20: DualTextureEffect's only portable vertex shape -- the stride-28 two-UV-set
        // sibling was never ported on the D3D-family renderers, so it is not the common denominator
        // this cross-renderer fixture can drive.
        const std::vector<VtxPT> verts = MakeQuad();
        const VertexDeclaration decl = PositionTextureDecl();

        int wrong = 0;
        for (int i = 0; i < kPairs; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            const int got = DecodeIdentity(RenderInto(dev, *rt, [&](GraphicsDevice& d) {
                d.getSamplerStatesProperty()[0] = sampler;
                d.getSamplerStatesProperty()[1] = sampler;
                DualTextureEffect fx(d);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(textures[idx].get());
                fx.setTexture2Property(white.get());
                fx.Apply();
                d.DrawUserPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 2, decl);
            }));
            if (got != i) ++wrong;
        }
        check(wrong == 0,
              "G1: " + std::to_string(kPairs) +
                  " DualTextureEffect draws over two live texture slots (wrong=" +
                  std::to_string(wrong) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // H -- effect families at high cardinality
    // ---------------------------------------------------------------------------------------------

    void RunH(GraphicsDevice& dev)
    {
        constexpr int kPerFamily = 70; // past D3D12's own starting capacity, per family
        auto rt = MakeTarget(dev);
        const SamplerState sampler = MakeSampler();

        std::vector<std::unique_ptr<Texture2D>> textures;
        for (int i = 0; i < kPerFamily; ++i) textures.push_back(MakeIdentityTexture(dev, i));

        // BasicEffect is already covered by legs A..E; AlphaTestEffect is the second family that
        // binds exactly one texture, so it gets the identity oracle unchanged.
        {
            const std::vector<VtxPT> verts = MakeQuad();
            const VertexDeclaration decl = PositionTextureDecl();
            int wrong = 0;
            for (int i = 0; i < kPerFamily; ++i)
            {
                const auto idx = static_cast<std::size_t>(i);
                const int got = DecodeIdentity(RenderInto(dev, *rt, [&](GraphicsDevice& d) {
                    d.getSamplerStatesProperty()[0] = sampler;
                    AlphaTestEffect fx(d);
                    fx.setWorldProperty(Matrix::getIdentityProperty());
                    fx.setViewProperty(Matrix::getIdentityProperty());
                    fx.setProjectionProperty(Matrix::getIdentityProperty());
                    fx.setTextureProperty(textures[idx].get());
                    fx.Apply();
                    d.DrawUserPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 2, decl);
                }));
                if (got != i) ++wrong;
            }
            check(wrong == 0,
                  "H1: " + std::to_string(kPerFamily) + " AlphaTestEffect draws (wrong=" +
                      std::to_string(wrong) + ")");
        }

        // EnvironmentMapEffect binds a Texture2D AND a TextureCube, so it is the family that puts
        // two DIFFERENT resource kinds into the pool at once. Its lighting makes the result not
        // byte-exact, so the assertion here is the one that stays honest at this cardinality: every
        // draw must produce real, non-sentinel coverage rather than a dropped or black frame.
        if (cubeSupported_)
        {
            std::vector<std::unique_ptr<TextureCube>> cubes;
            for (int i = 0; i < kPerFamily; ++i)
            {
                auto cube = std::make_unique<TextureCube>(dev, 2, false, SurfaceFormat::Color);
                const std::array<Color, 4> face = EncodeIdentity(i);
                for (int f = 0; f < 6; ++f)
                    cube->SetData(static_cast<CubeMapFace>(f), face.data(), 4);
                cubes.push_back(std::move(cube));
            }

            const std::vector<VtxPNT> verts = MakeQuadNormalTexture();
            const VertexDeclaration decl = PositionNormalTextureDecl();
            int uncovered = 0;
            for (int i = 0; i < kPerFamily; ++i)
            {
                const auto idx = static_cast<std::size_t>(i);
                const std::vector<Color> pix = RenderInto(dev, *rt, [&](GraphicsDevice& d) {
                    d.getSamplerStatesProperty()[0] = sampler;
                    d.getSamplerStatesProperty()[1] = sampler;
                    EnvironmentMapEffect fx(d);
                    fx.setWorldProperty(Matrix::getIdentityProperty());
                    fx.setViewProperty(Matrix::getIdentityProperty());
                    fx.setProjectionProperty(Matrix::getIdentityProperty());
                    fx.setTextureProperty(textures[idx].get());
                    fx.setEnvironmentMapProperty(cubes[idx].get());
                    fx.Apply();
                    d.DrawUserPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 2, decl);
                });
                for (const Color& c : pix)
                    if (c.getRProperty() == kSentinel.getRProperty() &&
                        c.getGProperty() == kSentinel.getGProperty() &&
                        c.getBProperty() == kSentinel.getBProperty())
                    {
                        ++uncovered;
                        break;
                    }
            }
            check(uncovered == 0,
                  "H2: " + std::to_string(kPerFamily) +
                      " EnvironmentMapEffect draws (Texture2D + TextureCube live together) all "
                      "covered the destination (uncovered draws=" + std::to_string(uncovered) + ")");
        }
        else
        {
            note(std::string(kRendererName) +
                 " cannot create a TextureCube; H2 (EnvironmentMapEffect cardinality) is not observable");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // I -- SpriteBatch
    // ---------------------------------------------------------------------------------------------

    void RunI(GraphicsDevice& dev)
    {
        constexpr int kSprites = 96;
        std::vector<std::unique_ptr<Texture2D>> textures;
        for (int i = 0; i < kSprites; ++i) textures.push_back(MakeIdentityTexture(dev, i));

        auto rt = MakeTarget(dev);
        SamplerState sampler = MakeSampler();

        // One Begin/End per texture: every sprite flush rebinds its own texture, which is where a
        // stale cached binding would show up.
        int wrongSeparate = 0;
        for (int i = 0; i < kSprites; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            dev.SetRenderTarget(rt.get());
            dev.setViewportProperty(Viewport(0, 0, 2, 2));
            dev.Clear(kSentinel);
            {
                SpriteBatch sb(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                         nullptr, Matrix::getIdentityProperty());
                sb.Draw(*textures[idx], Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2),
                        Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetDeviceState(dev, kBBW, kBBH);
            std::vector<Color> pix(4, Color(0, 0, 0, 0));
            rt->GetData(pix.data(), 0, 4);
            if (DecodeIdentity(pix) != i) ++wrongSeparate;
        }
        check(wrongSeparate == 0,
              "I1: " + std::to_string(kSprites) +
                  " SpriteBatch flushes, one distinct texture each (wrong=" +
                  std::to_string(wrongSeparate) + ")");

        // All of them inside ONE Begin/End: the batch has to hold every texture at once.
        dev.SetRenderTarget(rt.get());
        dev.setViewportProperty(Viewport(0, 0, 2, 2));
        dev.Clear(kSentinel);
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                     nullptr, Matrix::getIdentityProperty());
            for (int i = 0; i < kSprites; ++i)
                sb.Draw(*textures[static_cast<std::size_t>(i)], Rectangle(0, 0, 2, 2),
                        Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
                        SpriteEffects::None, 0.0f);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(4, Color(0, 0, 0, 0));
        rt->GetData(pix.data(), 0, 4);
        // Opaque blending, painter's order: the LAST sprite drawn is the one that survives.
        check(DecodeIdentity(pix) == kSprites - 1,
              "I2: " + std::to_string(kSprites) +
                  " textures in ONE SpriteBatch leave the last one visible (decoded " +
                  std::to_string(DecodeIdentity(pix)) + ", expected " + std::to_string(kSprites - 1) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // J -- draw paths
    // ---------------------------------------------------------------------------------------------

    void RunJ(GraphicsDevice& dev)
    {
        constexpr int kPerPath = 70;
        auto rt = MakeTarget(dev);
        const SamplerState sampler = MakeSampler();

        std::vector<std::unique_ptr<Texture2D>> textures;
        for (int i = 0; i < kPerPath; ++i) textures.push_back(MakeIdentityTexture(dev, i));

        struct Route { Path path; const char* name; };
        const std::array<Route, 3> routes{{{Path::NonIndexed, "non-indexed"},
                                           {Path::Indexed, "indexed"},
                                           {Path::UserRaw, "DrawUser"}}};
        for (const Route& r : routes)
        {
            int wrong = 0;
            for (int i = 0; i < kPerPath; ++i)
                if (DrawAndDecode(dev, *rt, *textures[static_cast<std::size_t>(i)], sampler, r.path) != i)
                    ++wrong;
            check(wrong == 0,
                  std::string("J: ") + std::to_string(kPerPath) + " " + r.name +
                      " draws over distinct textures (wrong=" + std::to_string(wrong) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // K -- bulk acquire and bulk release, repeated
    // ---------------------------------------------------------------------------------------------
    //
    // Legs A and E cover the two extremes: everything live at once, and one thing live at a time.
    // This is the shape in between, and the one a per-frame pool gets wrong: a WHOLE ROUND's worth
    // of resources acquired, used, and released together, over and over. 320 textures are created
    // across the rounds, five times D3D12's starting capacity, but only 40 are ever live.
    //
    // No Present() here on purpose. A public frame boundary is not portable in this fixture's
    // environment -- the D3D12 renderer has no swap chain under this dev loop (DX-100/DX-102) and
    // throws from Present -- and the property being measured is reclamation between rounds, which
    // does not need one. The fence-gated, submission-crossing half of that property is measured
    // natively instead, in examples/directx12_descriptor_allocator_test.cpp's own frame leg.

    void RunK(GraphicsDevice& dev)
    {
        constexpr int kRounds = 8;
        constexpr int kPerRound = 40; // 320 total, five times the starting capacity
        const SamplerState sampler = MakeSampler();

        int wrong = 0;
        for (int round = 0; round < kRounds; ++round)
        {
            std::vector<std::unique_ptr<Texture2D>> textures;
            auto rt = MakeTarget(dev);
            for (int i = 0; i < kPerRound; ++i)
                textures.push_back(MakeIdentityTexture(dev, round * kPerRound + i));
            for (int i = 0; i < kPerRound; ++i)
                if (DrawAndDecode(dev, *rt, *textures[static_cast<std::size_t>(i)], sampler) !=
                    round * kPerRound + i)
                    ++wrong;
            // Round boundary: every texture and the target are released together here.
        }
        check(wrong == 0,
              "K1: " + std::to_string(kRounds) + " rounds x " + std::to_string(kPerRound) +
                  " distinct textures, all released between rounds (wrong=" + std::to_string(wrong) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // L -- destroy then recreate; no stale content may survive
    // ---------------------------------------------------------------------------------------------

    void RunL(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev);
        const SamplerState sampler = MakeSampler();

        // Fill the pool well past its starting capacity, then drop everything, then recreate a
        // smaller set with DIFFERENT identities. A recycled slot that still carries the old
        // descriptor decodes to an identity from the first generation, which cannot appear here.
        {
            std::vector<std::unique_ptr<Texture2D>> first;
            for (int i = 0; i < 100; ++i) first.push_back(MakeIdentityTexture(dev, i));
            for (int i = 0; i < 100; ++i)
                (void)DrawAndDecode(dev, *rt, *first[static_cast<std::size_t>(i)], sampler);
        }

        std::vector<std::unique_ptr<Texture2D>> second;
        for (int i = 0; i < 100; ++i) second.push_back(MakeIdentityTexture(dev, 1000 + i));
        int wrong = 0;
        int staleFromFirstGeneration = 0;
        for (int i = 0; i < 100; ++i)
        {
            const int got = DrawAndDecode(dev, *rt, *second[static_cast<std::size_t>(i)], sampler);
            if (got != 1000 + i) ++wrong;
            if (got >= 0 && got < 100) ++staleFromFirstGeneration;
        }
        check(wrong == 0 && staleFromFirstGeneration == 0,
              "L1: 100 textures recreated after 100 were destroyed decode as the NEW generation "
              "(wrong=" + std::to_string(wrong) + ", decoded-as-destroyed=" +
                  std::to_string(staleFromFirstGeneration) + ")");

        // Interleaved churn: destroy one, create one, repeatedly, so slots are reused while other
        // resources stay live around them.
        std::vector<std::unique_ptr<Texture2D>> live;
        for (int i = 0; i < 40; ++i) live.push_back(MakeIdentityTexture(dev, 2000 + i));
        int churnWrong = 0;
        for (int step = 0; step < 200; ++step)
        {
            const auto slot = static_cast<std::size_t>(step % 40);
            const int id = 3000 + step;
            live[slot] = MakeIdentityTexture(dev, id);
            if (DrawAndDecode(dev, *rt, *live[slot], sampler) != id) ++churnWrong;
            // A neighbour that was NOT replaced must still be intact.
            const auto neighbour = static_cast<std::size_t>((step + 7) % 40);
            const int neighbourGot = DrawAndDecode(dev, *rt, *live[neighbour], sampler);
            if (neighbourGot < 0) ++churnWrong;
        }
        check(churnWrong == 0,
              "L2: 200 interleaved destroy/create steps around 40 live textures (wrong=" +
                  std::to_string(churnWrong) + ")");
    }

    // ---------------------------------------------------------------------------------------------
    // M -- cube and volume textures, where supported
    // ---------------------------------------------------------------------------------------------

    void RunM(GraphicsDevice& dev)
    {
        if (!cubeSupported_)
        {
            note(std::string(kRendererName) + " cannot create a TextureCube; M1 is not observable");
            return;
        }

        // 70 live cubes, each read back through its own public GetData -- past the starting
        // capacity, and the readback proves the resource is real rather than merely constructed.
        constexpr int kCubes = 70;
        std::vector<std::unique_ptr<TextureCube>> cubes;
        for (int i = 0; i < kCubes; ++i)
        {
            auto cube = std::make_unique<TextureCube>(dev, 2, false, SurfaceFormat::Color);
            const std::array<Color, 4> face = EncodeIdentity(i);
            for (int f = 0; f < 6; ++f)
                cube->SetData(static_cast<CubeMapFace>(f), face.data(), 4);
            cubes.push_back(std::move(cube));
        }

        int wrong = 0;
        int unreadable = 0;
        for (int i = 0; i < kCubes; ++i)
        {
            std::vector<Color> pix(4, Color(0, 0, 0, 0));
            bool ok = true;
            try
            {
                cubes[static_cast<std::size_t>(i)]->GetData(CubeMapFace::PositiveX, pix.data(), 4);
            }
            catch (...)
            {
                ok = false;
            }
            if (!ok) { ++unreadable; continue; }
            if (DecodeIdentity(pix) != i) ++wrong;
        }
        if (unreadable == kCubes)
        {
            note(std::string(kRendererName) +
                 " rejects TextureCube::GetData; M1 asserts creation only at this cardinality");
            check(true, "M1: " + std::to_string(kCubes) +
                            " simultaneously live TextureCubes were all created");
        }
        else
        {
            check(wrong == 0 && unreadable == 0,
                  "M1: " + std::to_string(kCubes) +
                      " simultaneously live TextureCubes each read back as themselves (wrong=" +
                      std::to_string(wrong) + ", unreadable=" + std::to_string(unreadable) + ")");
        }
    }

public:
    DescriptorCapacityContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int Result() const { return result_; }

    void Draw(const GameTime& gameTime) override
    {
        Game::Draw(gameTime);
        if (done_) { Exit(); return; }
        done_ = true;

        if (!kRasterizes)
        {
            std::printf("[SKIP] %s does not rasterize; descriptor capacity is not observable\n",
                        kRendererName);
            result_ = 77;
            Exit();
            return;
        }

        GraphicsDevice& dev = getGraphicsDeviceProperty();
        std::printf("=== REMED-GFX-177 descriptor capacity contract -- renderer %s ===\n", kRendererName);

        // One probe, before any leg: construct a cube and write one face. Whatever this renderer
        // does with cube textures, it does it here and not inside a cardinality measurement.
        try
        {
            TextureCube probe(dev, 2, false, SurfaceFormat::Color);
            const std::array<Color, 4> face = EncodeIdentity(7);
            probe.SetData(CubeMapFace::PositiveX, face.data(), 4);
            cubeSupported_ = true;
        }
        catch (...)
        {
            cubeSupported_ = false;
        }

        runLeg("A", [&] { RunA(dev); });
        runLeg("B", [&] { RunB(dev); });
        runLeg("C", [&] { RunC(dev); });
        runLeg("D", [&] { RunD(dev); });
        runLeg("E", [&] { RunE(dev); });
        runLeg("F", [&] { RunF(dev); });
        runLeg("G", [&] { RunG(dev); });
        runLeg("H", [&] { RunH(dev); });
        runLeg("I", [&] { RunI(dev); });
        runLeg("J", [&] { RunJ(dev); });
        runLeg("K", [&] { RunK(dev); });
        runLeg("L", [&] { RunL(dev); });
        runLeg("M", [&] { RunM(dev); });

        std::printf("=== %d/%d checks passed on %s ===\n", passCount_, totalCount_, kRendererName);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }
};

int main()
{
    DescriptorCapacityContractTest game;
    game.Run();
    return game.Result();
}
