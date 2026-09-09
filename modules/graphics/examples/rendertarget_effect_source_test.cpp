// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-152: a RenderTarget2D handed to a stock or custom 3D effect as its TEXTURE must be
// sampled, not reinterpreted.
//
//     SetRenderTarget(A)                  // producer
//     draw a pattern into A
//     SetRenderTarget(B or nullptr)
//     effect.Texture = A                  // consumer -- A is now an ordinary sampler source
//     DrawUserPrimitives(...)
//
// THE DEFECT this file reproduces is SDL_GPU-local, and it is a TYPE defect, not a
// synchronization, ordering, layout or resource-visibility one. A public `Texture2D` and a public
// `RenderTarget2D` reach every draw as the same static type, `const ITextureRenderer*`. Their
// concrete renderers are `SdlGpuTextureRenderer` and `SdlGpuRenderTargetRenderer` -- unrelated
// SIBLINGS, both merely deriving from `ITextureRenderer`, and the former is even `final`, so a
// render target can never be one. Thirteen sites in `SdlGpuRenderer.cpp` nevertheless did
//
//     command.texture = static_cast<const SdlGpuTextureRenderer*>(params.texture0);
//
// and read `->Texture()` off the result at render time. For a render target that reads the object's
// bytes at the offset where a plain texture keeps its `SDL_GPUTexture*`, which in
// `SdlGpuRenderTargetRenderer` is `mipMap_`, three bytes of uninitialised padding, and
// `multiSampleCount_`. The fabricated handle then went straight to `SDL_BindGPUFragmentSamplers`.
//
// Measured, not inferred (`cmake-build-sdlgpu-ubsan`, `DISPLAY=:101`):
//
//     SdlGpuRenderer.cpp:3367: runtime error: downcast of address 0x… which does not point
//         to an object of type 'SdlGpuTextureRenderer'
//         note: object is of type 'CNA::Internal::Renderers::SdlGpu::SdlGpuRenderTargetRenderer'
//     SdlGpuRenderer.cpp:4817: runtime error: member call on address 0x… which does not …
//     SdlGpuRenderer.hpp:119:  runtime error: member access within address 0x… which does …
//
// followed by SIGSEGV (exit 139). SpriteBatch was never affected: its own `Draw` already asked the
// object with `dynamic_cast`. That asymmetry is the whole finding -- one binding route asked, the
// other assumed, and only the assuming one could die.
//
// THE FIX is a single shared resolver, `ResolveSampledTextureEXT` / `ResolveSampledCubeEXT`, which
// every SpriteBatch and every stock/custom effect binding route now calls. It returns an
// `SdlGpuSampledTextureEXT`: the sampleable native handle (for MSAA, the RESOLVED one -- the
// multisample attachment is `COLOR_TARGET`-only and is not a legal sampler binding at all) paired
// with a `shared_ptr` that keeps its owner alive for as long as the deferred command can still be
// replayed. So this file judges TWO contracts that used to be separate:
//
//   * TYPE     -- the object decides what it is, by dynamic_cast, at one place;
//   * LIFETIME -- a command replayed at Present cannot outlive the resource it binds. Ordinary
//                 textures released their handle IMMEDIATELY in `~SdlGpuTextureRenderer`, so a
//                 short-lived public Texture2D drawn and then destroyed inside one frame left a
//                 dangling native handle in a queued command. Render targets already deferred.
//
// THE ORACLE. Every consumer leg draws the render-target source and an ordinary `Texture2D` control
// holding the SAME expected bytes through identical geometry, sampler and blend state, in the same
// frame, and requires them byte-identical. The control is built from the pattern function, never
// from the producer's own `GetData` -- reading the producer would materialise the very state under
// test. `GetData` appears only to observe the CONSUMER's destination, never as proof that a source
// is valid.
//
// THE PATTERN is 8x4 -- deliberately non-square, so a transpose cannot pass -- and every one of its
// 32 texels is unique in (R, G). A stale, zero, uniform, mirrored, rotated or swapped image each
// fails distinguishably.
//
// PROCESS ISOLATION. The pre-fix failure is a SIGSEGV, not an assertion, so with no supervisor one
// bad leg takes down the whole CTest shard and reports nothing about the legs behind it. Run with
// no arguments this binary is a SUPERVISOR: it re-execs itself once per leg as `--leg=<id>`,
// waits, and classifies each child as PASS (exit 0), FAIL (exit 1 -- checks ran and disagreed) or
// CRASH (killed by a signal, reported by name). A crash is therefore a first-class, attributable
// result rather than a lost run, and every leg is measured even when an earlier one dies. Post-fix
// nothing crashes and the same binary is an ordinary conformance fixture; `--leg=<id>` still runs
// exactly one leg in-process for debugging.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   A1 the finding itself           RT A -> BasicEffect textured -> RT B, non-indexed
//   A2 the finding, indexed         the same through DrawUserIndexedPrimitives
//   A3 backbuffer destination       the same consumer drawing on the backbuffer
//   B1 stock-effect families        BasicEffect (+vertex colour), AlphaTest, DualTexture slots 0
//                                   and 1, EnvironmentMap diffuse, Skinned, Pbr, SkinnedPbr
//   C1 SpriteBatch + custom effect  the route that already worked, plus a custom ShaderEffect
//   D1 draw modes                   DrawUser / DrawUserIndexed / VB / VB+IB / instanced
//   E1 multiplicity                 several draws sampling A in ONE consumer pass; two targets
//                                   sampled in alternating order
//   F1 A -> B -> A                  a target that is producer, then consumer, then producer again
//   G1 dimensions                   equal and unequal producer/consumer extents
//   H1 cross-family production      SpriteBatch->3D, 3D->SpriteBatch, 3D->another 3D family
//   I1 samplers and UVs             PointClamp, LinearClamp, Wrap, and a UV subregion
//   J1 formats and mips             SurfaceFormat::Color, mipMap false and true
//   K1 MSAA                         an applied multisample producer must bind the RESOLVED texture
//   L1 cube                         RenderTargetCube faces sampled through EnvironmentMapEffect
//   M1 lifetime                     source destroyed before the frame renders; create/use/dispose
//                                   cycles; handle reuse; interleaved lifetimes
//   N1 earlier-frame control        a producer from a previous frame, consumed later
//   O1 honest failure               an unsupported texture must throw, deterministically
//   P1 ordered Clear interaction    REMED-GFX-156's ordered Clear must not erase a producer
//
// Exit code 0 = all checks PASS, 1 = any FAIL or CRASH.

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
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX152_CAN_FORK 1
#else
#define CNA_GFX152_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kPW = 8;   ///< producer pattern width  -- non-square on purpose
    constexpr int kPH = 4;   ///< producer pattern height

    /** @brief Whether this renderer rasterizes and can read a render target back at all. */
#if defined(CNA_RENDERER_HEADLESS)
    constexpr bool kRasterizes = false;
#else
    constexpr bool kRasterizes = true;
#endif

#if defined(CNA_RENDERER_HEADLESS)
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr const char* kRendererName = "SDL_RENDERER";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr const char* kRendererName = "DIRECTX12";
#elif defined(CNA_RENDERER_LLGL)
    constexpr const char* kRendererName = "LLGL";
#else
#error "REMED-GFX-152: this renderer has no declared render-target effect-source contract."
#endif

    /**
     * @brief Whether a custom `ShaderEffect` can be bound to a 3D draw on this renderer.
     *
     * SDL_GPU routes a custom effect through `SpriteBatch` only (`SetCustomEffect` lives on its
     * sprite renderer and the pipeline override is carried on `SpriteCommand`); its stock 3D paths
     * have no custom-shader slot at all. That is a real, pre-existing capability boundary of the
     * renderer, not something REMED-GFX-152 introduced or should paper over — so leg C1 exercises a
     * custom effect on the route that HAS one, and says so rather than skipping silently.
     */
    constexpr bool kCustomEffectIn3D =
#if defined(CNA_RENDERER_SDL_GPU)
        false;
#else
        false;   // conservatively declared everywhere; C1's SpriteBatch custom-effect leg still runs
#endif

    /**
     * @brief Whether this renderer can create a `RenderTarget2D` in a second sampleable format.
     *
     * SDL_GPU creates every `Texture2D` and every render target as `R8G8B8A8_UNORM` — one fixed
     * format, chosen at the call to `SDL_CreateGPUTexture` and not derived from the public
     * `SurfaceFormat`. So `SurfaceFormat::Color` is the only sampleable colour format it genuinely
     * offers, and leg J1 declares that boundary instead of pretending to measure a second one.
     */
    // plans/plan_runtimerenderer.md RTR-P9-10: this guard used to answer `false` in BOTH arms, so it
    // decided nothing and every renderer printed the boundary note below -- a note whose text
    // ("creates every Texture2D and render target as one fixed native colour format") is true of
    // SDL_GPU and of no other renderer here. It now says what its own comment above always
    // described, so only SDL_GPU declares that boundary.
    constexpr bool kSecondSampleableFormat =
#if defined(CNA_RENDERER_SDL_GPU)
        false;
#else
        true;
#endif

    /**
     * @brief Whether the skinned/PBR families accept this fixture's VertexPositionTexture stride.
     *
     * A vertex-LAYOUT boundary, not a render-to-texture one, and the same shape as REMED-GFX-151's
     * own `kDualTextureAcceptsPositionTexture`. Every family here is fed one stride-20 quad so a
     * single expected image is valid across all of them; SkinnedEffect and the PBR pair genuinely
     * want normal/tangent-bearing strides, and some renderers enforce that where others tolerate it:
     * EasyGL validates the stock program's declared attributes, WEBGPU falls through to
     * DrawColoredPrimitives, and D3D9 reports that stride 20 has no matching CNA vertex layout.
     * Their texture SLOT is the same ITextureRenderer binding the other families use, so the
     * contract is still covered there by the remaining families.
     */
    constexpr bool kSkinnedFamiliesAcceptPositionTexture =
#if defined(CNA_RENDERER_EASYGL) || defined(CNA_RENDERER_WEBGPU) || \
    defined(CNA_RENDERER_DIRECTX9) || defined(CNA_RENDERER_DIRECTX11) || \
    defined(CNA_RENDERER_DIRECTX12) || defined(CNA_RENDERER_LLGL)
        false;
#else
        true;
#endif

    /**
     * @brief Whether BasicEffect accepts VertexColorEnabled on a VertexPositionTexture stream.
     *
     * D3D9 has no vertex layout for "stride 20, lighting off, vertex colour on, textured"
     * (plans/plan_dx9.md D9-82b) and rejects it deterministically. A vertex-layout boundary, unrelated to
     * how a texture is resolved -- the plain BasicEffect family covers the same binding site there.
     */
    constexpr bool kBasicEffectAcceptsVertexColorWithoutColorStream =
#if defined(CNA_RENDERER_DIRECTX9)
        false;
#else
        true;
#endif

    /**
     * @brief Whether `DualTextureEffect` accepts this fixture's `VertexPositionTexture` geometry.
     *
     * D3D9 rejects it outright -- "stride 20 with vertexColor=false has no matching CNA vertex
     * layout (plans/plan_dx9.md D9-82d)" -- a documented, pre-existing vertex-layout boundary of that
     * renderer and nothing to do with texture resolution. The same declaration REMED-GFX-151's own
     * fixture already carries.
     */
    constexpr bool kDualTextureAcceptsPositionTexture =
#if defined(CNA_RENDERER_DIRECTX9)
        false;
#else
        true;
#endif

    /**
     * @brief Whether EnvironmentMapEffect accepts a VertexPositionTexture stream.
     *
     * EasyGL and D3D9 require a normal-bearing stride for it; D3D11 and D3D12 say so outright --
     * "EnvironmentMapEffect (env_map3d) requires stride 32 (VertexPositionNormalTexture)". Same
     * kind of boundary as above; leg L1's cube-sampling sub-check is recorded rather than asserted
     * there, and its face-aliasing checks still run.
     */
    constexpr bool kEnvMapAcceptsPositionTexture =
#if defined(CNA_RENDERER_EASYGL) || defined(CNA_RENDERER_DIRECTX9) || \
    defined(CNA_RENDERER_DIRECTX11) || defined(CNA_RENDERER_DIRECTX12) || \
    defined(CNA_RENDERER_LLGL)
        false;
#else
        true;
#endif

    /**
     * @brief Whether a mipMap=true RenderTarget2D is implemented at all.
     *
     * True everywhere. It was false on WEBGPU while that renderer's `RenderTarget2D`
     * constructor raised a catchable `std::runtime_error` for `mipMap=true`;
     * `plans/plan_webgpu.md` `WEBGPU-164` allocates the chain and regenerates it from level 0
     * on unbind, so the legs below measure a value there rather than a refusal.
     */
    constexpr bool kMipmappedRenderTargetSupported = true;

    /**
     * @brief The producer pattern texel at (@p x, @p y), (0, 0) being the TOP-LEFT corner.
     *
     * R is a function of x alone and G of y alone, so (R, G) is unique across all 32 texels and
     * identifies both axes independently. B mixes them so a 180-degree rotation is distinguishable,
     * and A takes four non-trivial values so a dropped, forced-opaque or premultiplied alpha cannot
     * pass. Every value is well away from 0 and 255, so a stale or zero image cannot coincide.
     */
    Color PatternColor(int x, int y)
    {
        const int r = 20 + x * 30;
        const int g = 25 + y * 70;
        const int b = 40 + ((x * 5 + y * 3) % 7) * 30;
        const int a = 255 - ((x + 2 * y) % 4) * 55;
        return Color(static_cast<bytecs>(r), static_cast<bytecs>(g),
                     static_cast<bytecs>(b), static_cast<bytecs>(a));
    }

    /** @brief A second, unmistakably different pattern, for legs that must tell two producers apart. */
    Color AltPatternColor(int x, int y)
    {
        const int r = 245 - x * 30;
        const int g = 235 - y * 70;
        const int b = 30 + ((x * 3 + y * 5) % 7) * 30;
        const int a = 90 + ((x + 3 * y) % 4) * 55;
        return Color(static_cast<bytecs>(r), static_cast<bytecs>(g),
                     static_cast<bytecs>(b), static_cast<bytecs>(a));
    }

    /** @brief Formats a colour as R,G,B,A for diagnostics. */
    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /** @brief Exact byte equality on all four channels. */
    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /** @brief One consumer family this fixture knows how to draw with. */
    enum class Family
    {
        Basic,            ///< BasicEffect, TextureEnabled
        BasicVertexColor, ///< BasicEffect, TextureEnabled + VertexColorEnabled
        AlphaTest,        ///< AlphaTestEffect
        DualSlot0,        ///< DualTextureEffect, source in slot 0 (slot 1 = white identity)
        DualSlot1,        ///< DualTextureEffect, source in slot 1 (slot 0 = white identity)
        EnvMapDiffuse,    ///< EnvironmentMapEffect's own diffuse texture slot
        Skinned,          ///< SkinnedEffect with one identity bone
        Pbr,              ///< PbrEffect, unlit-equivalent material
        SkinnedPbr,       ///< SkinnedPbrEffect with one identity bone
    };

    /** @brief Human-readable name of a consumer family, for check labels. */
    const char* FamilyName(Family f)
    {
        switch (f)
        {
            case Family::Basic:            return "BasicEffect";
            case Family::BasicVertexColor: return "BasicEffect+VertexColor";
            case Family::AlphaTest:        return "AlphaTestEffect";
            case Family::DualSlot0:        return "DualTextureEffect slot 0";
            case Family::DualSlot1:        return "DualTextureEffect slot 1";
            case Family::EnvMapDiffuse:    return "EnvironmentMapEffect diffuse";
            case Family::Skinned:          return "SkinnedEffect";
            case Family::Pbr:              return "PbrEffect";
            case Family::SkinnedPbr:       return "SkinnedPbrEffect";
        }
        return "?";
    }

    /**
     * @brief The PACKED stream form of VertexPositionTexture -- 20 bytes, what the GPU reads.
     *
     * `sizeof(VertexPositionTexture)` is 32, not 20: the type derives from the polymorphic
     * `IVertexType`, so it carries a vptr and tail padding (REMED-GFX-125's finding). Handing that
     * to `VertexBuffer::SetDataRaw`, whose stride argument is the GPU stream stride, would declare
     * a 32-byte stream that some renderers then read as VertexPositionNormalTexture. This struct is
     * the stream layout itself, so stride and contents agree on every renderer.
     */
    struct PackedPositionTexture
    {
        float x, y, z;
        float u, v;
    };
    static_assert(sizeof(PackedPositionTexture) == 20, "the packed stream stride must be 20 bytes");

    /** @brief How the consumer geometry reaches the device. */
    enum class DrawMode
    {
        UserPrimitives,        ///< DrawUserPrimitives
        UserIndexedPrimitives, ///< DrawUserIndexedPrimitives
        VertexBuffer,          ///< SetVertexBuffer + DrawPrimitives
        IndexedVertexBuffer,   ///< SetVertexBuffer + Indices + DrawIndexedPrimitives
        Instanced,             ///< DrawInstancedPrimitives, one instance
    };

    /** @brief Human-readable name of a draw mode, for check labels. */
    const char* DrawModeName(DrawMode m)
    {
        switch (m)
        {
            case DrawMode::UserPrimitives:        return "DrawUserPrimitives";
            case DrawMode::UserIndexedPrimitives: return "DrawUserIndexedPrimitives";
            case DrawMode::VertexBuffer:          return "DrawPrimitives(VB)";
            case DrawMode::IndexedVertexBuffer:   return "DrawIndexedPrimitives(VB+IB)";
            case DrawMode::Instanced:             return "DrawInstancedPrimitives";
        }
        return "?";
    }

    // A custom sprite shader that multiplies the sampled texture by its own uniform colour. Used by
    // leg C1: with the uniform at exactly 1.0 it is the identity, so the render-target source must
    // still reproduce the pattern byte-for-byte -- and the effect being genuinely bound is proved
    // separately by a second draw at a non-identity uniform.
    const char* kCustomVertSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 fragUV;
layout(set = 1, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    vec2 ndc = (inPos / pc.vpSize_pad.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    fragUV = inUV;
}
)GLSL";

    const char* kCustomFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 3, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    outColor = texture(uTexture, fragUV) * pc.color;
}
)GLSL";
}

/**
 * @brief REMED-GFX-152's public "a render target is an ordinary sampler source" contract.
 */
class RenderTargetEffectSourceTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    std::string onlyLeg_;   ///< when non-empty, the single leg id this process runs

    Texture2D whiteTex_;      ///< 1x1 opaque white -- the multiplicative identity for slot pairs
    Texture2D patternTex_;    ///< kPW x kPH PatternColor    -- the ordinary-texture control
    Texture2D altPatternTex_; ///< kPW x kPH AltPatternColor -- the second control

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void info(const std::string& text)
    {
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
    }

    /**
     * @brief Records a capability this renderer genuinely does not have.
     *
     * Distinct from info() because it also marks the run as having EXPLAINED itself: a leg that
     * measures nothing is a pass only when it said why, never merely because it was empty.
     */
    void boundary(const std::string& text)
    {
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
        boundaryDeclared_ = true;
    }

    bool boundaryDeclared_ = false;

    /** @brief Whether this process should run leg @p id. */
    [[nodiscard]] bool wants(const char* id) const
    {
        return onlyLeg_.empty() || onlyLeg_ == id;
    }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
    }

    // ---------------------------------------------------------------- readback

    /** @brief The outcome of a public GetData call on a consumer DESTINATION. */
    struct Readback
    {
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> pixels;
        int w = 0;
        int h = 0;

        [[nodiscard]] bool ok() const { return !threwNotSupported && !threwSomethingElse; }
        [[nodiscard]] const Color& at(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(y) * w + x];
        }
    };

    /// Reads a whole target back, pre-filled with a poison value so a renderer that writes nothing
    /// cannot be mistaken for one that wrote transparent black.
    static Readback ReadWhole(RenderTarget2D& target, int w, int h)
    {
        Readback r;
        r.w = w;
        r.h = h;
        r.pixels.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            target.GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
        }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// Asserts an unreadable renderer rejected, and reports whether the caller should continue.
    bool RequireReadable(const Readback& r, const std::string& label)
    {
        if (!kRasterizes)
        {
            check(r.threwNotSupported,
                  label + ": declared non-rasterizing, must throw NotSupportedException");
            return false;
        }
        if (!r.ok())
        {
            check(false, label + ": readback failed" +
                         (r.threwNotSupported ? " (NotSupportedException)" : " (" + r.otherWhat + ")"));
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------- geometry

    /// The standard XNA texture-on-quad UV map: NDC top-left -> UV (0, 0).
    static void FillQuad(VertexPositionTexture* q,
                         float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f)
    {
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, Vector2(u0, v0) }; q[1] = { bl, Vector2(u0, v1) }; q[2] = { br, Vector2(u1, v1) };
        q[3] = { tl, Vector2(u0, v0) }; q[4] = { br, Vector2(u1, v1) }; q[5] = { tr, Vector2(u1, v0) };
    }

    /// Indexed form of the same quad -- four vertices, six indices.
    static void FillQuadIndexed(VertexPositionTexture* q, std::uint16_t* idx)
    {
        q[0] = { Vector3(-1.f,  1.f, 0.f), Vector2(0.f, 0.f) };
        q[1] = { Vector3(-1.f, -1.f, 0.f), Vector2(0.f, 1.f) };
        q[2] = { Vector3( 1.f, -1.f, 0.f), Vector2(1.f, 1.f) };
        q[3] = { Vector3( 1.f,  1.f, 0.f), Vector2(1.f, 0.f) };
        idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
    }

    // ---------------------------------------------------------------- producers

    /// Draws @p source 1:1 into the currently bound target with point sampling.
    void Blit(GraphicsDevice& dev, const Texture2D& source, int w = kPW, int h = kPH)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(source, Rectangle(0, 0, w, h), Rectangle(0, 0, source.getWidthProperty(),
                                                          source.getHeightProperty()), Color::White);
        sb.End();
    }

    /// Produces the pattern inside @p rt through ordinary (never-defective) Texture2D SpriteBatch
    /// sampling and unbinds it. The target is NEVER read back here -- that is the point.
    void ProduceInto(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        Blit(dev, source, rt.getWidthProperty(), rt.getHeightProperty());
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// Produces the pattern inside @p rt through a stock 3D draw instead of SpriteBatch, so a leg
    /// can prove the two production routes are interchangeable as far as a consumer is concerned.
    void ProduceInto3D(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        VertexPositionTexture q[6];
        FillQuad(q);
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(const_cast<Texture2D*>(&source));
        fx.VertexColorEnabled = false;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    // ---------------------------------------------------------------- consumers

    /**
     * @brief Issues ONE consumer draw sampling @p src through @p family and @p mode.
     *
     * Every family is configured so that its output is the sampled texel unchanged: unit diffuse,
     * no lighting contribution that would tint it, white identity in any second slot, and an alpha
     * test that admits every alpha. That makes a single expected image valid for all of them, so a
     * family that binds the wrong resource fails on colour rather than on a per-family tolerance.
     */
    void ConsumeDraw(GraphicsDevice& dev, Texture2D* src, Family family, DrawMode mode,
                     float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f)
    {
        VertexPositionTexture q[6];
        std::uint16_t idx[6];
        FillQuad(q, u0, v0, u1, v1);

        // A draw that throws mid-leg must not leave the device holding this fixture's buffers: the
        // next leg would then draw with them, and teardown would fault on a freed binding.
        struct BindingScope
        {
            GraphicsDevice& d;
            ~BindingScope() { d.setIndicesProperty(nullptr); d.SetVertexBuffer(nullptr); }
        };

        auto pack = [](const VertexPositionTexture* src, int n, std::vector<PackedPositionTexture>& out)
        {
            out.resize(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                out[static_cast<std::size_t>(i)] = {
                    src[i].Position.X, src[i].Position.Y, src[i].Position.Z,
                    src[i].TextureCoordinate.X, src[i].TextureCoordinate.Y };
        };

        auto issue = [&](void)
        {
            switch (mode)
            {
                case DrawMode::UserPrimitives:
                    dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                    break;
                case DrawMode::UserIndexedPrimitives:
                {
                    VertexPositionTexture iq[4];
                    FillQuadIndexed(iq, idx);
                    dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, iq, 0, 4, idx, 0, 2);
                    break;
                }
                case DrawMode::VertexBuffer:
                {
                    std::vector<PackedPositionTexture> packed;
                    pack(q, 6, packed);
                    VertexBuffer vb(dev, 6);
                    vb.SetDataRaw(packed.data(), 6, static_cast<int>(sizeof(PackedPositionTexture)));
                    BindingScope scope{dev};
                    dev.SetVertexBuffer(&vb);
                    dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                    break;
                }
                case DrawMode::IndexedVertexBuffer:
                {
                    VertexPositionTexture iq[4];
                    FillQuadIndexed(iq, idx);
                    std::vector<PackedPositionTexture> packed;
                    pack(iq, 4, packed);
                    VertexBuffer vb(dev, 4);
                    vb.SetDataRaw(packed.data(), 4, static_cast<int>(sizeof(PackedPositionTexture)));
                    IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
                    ib.SetData(idx, 0, 6);
                    BindingScope scope{dev};
                    dev.SetVertexBuffer(&vb);
                    dev.setIndicesProperty(&ib);
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                    break;
                }
                case DrawMode::Instanced:
                {
                    VertexPositionTexture iq[4];
                    FillQuadIndexed(iq, idx);
                    std::vector<PackedPositionTexture> packed;
                    pack(iq, 4, packed);
                    VertexBuffer vb(dev, 4);
                    vb.SetDataRaw(packed.data(), 4, static_cast<int>(sizeof(PackedPositionTexture)));
                    IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
                    ib.SetData(idx, 0, 6);
                    BindingScope scope{dev};
                    dev.SetVertexBuffer(&vb);
                    dev.setIndicesProperty(&ib);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
                    break;
                }
            }
        };

        switch (family)
        {
            case Family::Basic:
            case Family::BasicVertexColor:
            {
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(src);
                fx.VertexColorEnabled = (family == Family::BasicVertexColor);
                fx.Apply();
                issue();
                break;
            }
            case Family::AlphaTest:
            {
                AlphaTestEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(src);
                // Greater-than-zero admits every alpha the pattern uses (min 90), so the alpha test
                // cannot mask a texel and turn a resource defect into a shape difference.
                fx.setAlphaFunctionProperty(CompareFunction::Greater);
                fx.setReferenceAlphaProperty(0);
                fx.Apply();
                issue();
                break;
            }
            case Family::DualSlot0:
            case Family::DualSlot1:
            {
                // The other slot carries an opaque-white 1x1, the multiplicative identity for this
                // effect's base.rgb * 2 * second.rgb combine, so only the slot under test can move
                // a texel -- and slot 1 gets its own leg because it is a SEPARATE binding site.
                DualTextureEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                if (family == Family::DualSlot0)
                {
                    fx.setTextureProperty(src);
                    fx.setTexture2Property(&whiteTex_);
                }
                else
                {
                    fx.setTextureProperty(&whiteTex_);
                    fx.setTexture2Property(src);
                }
                fx.Apply();
                issue();
                break;
            }
            case Family::EnvMapDiffuse:
            {
                // EnvironmentMapEffect's DIFFUSE slot is an ordinary ITextureRenderer binding, the
                // same site the crash lived at; its cube slot goes through the separate cube
                // resolver and is leg L1's subject. A zero environment amount keeps the reflection
                // out of the result so the diffuse texel survives unchanged.
                EnvironmentMapEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(src);
                fx.setEnvironmentMapProperty(envCube_.get());
                fx.setEnvironmentMapAmountProperty(0.0f);
                fx.setFresnelFactorProperty(0.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.f, 0.f, 0.f));
                fx.Apply();
                issue();
                break;
            }
            case Family::Skinned:
            {
                SkinnedEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(src);
                const std::vector<Matrix> bones = {Matrix::getIdentityProperty()};
                fx.SetBoneTransforms(bones);
                fx.setWeightsPerVertexProperty(1);
                fx.Apply();
                issue();
                break;
            }
            case Family::Pbr:
            {
                PbrEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(src);
                fx.setMetallicFactorProperty(0.0f);
                fx.setRoughnessFactorProperty(1.0f);
                fx.Apply();
                issue();
                break;
            }
            case Family::SkinnedPbr:
            {
                SkinnedPbrEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(src);
                fx.setMetallicFactorProperty(0.0f);
                fx.setRoughnessFactorProperty(1.0f);
                const std::vector<Matrix> bones = {Matrix::getIdentityProperty()};
                fx.SetBoneTransforms(bones);
                fx.setWeightsPerVertexProperty(1);
                fx.Apply();
                issue();
                break;
            }
        }
    }

    std::unique_ptr<TextureCube> envCube_;  ///< inert cube for EnvironmentMapEffect's cube slot

    /**
     * @brief The differential oracle every consumer leg uses.
     *
     * Runs the SAME consumer twice into a fresh destination -- once with the render target as its
     * texture, once with an ordinary Texture2D holding the identical expected bytes -- and requires
     * the two destinations byte-identical. A family whose own maths tints or clamps the texel
     * cancels out, because it applies equally to both sides; only a difference in WHICH RESOURCE
     * was bound can survive. The producer is never read.
     */
    void CheckConsumer(GraphicsDevice& dev, const std::string& label, Family family, DrawMode mode,
                       const Texture2D& control, int dstW = kPW, int dstH = kPH,
                       const std::function<void(GraphicsDevice&, RenderTarget2D&)>& produce = {},
                       float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f)
    {
        Readback reads[2];
        for (int side = 0; side < 2; ++side)
        {
            // A fresh producer per side: a side that only passed because the other had already
            // materialised the same target would prove nothing.
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            if (produce) produce(dev, a);
            else         ProduceInto(dev, a, control);

            RenderTarget2D dst(dev, dstW, dstH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a)
                                         : const_cast<Texture2D*>(&control);

            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            ConsumeDraw(dev, src, family, mode, u0, v0, u1, v1);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            reads[side] = ReadWhole(dst, dstW, dstH);
        }

        if (!RequireReadable(reads[0], label + " render-target source")) return;
        if (!RequireReadable(reads[1], label + " Texture2D control"))     return;

        int mismatched = 0;
        std::string first;
        for (int y = 0; y < dstH; ++y)
            for (int x = 0; x < dstW; ++x)
                if (!Same(reads[0].at(x, y), reads[1].at(x, y)))
                {
                    ++mismatched;
                    if (first.empty())
                        first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                ") rt=" + ColorText(reads[0].at(x, y)) +
                                " tex=" + ColorText(reads[1].at(x, y));
                }
        const int total = dstW * dstH;
        check(mismatched == 0,
              label + ": a never-read render target and its Texture2D control sample identically (" +
              std::to_string(total - mismatched) + "/" + std::to_string(total) + ")" + first);
    }

    /**
     * @brief CheckConsumer, with the render-target source DESTROYED before the frame renders.
     *
     * REMED-GFX-166. Identical to CheckConsumer except for one thing: side 0's source render target
     * is destroyed while the destination is STILL BOUND, so no flush can have consumed the queued
     * work yet on any renderer. Side 1 keeps its ordinary Texture2D alive, exactly as before, so the
     * comparison isolates the disposal and nothing else -- whatever a family's shading does to the
     * sampled value, it does to both sides.
     *
     * The control side must also carry real content (a quarter of its pixels differing from the
     * opaque-black clear), or the leg FAILS rather than passing on two equally empty images: a
     * dropped producer makes side 0 empty, and "both empty" must never read as "identical".
     *
     * @param dev     The device.
     * @param label   Check label prefix.
     * @param family  Consumer effect family.
     * @param mode    How the consumer geometry reaches the device.
     * @param control The ordinary Texture2D the live side samples.
     * @return True when the comparison actually ran; false when the live control rendered nothing,
     *         which is a capability observation about the family and NOT a lifetime result -- the
     *         caller must then declare a boundary, never count a pass.
     */
    bool CheckDeadSourceConsumer(GraphicsDevice& dev, const std::string& label, Family family,
                                 DrawMode mode, const Texture2D& control)
    {
        Readback reads[2];
        for (int side = 0; side < 2; ++side)
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, control);
                Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a)
                                             : const_cast<Texture2D*>(&control);
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, src, family, mode);
            }   // <-- `a` dies here, with dst still bound: nothing can have flushed
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            reads[side] = ReadWhole(dst, kPW, kPH);
        }

        if (!RequireReadable(reads[0], label + " destroyed render-target source")) return true;
        if (!RequireReadable(reads[1], label + " live Texture2D control"))         return true;

        int lively = 0;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
                if (!Same(reads[1].at(x, y), Color(0, 0, 0, 255))) ++lively;
        if (lively < kPW * kPH / 4)
        {
            boundary(label + ": the LIVE control rendered nothing (" + std::to_string(lively) + "/" +
                     std::to_string(kPW * kPH) + " non-black) on " + kRendererName +
                     ", so the destroyed-source comparison would measure nothing here -- recorded, "
                     "not counted");
            return false;
        }

        int mismatched = 0;
        std::string first;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
                if (!Same(reads[0].at(x, y), reads[1].at(x, y)))
                {
                    ++mismatched;
                    if (first.empty())
                        first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                ") deadRt=" + ColorText(reads[0].at(x, y)) +
                                " liveTex=" + ColorText(reads[1].at(x, y));
                }
        const int total = kPW * kPH;
        check(mismatched == 0,
              label + ": a render target DESTROYED before the frame renders samples identically to "
              "a live control (" + std::to_string(total - mismatched) + "/" +
              std::to_string(total) + ")" + first);
        return true;
    }

    /// Asserts a destination reproduces @p want exactly over its whole extent.
    void CheckExact(const Readback& r, const std::string& label, Color (*want)(int, int),
                    int w = kPW, int h = kPH)
    {
        int good = 0;
        std::string first;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const Color& got = r.at(x, y);
                if (Same(got, want(x, y))) ++good;
                else if (first.empty())
                    first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                            ") want=" + ColorText(want(x, y)) + " got=" + ColorText(got);
            }
        check(good == w * h, label + " (" + std::to_string(good) + "/" +
                             std::to_string(w * h) + ")" + first);
    }

    // ================================================================ legs

    /** @brief Leg A1/A2/A3 -- the finding itself, on the three destinations that matter. */
    void LegA(GraphicsDevice& dev)
    {
        if (wants("A1"))
            CheckConsumer(dev, "A1 RT -> BasicEffect textured -> RT", Family::Basic,
                          DrawMode::UserPrimitives, patternTex_);
        if (wants("A2"))
            CheckConsumer(dev, "A2 RT -> BasicEffect textured -> RT, indexed", Family::Basic,
                          DrawMode::UserIndexedPrimitives, patternTex_);
        if (wants("A3"))
        {
            // REMED-GFX-167: this leg used to be a declared boundary on WEBGPU -- "passes this
            // sequence but then dies during process teardown". It was never teardown: the source
            // `a` below is destroyed while its consumer draw is still only QUEUED for the
            // backbuffer, and WebGPU replayed that command through the freed object's vtable at the
            // next Present(). The boundary is gone because the defect is; the dedicated matrix now
            // lives in deferred_source_lifetime_test.cpp.
            // The backbuffer destination cannot be read on every renderer, so this leg judges what
            // it CAN judge everywhere: the sequence completes, binds a real resource and does not
            // terminate the process. Where the destination is observable the A1/A2 oracle already
            // covers the pixels; the value here is that the crash route is exercised against the
            // SWAPCHAIN pass too, which selects a different pipeline and a different segment.
            bool threw = false;
            std::string what;
            try
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, patternTex_);
                ResetState(dev);
                ConsumeDraw(dev, &a, Family::Basic, DrawMode::UserPrimitives);
            }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            catch (...)                     { threw = true; what = "(non-std exception)"; }
            check(!threw, std::string("A3 RT -> BasicEffect textured -> BACKBUFFER completes") +
                          (threw ? std::string(": threw ") + what : std::string()));
        }
    }

    /** @brief Leg B1 -- every stock-effect family that has a 2D texture slot. */
    void LegB(GraphicsDevice& dev)
    {
        const Family families[] = {
            Family::Basic, Family::BasicVertexColor, Family::AlphaTest,
            Family::DualSlot0, Family::DualSlot1, Family::EnvMapDiffuse,
            Family::Skinned, Family::Pbr, Family::SkinnedPbr,
        };
        for (Family f : families)
        {
            if ((f == Family::DualSlot0 || f == Family::DualSlot1) &&
                !kDualTextureAcceptsPositionTexture)
            {
                boundary(std::string("B1 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": DualTextureEffect rejects this fixture's stride-20 stream "
                         "(plans/plan_dx9.md D9-82d)");
                continue;
            }
            if (f == Family::BasicVertexColor && !kBasicEffectAcceptsVertexColorWithoutColorStream)
            {
                boundary(std::string("B1 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": no vertex layout for a textured, vertex-coloured, unlit stride-20 stream "
                         "(plans/plan_dx9.md D9-82b) -- a vertex-layout boundary, unrelated to how a "
                         "texture is resolved");
                continue;
            }
            if (f == Family::EnvMapDiffuse && !kEnvMapAcceptsPositionTexture)
            {
                boundary(std::string("B1 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": EnvironmentMapEffect requires a normal-bearing stream here");
                continue;
            }
            if ((f == Family::Skinned || f == Family::Pbr || f == Family::SkinnedPbr) &&
                !kSkinnedFamiliesAcceptPositionTexture)
            {
                boundary(std::string("B1 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": it rejects this fixture's VertexPositionTexture stride (a vertex-layout "
                         "boundary, unrelated to how a texture is resolved)");
                continue;
            }
            if (f == Family::EnvMapDiffuse && envCube_ == nullptr)
            {
                // The subject here is the effect's ordinary DIFFUSE slot, but the effect still
                // requires a cube in its cube slot, and this renderer could not create one.
                boundary(std::string("B1 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": no TextureCube could be created for its cube slot, which the effect "
                         "requires even when only its diffuse slot is under test");
                continue;
            }
            CheckConsumer(dev, std::string("B1 ") + FamilyName(f), f,
                          DrawMode::UserPrimitives, patternTex_);
        }
    }

    /** @brief Leg C1 -- SpriteBatch, and a custom ShaderEffect on the route that supports one. */
    void LegC(GraphicsDevice& dev)
    {
        // SpriteBatch already used dynamic_cast pre-fix, so this is the positive control that says
        // the shared resolver did not regress the route that was correct.
        for (int side = 0; side < 2; ++side)
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a) : &patternTex_;

            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            Blit(dev, *src);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            Readback r = ReadWhole(dst, kPW, kPH);
            if (!RequireReadable(r, "C1 SpriteBatch")) return;
            CheckExact(r, std::string("C1 SpriteBatch consumes a ") +
                          (side == 0 ? "render target" : "Texture2D control"), PatternColor);
        }

        // A custom effect resolves its sprite source through the very same resolver, so a render
        // target must reach a user shader as an ordinary sampler2D.
        //
        // Judged DIFFERENTIALLY -- render target versus Texture2D control through the identical
        // custom shader -- not against the raw pattern. Whatever colour convention this renderer's
        // custom-sprite route applies (it is not the identity even at uniform 1.0 here; see the
        // independent observation recorded with this task) applies equally to both sides and
        // cancels, leaving only a difference in WHICH RESOURCE was bound. Judging the custom route
        // against the raw pattern instead would fold an unrelated colour-convention question into
        // this ticket.
        ShaderEffect custom(dev, kCustomVertSrc, kCustomFragSrc);
        if (!custom.IsEffectValid())
        {
            boundary("C1 custom ShaderEffect did not compile on " + std::string(kRendererName) +
                 " -- custom-effect sub-leg not measured here");
            return;
        }
        custom.SetUniformVec4("color", 1.0f, 1.0f, 1.0f, 1.0f);
        Readback custReads[2];
        for (int side = 0; side < 2; ++side)
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a) : &patternTex_;

            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr, &custom);
                sb.Draw(*src, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            custReads[side] = ReadWhole(dst, kPW, kPH);
            if (!RequireReadable(custReads[side], "C1 custom effect")) return;
        }
        {
            int mismatched = 0;
            std::string first;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (!Same(custReads[0].at(x, y), custReads[1].at(x, y)))
                    {
                        ++mismatched;
                        if (first.empty())
                            first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") rt=" + ColorText(custReads[0].at(x, y)) +
                                    " tex=" + ColorText(custReads[1].at(x, y));
                    }
            check(mismatched == 0,
                  "C1 a custom ShaderEffect samples a render target exactly as it samples a "
                  "Texture2D control (" + std::to_string(kPW * kPH - mismatched) + "/" +
                  std::to_string(kPW * kPH) + ")" + first);
        }

        // Discrimination: the differential check above would also pass on a renderer that ignored
        // the custom effect entirely, so the shader must be shown to be genuinely bound. A halved
        // uniform must visibly darken the sample.
        //
        // Whether this renderer HAS a real compiled custom-shader path is asked first, and asked
        // with an ordinary Texture2D -- a property of the renderer, measurable without any render
        // target. Only where the answer is yes is the render-target sample then required to darken
        // too. Otherwise the boundary is declared, rather than either failing a renderer for a
        // capability it never claimed or silently dropping the check where it does matter.
        auto darkenedSample = [&](Texture2D& src, float scale) -> std::optional<Color>
        {
            custom.SetUniformVec4("color", scale, scale, scale, 1.0f);
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr, &custom);
                sb.Draw(src, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW, kPH);
            if (!r.ok()) return std::nullopt;
            return r.at(kPW - 1, 0);
        };

        const std::optional<Color> texUnit = darkenedSample(patternTex_, 1.0f);
        const std::optional<Color> texHalf = darkenedSample(patternTex_, 0.5f);
        if (!texUnit || !texHalf)
        {
            boundary("C1 custom-effect discrimination could not be read back on " +
                     std::string(kRendererName) + " -- not measured");
            return;
        }
        if (texUnit->getRProperty() == texHalf->getRProperty())
        {
            boundary(std::string("C1 ") + kRendererName + " does not honour a custom ShaderEffect's "
                     "own uniform even for an ordinary Texture2D (unit and half uniforms both read " +
                     ColorText(*texUnit) + "), so it has no real compiled custom-shader path here -- "
                     "the render-target/Texture2D equivalence above still stands, but this renderer "
                     "cannot discriminate a bound shader from an ignored one");
            return;
        }

        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);
        const std::optional<Color> rtHalf = darkenedSample(a, 0.5f);
        if (!rtHalf)
        {
            check(false, "C1 custom-effect discrimination: the render-target run could not be read");
            return;
        }
        const Color full = custReads[0].at(kPW - 1, 0);
        check(rtHalf->getRProperty() < full.getRProperty(),
              std::string("C1 the custom shader is genuinely bound (uniform 0.5 darkens the "
                          "render-target sample: got ") + ColorText(*rtHalf) + " vs unit-uniform " +
              ColorText(full) + ")");
    }

    /**
     * @brief Leg M2 -- REMED-GFX-166 across every stock-effect family with a 2D texture slot.
     *
     * Leg M1 proves the destroyed-source contract on one family and one draw mode. The Vulkan defect
     * it exposed lived in the DESTINATION ownership, which is family-independent -- but "should be
     * family-independent" is a prediction, and the whole point of this fixture is that the previous
     * ticket's ten-versus-thirteen site count was also a prediction. So each family is measured.
     */
    void LegM2(GraphicsDevice& dev)
    {
        int measured = 0;
        const Family families[] = {
            Family::Basic, Family::BasicVertexColor, Family::AlphaTest,
            Family::DualSlot0, Family::DualSlot1, Family::EnvMapDiffuse,
            Family::Skinned, Family::Pbr, Family::SkinnedPbr,
        };
        for (Family f : families)
        {
            if ((f == Family::DualSlot0 || f == Family::DualSlot1) &&
                !kDualTextureAcceptsPositionTexture)
            {
                boundary(std::string("M2 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": DualTextureEffect rejects this fixture's stride-20 stream "
                         "(plans/plan_dx9.md D9-82d)");
                continue;
            }
            if (f == Family::BasicVertexColor && !kBasicEffectAcceptsVertexColorWithoutColorStream)
            {
                boundary(std::string("M2 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": no vertex layout for a textured, vertex-coloured, unlit stride-20 stream "
                         "(plans/plan_dx9.md D9-82b) -- a vertex-layout boundary, unrelated to how a "
                         "texture is resolved");
                continue;
            }
            if (f == Family::EnvMapDiffuse && !kEnvMapAcceptsPositionTexture)
            {
                boundary(std::string("M2 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": EnvironmentMapEffect requires a normal-bearing stream here");
                continue;
            }
            if ((f == Family::Skinned || f == Family::Pbr || f == Family::SkinnedPbr) &&
                !kSkinnedFamiliesAcceptPositionTexture)
            {
                boundary(std::string("M2 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": it rejects this fixture's VertexPositionTexture stride (a vertex-layout "
                         "boundary, unrelated to how a texture is resolved)");
                continue;
            }
            if (f == Family::EnvMapDiffuse && envCube_ == nullptr)
            {
                // The subject here is the effect's ordinary DIFFUSE slot, but the effect still
                // requires a cube in its cube slot, and this renderer could not create one.
                boundary(std::string("M2 ") + FamilyName(f) + " skipped on " + kRendererName +
                         ": no TextureCube could be created for its cube slot, which the effect "
                         "requires even when only its diffuse slot is under test");
                continue;
            }
            if (CheckDeadSourceConsumer(dev, std::string("M2 ") + FamilyName(f), f,
                                        DrawMode::UserPrimitives, patternTex_))
                ++measured;
        }
        // A run in which NOTHING measured is a failure, however many boundaries it declared: the
        // point of this leg is coverage across families, and "every family was unmeasurable" would
        // otherwise pass silently on the strength of the declarations alone.
        check(measured > 0, "M2 at least one stock-effect family was actually measurable on " +
                            std::string(kRendererName) + " (" + std::to_string(measured) + "/" +
                            std::to_string(static_cast<int>(std::size(families))) + ")");
    }

    /**
     * @brief Leg M3 -- REMED-GFX-166 across every way the consumer geometry reaches the device.
     *
     * The destination is captured at the same enqueue choke point for all of them, so a mode that
     * pushed its command anywhere else would show up here and nowhere else.
     */
    void LegM3(GraphicsDevice& dev)
    {
        const DrawMode modes[] = {
            DrawMode::UserPrimitives, DrawMode::UserIndexedPrimitives,
            DrawMode::VertexBuffer, DrawMode::IndexedVertexBuffer, DrawMode::Instanced,
        };
        for (DrawMode m : modes)
        {
            if (m == DrawMode::Instanced && !SupportsInstancing(dev))
            {
                boundary(std::string("M3 ") + kRendererName + " does not implement "
                         "DrawInstancedPrimitives at all -- the same pre-existing capability "
                         "boundary leg D1 declares");
                continue;
            }
            (void)CheckDeadSourceConsumer(dev, std::string("M3 ") + DrawModeName(m), Family::Basic,
                                          m, patternTex_);
        }
    }

    /** @brief Leg D1 -- every way the consumer geometry can reach the device. */
    void LegD(GraphicsDevice& dev)
    {
        const DrawMode modes[] = {
            DrawMode::UserPrimitives, DrawMode::UserIndexedPrimitives,
            DrawMode::VertexBuffer, DrawMode::IndexedVertexBuffer, DrawMode::Instanced,
        };
        for (DrawMode m : modes)
        {
            if (m == DrawMode::Instanced && !SupportsInstancing(dev))
            {
                boundary(std::string("D1 ") + kRendererName + " does not implement "
                     "DrawInstancedPrimitives at all -- a pre-existing capability boundary of the "
                     "renderer, unrelated to how a texture is resolved; the other four draw modes "
                     "carry the contract here");
                continue;
            }
            CheckConsumer(dev, std::string("D1 ") + DrawModeName(m), Family::Basic, m, patternTex_);
        }
    }

    /**
     * @brief Probes once whether this renderer implements `DrawInstancedPrimitives`.
     *
     * Asked with a real draw rather than a capability flag, inside a bind cycle this method opens
     * and closes itself, so a throwing probe cannot leave a target bound for the next leg.
     */
    bool SupportsInstancing(GraphicsDevice& dev)
    {
        RenderTarget2D probe(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
        bool supported = true;
        try
        {
            dev.SetRenderTarget(&probe);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            ConsumeDraw(dev, &patternTex_, Family::Basic, DrawMode::Instanced);
        }
        catch (...) { supported = false; }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        return supported;
    }

    /** @brief Leg E1 -- several consumers of one producer, and two producers alternating. */
    void LegE(GraphicsDevice& dev)
    {
        // Three draws sampling the same producer inside ONE consumer bind cycle. A resolver that
        // resolved per-BIND rather than per-DRAW, or a cache keyed on the wrong identity, breaks
        // here and nowhere else.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            ConsumeDraw(dev, &a, Family::Basic,     DrawMode::UserPrimitives);
            ConsumeDraw(dev, &a, Family::AlphaTest, DrawMode::UserPrimitives);
            ConsumeDraw(dev, &a, Family::Basic,     DrawMode::UserIndexedPrimitives);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW, kPH);
            if (RequireReadable(r, "E1 three consumers, one pass"))
                CheckExact(r, "E1 three draws sampling one producer in a single consumer pass",
                           PatternColor);
        }

        // Two live producers holding DIFFERENT patterns, sampled alternately. A stale or swapped
        // binding fails on every texel rather than on a few, because AltPatternColor's R runs the
        // opposite way to PatternColor's.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            ProduceInto(dev, b, altPatternTex_);

            struct Step { RenderTarget2D* src; Color (*want)(int, int); const char* name; };
            const Step steps[] = {
                { &a, PatternColor,    "E1 alternating: A" },
                { &b, AltPatternColor, "E1 alternating: B" },
                { &a, PatternColor,    "E1 alternating: A again" },
                { &b, AltPatternColor, "E1 alternating: B again" },
            };
            for (const Step& s : steps)
            {
                RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, s.src, Family::Basic, DrawMode::UserPrimitives);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                Readback r = ReadWhole(dst, kPW, kPH);
                if (RequireReadable(r, s.name)) CheckExact(r, s.name, s.want);
            }
        }
    }

    /** @brief Leg F1 -- A -> B -> A: a target is producer, then consumer, then producer again. */
    void LegF(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);

        // A -> B through a stock 3D consumer.
        dev.SetRenderTarget(&b);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        ConsumeDraw(dev, &a, Family::Basic, DrawMode::UserPrimitives);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        // B -> A, back into the target that was the original producer. If A's own resource identity
        // were confused with B's, this overwrite would corrupt the very thing being read.
        dev.SetRenderTarget(&a);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        ConsumeDraw(dev, &b, Family::Basic, DrawMode::UserPrimitives);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback ra = ReadWhole(a, kPW, kPH);
        if (RequireReadable(ra, "F1 A -> B -> A"))
            CheckExact(ra, "F1 A -> B -> A round trip preserves the pattern exactly", PatternColor);
    }

    /** @brief Leg G1 -- equal and unequal producer/consumer extents. */
    void LegG(GraphicsDevice& dev)
    {
        CheckConsumer(dev, "G1 equal dimensions (8x4 -> 8x4)", Family::Basic,
                      DrawMode::UserPrimitives, patternTex_, kPW, kPH);
        // A destination twice the size samples the same 8x4 source over the whole quad, so both
        // sides see identical magnification -- the oracle stays a difference test, not a
        // resampling-accuracy test.
        CheckConsumer(dev, "G1 different dimensions (8x4 source -> 16x8 destination)", Family::Basic,
                      DrawMode::UserPrimitives, patternTex_, kPW * 2, kPH * 2);
    }

    /** @brief Leg H1 -- production route and consumption route are independent. */
    void LegH(GraphicsDevice& dev)
    {
        // Produced by SpriteBatch, consumed in 3D (the default) is leg A1; here the producer is a
        // stock 3D draw instead, so both production routes are proved interchangeable.
        CheckConsumer(dev, "H1 produced by stock 3D, consumed by stock 3D", Family::Basic,
                      DrawMode::UserPrimitives, patternTex_, kPW, kPH,
                      [this](GraphicsDevice& d, RenderTarget2D& rt)
                      { ProduceInto3D(d, rt, patternTex_); });

        // Produced by stock 3D, consumed by a DIFFERENT stock family.
        CheckConsumer(dev, "H1 produced by stock 3D, consumed by AlphaTestEffect", Family::AlphaTest,
                      DrawMode::UserPrimitives, patternTex_, kPW, kPH,
                      [this](GraphicsDevice& d, RenderTarget2D& rt)
                      { ProduceInto3D(d, rt, patternTex_); });

        // Produced by stock 3D, consumed by SpriteBatch.
        for (int side = 0; side < 2; ++side)
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto3D(dev, a, patternTex_);
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a) : &patternTex_;
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            Blit(dev, *src);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW, kPH);
            if (RequireReadable(r, "H1 3D producer -> SpriteBatch consumer"))
                CheckExact(r, std::string("H1 produced by stock 3D, consumed by SpriteBatch (") +
                              (side == 0 ? "render target" : "Texture2D control") + ")",
                           PatternColor);
        }
    }

    /** @brief Leg I1 -- sampler states and UV subregions apply to a target as to a texture. */
    void LegI(GraphicsDevice& dev)
    {
        struct Case { const char* name; SamplerState sampler; };
        const Case cases[] = {
            { "I1 PointClamp",  SamplerState::PointClamp  },
            { "I1 LinearClamp", SamplerState::LinearClamp },
            { "I1 LinearWrap",  SamplerState::LinearWrap  },
        };
        for (const Case& c : cases)
        {
            // The differential oracle makes even Linear and Wrap exact: whatever filtering or
            // addressing does, it does identically to both sides, so any surviving difference is a
            // resource difference.
            Readback reads[2];
            bool bad = false;
            for (int side = 0; side < 2; ++side)
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, patternTex_);
                RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a) : &patternTex_;
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.getSamplerStatesProperty()[0] = c.sampler;
                dev.getSamplerStatesProperty()[1] = c.sampler;
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, src, Family::Basic, DrawMode::UserPrimitives);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                reads[side] = ReadWhole(dst, kPW, kPH);
                if (!RequireReadable(reads[side], c.name)) { bad = true; break; }
            }
            if (bad) continue;
            int mismatched = 0;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (!Same(reads[0].at(x, y), reads[1].at(x, y))) ++mismatched;
            check(mismatched == 0, std::string(c.name) +
                  ": a render target and its Texture2D control sample identically (" +
                  std::to_string(kPW * kPH - mismatched) + "/" + std::to_string(kPW * kPH) + ")");
        }

        // A UV subregion: the consumer samples only the right half of the source. If the wrong
        // resource were bound the halves would not agree either, and if the UVs were ignored both
        // sides would still agree -- so this is paired with a discrimination check below.
        CheckConsumer(dev, "I1 UV subregion (right half of the source)", Family::Basic,
                      DrawMode::UserPrimitives, patternTex_, kPW, kPH, {}, 0.5f, 0.f, 1.0f, 1.0f);

        {
            // Discrimination: the right-half draw must NOT equal the full-source draw, or the
            // subregion check above would pass for a renderer that ignores UVs entirely.
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            Readback got[2];
            for (int half = 0; half < 2; ++half)
            {
                RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, &a, Family::Basic, DrawMode::UserPrimitives,
                            half == 0 ? 0.0f : 0.5f, 0.f, 1.0f, 1.0f);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                got[half] = ReadWhole(dst, kPW, kPH);
                if (!RequireReadable(got[half], "I1 UV discrimination")) return;
            }
            bool differs = false;
            for (int y = 0; y < kPH && !differs; ++y)
                for (int x = 0; x < kPW && !differs; ++x)
                    if (!Same(got[0].at(x, y), got[1].at(x, y))) differs = true;
            check(differs, "I1 the UV subregion genuinely selects a different region "
                           "(full-source and right-half draws differ)");
        }
    }

    /** @brief Leg J1 -- formats and mip chains. */
    void LegJ(GraphicsDevice& dev)
    {
        CheckConsumer(dev, "J1 SurfaceFormat::Color, mipMap=false", Family::Basic,
                      DrawMode::UserPrimitives, patternTex_);

        if (!kMipmappedRenderTargetSupported)
            boundary(std::string("J1 ") + kRendererName + " does not implement a mipMap=true "
                     "RenderTarget2D at all (plans/plan_webgpu.md WEBGPU-53/54) -- boundary recorded");

        // A mipmapped target must still be sampleable at level 0 through a 3D effect.
        if (kMipmappedRenderTargetSupported)
        {
            Readback reads[2];
            bool bad = false;
            for (int side = 0; side < 2; ++side)
            {
                RenderTarget2D a(dev, kPW, kPH, true, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, patternTex_);
                RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a) : &patternTex_;
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, src, Family::Basic, DrawMode::UserPrimitives);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                reads[side] = ReadWhole(dst, kPW, kPH);
                if (!RequireReadable(reads[side], "J1 mipMap=true")) { bad = true; break; }
            }
            if (!bad)
            {
                int mismatched = 0;
                for (int y = 0; y < kPH; ++y)
                    for (int x = 0; x < kPW; ++x)
                        if (!Same(reads[0].at(x, y), reads[1].at(x, y))) ++mismatched;
                check(mismatched == 0, "J1 a mipmapped render target samples identically to its "
                      "Texture2D control at level 0 (" + std::to_string(kPW * kPH - mismatched) +
                      "/" + std::to_string(kPW * kPH) + ")");
            }
        }

        if (!kSecondSampleableFormat)
            boundary(std::string("J1 ") + kRendererName + " creates every Texture2D and render target as "
                 "one fixed native colour format, so SurfaceFormat::Color is the only sampleable "
                 "colour format it offers -- capability boundary, not measured");
    }

    /** @brief Leg K1 -- an MSAA producer must bind its RESOLVED texture, never the attachment. */
    void LegK(GraphicsDevice& dev)
    {
        RenderTarget2D probe(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
        const int applied = probe.getMultiSampleCountProperty();
        info("K1 requested MultiSampleCount 4, applied " + std::to_string(applied));
        if (applied <= 1)
        {
            // Not a failure: a device may decline multisampling entirely (llvmpipe here reports
            // "MSAA up to 4x" yet applies 0). There is then no resolve to get wrong, so the
            // sub-leg is declared rather than asserted -- the same reading REMED-GFX-151's own E0
            // check takes. Whether a renderer SHOULD apply the requested count is REMED-GFX-154's
            // question, not this one's.
            boundary(std::string("K1 ") + kRendererName + " applied multisample count " +
                     std::to_string(applied) + " for a requested 4, so there is no MSAA resolve to "
                     "measure here -- capability recorded, not asserted");
            return;
        }
        check(true, "K1 the MSAA sub-leg genuinely applies a multisample count on " +
                    std::string(kRendererName) + " (applied " + std::to_string(applied) + ")");

        // The multisample attachment is COLOR_TARGET-only and is not a legal sampler binding; the
        // resolve happens at render-pass end, before any consumer. Nothing here waits, presents or
        // reads the producer to make that true. CheckConsumer builds its own single-sample
        // producer, so the MSAA pair is run explicitly.
        Readback reads[2];
        for (int side = 0; side < 2; ++side)
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a) : &patternTex_;
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            ConsumeDraw(dev, src, Family::Basic, DrawMode::UserPrimitives);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            reads[side] = ReadWhole(dst, kPW, kPH);
            if (!RequireReadable(reads[side], "K1 MSAA")) return;
        }
        int mismatched = 0;
        std::string first;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
                if (!Same(reads[0].at(x, y), reads[1].at(x, y)))
                {
                    ++mismatched;
                    if (first.empty())
                        first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                ") rt=" + ColorText(reads[0].at(x, y)) +
                                " tex=" + ColorText(reads[1].at(x, y));
                }
        check(mismatched == 0, "K1 an applied-MSAA producer binds its resolved texture, not the "
              "multisample attachment (" + std::to_string(kPW * kPH - mismatched) + "/" +
              std::to_string(kPW * kPH) + ")" + first);
    }

    /** @brief Leg L1 -- RenderTargetCube through the cube resolver. */
    void LegL(GraphicsDevice& dev)
    {
        const int kFace = kPH;   // a small square face
        std::unique_ptr<RenderTargetCube> cube;
        try
        {
            cube = std::make_unique<RenderTargetCube>(dev, kFace, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0,
                                                      RenderTargetUsage::PreserveContents);
        }
        catch (const std::exception& e)
        {
            boundary(std::string("L1 ") + kRendererName + " cannot create a RenderTargetCube here (" +
                 e.what() + ") -- capability boundary recorded");
            return;
        }

        // Two faces get UNMISTAKABLY different solid colours, so a cube whose faces alias, or whose
        // face index is dropped, cannot pass.
        const Color faceColors[2] = { Color(230, 40, 60, 255), Color(30, 90, 220, 255) };
        const CubeMapFace faces[2] = { CubeMapFace::PositiveX, CubeMapFace::NegativeX };
        bool produced = true;
        for (int i = 0; i < 2; ++i)
        {
            try
            {
                dev.SetRenderTarget(cube.get(), faces[i]);
                ResetState(dev);
                dev.Clear(faceColors[i]);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
            }
            catch (const std::exception& e)
            {
                boundary(std::string("L1 producing into cube face ") + std::to_string(i) +
                     " is unsupported here (" + e.what() + ") -- capability boundary recorded");
                produced = false;
                break;
            }
        }
        if (!produced) return;

        // Sampling the cube through EnvironmentMapEffect is the public route that binds an
        // ITextureCubeRenderer. The value judged is that it completes and binds a real resource; the
        // per-face colour is judged by GetData, which is the cube's own established readback
        // contract (REMED-GFX-134) rather than a new oracle invented here.
        bool threw = false;
        std::string what;
        if (!kEnvMapAcceptsPositionTexture)
        {
            boundary(std::string("L1 ") + kRendererName + " needs a normal-bearing stream for "
                     "EnvironmentMapEffect, so the cube-slot draw is recorded "
                     "rather than asserted here; the face-aliasing checks below still run");
        }
        else try
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            VertexPositionTexture q[6];
            FillQuad(q);
            EnvironmentMapEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex_);
            fx.setEnvironmentMapProperty(cube.get());
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW, kPH);
            (void)r;
        }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        catch (...)                     { threw = true; what = "(non-std exception)"; }
        if (kEnvMapAcceptsPositionTexture)
            check(!threw, std::string("L1 a rendered-into RenderTargetCube reaches "
                                      "EnvironmentMapEffect's cube slot without an unsafe cast") +
                          (threw ? std::string(": threw ") + what : std::string()));

        // Faces must not alias -- read each face back through the cube's own GetData contract.
        for (int i = 0; i < 2; ++i)
        {
            std::vector<Color> px(static_cast<std::size_t>(kFace) * kFace, Color(0xCD, 0xCD, 0xCD, 0xCD));
            bool readOk = true;
            try
            {
                cube->GetData(faces[i], px.data(), 0, static_cast<int>(px.size()));
            }
            catch (const System::NotSupportedException&)
            {
                boundary(std::string("L1 face readback is unsupported on ") + kRendererName +
                     " -- aliasing not measured");
                readOk = false;
            }
            catch (const std::exception& e)
            {
                check(false, std::string("L1 face ") + std::to_string(i) + " readback failed: " + e.what());
                readOk = false;
            }
            if (!readOk) break;
            check(Same(px[0], faceColors[i]),
                  std::string("L1 cube face ") + (i == 0 ? "+X" : "-X") + " holds its OWN colour, "
                  "not the other face's (want " + ColorText(faceColors[i]) + ", got " +
                  ColorText(px[0]) + ")");
        }
    }

    /** @brief Leg M1 -- disposal, handle reuse and interleaved lifetimes. */
    void LegM(GraphicsDevice& dev)
    {
        // The source render target is destroyed BEFORE the frame renders. This renderer replays
        // draws at Present, so the queued consumer must still bind a live native handle -- the
        // keepAlive contract, judged by a real sampled result rather than by "did not crash".
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, patternTex_);
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, &a, Family::Basic, DrawMode::UserPrimitives);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
            }   // <-- a is destroyed here, with its consumer draw still only QUEUED
            Readback r = ReadWhole(dst, kPW, kPH);
            if (RequireReadable(r, "M1 source destroyed before render"))
                CheckExact(r, "M1 a render-target source destroyed before the frame renders is "
                              "still sampled correctly", PatternColor);
        }

        // The same for an ordinary Texture2D, which pre-fix released its native handle IMMEDIATELY
        // in its destructor while its consumer draw was still queued.
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            {
                std::vector<std::uint8_t> px(static_cast<std::size_t>(kPW) * kPH * 4);
                for (int y = 0; y < kPH; ++y)
                    for (int x = 0; x < kPW; ++x)
                    {
                        const Color c = PatternColor(x, y);
                        const std::size_t o = (static_cast<std::size_t>(y) * kPW + x) * 4;
                        px[o + 0] = c.getRProperty(); px[o + 1] = c.getGProperty();
                        px[o + 2] = c.getBProperty(); px[o + 3] = c.getAProperty();
                    }
                Texture2D shortLived = Texture2D::CreateFromPixels(dev, kPW, kPH, px);
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, &shortLived, Family::Basic, DrawMode::UserPrimitives);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
            }   // <-- shortLived is destroyed here, with its consumer draw still only QUEUED
            Readback r = ReadWhole(dst, kPW, kPH);
            if (RequireReadable(r, "M1 Texture2D destroyed before render"))
                CheckExact(r, "M1 an ordinary Texture2D destroyed before the frame renders is "
                              "still sampled correctly", PatternColor);
        }

        // Repeated create/use/dispose cycles. A recycled native handle index must never resurrect
        // an earlier binding: each round uses a different pattern and must read back its own.
        for (int round = 0; round < 4; ++round)
        {
            const bool alt = (round % 2) == 1;
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, alt ? altPatternTex_ : patternTex_);
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            ConsumeDraw(dev, &a, Family::Basic, DrawMode::UserPrimitives);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW, kPH);
            if (!RequireReadable(r, "M1 cycle " + std::to_string(round))) break;
            CheckExact(r, "M1 create/use/dispose round " + std::to_string(round) +
                          " reads its OWN pattern (handle reuse is safe)",
                       alt ? AltPatternColor : PatternColor);
        }

        // Two targets with INTERLEAVED lifetimes: the first is destroyed while the second is still
        // live and queued, so the release queue must not disturb the survivor.
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            RenderTarget2D keep(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            ProduceInto(dev, keep, altPatternTex_);
            {
                RenderTarget2D temp(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                    RenderTargetUsage::DiscardContents);
                ProduceInto(dev, temp, patternTex_);
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                ConsumeDraw(dev, &temp, Family::Basic, DrawMode::UserPrimitives);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
            }   // temp dies; keep must be untouched
            RenderTarget2D dst2(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst2);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            ConsumeDraw(dev, &keep, Family::Basic, DrawMode::UserPrimitives);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            Readback r1 = ReadWhole(dst, kPW, kPH);
            Readback r2 = ReadWhole(dst2, kPW, kPH);
            if (RequireReadable(r1, "M1 interleaved, dead source"))
                CheckExact(r1, "M1 interleaved lifetimes: the destroyed source's draw is correct",
                           PatternColor);
            if (RequireReadable(r2, "M1 interleaved, live source"))
                CheckExact(r2, "M1 interleaved lifetimes: the surviving source is undisturbed",
                           AltPatternColor);
        }
    }

    /** @brief Leg N1 -- a producer from an EARLIER public frame, consumed later. */
    void LegN(GraphicsDevice& dev)
    {
        // earlierFrameTarget_ was produced during frame 1 (see Draw); this runs in frame 2. It is
        // the control for the same-frame legs: if this failed too, the finding would not be about
        // same-frame ordering at all.
        if (earlierFrameTarget_ == nullptr)
        {
            boundary("N1 no earlier-frame producer available -- sub-leg not measured");
            return;
        }
        RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        ConsumeDraw(dev, earlierFrameTarget_.get(), Family::Basic, DrawMode::UserPrimitives);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWhole(dst, kPW, kPH);
        if (RequireReadable(r, "N1 earlier-frame producer"))
            CheckExact(r, "N1 a producer from an earlier public frame is sampled correctly later",
                       PatternColor);
    }

    /** @brief Leg O1 -- an unsupported texture must fail honestly, never abort. */
    void LegO(GraphicsDevice& dev)
    {
        // A null texture on a slot that requires one must not be reinterpreted, must not bind a
        // stale handle from a previous draw and must not terminate the process. The public layer
        // may reject it or the renderer may skip the draw; what is forbidden is a crash or a
        // silently-wrong resource.
        bool survived = true;
        std::string what;
        try
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            VertexPositionTexture q[6];
            FillQuad(q);
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(nullptr);
            fx.VertexColorEnabled = false;
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW, kPH);
            (void)r;
        }
        catch (const std::exception& e) { what = e.what(); }
        catch (...)                     { survived = false; what = "(non-std exception)"; }
        check(survived, std::string("O1 a textured effect with a null texture fails or skips "
                                    "deterministically, without terminating the process") +
                        (what.empty() ? std::string() : std::string(" (threw: ") + what + ")"));
    }

    /** @brief Leg P1 -- REMED-GFX-156's ordered Clear must not erase a producer. */
    void LegP(GraphicsDevice& dev)
    {
        // Produce A, then issue an ordered Clear on a DIFFERENT destination, then consume A. The
        // Clear belongs to its own bind cycle; if segmentation confused the two, A would arrive at
        // its consumer wiped.
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);

        RenderTarget2D other(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(&other);
        ResetState(dev);
        dev.Clear(Color(15, 25, 35, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        ConsumeDraw(dev, &a, Family::Basic, DrawMode::UserPrimitives);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWhole(dst, kPW, kPH);
        if (RequireReadable(r, "P1 ordered Clear between producer and consumer"))
            CheckExact(r, "P1 an ordered Clear on another destination between producer and consumer "
                          "leaves the producer intact", PatternColor);

        // A mid-cycle Clear inside the CONSUMER's own pass must still precede the consumer draw.
        RenderTarget2D dst2(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst2);
        ResetState(dev);
        dev.Clear(Color(200, 10, 10, 255));
        dev.Clear(Color(0, 0, 0, 255));
        ConsumeDraw(dev, &a, Family::Basic, DrawMode::UserPrimitives);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r2 = ReadWhole(dst2, kPW, kPH);
        if (RequireReadable(r2, "P1 two ordered Clears then the consumer"))
            CheckExact(r2, "P1 two ordered Clears followed by the consumer draw keep public order",
                       PatternColor);
    }

    std::unique_ptr<RenderTarget2D> earlierFrameTarget_;
    int frame_ = 0;

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();

        const std::vector<std::uint8_t> white = {255, 255, 255, 255};
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, white);

        std::vector<std::uint8_t> pattern(static_cast<std::size_t>(kPW) * kPH * 4);
        std::vector<std::uint8_t> alt(static_cast<std::size_t>(kPW) * kPH * 4);
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const std::size_t o = (static_cast<std::size_t>(y) * kPW + x) * 4;
                const Color p = PatternColor(x, y);
                const Color a = AltPatternColor(x, y);
                pattern[o + 0] = p.getRProperty(); pattern[o + 1] = p.getGProperty();
                pattern[o + 2] = p.getBProperty(); pattern[o + 3] = p.getAProperty();
                alt[o + 0] = a.getRProperty(); alt[o + 1] = a.getGProperty();
                alt[o + 2] = a.getBProperty(); alt[o + 3] = a.getAProperty();
            }
        patternTex_    = Texture2D::CreateFromPixels(dev, kPW, kPH, pattern);
        altPatternTex_ = Texture2D::CreateFromPixels(dev, kPW, kPH, alt);

        // An inert 1x1 cube for EnvironmentMapEffect's cube slot in leg B1, whose subject is the
        // effect's ordinary DIFFUSE binding, not its cube one.
        try
        {
            envCube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
            const Color face[1] = { Color(255, 255, 255, 255) };
            for (int f = 0; f < 6; ++f)
                envCube_->SetData(static_cast<CubeMapFace>(f), face, 0, 1);
        }
        catch (...) { envCube_.reset(); }
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        auto& dev = getGraphicsDeviceProperty();
        ++frame_;

        if (frame_ == 1)
        {
            // Frame 1 exists only to give leg N1 a producer from an EARLIER public frame. Every
            // other leg runs in frame 2 and produces its own source in that same frame.
            earlierFrameTarget_ = std::make_unique<RenderTarget2D>(
                dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                RenderTargetUsage::PreserveContents);
            ProduceInto(dev, *earlierFrameTarget_, patternTex_);
            return;
        }

        done_ = true;
        std::printf("[INFO] REMED-GFX-152 render-target-as-effect-source on %s (%dx%d pattern%s)\n",
                    kRendererName, kPW, kPH,
                    onlyLeg_.empty() ? "" : (", leg " + onlyLeg_).c_str());
        std::fflush(stdout);

        if (!kCustomEffectIn3D)
            info(std::string("D legs: ") + kRendererName + " has no custom-effect slot on its stock "
                 "3D paths (a custom ShaderEffect is a SpriteBatch route there) -- leg C1 exercises "
                 "a custom effect on the route that has one");

        // Every leg runs behind a recovery wrapper. A leg that throws mid-bind-cycle would
        // otherwise leave a render target BOUND, and the shutdown Present then throws
        // "Cannot present while render targets are bound" from a noexcept path -- turning a plain
        // failed check into a std::terminate that this fixture would have to report as a CRASH.
        // Unbinding here keeps the crash classification meaningful: it stays reserved for the
        // production defect, not for the harness's own error handling.
        auto runLeg = [&](const char* id, void (RenderTargetEffectSourceTest::*leg)(GraphicsDevice&))
        {
            if (!wants(id)) return;
            try
            {
                (this->*leg)(dev);
            }
            catch (const std::exception& e)
            {
                check(false, std::string("leg ") + id + ": an unexpected exception escaped: " + e.what());
            }
            catch (...)
            {
                check(false, std::string("leg ") + id + ": an unexpected non-std exception escaped");
            }
            try
            {
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                dev.setIndicesProperty(nullptr);
                dev.SetVertexBuffer(nullptr);
                ResetState(dev);
            }
            catch (...) { /* teardown of teardown -- nothing useful left to say */ }
        };

        if (wants("A1") || wants("A2") || wants("A3"))
            runLeg(onlyLeg_.empty() ? "A1" : onlyLeg_.c_str(), &RenderTargetEffectSourceTest::LegA);
        runLeg("B1", &RenderTargetEffectSourceTest::LegB);
        runLeg("C1", &RenderTargetEffectSourceTest::LegC);
        runLeg("D1", &RenderTargetEffectSourceTest::LegD);
        runLeg("E1", &RenderTargetEffectSourceTest::LegE);
        runLeg("F1", &RenderTargetEffectSourceTest::LegF);
        runLeg("G1", &RenderTargetEffectSourceTest::LegG);
        runLeg("H1", &RenderTargetEffectSourceTest::LegH);
        runLeg("I1", &RenderTargetEffectSourceTest::LegI);
        runLeg("J1", &RenderTargetEffectSourceTest::LegJ);
        runLeg("K1", &RenderTargetEffectSourceTest::LegK);
        runLeg("L1", &RenderTargetEffectSourceTest::LegL);
        runLeg("M1", &RenderTargetEffectSourceTest::LegM);
        runLeg("M2", &RenderTargetEffectSourceTest::LegM2);
        runLeg("M3", &RenderTargetEffectSourceTest::LegM3);
        runLeg("N1", &RenderTargetEffectSourceTest::LegN);
        runLeg("O1", &RenderTargetEffectSourceTest::LegO);
        runLeg("P1", &RenderTargetEffectSourceTest::LegP);

        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        // A leg may legitimately have nothing to measure on a renderer that lacks the capability --
        // but only when it SAID so through boundary(). An empty run that explained nothing is a
        // failure, so a leg cannot pass by quietly doing nothing.
        result_ = (passCount_ == totalCount_ && (totalCount_ > 0 || boundaryDeclared_)) ? 0 : 1;
        Exit();
    }

public:
    explicit RenderTargetEffectSourceTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    /** @brief Every leg id this fixture knows, in the order the supervisor runs them. */
    const char* const kLegIds[] = {
        "A1", "A2", "A3", "B1", "C1", "D1", "E1", "F1",
        "G1", "H1", "I1", "J1", "K1", "L1", "M1", "M2", "M3", "N1", "O1", "P1",
    };

#if CNA_GFX152_CAN_FORK
    // Matches CNA::Examples::kSkipExitCode (common/PixelTestGame.hpp) -- a leg that exits with this
    // code decided for itself, before this supervisor is involved, that its environment can't
    // support the check (LLGL-55: e.g. the shared Vulkan-WSI-unavailable guard,
    // examples/common/LlglVulkanWsiSkipGuard.cpp). That is not the same as the leg failing, so it
    // must not be counted or reported as one.
    constexpr int kLegSkipExitCode = 77;

    /**
     * @brief Runs one leg in a child process and classifies the outcome.
     *
     * The pre-fix failure is a SIGSEGV, so a leg's result has three states, not two. Reporting the
     * signal by name is what makes "the process died" an attributable measurement rather than a
     * lost run -- and re-execing means one leg's crash cannot cost the remaining legs.
     *
     * @param exePath  This binary's own path.
     * @param legId    The leg to run.
     * @param crashed  Set to true when the child was killed by a signal.
     * @param skipped  Set to true when the child exited with kLegSkipExitCode.
     * @return True when the child exited 0.
     */
    bool RunLegIsolated(const char* exePath, const char* legId, bool& crashed, bool& skipped)
    {
        crashed = false;
        skipped = false;
        std::string arg = std::string("--leg=") + legId;

        const pid_t pid = fork();
        if (pid < 0)
        {
            std::printf("[FAIL] supervisor: fork() failed for leg %s\n", legId);
            return false;
        }
        if (pid == 0)
        {
            char* argv[3];
            argv[0] = const_cast<char*>(exePath);
            argv[1] = const_cast<char*>(arg.c_str());
            argv[2] = nullptr;
            execv(exePath, argv);
            std::_Exit(127);
        }

        int status = 0;
        while (waitpid(pid, &status, 0) < 0) { /* EINTR */ }

        if (WIFSIGNALED(status))
        {
            const int sig = WTERMSIG(status);
            crashed = true;
            std::printf("[CRASH] leg %s: killed by signal %d (%s)%s\n", legId, sig,
                        strsignal(sig), WCOREDUMP(status) ? " (core dumped)" : "");
            std::fflush(stdout);
            return false;
        }
        if (WIFEXITED(status))
        {
            const int code = WEXITSTATUS(status);
            if (code == 0) return true;
            if (code == kLegSkipExitCode)
            {
                skipped = true;
                std::printf("[SKIP] leg %s: exited %d\n", legId, code);
                std::fflush(stdout);
                return false;
            }
            std::printf("[FAIL] leg %s: exited %d (checks ran and disagreed)\n", legId, code);
            std::fflush(stdout);
            return false;
        }
        std::printf("[FAIL] leg %s: neither exited nor signalled (status %d)\n", legId, status);
        std::fflush(stdout);
        return false;
    }
#endif
}

int main(int argc, char** argv)
{
    std::string onlyLeg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind("--leg=", 0) == 0) onlyLeg = a.substr(6);
    }

#if CNA_GFX152_CAN_FORK
    if (onlyLeg.empty())
    {
        // Supervisor. Each leg runs in its own child so a SIGSEGV is a reported result rather than
        // the end of the run.
        std::printf("[INFO] REMED-GFX-152 supervisor: %zu legs, each in its own process\n",
                    sizeof(kLegIds) / sizeof(kLegIds[0]));
        std::fflush(stdout);
        int passed = 0, crashes = 0, skips = 0;
        const int total = static_cast<int>(sizeof(kLegIds) / sizeof(kLegIds[0]));
        for (const char* leg : kLegIds)
        {
            bool crashed = false, skipped = false;
            if (RunLegIsolated(argv[0], leg, crashed, skipped)) ++passed;
            if (crashed) ++crashes;
            if (skipped) ++skips;
        }
        std::printf("[INFO] supervisor: %d/%d legs passed, %d crashed, %d skipped\n",
                    passed, total, crashes, skips);
        std::fflush(stdout);
        const int failed = total - passed - skips;
        if (failed > 0) return 1;
        return (skips > 0) ? kLegSkipExitCode : 0;
    }
#endif

    RenderTargetEffectSourceTest game(onlyLeg);
    game.Run();
    return game.Result();
}
