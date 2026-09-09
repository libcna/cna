// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-169: every stock 3D effect must sample its textures through the public
// `GraphicsDevice.SamplerStates[slot]`, exactly like SpriteBatch already does.
//
// THE CONTRACT. `SamplerStates[slot]` defines filtering and addressing for the texture bound to
// that slot. It is captured when the draw is issued, so changing it afterwards cannot retroactively
// alter an already-queued draw, and two draws that share a texture but not a sampler must remain
// observably different. Texture identity and sampler identity are INDEPENDENT descriptor inputs: a
// renderer that caches descriptor sets must key on both, or the second draw silently inherits the
// first one's sampler. An unset slot may use the documented default; an explicit sampler must never
// be replaced by it.
//
// THE DEFECT this reproduces is Vulkan-local and is the stock-3D half of a fix SpriteBatch already
// received. Vulkan's `ApplySamplerState` is correct -- it builds and caches a per-slot `VkSampler`
// into `slotSamplers_[slot]` from a complete, correct XNA ordinal mapping -- and SpriteBatch reads
// it (`VulkanSpriteBatchRenderer::FlushTexture`, whose own comment records the identical fix being
// applied there under Task 665: "previously always used the texture's own pre-baked descriptor set,
// built once at texture-load time with a fixed default sampler, completely bypassing
// SetSamplerFilter/SetSamplerAddressMode"). The stock 3D descriptor builders never received that
// fix: six of them take NO sampler parameter at all, so the sampler is neither written into the
// descriptor nor present in the cache key, and every one of their 15 combined-image-sampler
// bindings is hardcoded to `defaultSampler_` (VK_FILTER_LINEAR + VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_
// EDGE). `PointClamp` therefore still filters linearly and `LinearWrap` still clamps.
//
// WHICH FAMILIES. Vulkan's replay dispatch selects exactly one descriptor set per draw:
//
//   useAlphaTest   -> descSet            GetOrCreateTexSamplerDescSet(view, slotSamplers_[0])  OK
//   useDualTexture -> dualTexDescSet     GetOrCreateDualTexDescSet(..., sampler0, sampler1)    OK
//   useEnvMap      -> envMapDescSet      GetOrCreateEnvMapDescSet                          BROKEN
//   useSkinned     -> skinnedDescSet     GetOrCreateSkinnedDescSet                         BROKEN
//   usePbr         -> pbrDescSet         GetOrCreatePbr(Skinned)DescSet                    BROKEN
//   useLitTextured -> litTexturedDescSet GetOrCreateLitTexturedDescSet                     BROKEN
//   useFogTex3D    -> fogTex3DDescSet    GetOrCreateFogTex3DDescSet                        BROKEN
//
// `useFogTex3D` is set for ANY plain textured BasicEffect draw -- it is the fog-CAPABLE bundle, not
// "fog is on" -- so the most ordinary textured 3D draw in the API binds a broken descriptor even
// though the correct `descSet` was computed one line earlier and then went unused.
//
// THE ORACLE is modulation-independent, because a lit or fogged effect scales the sampled texel and
// an exact-RGBA expectation would then be asserting the lighting model rather than the sampler.
// Instead: a 4x4 texture magnified 4x onto a 16x16 target must produce 4x4 BLOCKS of 4x4 pixels,
// each block internally CONSTANT, with horizontally and vertically adjacent blocks DIFFERENT. Point
// filtering satisfies that for any spatially uniform modulation; bilinear filtering cannot, because
// it varies continuously inside every block. Every lit family is driven with ambient-only lighting
// (all directional lights disabled, AmbientLightColor = white) precisely so the modulation IS
// spatially uniform and the block structure measures the sampler alone. Where the modulation is the
// identity -- plain BasicEffect and AlphaTestEffect -- the exact stored texel is asserted as well.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   P   PointClamp             block-uniform, at most 16 distinct colours
//   L   LinearClamp            NOT block-uniform -- the differential, so P cannot pass on a
//                              renderer that ignores the sampler and happens to filter linearly...
//                              which is exactly the pre-fix state, and is why P alone is not enough
//   W   PointWrap vs PointClamp with UVs to 2.0: the two images must DIFFER
//   K   descriptor-cache key   two draws, ONE frame, SAME texture, different samplers -> the two
//                              halves must differ. This is the check a sampler-blind cache key
//                              fails even after the descriptor write is corrected
//   S   deferred snapshot      set PointClamp, queue draw A; set LinearClamp, queue draw B; render
//                              once -> A is block-uniform and B is not
//   M   multi-slot             slot 0 and slot 1 given different samplers, then swapped
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
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/Exception.hpp"

#include <algorithm>
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
#elif defined(CNA_RENDERER_LLGL)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "LLGL";
#else
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "UNKNOWN";
#endif

    constexpr int kBBW = 128;
    constexpr int kBBH = 96;
    /// 4x4 texture magnified 4x, so one texel is one 4x4 block of the destination.
    constexpr int kRT = 16;
    constexpr int kTex = 4;
    constexpr int kBlock = kRT / kTex;

    const Color kSentinel(11, 13, 17, 255);

    /// Stride-20 VertexPositionTexture.
    struct VtxPT { float px, py, pz; float u, v; };
    static_assert(sizeof(VtxPT) == 20, "stride 20");
    /// plans/plan_webgpu.md WEBGPU-159: a stride-28 Position + TEXCOORD0 + TEXCOORD1 vertex, for the
    /// one assertion below that needs `DualTextureEffect`'s SECOND UV set to exist -- see
    /// `RunM_MultiSlot`'s M3 for why a single-UV mesh cannot carry that assertion.
    struct VtxPTT { float px, py, pz; float u, v; float u1, v1; };
    static_assert(sizeof(VtxPTT) == 28, "stride 28");
    /// Stride-32 VertexPositionNormalTexture.
    struct VtxPNT { float px, py, pz; float nx, ny, nz; float u, v; };
    static_assert(sizeof(VtxPNT) == 32, "stride 32");
    /// Stride-52 VertexPositionNormalTextureSkinned.
    struct VtxPNTS { float px, py, pz; float nx, ny, nz; float u, v;
                     float w0, w1, w2, w3; std::uint8_t i0, i1, i2, i3; };
    static_assert(sizeof(VtxPNTS) == 52, "stride 52");
    /// Stride-48 VertexPositionNormalTangentTexture.
    struct VtxPbr { float px, py, pz; float nx, ny, nz; float tx, ty, tz, tw; float u, v; };
    static_assert(sizeof(VtxPbr) == 48, "stride 48");
    /// Stride-68 VertexPositionNormalTangentTextureSkinned.
    struct VtxPbrS { float px, py, pz; float nx, ny, nz; float tx, ty, tz, tw; float u, v;
                     float w0, w1, w2, w3; std::uint8_t i0, i1, i2, i3; };
    static_assert(sizeof(VtxPbrS) == 68, "stride 68");

    /// A full-viewport quad in NDC with identity matrices, so destination pixel (x,y) reads texture
    /// coordinate ((x+0.5)/kRT * uvMax, (y+0.5)/kRT * uvMax) with no projection to reason about.
    template <typename V>
    std::vector<V> MakeQuad(float uvMax)
    {
        V tl{}, bl{}, br{}, tr{};
        auto place = [](V& v, float x, float y) {
            v.px = x; v.py = y; v.pz = 0.0f;
        };
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
    void FillTangents(std::vector<V>& v)
    {
        for (auto& x : v) { x.tx = 1.0f; x.ty = 0.0f; x.tz = 0.0f; x.tw = 1.0f; }
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

    std::uint32_t Pack(const Color& c)
    {
        return (static_cast<std::uint32_t>(c.getRProperty()) << 24) |
               (static_cast<std::uint32_t>(c.getGProperty()) << 16) |
               (static_cast<std::uint32_t>(c.getBProperty()) << 8) |
                static_cast<std::uint32_t>(c.getAProperty());
    }
}

class StockEffectSamplerContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    /// 4x4, every texel unique, first/last rows and columns strongly different, 0 and 255 channels
    /// present, mid-tones present, and asymmetric under both reflections.
    std::vector<Color> pattern_;
    std::vector<Color> pattern2_;
    Texture2D tex_, tex2_, white_;
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

    static std::vector<Color> MakePattern(int seed)
    {
        std::vector<Color> p;
        p.reserve(16);
        static const int kB[4] = {255, 40, 190, 0};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                p.emplace_back(static_cast<std::uint8_t>((x * 80 + seed * 7) & 0xFF),
                               static_cast<std::uint8_t>((y * 80 + seed * 23) & 0xFF),
                               static_cast<std::uint8_t>((kB[x] ^ (y * 37)) & 0xFF),
                               static_cast<std::uint8_t>(255));
        return p;
    }

    static std::vector<std::uint8_t> Bytes(const std::vector<Color>& p)
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

    // ------------------------------------------------------------------------------------------
    // Oracles
    // ------------------------------------------------------------------------------------------

    /// True when the image is made of `kTex/scale`-sized constant blocks: every pixel inside a block
    /// equals that block's first pixel. `blockPx` is the block edge in destination pixels.
    static bool BlockUniform(const std::vector<Color>& pix, int blockPx, std::string& why)
    {
        for (int by = 0; by < kRT; by += blockPx)
            for (int bx = 0; bx < kRT; bx += blockPx)
            {
                const Color& ref = pix[static_cast<std::size_t>(by) * kRT + bx];
                for (int y = by; y < by + blockPx; ++y)
                    for (int x = bx; x < bx + blockPx; ++x)
                        if (!SameColor(pix[static_cast<std::size_t>(y) * kRT + x], ref))
                        {
                            why = "block(" + std::to_string(bx / blockPx) + "," +
                                  std::to_string(by / blockPx) + ") varies: (" + std::to_string(bx) +
                                  "," + std::to_string(by) + ")=" + Str(ref) + " vs (" +
                                  std::to_string(x) + "," + std::to_string(y) + ")=" +
                                  Str(pix[static_cast<std::size_t>(y) * kRT + x]);
                            return false;
                        }
            }
        return true;
    }

    /// Adjacent blocks must differ, so a uniform fill (a draw that produced nothing, or a texture
    /// that collapsed to one colour) cannot pass BlockUniform vacuously.
    static bool BlocksDistinct(const std::vector<Color>& pix, int blockPx, int& distinct)
    {
        std::set<std::uint32_t> seen;
        for (int by = 0; by < kRT; by += blockPx)
            for (int bx = 0; bx < kRT; bx += blockPx)
                seen.insert(Pack(pix[static_cast<std::size_t>(by) * kRT + bx]));
        distinct = static_cast<int>(seen.size());
        return distinct >= 4;
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

    // ------------------------------------------------------------------------------------------
    // Device helpers
    // ------------------------------------------------------------------------------------------

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

    static void ResetDeviceState(GraphicsDevice& dev, int w, int h)
    {
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    /// Draws one quad through `draw` into a fresh target and returns the whole target.
    std::vector<Color> Render(GraphicsDevice& dev, const std::function<void(GraphicsDevice&)>& draw)
    {
        RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);
        draw(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    // ------------------------------------------------------------------------------------------
    // One draw per stock-effect family. Every lit family uses AMBIENT-ONLY lighting with all
    // directional lights disabled, so the modulation is spatially uniform and the block structure
    // measures the sampler rather than the lighting model.
    // ------------------------------------------------------------------------------------------

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
    static void DrawQuad(GraphicsDevice& dev, const std::vector<V>& verts)
    {
        VertexBuffer vb(dev, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(V)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    /// The same draw with an explicit declaration, for a layout no canonical stride table names.
    template <typename V>
    static void DrawDeclaredQuad(GraphicsDevice& dev, const std::vector<V>& verts,
                                 const VertexDeclaration& declaration)
    {
        VertexBuffer vb(dev, declaration, static_cast<int>(verts.size()), BufferUsage::None);
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(V)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawBasicPlain(GraphicsDevice& dev, float uvMax)
    {
        BasicEffect fx(dev);
        Matrices(fx);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex_);
        fx.setLightingEnabledProperty(false);
        fx.Apply();
        DrawQuad(dev, MakeQuad<VtxPT>(uvMax));
    }

    void DrawBasicLit(GraphicsDevice& dev, float uvMax)
    {
        BasicEffect fx(dev);
        Matrices(fx);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex_);
        AmbientOnly(fx);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.Apply();
        auto v = MakeQuad<VtxPNT>(uvMax);
        FillNormals(v);
        DrawQuad(dev, v);
    }

    void DrawAlphaTest(GraphicsDevice& dev, float uvMax)
    {
        AlphaTestEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex_);
        fx.Apply();
        DrawQuad(dev, MakeQuad<VtxPT>(uvMax));
    }

    void DrawDualTexture(GraphicsDevice& dev, float uvMax)
    {
        DualTextureEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex_);
        fx.setTexture2Property(&white_);
        fx.Apply();
        DrawQuad(dev, MakeQuad<VtxPT>(uvMax));
    }

    void DrawEnvMapBase(GraphicsDevice& dev, float uvMax)
    {
        EnvironmentMapEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex_);
        fx.setEnvironmentMapProperty(cube_.get());
        // The cube contributes nothing, so this leg measures the BASE texture's slot-0 sampler.
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        AmbientOnly(fx);
        fx.Apply();
        auto v = MakeQuad<VtxPNT>(uvMax);
        FillNormals(v);
        DrawQuad(dev, v);
    }

    void DrawSkinned(GraphicsDevice& dev, float uvMax)
    {
        SkinnedEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex_);
        AmbientOnly(fx);
        fx.setSpecularColorProperty(Vector3::Zero);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.Apply();
        auto v = MakeQuad<VtxPNTS>(uvMax);
        FillNormals(v);
        FillSkin(v);
        DrawQuad(dev, v);
    }

    void DrawPbr(GraphicsDevice& dev, float uvMax)
    {
        PbrEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex_);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.Apply();
        auto v = MakeQuad<VtxPbr>(uvMax);
        FillNormals(v);
        FillTangents(v);
        DrawQuad(dev, v);
    }

    void DrawPbrSkinned(GraphicsDevice& dev, float uvMax)
    {
        SkinnedPbrEffect fx(dev);
        Matrices(fx);
        fx.setTextureProperty(&tex_);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.Apply();
        auto v = MakeQuad<VtxPbrS>(uvMax);
        FillNormals(v);
        FillTangents(v);
        FillSkin(v);
        DrawQuad(dev, v);
    }

    struct Family
    {
        const char* name;
        void (StockEffectSamplerContractTest::*draw)(GraphicsDevice&, float);
        /// The Vulkan descriptor builder this family routes to, named so a failure points at it.
        const char* descriptorPath;
        /// True when the effect's modulation is the identity, so the exact stored texel is asserted.
        bool identityModulation;
    };

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        pattern_ = MakePattern(0);
        pattern2_ = MakePattern(3);
        tex_ = Texture2D::CreateFromPixels(dev, kTex, kTex, Bytes(pattern_));
        tex2_ = Texture2D::CreateFromPixels(dev, kTex, kTex, Bytes(pattern2_));
        white_ = Texture2D::CreateFromPixels(dev, 1, 1,
                     std::vector<std::uint8_t>{255, 255, 255, 255});
        // A non-rasterizing renderer rejects cube uploads by REMED-GFX-135's contract, and it never
        // reaches the effect families anyway, so an unavailable cube must not abort the run before
        // the honest-rejection leg gets to assert anything.
        try
        {
            cube_ = std::make_unique<TextureCube>(dev, kTex, false, SurfaceFormat::Color);
            for (int f = 0; f < 6; ++f)
                cube_->SetData(static_cast<CubeMapFace>(f), pattern_.data(),
                               static_cast<int>(pattern_.size()));
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

        std::printf("=== REMED-GFX-169 stock-effect sampler contract on %s ===\n", kRendererName);

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

        static const Family kFamilies[] = {
            {"BasicEffect(plain textured)", &StockEffectSamplerContractTest::DrawBasicPlain,
             "fogTex3DDescSet",     true},
            {"BasicEffect(lit textured)",   &StockEffectSamplerContractTest::DrawBasicLit,
             "litTexturedDescSet",  false},
            {"AlphaTestEffect",             &StockEffectSamplerContractTest::DrawAlphaTest,
             "descSet (already correct)", true},
            {"DualTextureEffect slot0",     &StockEffectSamplerContractTest::DrawDualTexture,
             "dualTexDescSet (already correct)", false},
            {"EnvironmentMapEffect base",   &StockEffectSamplerContractTest::DrawEnvMapBase,
             "envMapDescSet",       false},
            {"SkinnedEffect",               &StockEffectSamplerContractTest::DrawSkinned,
             "skinnedDescSet",      false},
            {"PbrEffect",                   &StockEffectSamplerContractTest::DrawPbr,
             "pbrDescSet",          false},
            {"SkinnedPbrEffect",            &StockEffectSamplerContractTest::DrawPbrSkinned,
             "pbrDescSet(skinned)", false},
        };

        for (const Family& f : kFamilies) RunFamily(dev, f);
        RunK_DescriptorCacheKey(dev);
        RunS_DeferredSnapshot(dev);
        RunM_MultiSlot(dev);
        Finish();
    }

private:
    void Finish()
    {
        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

    void RunFamily(GraphicsDevice& dev, const Family& f)
    {
        const std::string tag = std::string(f.name) + " [" + f.descriptorPath + "]";

        // P -- PointClamp must produce exact 4x4 blocks.
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        // A renderer may not implement this family's vertex stride at all -- SOFTWARE, for one,
        // documents 16/20/24/32/52 and rejects the PBR strides 48/68. That is a declared capability
        // boundary, not a sampler defect, so it is reported as a skip rather than counted as a
        // failure; it is NOT a renderer-specific exception to the sampler contract, because a
        // renderer that DOES draw the family is still held to every check below.
        std::vector<Color> point;
        try
        {
            point = Render(dev, [&](GraphicsDevice& d) { (this->*f.draw)(d, 1.0f); });
        }
        catch (const std::exception& e)
        {
            std::printf("[SKIP] %s: this renderer does not implement the family (%s)\n",
                        tag.c_str(), e.what());
            std::fflush(stdout);
            return;
        }
        std::string why;
        const bool blocky = BlockUniform(point, kBlock, why);
        int blocks = 0;
        const bool distinctBlocks = BlocksDistinct(point, kBlock, blocks);
        check(distinctBlocks, tag + " P0: the draw produced a real magnified image (" +
                              std::to_string(blocks) + " distinct blocks)");
        check(blocky, tag + " P1: PointClamp gives constant 4x4 blocks -- one texel per block. " +
                      (blocky ? std::string() : why));
        const int distinct = DistinctColors(point);
        check(distinct <= kTex * kTex,
              tag + " P2: PointClamp produces at most " + std::to_string(kTex * kTex) +
              " distinct colours (got " + std::to_string(distinct) + ")");

        if (f.identityModulation)
        {
            int bad = 0;
            for (int y = 0; y < kRT; ++y)
                for (int x = 0; x < kRT; ++x)
                {
                    const Color& want = pattern_[static_cast<std::size_t>(y / kBlock) * kTex +
                                                 static_cast<std::size_t>(x / kBlock)];
                    const Color& got = point[static_cast<std::size_t>(y) * kRT + x];
                    if (got.getRProperty() != want.getRProperty() ||
                        got.getGProperty() != want.getGProperty() ||
                        got.getBProperty() != want.getBProperty()) ++bad;
                }
            check(bad == 0, tag + " P3: every pixel is the exact stored texel (mismatches=" +
                            std::to_string(bad) + ")");
        }

        // L -- LinearClamp must NOT be block-uniform. Without this, P1 would also pass on a
        // renderer that ignores the sampler and happens to be filtering linearly is impossible --
        // but P1 alone WOULD pass on one that ignores it and happens to be filtering by point.
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        const std::vector<Color> linear =
            Render(dev, [&](GraphicsDevice& d) { (this->*f.draw)(d, 1.0f); });
        std::string why2;
        check(!BlockUniform(linear, kBlock, why2),
              tag + " L1: LinearClamp interpolates -- the same draw is NOT block-uniform");
        check(DiffPixels(point, linear) > 0,
              tag + " L2: Point and Linear are different images");

        // W -- address mode, driven past [0,1] by UVs to 2.0.
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> pclamp =
            Render(dev, [&](GraphicsDevice& d) { (this->*f.draw)(d, 2.0f); });
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Wrap);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Wrap);
        const std::vector<Color> pwrap =
            Render(dev, [&](GraphicsDevice& d) { (this->*f.draw)(d, 2.0f); });
        check(DiffPixels(pclamp, pwrap) > 0,
              tag + " W1: PointWrap and PointClamp differ when UVs leave [0,1]");
        std::string why3;
        check(BlockUniform(pwrap, kBlock / 2, why3),
              tag + " W2: PointWrap still selects one texel per sample (2x2 blocks at UV 2.0). " +
              (why3.empty() ? std::string() : why3));

        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Wrap);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Wrap);
    }

    /// The descriptor-cache-key check: two draws, ONE frame, SAME texture, different samplers.
    /// A cache keyed only on the image view hands the second draw the first one's descriptor.
    void RunK_DescriptorCacheKey(GraphicsDevice& dev)
    {
        RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);
        // Left half point, right half linear, same texture, same frame, no flush between them.
        dev.setViewportProperty(Viewport(0, 0, kRT / 2, kRT));
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        DrawBasicPlain(dev, 1.0f);
        dev.setViewportProperty(Viewport(kRT / 2, 0, kRT / 2, kRT));
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Clamp);
        DrawBasicPlain(dev, 1.0f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));

        // The two halves render the same texture over the same UV range, so with correct sampling
        // the left half is piecewise constant in x and the right half is not. Each half is drawn
        // into a viewport of HALF the target's width, so the 4-texel texture is magnified 2x, not
        // 4x, and one texel is 2 destination pixels wide -- not kBlock. Using kBlock here would
        // count the genuine texel boundaries at x=2 and x=6 as failures (2 per row x 16 rows = the
        // 32 that a correct point sampler still reports).
        constexpr int kHalfBlock = (kRT / 2) / kTex;
        int leftVar = 0, rightVar = 0;
        for (int y = 0; y < kRT; ++y)
        {
            for (int x = 1; x < kRT / 2; ++x)
                if ((x % kHalfBlock) != 0 &&
                    !SameColor(pix[static_cast<std::size_t>(y) * kRT + x],
                               pix[static_cast<std::size_t>(y) * kRT + x - 1])) ++leftVar;
            for (int x = kRT / 2 + 1; x < kRT; ++x)
                if (!SameColor(pix[static_cast<std::size_t>(y) * kRT + x],
                               pix[static_cast<std::size_t>(y) * kRT + x - 1])) ++rightVar;
        }
        check(leftVar == 0,
              "K1: the PointClamp half stays piecewise constant when a LinearClamp draw follows it "
              "in the SAME frame (variations inside blocks=" + std::to_string(leftVar) + ")");
        check(rightVar > 0,
              "K2: the LinearClamp half really did interpolate, so K1 is not two point draws "
              "(variations=" + std::to_string(rightVar) + ")");
    }

    /// A queued draw must observe the sampler that was public when it was issued, not the one
    /// current at replay.
    void RunS_DeferredSnapshot(GraphicsDevice& dev)
    {
        RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        ResetDeviceState(dev, kRT, kRT);
        dev.Clear(kSentinel);
        dev.setViewportProperty(Viewport(0, 0, kRT, kRT / 2));
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        DrawBasicPlain(dev, 1.0f);
        // Change the public sampler AFTER the first draw is queued and before anything is replayed.
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Clamp);
        dev.setViewportProperty(Viewport(0, kRT / 2, kRT, kRT / 2));
        DrawBasicPlain(dev, 1.0f);
        // And once more, so the LAST public state differs from both draws' captured states.
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Wrap);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetDeviceState(dev, kBBW, kBBH);
        std::vector<Color> pix(static_cast<std::size_t>(kRT) * kRT, Color(0, 0, 0, 0));
        rt.GetData(pix.data(), 0, static_cast<int>(pix.size()));

        int topVar = 0, bottomVar = 0;
        for (int y = 0; y < kRT / 2; ++y)
            for (int x = 1; x < kRT; ++x)
                if ((x % kBlock) != 0 &&
                    !SameColor(pix[static_cast<std::size_t>(y) * kRT + x],
                               pix[static_cast<std::size_t>(y) * kRT + x - 1])) ++topVar;
        for (int y = kRT / 2; y < kRT; ++y)
            for (int x = 1; x < kRT; ++x)
                if (!SameColor(pix[static_cast<std::size_t>(y) * kRT + x],
                               pix[static_cast<std::size_t>(y) * kRT + x - 1])) ++bottomVar;
        check(topVar == 0,
              "S1: a draw queued under PointClamp keeps it after the public sampler is changed twice "
              "(variations inside blocks=" + std::to_string(topVar) + ")");
        check(bottomVar > 0,
              "S2: the draw queued under LinearClamp kept ITS sampler too (variations=" +
              std::to_string(bottomVar) + ")");
    }

    /// DualTextureEffect's two slots must be independent: slot 0 and slot 1 given different
    /// samplers, then swapped, must produce different images.
    void RunM_MultiSlot(GraphicsDevice& dev)
    {
        auto drawDual = [&](GraphicsDevice& d) {
            DualTextureEffect fx(d);
            Matrices(fx);
            fx.setTextureProperty(&tex_);
            fx.setTexture2Property(&tex2_);
            fx.Apply();
            DrawQuad(d, MakeQuad<VtxPT>(1.0f));
        };

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        std::vector<Color> a;
        try
        {
            a = Render(dev, drawDual);
        }
        catch (const std::exception& e)
        {
            // D3D9 declares stride 20 with vertexColor=false unsupported for DualTextureEffect
            // (plans/plan_dx9.md D9-82d), so it has no two-slot stock family to measure here.
            std::printf("[SKIP] M: this renderer does not implement DualTextureEffect (%s)\n", e.what());
            std::fflush(stdout);
            return;
        }

        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> b = Render(dev, drawDual);

        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Point, TextureAddressMode::Clamp);
        const std::vector<Color> both = Render(dev, drawDual);

        check(DiffPixels(a, b) > 0,
              "M1: DualTextureEffect -- swapping the samplers of slot 0 and slot 1 changes the image "
              "(differing=" + std::to_string(DiffPixels(a, b)) + ")");
        std::string why;
        check(BlockUniform(both, kBlock, why),
              "M2: both slots PointClamp -- the combined image is block-uniform. " + why);
        // plans/plan_webgpu.md WEBGPU-159. M3 used to make this assertion on the SAME single-UV quad
        // M1/M2 use, and that premise is unsound: `DualTextureEffect` samples Texture2 with
        // TEXCOORD1, and a mesh that declares no TEXCOORD1 supplies (0,0,0,1) for it -- D3D9's rule,
        // which XNA is defined against, and equally OpenGL's disabled generic vertex attribute. So
        // Texture2 is sampled at ONE constant coordinate, its magnification filter cannot be
        // observed, and the image stays block-uniform however slot 1 is set. It passed only on
        // renderers that sampled both textures with TEXCOORD0; the reference renderer, which binds
        // TEXCOORD1 by semantic, has been failing it.
        //
        // Asked properly: a quad that DOES carry a second UV set. A renderer whose native layout
        // cannot express a stride-28 dual-UV declaration refuses the draw, and says so here rather
        // than being credited with an assertion it never ran.
        auto dualUv = MakeQuad<VtxPTT>(1.0f);
        for (auto& vertex : dualUv) { vertex.u1 = vertex.u; vertex.v1 = vertex.v; }
        const VertexDeclaration dualUvDeclaration(
            28,
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
             VertexElement(12, VertexElementFormat::Vector2,
                           VertexElementUsage::TextureCoordinate, 0),
             VertexElement(20, VertexElementFormat::Vector2,
                           VertexElementUsage::TextureCoordinate, 1)});
        auto drawDualUv = [&](GraphicsDevice& d) {
            DualTextureEffect fx(d);
            Matrices(fx);
            fx.setTextureProperty(&tex_);
            fx.setTexture2Property(&tex2_);
            fx.Apply();
            DrawDeclaredQuad(d, dualUv, dualUvDeclaration);
        };
        SetSlot(dev, 0, TextureFilter::Point, TextureAddressMode::Clamp);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Clamp);
        try
        {
            const std::vector<Color> secondUv = Render(dev, drawDualUv);
            check(!BlockUniform(secondUv, kBlock, why),
                  "M3: with a real TEXCOORD1, slot 1 Linear alone breaks block uniformity, so "
                  "slot 1's sampler really is consulted");
        }
        catch (const std::exception& e)
        {
            std::printf("[SKIP] M3: this renderer cannot express a stride-28 dual-UV declaration "
                        "(%s)\n", e.what());
            std::fflush(stdout);
        }
        SetSlot(dev, 0, TextureFilter::Linear, TextureAddressMode::Wrap);
        SetSlot(dev, 1, TextureFilter::Linear, TextureAddressMode::Wrap);
    }

public:
    StockEffectSamplerContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    StockEffectSamplerContractTest game;
    game.Run();
    return game.getResult();
}
