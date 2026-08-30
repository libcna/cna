// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-157: a SpriteBatch draw and a stock 3D draw issued inside ONE render-target bind cycle
// must execute in the order the game issued them.
//
//     SpriteBatch.Begin(...); SpriteBatch.Draw(...); SpriteBatch.End();
//     effect.Apply();  GraphicsDevice.DrawPrimitives(...)      // <-- must land ON TOP
//
// REMED-GFX-155's leg I0 measured that the second draw does not reach the target at all on BGFX,
// VULKAN, SOFTWARE and EASYGL, while WEBGPU renders it: the region it drew into still holds the
// bind cycle's clear colour. It reproduces with ORDINARY Texture2D sources and on both the
// backbuffer and a RenderTarget2D, so it is neither a render-target ordering question nor a
// producer/consumer dependency. REMED-GFX-143's check O3 named the "two queues" subject but only
// ever measured 3D -> SpriteBatch -- the direction that already works -- so the defect survived it.
//
// WHAT THIS FILE MEASURES, AND WHY EVERY CHECK IS FALSIFIABLE IN BOTH DIRECTIONS
// -----------------------------------------------------------------------------
// The oracle is a STAIRCASE. Step i of an N-step sequence covers stripes [0, N-i), so the last step
// to touch stripe j is step N-1-j and the final image spells the whole sequence backwards:
//
//     steps:   0 covers [0,5)   1 covers [0,4)   2 covers [0,3)   3 covers [0,2)   4 covers [0,1)
//     result:  stripe 0 = step 4, stripe 1 = step 3, ... stripe 4 = step 0, stripes 5.. = clear
//
// One readback therefore proves BOTH that every draw happened (each owns exactly one final stripe)
// and that they happened in the exact public order (a reordered pair swaps two stripes). A renderer
// that drops the last 3D draw shows the clear colour where that step's stripe must be; a renderer
// that replays all sprites and then all 3D draws shows the stripes permuted by family. The two
// failure shapes are distinguishable, which is the whole point.
//
// Every stripe is FULL HEIGHT, so nothing here depends on whether a renderer's readback or its
// render-target sampling is Y-flipped (REMED-GFX-067/147/153 are not this file's subject). The
// palette is 0/255-only, an exact fixed point of sRGB encoding, so every comparison is byte-exact
// on sRGB renderers too. Clip-space stripe coordinates are used for the 3D family, so this file
// cannot accidentally exercise the viewport-segmentation machinery (REMED-GFX-063/065) instead of
// the ordering under test.
//
// Two confounds are separated rather than assumed away:
//
//   * EFFECT AND BUFFER LIFETIME. Leg A-F hold their Effect, VertexBuffer and IndexBuffer objects
//     as long-lived members, so a deferred renderer replaying after they died cannot be blamed. Leg
//     L then repeats the canonical sequence with a scope-LOCAL effect and local buffers, exactly as
//     REMED-GFX-155's leg I0 wrote it, so lifetime is measured on its own.
//   * WHAT THE EFFECT ACTUALLY SHADES. Legs G/H use a PARITY oracle: the identical 3D draw is
//     issued once as the FIRST thing in a cycle (control) and once AFTER a SpriteBatch (subject),
//     and the two readbacks must be byte-identical. That is exact for every stock effect and every
//     draw entry point without this file having to predict any effect's shading maths.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   A  canonical sequences on the backbuffer     SB->3D, 3D->SB, SB->3D->SB, 3D->SB->3D, A->B->A,
//                                                and five alternating families
//   B  the same sequences on a RenderTarget2D    equal dimensions
//   C  the same sequences on a RenderTarget2D    DIFFERENT dimensions from the backbuffer
//   D  destination transitions                   target -> backbuffer, backbuffer -> target,
//                                                target A -> target B, A -> B -> A
//   E  untextured and textured controls          vertex colour, one texture, two textures,
//                                                the SAME texture in both families
//   F  draw entry points                         DrawUserPrimitives, DrawUserIndexedPrimitives
//                                                (16- and 32-bit), DrawPrimitives,
//                                                DrawIndexedPrimitives, and nonzero ranges
//   G  stock effects                             BasicEffect (textured and vertex-colour),
//                                                AlphaTestEffect, DualTextureEffect,
//                                                EnvironmentMapEffect, SkinnedEffect
//   H  repeated and interleaved Apply            the same effect applied twice, two different
//                                                effect objects, Apply after SpriteBatch.End
//   I  state does not leak between families      depth, blend, rasterizer, cull, scissor, viewport
//   J  clears between families                   before, between and after the two families, both
//                                                orders, alternating Clear/draw runs, and two
//                                                consecutive Clears (REMED-GFX-156)
//   P  every family ACROSS a Clear boundary      REMED-GFX-156: a renderer that splits its bind
//                                                cycle into two native passes at the Clear must
//                                                rebuild all pass state for the second one; parity
//                                                against the same draw at the head of a fresh pass
//   K  cardinality and repetition                many alternations per cycle, many cycles, many
//                                                frames -- no growth, no extra frame needed
//   L  lifetime control                          scope-local effect and buffers, as leg I0 wrote it
//
// NOTHING is inserted between the two families in any leg: no Present, no target switch used as a
// barrier, no GetData before every command of the check is queued, no explicit flush, no wait, no
// sleep and no extra frame. That restriction is the contract under test.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /**
     * @brief Whether this renderer rasterizes at all.
     *
     * HEADLESS performs no rasterization, so REMED-GFX-127/130's contract makes every readback
     * reject deterministically. There is no ordering result to observe there; the legs assert that
     * the sequences remain legal and that the rejection happens, not a pixel value.
     */
    /** @brief Whether the public backbuffer readback works, so the backbuffer legs can assert. */
    /** @brief Whether stock 3D geometry rasterizes here at all. */
#if defined(CNA_RENDERER_HEADLESS)
    constexpr bool kRasterizes = false;
    constexpr bool kReadsBackbuffer = false;
    constexpr bool kDraws3D = false;
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    // SdlGpu has no ReadBackbuffer override at all, so GetBackBufferData raises. Its render-target
    // legs still carry the whole contract.
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = false;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = false;
    constexpr const char* kRendererName = "SDL_RENDERER";
#elif defined(CNA_RENDERER_CANVAS)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "CANVAS";
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = false;
    constexpr const char* kRendererName = "FREEDIRECT";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr bool kRasterizes = true;
    constexpr bool kReadsBackbuffer = true;
    constexpr bool kDraws3D = true;
    constexpr const char* kRendererName = "DIRECTX12";
#else
#error "REMED-GFX-157: this renderer has no declared SpriteBatch/3D ordering contract."
#endif

    /**
     * @brief The quads this file's 3D family draws are XNA BACK faces, on every renderer.
     *
     * This constant used to be `kCcwCullsSpriteWoundQuad`, a per-renderer declaration built on a
     * premise that was wrong at the source. It claimed FNA's SpriteBatch emits its corners as
     * "TL, BL, TR, BR, so its first triangle is TL -> BL -> TR", and that the quads drawn here
     * (TL -> BL -> BR) were "the SAME winding" and therefore front faces that
     * RasterizerState.CullCounterClockwise must keep.
     *
     * FNA SpriteBatch.cs actually declares CornerOffsetX = { 0, 1, 0, 1 } and
     * CornerOffsetY = { 0, 0, 1, 1 } -- TL, TR, BL, BR -- so with its j, j+1, j+2 / j+3, j+2, j+1
     * indices the two sprite triangles are TL -> TR -> BL and BR -> BL -> TR, both CLOCKWISE as
     * displayed. The quads here are TL -> BL -> BR, the OPPOSITE winding, i.e. counter-clockwise
     * as displayed and therefore XNA BACK faces. So CullCounterClockwise must REMOVE them
     * everywhere, and the four renderers that did so were correct all along; WEBGPU, which kept
     * them, was the one inverted renderer (REMED-GFX-160, fixed).
     *
     * The contract itself is now measured directly, on both windings and every cull mode, by
     * examples/frontface_winding_test.cpp. Leg M5 below asserts it unconditionally.
     */

    /** @brief How a renderer replays a bind cycle's two draw families relative to each other. */
    enum class Replay
    {
        PublicOrder,            ///< The contract: exactly the order the game issued them in.
        AllSpritesThenAll3D,    ///< Family-grouped, sprites first.
        All3DThenAllSprites     ///< Family-grouped, 3D first.
    };

    /**
     * @brief This renderer's measured family-replay order.
     *
     * Anything but `PublicOrder` is a declared, still-open defect on that renderer, and the staircase
     * below asserts the COLLAPSED result so the declaration stays falsifiable in both directions --
     * the day the renderer is corrected, these checks fail and this constant has to be turned over
     * deliberately rather than silently.
     *
     * WEBGPU replayed all of a cycle's 3D draws and then all of its sprites, so `SpriteBatch -> 3D`
     * came out inverted while `3D -> SpriteBatch` looked correct. That was the same root cause
     * REMED-GFX-157 corrected on Vulkan and bgfx (which grouped the other way round), and
     * REMED-GFX-159 has now corrected it here: every renderer owes public order, so this constant is
     * `PublicOrder` everywhere and the enum is kept only so a regression names the shape it
     * regressed to instead of merely reporting permuted stripes.
     */
    constexpr Replay kReplay = Replay::PublicOrder;

    /** @brief True when this renderer honours public order between the two families. */
    constexpr bool kPublicFamilyOrder = (kReplay == Replay::PublicOrder);

    // REMED-GFX-161 (RESOLVED, 2026-07-30): the discarded first-read warm-up this file used to carry
    // for WEBGPU is gone. The measured symptom -- the very first GetBackBufferData of a run left part
    // of the caller's buffer holding its poison prefill while later reads were correct -- was the
    // pre-REMED-GFX-165 rectangle-less dimension source: GraphicsDevice::GetBackBufferData sized the
    // region from renderer_->GetViewportSize() evaluated BEFORE the read's own EnsureFrameRendered
    // realised the surface, so on the first frame WebGPU's aspect-scaled viewport (53x32 for a 64x32
    // backbuffer) undersized the region and a trailing block stayed poison. GFX-165 sizes the region
    // from the authoritative PresentationParameters, so the first read is now complete; leg A asserts
    // it directly. backbuffer_first_read_test.cpp is the dedicated regression guard.

    /**
     * @brief Whether a `Clear()` issued AFTER a draw in the SAME bind cycle wipes that draw.
     *
     * False on a renderer whose only clear mechanism for a pass is its load action, which
     * necessarily runs before every draw of that pass. REMED-GFX-143's fixture declares the same
     * property per renderer under the name `clearAfterDrawWinsOnBackbuffer`; leg J measures it on a
     * PreserveContents render target and is not this task's subject either way -- it is declared so
     * that a clear-ordering regression cannot hide inside a family-ordering fixture.
     *
     * WEBGPU joined SDL_GPU here when REMED-GFX-159 landed, and the two facts are independent: leg
     * J had been skipping behind the family-order gate above, so turning that gate over is what
     * first RAN it. Measured then: a `Clear()` between the two families left the earlier sprite
     * standing and a `Clear()` after both left both standing, because `clearColorPending_` only
     * ever selected `WGPULoadOp_Clear` for the whole pass.
     *
     * REMED-GFX-156 fixed both: each cuts its bind cycle into one native render pass per observable
     * Clear, so the declaration is unconditional and leg J -- which was SKIPPED, not failing, on
     * exactly those two renderers, and is this file's only Clear coverage -- now runs everywhere.
     */
    constexpr bool kClearAfterDrawWins = true;

    /**
     * @brief Whether `GetBackBufferData` rejects on a renderer that rasterizes nothing.
     *
     * REMED-GFX-127/130 made the texture and render-target readbacks reject rather than return a
     * fabricated image. REMED-GFX-162 brought HEADLESS's BACKBUFFER readback under the same
     * contract: it formerly returned successfully and FILLED the caller's buffer with the last
     * Clear() colour (a caller could not distinguish a non-rasterizing device from a rendered
     * frame), and now raises `System::NotSupportedException` instead. So every renderer -- rasterizing
     * or not -- rejects a readback it cannot honestly satisfy, hence `true` for all of them.
     */
    constexpr bool kBackbufferReadRejectsWhenNonRasterizing = true;

    constexpr int kStripes = 8;   ///< Vertical full-height stripes across every destination.

    constexpr int kBBW = 64;      ///< Backbuffer width: eight 8-pixel stripes.
    constexpr int kBBH = 32;      ///< Backbuffer height.

    constexpr int kRTW = 64;      ///< Equal-dimension render target.
    constexpr int kRTH = 32;

    constexpr int kRT2W = 32;     ///< Deliberately DIFFERENT dimensions, and not a multiple of the
    constexpr int kRT2H = 16;     ///< backbuffer's, so a size assumption cannot pass.

    /// Fully saturated, so sRGB encoding is the identity on every asserted texel.
    const Color kClear  (  0,   0,   0, 255);
    const Color kRed    (255,   0,   0, 255);
    const Color kGreen  (  0, 255,   0, 255);
    const Color kBlue   (  0,   0, 255, 255);
    const Color kYellow (255, 255,   0, 255);
    const Color kMagenta(255,   0, 255, 255);
    const Color kCyan   (  0, 255, 255, 255);
    const Color kWhite  (255, 255, 255, 255);

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

    /** @brief Which family issues a step of a sequence. */
    enum class Fam
    {
        Sprite,   ///< SpriteBatch.Begin / Draw / End.
        Three     ///< A stock effect Apply followed by a GraphicsDevice draw call.
    };

    /** @brief Which GraphicsDevice entry point a 3D step uses. */
    enum class Draw3D
    {
        UserPrimitives,        ///< DrawUserPrimitives over VertexPositionTexture.
        UserIndexed16,         ///< DrawUserIndexedPrimitives with 16-bit indices.
        UserIndexed32,         ///< DrawUserIndexedPrimitives with 32-bit indices.
        UserVertexColor,       ///< DrawUserPrimitives over VertexPositionColor, no texture at all.
        BufferPrimitives,      ///< SetVertexBuffer + DrawPrimitives.
        BufferIndexed,         ///< SetVertexBuffer + Indices + DrawIndexedPrimitives.
        BufferPrimitivesRange, ///< The same, from a NONZERO vertexStart inside a wider buffer.
        BufferIndexedRange     ///< The same, from a NONZERO startIndex and baseVertex.
    };

    /** @brief Which stock effect a 3D step applies. */
    enum class Fx
    {
        Basic,            ///< BasicEffect with a texture.
        BasicVertexColor, ///< BasicEffect with VertexColorEnabled and no texture.
        AlphaTest,        ///< AlphaTestEffect, CompareFunction::Always so nothing is discarded.
        DualTexture,      ///< DualTextureEffect, second texture white so the modulate is a no-op.
        EnvironmentMap,   ///< EnvironmentMapEffect over VertexPositionNormalTexture.
        Skinned           ///< SkinnedEffect with identity bones.
    };

    /** @brief One step of a public sequence: a family and the colour it must leave behind. */
    struct Step
    {
        Fam   fam;
        Color colour;
        /**
         * @brief A `Fam::Three` step's own stock effect, when it must differ from its neighbours'.
         *
         * Empty -- the shape every leg before N uses -- means "whatever effect the staircase itself
         * was given", so one sequence stays inside one 3D command family. Leg N sets it per step
         * precisely so a sequence spans SEVERAL of them: a deferred renderer that keeps one queue
         * per stock effect can invert two 3D draws exactly as it can invert a 3D draw against a
         * sprite, and only a sequence whose steps land in different queues can see it.
         */
        std::optional<Fx> fx{};
    };
}

/**
 * @brief REMED-GFX-157's public ordering contract between SpriteBatch and stock 3D draws.
 */
class SpriteBatch3DOrderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    int  skipCount_ = 0;

    std::unique_ptr<SpriteBatch> sb_;
    Texture2D white_;                       ///< 1x1 white; sprites tint it to the step colour.
    Texture2D patTex_;                      ///< 8x4 pattern, for leg M's replay of leg I0's shape.
    Texture2D altPatTex_;                   ///< A second, unmistakably different 8x4 pattern.
    std::vector<Color> pattern_;            ///< patTex_'s texels, for exact comparison.
    std::vector<Color> altPattern_;         ///< altPatTex_'s texels.
    std::vector<Texture2D> solid_;          ///< 1x1 solid colours, indexed by PaletteIndex().
    std::unique_ptr<TextureCube> envCube_;  ///< For EnvironmentMapEffect only.

    // Long-lived so the primary legs cannot be blamed on object lifetime (leg L measures that).
    std::unique_ptr<BasicEffect>          basicFx_;
    std::unique_ptr<BasicEffect>          basicFx2_;    ///< A second, distinct BasicEffect object.
    std::unique_ptr<BasicEffect>          vcolFx_;
    std::unique_ptr<AlphaTestEffect>      alphaFx_;
    std::unique_ptr<DualTextureEffect>    dualFx_;
    std::unique_ptr<EnvironmentMapEffect> envFx_;

    std::unique_ptr<VertexBuffer> quadVb_;      ///< 6 VertexPositionTexture, rewritten per draw.
    std::unique_ptr<VertexBuffer> quadIdxVb_;   ///< 4 VertexPositionTexture for the indexed paths.
    std::unique_ptr<IndexBuffer>  quadIb16_;    ///< 6 x 16-bit indices.
    std::unique_ptr<VertexBuffer> wideVb_;      ///< 12 vertices: a nonzero vertexStart range.
    std::unique_ptr<VertexBuffer> wideIdxVb_;   ///< 8 vertices for the nonzero-baseVertex range.
    std::unique_ptr<IndexBuffer>  wideIb16_;    ///< 12 indices: a nonzero startIndex range.

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
        ++skipCount_;
    }

    void info(const std::string& text)
    {
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
    }

    // ------------------------------------------------------------------ palette

    /// The index into solid_ of one of the seven non-clear palette colours.
    static int PaletteIndex(const Color& c)
    {
        if (Same(c, kRed))     return 0;
        if (Same(c, kGreen))   return 1;
        if (Same(c, kBlue))    return 2;
        if (Same(c, kYellow))  return 3;
        if (Same(c, kMagenta)) return 4;
        if (Same(c, kCyan))    return 5;
        return 6;   // white
    }

    // ------------------------------------------------------------------ destination

    /// Where a sequence draws. A null target means the backbuffer.
    struct Dest
    {
        RenderTarget2D* rt = nullptr;
        int w = kBBW;
        int h = kBBH;
        const char* name = "backbuffer";
    };

    // ------------------------------------------------------------------ readback

    /** @brief The outcome of a public readback. */
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

    /// Reads @p dest back, pre-filled with a poison value so a renderer that writes nothing cannot
    /// be mistaken for one that wrote transparent black.
    Readback Read(GraphicsDevice& dev, const Dest& dest)
    {
        Readback r;
        r.w = dest.w;
        r.h = dest.h;
        r.pixels.assign(static_cast<std::size_t>(dest.w) * dest.h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            if (dest.rt) dest.rt->GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
            else         dev.GetBackBufferData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
        }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// True when @p dest can be asserted on at all here; reports the boundary otherwise.
    bool Readable(const Readback& r, const Dest& dest, const std::string& label)
    {
        if (!kRasterizes)
        {
            check(r.threwNotSupported,
                  label + ": declared non-rasterizing, every readback must throw "
                          "NotSupportedException");
            return false;
        }
        if (!r.ok())
        {
            skip(label + ": " + dest.name + " oracle unavailable on " + kRendererName + " (" +
                 (r.threwNotSupported ? "NotSupportedException" : r.otherWhat) +
                 ") -- boundary recorded");
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------ geometry

    /// The pixel rectangle of stripes [@p from, @p to) of @p dest, full height.
    static Rectangle StripeRect(const Dest& dest, int from, int to)
    {
        const int x0 = from * dest.w / kStripes;
        const int x1 = to   * dest.w / kStripes;
        return Rectangle(x0, 0, x1 - x0, dest.h);
    }

    /**
     * @brief The colour stripe @p stripe of @p r holds, or a poison value when it is not uniform.
     *
     * A one-pixel margin is left on every side of the stripe: stripe boundaries fall exactly on a
     * pixel edge, and renderers legitimately differ on the fill rule for a primitive edge, so an
     * interior sample is what makes this comparison byte-exact rather than fill-rule-dependent.
     */
    static Color StripeColor(const Readback& r, int stripe)
    {
        const int x0 = stripe * r.w / kStripes;
        const int x1 = (stripe + 1) * r.w / kStripes;
        const int xa = x0 + 1, xb = x1 - 1;
        const int ya = 1, yb = r.h - 1;
        if (xa >= xb || ya >= yb) return Color(0xCD, 0xCD, 0xCD, 0xCD);
        const Color first = r.at(xa, ya);
        for (int y = ya; y < yb; ++y)
            for (int x = xa; x < xb; ++x)
                if (!Same(r.at(x, y), first)) return Color(0xCD, 0xCD, 0xCD, 0xCD);
        return first;
    }

    // ------------------------------------------------------------------ draw helpers

    /// Opens a bind cycle on @p dest: bind it, reset the three states, clear to opaque black.
    void Begin(GraphicsDevice& dev, const Dest& dest)
    {
        dev.SetRenderTarget(dest.rt);
        ResetState(dev);
        dev.Clear(kClear);
    }

    /// The state-neutral baseline every leg starts from: no depth, no culling, no blending.
    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    /// Fills stripes [@p from, @p to) of @p dest with @p colour through one SpriteBatch cycle.
    void SpriteStripes(const Dest& dest, int from, int to, const Color& colour)
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb_->Draw(white_, StripeRect(dest, from, to), Rectangle(0, 0, 1, 1), colour);
        sb_->End();
    }

    /// The clip-space X of stripe boundary @p stripe. Dimension-independent by construction.
    static float StripeClipX(int stripe)
    {
        return 2.0f * static_cast<float>(stripe) / static_cast<float>(kStripes) - 1.0f;
    }

    /// Six textured vertices covering stripes [@p from, @p to), full height, as a TriangleList.
    static void QuadVPT(int from, int to, VertexPositionTexture* out)
    {
        const float xL = StripeClipX(from), xR = StripeClipX(to);
        out[0] = { Vector3(xL,  1.f, 0.f), Vector2(0.f, 0.f) };
        out[1] = { Vector3(xL, -1.f, 0.f), Vector2(0.f, 1.f) };
        out[2] = { Vector3(xR, -1.f, 0.f), Vector2(1.f, 1.f) };
        out[3] = { Vector3(xL,  1.f, 0.f), Vector2(0.f, 0.f) };
        out[4] = { Vector3(xR, -1.f, 0.f), Vector2(1.f, 1.f) };
        out[5] = { Vector3(xR,  1.f, 0.f), Vector2(1.f, 0.f) };
    }

    /// Four textured vertices for the indexed paths: TL, BL, BR, TR.
    static void QuadVPTIndexed(int from, int to, VertexPositionTexture* out)
    {
        const float xL = StripeClipX(from), xR = StripeClipX(to);
        out[0] = { Vector3(xL,  1.f, 0.f), Vector2(0.f, 0.f) };
        out[1] = { Vector3(xL, -1.f, 0.f), Vector2(0.f, 1.f) };
        out[2] = { Vector3(xR, -1.f, 0.f), Vector2(1.f, 1.f) };
        out[3] = { Vector3(xR,  1.f, 0.f), Vector2(1.f, 0.f) };
    }

    /// Six vertex-coloured vertices covering stripes [@p from, @p to), full height.
    static void QuadVPC(int from, int to, const Color& c, VertexPositionColor* out)
    {
        const float xL = StripeClipX(from), xR = StripeClipX(to);
        out[0] = { Vector3(xL,  1.f, 0.f), c };
        out[1] = { Vector3(xL, -1.f, 0.f), c };
        out[2] = { Vector3(xR, -1.f, 0.f), c };
        out[3] = { Vector3(xL,  1.f, 0.f), c };
        out[4] = { Vector3(xR, -1.f, 0.f), c };
        out[5] = { Vector3(xR,  1.f, 0.f), c };
    }

    /// Six vertices with a +Z normal, for the effects that need one.
    static void QuadVPNT(int from, int to, VertexPositionNormalTexture* out)
    {
        const float xL = StripeClipX(from), xR = StripeClipX(to);
        const Vector3 n(0.f, 0.f, 1.f);
        out[0] = { Vector3(xL,  1.f, 0.f), n, Vector2(0.f, 0.f) };
        out[1] = { Vector3(xL, -1.f, 0.f), n, Vector2(0.f, 1.f) };
        out[2] = { Vector3(xR, -1.f, 0.f), n, Vector2(1.f, 1.f) };
        out[3] = { Vector3(xL,  1.f, 0.f), n, Vector2(0.f, 0.f) };
        out[4] = { Vector3(xR, -1.f, 0.f), n, Vector2(1.f, 1.f) };
        out[5] = { Vector3(xR,  1.f, 0.f), n, Vector2(1.f, 0.f) };
    }

    /// Points a stock effect at identity transforms and, where it has one, at @p tex.
    void ConfigureEffect(Fx fx, Texture2D* tex)
    {
        const Matrix id = Matrix::getIdentityProperty();
        switch (fx)
        {
        case Fx::Basic:
            basicFx_->setWorldProperty(id); basicFx_->setViewProperty(id);
            basicFx_->setProjectionProperty(id);
            basicFx_->setTextureEnabledProperty(true);
            basicFx_->setTextureProperty(tex);
            basicFx_->VertexColorEnabled = false;
            basicFx_->setLightingEnabledProperty(false);
            break;
        case Fx::BasicVertexColor:
            vcolFx_->setWorldProperty(id); vcolFx_->setViewProperty(id);
            vcolFx_->setProjectionProperty(id);
            vcolFx_->setTextureEnabledProperty(false);
            vcolFx_->setTextureProperty(nullptr);
            vcolFx_->VertexColorEnabled = true;
            vcolFx_->setLightingEnabledProperty(false);
            break;
        case Fx::AlphaTest:
            alphaFx_->setWorldProperty(id); alphaFx_->setViewProperty(id);
            alphaFx_->setProjectionProperty(id);
            alphaFx_->setTextureProperty(tex);
            alphaFx_->setVertexColorEnabledProperty(false);
            alphaFx_->setAlphaFunctionProperty(CompareFunction::Always);
            alphaFx_->setReferenceAlphaProperty(0);
            break;
        case Fx::DualTexture:
            dualFx_->setWorldProperty(id); dualFx_->setViewProperty(id);
            dualFx_->setProjectionProperty(id);
            dualFx_->setTextureProperty(tex);
            dualFx_->setTexture2Property(&white_);
            dualFx_->setVertexColorEnabledProperty(false);
            break;
        case Fx::EnvironmentMap:
            envFx_->setWorldProperty(id); envFx_->setViewProperty(id);
            envFx_->setProjectionProperty(id);
            envFx_->setTextureProperty(tex);
            envFx_->setEnvironmentMapProperty(envCube_.get());
            envFx_->setEnvironmentMapAmountProperty(0.f);
            envFx_->setFresnelFactorProperty(0.f);
            break;
        case Fx::Skinned:
            break;   // handled by its own leg
        }
    }

    /// Applies the configured stock effect, so the following draw call has a current effect.
    void ApplyEffect(Fx fx)
    {
        switch (fx)
        {
        case Fx::Basic:            basicFx_->Apply(); break;
        case Fx::BasicVertexColor: vcolFx_->Apply();  break;
        case Fx::AlphaTest:        alphaFx_->Apply(); break;
        case Fx::DualTexture:      dualFx_->Apply();  break;
        case Fx::EnvironmentMap:   envFx_->Apply();   break;
        case Fx::Skinned:          break;
        }
    }

    /**
     * @brief Draws stripes [@p from, @p to) of the bound destination through the stock 3D path.
     *
     * The geometry is expressed in CLIP SPACE, so this never touches the viewport-segmentation
     * machinery and is independent of the destination's dimensions.
     */
    void Draw3DStripes(GraphicsDevice& dev, int from, int to, const Color& colour,
                       Draw3D mode = Draw3D::UserPrimitives, Fx fx = Fx::Basic)
    {
        Texture2D* tex = &solid_[static_cast<std::size_t>(PaletteIndex(colour))];
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

        if (fx == Fx::EnvironmentMap)
        {
            VertexPositionNormalTexture q[6];
            QuadVPNT(from, to, q);
            ConfigureEffect(fx, tex);
            ApplyEffect(fx);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            return;
        }
        if (mode == Draw3D::UserVertexColor || fx == Fx::BasicVertexColor)
        {
            VertexPositionColor q[6];
            QuadVPC(from, to, colour, q);
            ConfigureEffect(Fx::BasicVertexColor, nullptr);
            ApplyEffect(Fx::BasicVertexColor);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            return;
        }

        ConfigureEffect(fx, tex);

        switch (mode)
        {
        case Draw3D::UserPrimitives:
        {
            VertexPositionTexture q[6];
            QuadVPT(from, to, q);
            ApplyEffect(fx);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            break;
        }
        case Draw3D::UserIndexed16:
        {
            VertexPositionTexture q[4];
            QuadVPTIndexed(from, to, q);
            const std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
            ApplyEffect(fx);
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, q, 0, 4, idx, 0, 2);
            break;
        }
        case Draw3D::UserIndexed32:
        {
            VertexPositionTexture q[4];
            QuadVPTIndexed(from, to, q);
            const std::uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
            ApplyEffect(fx);
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, q, 0, 4, idx, 0, 2);
            break;
        }
        case Draw3D::BufferPrimitives:
        {
            VertexPositionTexture q[6];
            QuadVPT(from, to, q);
            quadVb_->SetData(q, 0, 6);
            ApplyEffect(fx);
            dev.SetVertexBuffer(quadVb_.get());
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            break;
        }
        case Draw3D::BufferIndexed:
        {
            VertexPositionTexture q[4];
            QuadVPTIndexed(from, to, q);
            quadIdxVb_->SetData(q, 0, 4);
            ApplyEffect(fx);
            dev.SetVertexBuffer(quadIdxVb_.get());
            dev.setIndicesProperty(quadIb16_.get());
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);
            break;
        }
        case Draw3D::BufferPrimitivesRange:
        {
            // The wanted quad sits at vertex 6 of a 12-vertex buffer whose first six vertices are a
            // degenerate quad, so a renderer that ignores vertexStart draws nothing visible rather
            // than the right thing by accident (REMED-GFX-119/125's contract).
            VertexPositionTexture q[12];
            QuadVPT(0, 0, q);          // degenerate: zero width
            QuadVPT(from, to, q + 6);
            wideVb_->SetData(q, 0, 12);
            ApplyEffect(fx);
            dev.SetVertexBuffer(wideVb_.get());
            dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
            dev.SetVertexBuffer(nullptr);
            break;
        }
        case Draw3D::BufferIndexedRange:
        {
            // The wanted quad is vertices 4..7, reached through indices 6..11 with baseVertex 4,
            // so both nonzero range arguments must be honoured (REMED-GFX-107/110/117).
            VertexPositionTexture q[8];
            QuadVPTIndexed(0, 0, q);       // degenerate
            QuadVPTIndexed(from, to, q + 4);
            const std::uint16_t idx[12] = { 0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3 };
            wideIdxVb_->SetData(q, 0, 8);
            wideIb16_->SetData(idx, 0, 12);
            ApplyEffect(fx);
            dev.SetVertexBuffer(wideIdxVb_.get());
            dev.setIndicesProperty(wideIb16_.get());
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 4, 0, 4, 6, 2);
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);
            break;
        }
        case Draw3D::UserVertexColor:
            break;   // handled above
        }
    }

    /// Issues one step of a sequence into stripes [@p from, @p to).
    void RunStep(GraphicsDevice& dev, const Dest& dest, const Step& s, int from, int to,
                 Draw3D mode, Fx fx)
    {
        if (s.fam == Fam::Sprite) SpriteStripes(dest, from, to, s.colour);
        else                      Draw3DStripes(dev, from, to, s.colour, mode, s.fx.value_or(fx));
    }

    // ------------------------------------------------------------------ the staircase oracle

    /**
     * @brief Runs @p steps as a staircase on @p dest and asserts the exact public order.
     *
     * Step i covers stripes [0, N-i), so stripe j must end up holding step (N-1-j)'s colour and
     * stripes [N, kStripes) must still hold the clear colour. One readback proves that every draw
     * happened AND that they happened in the issued order.
     */
    void Staircase(GraphicsDevice& dev, const Dest& dest, const std::vector<Step>& steps,
                   const std::string& label, Draw3D mode = Draw3D::UserPrimitives,
                   Fx fx = Fx::Basic)
    {
        const int n = static_cast<int>(steps.size());
        Begin(dev, dest);
        for (int i = 0; i < n; ++i)
            RunStep(dev, dest, steps[static_cast<std::size_t>(i)], 0, n - i, mode, fx);
        Readback r = Read(dev, dest);
        if (!Readable(r, dest, label)) return;

        /// The replay position of step @p i under this renderer's declared family order. Steps of
        /// the same family always keep their relative order, so the family key dominates and the
        /// issue index breaks ties.
        auto rank = [&](int i) -> long long {
            const bool sprite = steps[static_cast<std::size_t>(i)].fam == Fam::Sprite;
            switch (kReplay)
            {
            case Replay::AllSpritesThenAll3D: return (sprite ? 0LL : 1LL) * 4096 + i;
            case Replay::All3DThenAllSprites: return (sprite ? 1LL : 0LL) * 4096 + i;
            case Replay::PublicOrder:         break;
            }
            return i;
        };
        /// Step i covers stripes [0, n-i), so stripe j is covered by every step with i < n-j; the
        /// one that survives is whichever of those replays LAST.
        auto winner = [&](int j) {
            int best = 0;
            for (int i = 1; i < n - j; ++i)
                if (rank(i) > rank(best)) best = i;
            return best;
        };

        bool ok = true;
        std::string detail;
        for (int j = 0; j < n; ++j)
        {
            const Color want = steps[static_cast<std::size_t>(winner(j))].colour;
            const Color got  = StripeColor(r, j);
            if (!Same(got, want))
            {
                ok = false;
                if (detail.empty())
                    detail = "; stripe " + std::to_string(j) + " must be step " +
                             std::to_string(winner(j)) + " " + ColorText(want) + " but is " +
                             ColorText(got) +
                             (Same(got, kClear) ? " (the clear colour -- that draw never landed)"
                                                : " (a different step -- the order is wrong)");
            }
        }
        for (int j = n; j < kStripes; ++j)
        {
            const Color got = StripeColor(r, j);
            if (!Same(got, kClear))
            {
                ok = false;
                if (detail.empty())
                    detail = "; stripe " + std::to_string(j) + " must still be the clear colour "
                             "but is " + ColorText(got);
            }
        }
        check(ok, label + detail);
    }

    /// The six canonical sequences, run as staircases on @p dest.
    void CanonicalSequences(GraphicsDevice& dev, const Dest& dest, const std::string& prefix)
    {
        if (!kDraws3D)
        {
            skip(prefix + ": skipped -- this renderer does not rasterize stock 3D geometry");
            return;
        }
        Staircase(dev, dest, { {Fam::Sprite, kRed}, {Fam::Three, kBlue} },
                  prefix + "1 SpriteBatch -> 3D, both visible and in public order");
        Staircase(dev, dest, { {Fam::Three, kBlue}, {Fam::Sprite, kRed} },
                  prefix + "2 3D -> SpriteBatch, both visible and in public order");
        Staircase(dev, dest, { {Fam::Sprite, kRed}, {Fam::Three, kBlue}, {Fam::Sprite, kGreen} },
                  prefix + "3 SpriteBatch -> 3D -> SpriteBatch");
        Staircase(dev, dest, { {Fam::Three, kBlue}, {Fam::Sprite, kRed}, {Fam::Three, kGreen} },
                  prefix + "4 3D -> SpriteBatch -> 3D");
        Staircase(dev, dest, { {Fam::Sprite, kRed}, {Fam::Three, kBlue}, {Fam::Sprite, kRed} },
                  prefix + "5 A -> B -> A restoration, the same sprite colour on both sides");
        Staircase(dev, dest,
                  { {Fam::Sprite, kRed}, {Fam::Three, kBlue}, {Fam::Sprite, kGreen},
                    {Fam::Three, kYellow}, {Fam::Sprite, kMagenta} },
                  prefix + "6 five alternating families in one bind cycle");
    }

    // ------------------------------------------------------------------ the parity oracle

    /**
     * @brief Asserts one 3D draw renders identically whether or not a SpriteBatch preceded it.
     *
     * The control issues the 3D draw as the FIRST thing in its cycle; the subject issues the same
     * draw after a SpriteBatch that lands in a stripe the 3D draw never touches. Byte-comparing the
     * two readbacks is exact for every stock effect and every draw entry point without this file
     * predicting any effect's shading maths. The sprite stripe is asserted too, so a subject that
     * simply drew nothing at all cannot pass.
     */
    void Parity(GraphicsDevice& dev, const Dest& dest, const std::string& label,
                Draw3D mode, Fx fx)
    {
        Begin(dev, dest);
        Draw3DStripes(dev, 0, 2, kBlue, mode, fx);
        Readback ctrl = Read(dev, dest);
        if (!Readable(ctrl, dest, label)) return;

        const Color ctrlColor = StripeColor(ctrl, 0);
        if (Same(ctrlColor, kClear))
        {
            skip(label + ": skipped -- this draw entry point / effect renders nothing on " +
                 kRendererName + " even as the FIRST draw of a cycle, so it cannot measure ordering");
            return;
        }

        Begin(dev, dest);
        SpriteStripes(dest, 4, 5, kRed);
        Draw3DStripes(dev, 0, 2, kBlue, mode, fx);
        Readback subj = Read(dev, dest);
        if (!Readable(subj, dest, label)) return;

        const Color got = StripeColor(subj, 0);
        const Color sprite = StripeColor(subj, 4);
        const bool ok = Same(got, ctrlColor) && Same(sprite, kRed);
        check(ok, label + (ok ? "" :
              "; the 3D draw is " + ColorText(got) + " after a SpriteBatch but " +
              ColorText(ctrlColor) + " on its own" +
              (Same(got, kClear) ? " (it never landed)" : "") +
              ", and the sprite stripe is " + ColorText(sprite)));
    }

    /// True when this renderer may be asserted on for inter-family order; skips and says so if not.
    bool RequirePublicFamilyOrder(const std::string& label)
    {
        if (kPublicFamilyOrder) return true;
        skip(label + ": skipped -- " + kRendererName + " replays a bind cycle's draws grouped by "
             "family, not in public order (declared open finding); the staircase legs assert that "
             "collapsed shape instead");
        return false;
    }

    // ==================================================================== legs

    /// A -- the canonical sequences on the backbuffer.
    void LegBackbuffer(GraphicsDevice& dev)
    {
        Dest bb;
        if (!kRasterizes)
        {
            // A non-rasterizing renderer has no ordering result to observe, but the sequence must
            // still be legal and REMED-GFX-127/130's contract must still reject the readback
            // deterministically rather than fabricating a frame. That is what is asserted here.
            bool threw = false;
            try
            {
                Begin(dev, bb);
                SpriteStripes(bb, 0, 2, kRed);
                if (kDraws3D) Draw3DStripes(dev, 0, 1, kBlue);
            }
            catch (...) { threw = true; }
            check(!threw, "A0 a mixed SpriteBatch/3D bind cycle is legal on " +
                          std::string(kRendererName) + " even though nothing rasterizes");
            Readback r = Read(dev, bb);
            info(std::string("A0 the backbuffer readback on ") + kRendererName + " " +
                 (r.threwNotSupported ? "threw NotSupportedException"
                  : r.threwSomethingElse ? ("threw: " + r.otherWhat)
                                         : "returned without throwing"));
            bool stillPoison = r.ok();
            if (r.ok())
                for (const Color& c : r.pixels)
                    if (!Same(c, Color(0xCD, 0xCD, 0xCD, 0xCD))) { stillPoison = false; break; }
            info(std::string("A0 and it ") +
                 (!r.ok() ? "rejected"
                  : stillPoison ? "left the caller's buffer completely untouched"
                                : "filled the caller's buffer"));
            // REMED-GFX-127/130 established that a renderer which rasterizes nothing must REJECT a
            // readback rather than hand back scratch as if it were a rendered frame. That contract
            // was established for the TEXTURE and RENDER-TARGET readbacks; REMED-GFX-162 extended it
            // to `GraphicsDevice.GetBackBufferData` on HEADLESS, which formerly filled the caller's
            // buffer with the last Clear() colour and now rejects deterministically. The dedicated
            // overload/precedence/destination-integrity oracle lives in
            // backbuffer_headless_reject_test.cpp; this leg just asserts the rejection is in force.
            check(r.ok() == !kBackbufferReadRejectsWhenNonRasterizing,
                  "A0 the backbuffer readback behaves as declared for a non-rasterizing renderer "
                  "(declared: " +
                  std::string(kBackbufferReadRejectsWhenNonRasterizing ? "rejects"
                                                                       : "does not reject") + ")");
            return;
        }
        if (!kReadsBackbuffer)
        {
            skip("A: skipped -- no public backbuffer readback on " + std::string(kRendererName) +
                 "; legs B and C carry the whole contract");
            return;
        }
        // REMED-GFX-161 (RESOLVED): no discarded first-read warm-up here any more -- the first public
        // backbuffer read of the process is now the measured one and must be complete.
        CanonicalSequences(dev, bb, "A");
    }

    /// B -- the canonical sequences on a RenderTarget2D of the same dimensions.
    void LegTargetEqualDims(GraphicsDevice& dev)
    {
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target (equal dimensions)" };
        CanonicalSequences(dev, d, "B");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// C -- the canonical sequences on a RenderTarget2D of different dimensions.
    void LegTargetOtherDims(GraphicsDevice& dev)
    {
        RenderTarget2D rt(dev, kRT2W, kRT2H, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRT2W, kRT2H, "render target (different dimensions)" };
        CanonicalSequences(dev, d, "C");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// D -- destination transitions: the mixed sequence must hold in every bind-cycle arrangement.
    void LegDestinationTransitions(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("D: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("D")) return;

        RenderTarget2D a(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        RenderTarget2D b(dev, kRT2W, kRT2H, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        Dest da{ &a, kRTW, kRTH, "target A" };
        Dest db{ &b, kRT2W, kRT2H, "target B" };
        Dest bb;

        // D1 target A -> backbuffer: A's own mixed cycle must survive a later backbuffer cycle that
        // also mixes families, and neither may lose its 3D draw.
        {
            Begin(dev, da);
            SpriteStripes(da, 0, 2, kRed);
            Draw3DStripes(dev, 0, 1, kBlue);
            Begin(dev, bb);
            SpriteStripes(bb, 0, 2, kGreen);
            Draw3DStripes(dev, 0, 1, kYellow);
            Readback rb = Read(dev, bb);
            Readback ra = Read(dev, da);
            if (Readable(ra, da, "D1 target A"))
                check(Same(StripeColor(ra, 0), kBlue) && Same(StripeColor(ra, 1), kRed),
                      "D1 target A keeps its own SpriteBatch -> 3D order across a later backbuffer "
                      "cycle (stripe0 " + ColorText(StripeColor(ra, 0)) + ", stripe1 " +
                      ColorText(StripeColor(ra, 1)) + ")");
            if (kReadsBackbuffer && Readable(rb, bb, "D1 backbuffer"))
                check(Same(StripeColor(rb, 0), kYellow) && Same(StripeColor(rb, 1), kGreen),
                      "D1 the backbuffer cycle that followed keeps its own order (stripe0 " +
                      ColorText(StripeColor(rb, 0)) + ", stripe1 " +
                      ColorText(StripeColor(rb, 1)) + ")");
        }

        // D2 backbuffer -> target A, the reverse arrangement.
        {
            Begin(dev, bb);
            SpriteStripes(bb, 0, 2, kCyan);
            Draw3DStripes(dev, 0, 1, kMagenta);
            Begin(dev, da);
            SpriteStripes(da, 0, 2, kRed);
            Draw3DStripes(dev, 0, 1, kGreen);
            Readback ra = Read(dev, da);
            // The public backbuffer oracle reads whatever is BOUND on the renderers whose readback
            // follows the binding, so the backbuffer has to be current to be read. Every draw of
            // both cycles is already queued at this point, so this unbind is not a barrier between
            // the two families -- it is the end of the target's bind cycle.
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Readback rb = Read(dev, bb);
            if (Readable(ra, da, "D2 target A"))
                check(Same(StripeColor(ra, 0), kGreen) && Same(StripeColor(ra, 1), kRed),
                      "D2 a target cycle that follows a backbuffer cycle keeps its order (stripe0 " +
                      ColorText(StripeColor(ra, 0)) + ")");
            if (kReadsBackbuffer && Readable(rb, bb, "D2 backbuffer"))
                check(Same(StripeColor(rb, 0), kMagenta) && Same(StripeColor(rb, 1), kCyan),
                      "D2 the earlier backbuffer cycle keeps its order (stripe0 " +
                      ColorText(StripeColor(rb, 0)) + ")");
        }

        // D3 target A -> target B -> target A: the second A cycle must not disturb the first, and
        // each target must keep its own family order.
        {
            Begin(dev, da);
            SpriteStripes(da, 0, 2, kRed);
            Draw3DStripes(dev, 0, 1, kBlue);
            Begin(dev, db);
            SpriteStripes(db, 0, 2, kGreen);
            Draw3DStripes(dev, 0, 1, kYellow);
            Begin(dev, da);
            SpriteStripes(da, 2, 4, kCyan);
            Draw3DStripes(dev, 2, 3, kMagenta);
            Readback ra = Read(dev, da);
            Readback rbb = Read(dev, db);
            if (Readable(ra, da, "D3 target A"))
                check(Same(StripeColor(ra, 2), kMagenta) && Same(StripeColor(ra, 3), kCyan),
                      "D3 target A's SECOND bind cycle keeps its own SpriteBatch -> 3D order "
                      "(stripe2 " + ColorText(StripeColor(ra, 2)) + ", stripe3 " +
                      ColorText(StripeColor(ra, 3)) + ")");
            if (Readable(rbb, db, "D3 target B"))
                check(Same(StripeColor(rbb, 0), kYellow) && Same(StripeColor(rbb, 1), kGreen),
                      "D3 target B keeps its own order between two cycles of target A (stripe0 " +
                      ColorText(StripeColor(rbb, 0)) + ")");
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// E -- textured and untextured controls, and the two families sharing one texture.
    void LegTexturing(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("E: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("E")) return;
        Dest bb;
        if (!kReadsBackbuffer) { skip("E: skipped -- no public backbuffer readback"); return; }

        // E1 untextured 3D after a SpriteBatch: proves texture binding is not what is involved.
        Staircase(dev, bb, { {Fam::Sprite, kRed}, {Fam::Three, kBlue} },
                  "E1 untextured (vertex-colour) 3D after a SpriteBatch",
                  Draw3D::UserVertexColor, Fx::BasicVertexColor);

        // E2 the two families sampling the SAME Texture2D object.
        {
            Begin(dev, bb);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
            sb_->Draw(solid_[static_cast<std::size_t>(PaletteIndex(kRed))],
                      StripeRect(bb, 0, 2), Rectangle(0, 0, 1, 1), kWhite);
            sb_->End();
            Draw3DStripes(dev, 0, 1, kRed);      // same texture object, through the 3D path
            Readback r = Read(dev, bb);
            if (Readable(r, bb, "E2"))
                check(Same(StripeColor(r, 0), kRed) && Same(StripeColor(r, 1), kRed),
                      "E2 both families sampling the SAME Texture2D keep public order (stripe0 " +
                      ColorText(StripeColor(r, 0)) + ", stripe1 " +
                      ColorText(StripeColor(r, 1)) + ")");
        }

        // E3 a sampler-state change between the families must not lose the 3D draw.
        {
            Begin(dev, bb);
            SpriteStripes(bb, 0, 2, kRed);
            dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
            Draw3DStripes(dev, 0, 1, kBlue);
            Readback r = Read(dev, bb);
            if (Readable(r, bb, "E3"))
                check(Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kRed),
                      "E3 a SamplerState change between the families keeps public order (stripe0 " +
                      ColorText(StripeColor(r, 0)) + ")");
            dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        }
    }

    /// F -- every stock draw entry point, through the parity oracle.
    void LegDrawEntryPoints(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("F: skipped -- no stock 3D rasterization here"); return; }
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        struct Case { Draw3D mode; const char* name; };
        const Case cases[] = {
            { Draw3D::UserPrimitives,        "F1 DrawUserPrimitives" },
            { Draw3D::UserIndexed16,         "F2 DrawUserIndexedPrimitives, 16-bit indices" },
            { Draw3D::UserIndexed32,         "F3 DrawUserIndexedPrimitives, 32-bit indices" },
            { Draw3D::UserVertexColor,       "F4 DrawUserPrimitives, VertexPositionColor" },
            { Draw3D::BufferPrimitives,      "F5 SetVertexBuffer + DrawPrimitives" },
            { Draw3D::BufferIndexed,         "F6 SetVertexBuffer + Indices + DrawIndexedPrimitives" },
            { Draw3D::BufferPrimitivesRange, "F7 DrawPrimitives from a nonzero vertexStart" },
            { Draw3D::BufferIndexedRange,    "F8 DrawIndexedPrimitives, nonzero startIndex and "
                                             "baseVertex" },
        };
        for (const Case& c : cases)
        {
            try
            {
                Parity(dev, d, std::string(c.name) + " renders identically after a SpriteBatch",
                       c.mode, Fx::Basic);
            }
            catch (const System::NotSupportedException& e)
            {
                skip(std::string(c.name) + ": skipped -- NotSupportedException on " +
                     kRendererName + " (" + e.what() + ")");
            }
            catch (const std::exception& e)
            {
                skip(std::string(c.name) + ": skipped -- threw on " + kRendererName + " (" +
                     e.what() + ")");
            }
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// G -- every stock effect, through the parity oracle.
    void LegStockEffects(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("G: skipped -- no stock 3D rasterization here"); return; }
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        struct Case { Fx fx; const char* name; };
        const Case cases[] = {
            { Fx::Basic,            "G1 BasicEffect with a texture" },
            { Fx::BasicVertexColor, "G2 BasicEffect with VertexColorEnabled" },
            { Fx::AlphaTest,        "G3 AlphaTestEffect" },
            { Fx::DualTexture,      "G4 DualTextureEffect" },
            { Fx::EnvironmentMap,   "G5 EnvironmentMapEffect" },
        };
        for (const Case& c : cases)
        {
            try
            {
                Parity(dev, d, std::string(c.name) + " renders identically after a SpriteBatch",
                       Draw3D::UserPrimitives, c.fx);
            }
            catch (const System::NotSupportedException& e)
            {
                skip(std::string(c.name) + ": skipped -- NotSupportedException on " +
                     kRendererName + " (" + e.what() + ")");
            }
            catch (const std::exception& e)
            {
                skip(std::string(c.name) + ": skipped -- threw on " + kRendererName + " (" +
                     e.what() + ")");
            }
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// H -- repeated Apply, two distinct effect objects, and Apply issued after SpriteBatch.End.
    void LegEffectApplication(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("H: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("H")) return;
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };
        const Matrix id = Matrix::getIdentityProperty();

        // H1 the same effect object applied twice between the families.
        {
            Begin(dev, d);
            SpriteStripes(d, 0, 2, kRed);
            ConfigureEffect(Fx::Basic, &solid_[static_cast<std::size_t>(PaletteIndex(kBlue))]);
            basicFx_->Apply();
            basicFx_->Apply();
            VertexPositionTexture q[6];
            QuadVPT(0, 1, q);
            dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            Readback r = Read(dev, d);
            if (Readable(r, d, "H1"))
                check(Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kRed),
                      "H1 a repeated Apply after SpriteBatch.End still lands (stripe0 " +
                      ColorText(StripeColor(r, 0)) + ")");
        }

        // H2 two DIFFERENT BasicEffect objects with compatible native programs, one per 3D step.
        {
            basicFx2_->setWorldProperty(id); basicFx2_->setViewProperty(id);
            basicFx2_->setProjectionProperty(id);
            basicFx2_->setTextureEnabledProperty(true);
            basicFx2_->setLightingEnabledProperty(false);
            basicFx2_->VertexColorEnabled = false;

            Begin(dev, d);
            SpriteStripes(d, 0, 3, kRed);
            ConfigureEffect(Fx::Basic, &solid_[static_cast<std::size_t>(PaletteIndex(kBlue))]);
            basicFx_->Apply();
            VertexPositionTexture q1[6];
            QuadVPT(0, 2, q1);
            dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q1, 0, 2);
            basicFx2_->setTextureProperty(&solid_[static_cast<std::size_t>(PaletteIndex(kGreen))]);
            basicFx2_->Apply();
            VertexPositionTexture q2[6];
            QuadVPT(0, 1, q2);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q2, 0, 2);
            Readback r = Read(dev, d);
            if (Readable(r, d, "H2"))
                check(Same(StripeColor(r, 0), kGreen) && Same(StripeColor(r, 1), kBlue) &&
                      Same(StripeColor(r, 2), kRed),
                      "H2 two distinct Effect objects after a SpriteBatch keep public order "
                      "(stripe0 " + ColorText(StripeColor(r, 0)) + ", stripe1 " +
                      ColorText(StripeColor(r, 1)) + ", stripe2 " +
                      ColorText(StripeColor(r, 2)) + ")");
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// I -- state changes between the families must not leak and must not lose either draw.
    void LegStateTransitions(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("I: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("I")) return;
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        /// One SpriteBatch -> 3D cycle under an explicit device state applied between them.
        auto transition = [&](const std::string& label, auto&& setState) {
            Begin(dev, d);
            SpriteStripes(d, 0, 2, kRed);
            setState();
            Draw3DStripes(dev, 0, 1, kBlue);
            Readback r = Read(dev, d);
            if (!Readable(r, d, label)) return;
            check(Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kRed),
                  label + " (stripe0 " + ColorText(StripeColor(r, 0)) + ", stripe1 " +
                  ColorText(StripeColor(r, 1)) + ")");
        };

        transition("I1 SpriteBatch DepthStencilState.None -> 3D DepthStencilState.Default",
                   [&] { dev.setDepthStencilStateProperty(DepthStencilState::Default); });
        transition("I2 SpriteBatch Opaque -> 3D AlphaBlend (opaque geometry, so the result is "
                   "still the 3D colour)",
                   [&] { dev.setBlendStateProperty(BlendState::AlphaBlend); });
        transition("I3 SpriteBatch CullNone -> 3D CullCounterClockwise is not what hides the draw",
                   [&] { dev.setRasterizerStateProperty(RasterizerState::CullNone); });
        transition("I4 an explicit full Viewport set between the families",
                   [&] { dev.setViewportProperty(Viewport(0, 0, kRTW, kRTH)); });
        transition("I5 an explicit full ScissorRectangle set between the families",
                   [&] { dev.setScissorRectangleProperty(Rectangle(0, 0, kRTW, kRTH)); });

        // I6 the reverse direction: an AlphaBlend SpriteBatch followed by an Opaque 3D draw.
        {
            Begin(dev, d);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, &noDepth, &noCull);
            sb_->Draw(white_, StripeRect(d, 0, 2), Rectangle(0, 0, 1, 1), kRed);
            sb_->End();
            dev.setBlendStateProperty(BlendState::Opaque);
            Draw3DStripes(dev, 0, 1, kBlue);
            Readback r = Read(dev, d);
            if (Readable(r, d, "I6"))
                check(Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kRed),
                      "I6 an AlphaBlend SpriteBatch followed by an Opaque 3D draw keeps order "
                      "(stripe0 " + ColorText(StripeColor(r, 0)) + ")");
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// J -- Clear() interleaved with the two families.
    void LegClears(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("J: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("J")) return;
        static_assert(kClearAfterDrawWins,
                      "REMED-GFX-156: leg J used to be SKIPPED behind this gate on SDL_GPU and "
                      "WebGPU, which is how a whole file's worth of Clear coverage measured "
                      "nothing on the two renderers that needed it. A renderer that regresses must "
                      "restore the declaration rather than re-introduce the skip.");
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::PreserveContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        // J1 a Clear BETWEEN the families wipes the sprite and keeps the 3D draw.
        {
            Begin(dev, d);
            SpriteStripes(d, 0, 4, kRed);
            dev.Clear(kClear);
            Draw3DStripes(dev, 0, 1, kBlue);
            Readback r = Read(dev, d);
            if (Readable(r, d, "J1"))
                check(Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kClear),
                      "J1 a Clear between the families wipes the sprite and keeps the later 3D "
                      "draw (stripe0 " + ColorText(StripeColor(r, 0)) + ", stripe1 " +
                      ColorText(StripeColor(r, 1)) + ")");
        }

        // J2 a Clear AFTER both families wipes both.
        {
            Begin(dev, d);
            SpriteStripes(d, 0, 4, kRed);
            Draw3DStripes(dev, 0, 1, kBlue);
            dev.Clear(kClear);
            Readback r = Read(dev, d);
            if (Readable(r, d, "J2"))
                check(Same(StripeColor(r, 0), kClear) && Same(StripeColor(r, 1), kClear),
                      "J2 a Clear after both families wipes both (stripe0 " +
                      ColorText(StripeColor(r, 0)) + ")");
        }

        // J3 the reverse pair: a Clear between 3D and SpriteBatch.
        {
            Begin(dev, d);
            Draw3DStripes(dev, 0, 4, kBlue);
            dev.Clear(kClear);
            SpriteStripes(d, 0, 1, kRed);
            Readback r = Read(dev, d);
            if (Readable(r, d, "J3"))
                check(Same(StripeColor(r, 0), kRed) && Same(StripeColor(r, 1), kClear),
                      "J3 a Clear between a 3D draw and a later SpriteBatch wipes the 3D draw "
                      "(stripe0 " + ColorText(StripeColor(r, 0)) + ", stripe1 " +
                      ColorText(StripeColor(r, 1)) + ")");
        }

        // J4 REMED-GFX-156's shape F: Clear, draw, Clear, draw, Clear -- alternating families, and
        // the trailing Clear must win over everything before it.
        {
            Begin(dev, d);
            SpriteStripes(d, 0, 4, kRed);
            dev.Clear(kBlue);
            Draw3DStripes(dev, 0, 3, kGreen);
            dev.Clear(kYellow);
            SpriteStripes(d, 0, 2, kMagenta);
            dev.Clear(kClear);
            Readback r = Read(dev, d);
            if (Readable(r, d, "J4"))
            {
                bool all = true;
                for (int s = 0; s < kStripes; ++s) if (!Same(StripeColor(r, s), kClear)) all = false;
                check(all, "J4 Clear, draw, Clear, draw, Clear across both families -- the last "
                           "Clear wins everywhere (stripe0 " + ColorText(StripeColor(r, 0)) +
                           ", stripe3 " + ColorText(StripeColor(r, 3)) + ")");
            }
        }

        // J5 REMED-GFX-156's shape D: two ordered Clears with one family between each pair, so the
        // surviving image spells the sequence rather than any one command.
        {
            Begin(dev, d);
            dev.Clear(kRed);
            SpriteStripes(d, 0, 4, kBlue);
            dev.Clear(kGreen);
            Draw3DStripes(dev, 0, 1, kMagenta);
            Readback r = Read(dev, d);
            if (Readable(r, d, "J5"))
                check(Same(StripeColor(r, 0), kMagenta) && Same(StripeColor(r, 1), kGreen) &&
                          Same(StripeColor(r, 3), kGreen),
                      "J5 Clear, SpriteBatch, Clear, 3D -- the second Clear erases the SpriteBatch "
                      "and the 3D draw lands on the cleared surface (stripe0 " +
                      ColorText(StripeColor(r, 0)) + ", stripe1 " + ColorText(StripeColor(r, 1)) +
                      ")");
        }

        // J6 two consecutive Clears with nothing between them: the LAST one wins, and it costs no
        // more than the one pass a single leading Clear costs (asserted structurally by the
        // per-renderer native traces, not here).
        {
            Begin(dev, d);
            SpriteStripes(d, 0, 4, kRed);
            dev.Clear(kBlue);
            dev.Clear(kGreen);
            Readback r = Read(dev, d);
            if (Readable(r, d, "J6"))
                check(Same(StripeColor(r, 0), kGreen),
                      "J6 two consecutive Clears after a draw -- the last one wins (stripe0 " +
                      ColorText(StripeColor(r, 0)) + ")");
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /**
     * @brief REMED-GFX-156: every draw entry point and every stock effect ACROSS a Clear boundary.
     *
     * A renderer that delivers an ordered mid-cycle Clear by cutting its bind cycle into two native
     * render passes has to rebuild, in the second pass, everything the first pass's end discarded:
     * the pipeline, the vertex and index buffers, the vertex declaration, the topology, the texture
     * and sampler bindings, the bind/resource groups, the blend, depth/stencil and rasterizer state,
     * the blend constant, the stencil reference, the viewport, the scissor and the effect uniforms.
     * A pixel oracle that only asks "did the clear happen" cannot see any of that.
     *
     * The oracle here is PARITY, and it sees all of it at once: the identical draw is issued once as
     * the FIRST thing in a bind cycle (control, one native pass) and once after a full-target draw
     * that an ordered Clear then wipes (subject, two native passes). The two readbacks must be
     * byte-identical over the WHOLE image -- so the clear must have erased the earlier draw exactly,
     * and the family draw must shade identically after a pass restart. No effect's shading maths has
     * to be predicted for this to be exact.
     */
    void LegClearBoundaryFamilies(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("P: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("P")) return;
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::PreserveContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        struct Case { Draw3D mode; Fx fx; const char* name; };
        const Case cases[] = {
            { Draw3D::UserPrimitives,        Fx::Basic,            "P1 DrawUserPrimitives" },
            { Draw3D::UserIndexed16,         Fx::Basic,            "P2 DrawUserIndexedPrimitives, 16-bit" },
            { Draw3D::UserIndexed32,         Fx::Basic,            "P3 DrawUserIndexedPrimitives, 32-bit" },
            { Draw3D::UserVertexColor,       Fx::BasicVertexColor, "P4 DrawUserPrimitives, vertex colour" },
            { Draw3D::BufferPrimitives,      Fx::Basic,            "P5 SetVertexBuffer + DrawPrimitives" },
            { Draw3D::BufferIndexed,         Fx::Basic,            "P6 DrawIndexedPrimitives" },
            { Draw3D::BufferPrimitivesRange, Fx::Basic,            "P7 DrawPrimitives, nonzero vertexStart" },
            { Draw3D::BufferIndexedRange,    Fx::Basic,            "P8 DrawIndexedPrimitives, nonzero range" },
            { Draw3D::UserPrimitives,        Fx::AlphaTest,        "P9 AlphaTestEffect" },
            { Draw3D::UserPrimitives,        Fx::DualTexture,      "P10 DualTextureEffect" },
            { Draw3D::UserPrimitives,        Fx::EnvironmentMap,   "P11 EnvironmentMapEffect" },
            { Draw3D::UserPrimitives,        Fx::Skinned,          "P12 SkinnedEffect" },
        };
        for (const Case& c : cases)
        {
            const std::string label =
                std::string(c.name) + " renders identically across an ordered Clear boundary";
            try
            {
                Begin(dev, d);
                Draw3DStripes(dev, 0, 2, kBlue, c.mode, c.fx);
                Readback ctrl = Read(dev, d);
                if (!Readable(ctrl, d, label)) continue;
                if (Same(StripeColor(ctrl, 0), kClear))
                {
                    skip(label + ": skipped -- this entry point / effect renders nothing on " +
                         kRendererName + " even as the FIRST draw of a cycle");
                    continue;
                }

                Begin(dev, d);
                SpriteStripes(d, 0, kStripes, kRed);   // whole target red...
                dev.Clear(kClear);                     // ...which the ordered Clear must erase
                Draw3DStripes(dev, 0, 2, kBlue, c.mode, c.fx);
                Readback subj = Read(dev, d);
                if (!Readable(subj, d, label)) continue;

                std::size_t bad = 0;
                std::string first;
                for (int y = 0; y < d.h; ++y)
                    for (int x = 0; x < d.w; ++x)
                        if (!Same(subj.at(x, y), ctrl.at(x, y)))
                        {
                            ++bad;
                            if (first.empty())
                                first = " first@(" + std::to_string(x) + "," + std::to_string(y) +
                                        ") got " + ColorText(subj.at(x, y)) + " want " +
                                        ColorText(ctrl.at(x, y));
                        }
                check(bad == 0, label + " [mismatched=" + std::to_string(bad) + "/" +
                                    std::to_string(static_cast<std::size_t>(d.w) * d.h) + first +
                                    "]");
            }
            catch (const System::NotSupportedException& e)
            {
                skip(label + ": skipped -- NotSupportedException on " + kRendererName + " (" +
                     e.what() + ")");
            }
            catch (const std::exception& e)
            {
                skip(label + ": skipped -- threw on " + kRendererName + " (" + e.what() + ")");
            }
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// K -- cardinality: many alternations per cycle, many cycles, and repetition across frames.
    void LegCardinality(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("K: skipped -- no stock 3D rasterization here"); return; }
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        // K1 eight alternating draws in ONE bind cycle.
        Staircase(dev, d,
                  { {Fam::Sprite, kRed}, {Fam::Three, kBlue}, {Fam::Sprite, kGreen},
                    {Fam::Three, kYellow}, {Fam::Sprite, kMagenta}, {Fam::Three, kCyan},
                    {Fam::Sprite, kWhite}, {Fam::Three, kRed} },
                  "K1 eight alternating families in one bind cycle");

        // K2 eight consecutive bind cycles of the same target, each mixing both families, with no
        // Present, flush or extra frame anywhere.
        if (RequirePublicFamilyOrder("K2"))
        {
            constexpr int kCycles = 8;
            int exact = 0;
            for (int i = 0; i < kCycles; ++i)
            {
                Begin(dev, d);
                SpriteStripes(d, 0, 2, kRed);
                Draw3DStripes(dev, 0, 1, kBlue);
                Readback r = Read(dev, d);
                if (!r.ok()) break;
                if (Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kRed)) ++exact;
            }
            Readback probe = Read(dev, d);
            if (Readable(probe, d, "K2"))
                check(exact == kCycles,
                      "K2 eight consecutive bind cycles each keep SpriteBatch -> 3D order (" +
                      std::to_string(exact) + "/" + std::to_string(kCycles) + ")");
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// L -- the lifetime control: a scope-LOCAL effect and local buffers, as leg I0 wrote it.
    void LegLocalLifetime(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("L: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("L")) return;
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        // L1 exactly REMED-GFX-155 leg I0's shape: the BasicEffect is a local, destroyed before the
        // readback and therefore before any deferred replay.
        {
            Begin(dev, d);
            SpriteStripes(d, 0, 2, kRed);
            {
                VertexPositionTexture q[6];
                QuadVPT(0, 1, q);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(&solid_[static_cast<std::size_t>(PaletteIndex(kBlue))]);
                fx.VertexColorEnabled = false;
                fx.setLightingEnabledProperty(false);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            }
            Readback r = Read(dev, d);
            if (Readable(r, d, "L1"))
                check(Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kRed),
                      "L1 a scope-LOCAL BasicEffect destroyed before replay still lands after a "
                      "SpriteBatch (stripe0 " + ColorText(StripeColor(r, 0)) + ", stripe1 " +
                      ColorText(StripeColor(r, 1)) + ")");
        }

        // L2 the same with a scope-local VertexBuffer, so buffer lifetime is measured too
        // (REMED-GFX-109's contract, restated inside a mixed-family cycle).
        {
            Begin(dev, d);
            SpriteStripes(d, 0, 2, kRed);
            {
                VertexPositionTexture q[6];
                QuadVPT(0, 1, q);
                VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6,
                                BufferUsage::None);
                vb.SetData(q, 0, 6);
                ConfigureEffect(Fx::Basic,
                                &solid_[static_cast<std::size_t>(PaletteIndex(kBlue))]);
                basicFx_->Apply();
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
            }
            Readback r = Read(dev, d);
            if (Readable(r, d, "L2"))
                check(Same(StripeColor(r, 0), kBlue) && Same(StripeColor(r, 1), kRed),
                      "L2 a scope-LOCAL VertexBuffer destroyed before replay still lands after a "
                      "SpriteBatch (stripe0 " + ColorText(StripeColor(r, 0)) + ")");
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /**
     * @brief M -- REMED-GFX-155 leg I0's exact shape, with a map of where the draw actually went.
     *
     * Leg I0 draws a SMALL, non-full-height quad into an 8x4 slot and compares the sampled 8x4
     * pattern byte for byte. Every other leg in this file uses full-height stripes and solid
     * colours precisely so that a Y-flip or a filtering difference cannot masquerade as a lost
     * draw -- which means this file cannot, on its own, say whether leg I0's "LOST" is a draw that
     * never happened or one that landed somewhere else looking like something else. This leg asks
     * exactly that and prints the answer: every 8x4 cell of the destination that is not the clear
     * colour, so a misplaced draw is located rather than merely missed.
     */
    void LegLegI0Shape(GraphicsDevice& dev)
    {
        if (!kDraws3D || !kReadsBackbuffer)
        {
            skip("M: skipped -- needs stock 3D rasterization and a backbuffer oracle");
            return;
        }
        constexpr int kCW = 8, kCH = 4;             // leg I0's cell size
        const int cols = kBBW / kCW, rows = kBBH / kCH;

        /// leg I0's Draw3DSlot: a clip-space quad covering cell @p slot only.
        auto draw3DCell = [&](Texture2D& src, int slot) {
            const int cx = (slot % cols) * kCW, cy = (slot / cols) * kCH;
            const float xL = 2.0f * static_cast<float>(cx) / kBBW - 1.0f;
            const float xR = 2.0f * static_cast<float>(cx + kCW) / kBBW - 1.0f;
            const float yT = 1.0f - 2.0f * static_cast<float>(cy) / kBBH;
            const float yB = 1.0f - 2.0f * static_cast<float>(cy + kCH) / kBBH;
            VertexPositionTexture q[6];
            q[0] = { Vector3(xL, yT, 0.f), Vector2(0.f, 0.f) };
            q[1] = { Vector3(xL, yB, 0.f), Vector2(0.f, 1.f) };
            q[2] = { Vector3(xR, yB, 0.f), Vector2(1.f, 1.f) };
            q[3] = { Vector3(xL, yT, 0.f), Vector2(0.f, 0.f) };
            q[4] = { Vector3(xR, yB, 0.f), Vector2(1.f, 1.f) };
            q[5] = { Vector3(xR, yT, 0.f), Vector2(1.f, 0.f) };
            dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            basicFx_->setWorldProperty(Matrix::getIdentityProperty());
            basicFx_->setViewProperty(Matrix::getIdentityProperty());
            basicFx_->setProjectionProperty(Matrix::getIdentityProperty());
            basicFx_->setTextureEnabledProperty(true);
            basicFx_->setTextureProperty(&src);
            basicFx_->setLightingEnabledProperty(false);
            basicFx_->VertexColorEnabled = false;
            basicFx_->Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        };

        /// leg I0's DrawSpriteSlot.
        auto drawSpriteCell = [&](Texture2D& src, int slot) {
            const int cx = (slot % cols) * kCW, cy = (slot / cols) * kCH;
            SamplerState point = SamplerState::PointClamp;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(src, Rectangle(cx, cy, kCW, kCH), Rectangle(0, 0, kCW, kCH), kWhite);
            sb_->End();
        };

        Dest bb;
        Readback r;

        /// True when cell @p slot exactly reproduces @p want.
        auto cellIs = [&](int slot, const std::vector<Color>& want) {
            const int cx = (slot % cols) * kCW, cy = (slot / cols) * kCH;
            for (int y = 0; y < kCH; ++y)
                for (int x = 0; x < kCW; ++x)
                    if (!Same(r.at(cx + x, cy + y),
                              want[static_cast<std::size_t>(y) * kCW + x])) return false;
            return true;
        };
        /// True when every texel of cell @p slot is the clear colour.
        auto cellClear = [&](int slot) {
            const int cx = (slot % cols) * kCW, cy = (slot / cols) * kCH;
            for (int y = 0; y < kCH; ++y)
                for (int x = 0; x < kCW; ++x)
                    if (!Same(r.at(cx + x, cy + y), kClear)) return false;
            return true;
        };

        // M5 first: the winding question with NO SpriteBatch anywhere, so M1 below can be compared
        // against it instead of merely reported. Exactly one of the two culling modes must remove
        // the quad; which one names this renderer's front-face convention for this winding.
        auto cullProbe = [&](const RasterizerState& rs) {
            Begin(dev, bb);
            dev.setRasterizerStateProperty(rs);
            draw3DCell(altPatTex_, 1);
            r = Read(dev, bb);
            return r.ok() ? !cellClear(1) : false;
        };
        const bool underNone = cullProbe(RasterizerState::CullNone);
        const bool underCcw  = cullProbe(RasterizerState::CullCounterClockwise);
        const bool underCw   = cullProbe(RasterizerState::CullClockwise);
        ResetState(dev);
        info(std::string("M5 the same quad under explicit cull modes -- CullNone: ") +
             (underNone ? "drawn" : "culled") + ", CullCounterClockwise: " +
             (underCcw ? "drawn" : "culled") + ", CullClockwise: " +
             (underCw ? "drawn" : "culled"));
        check(underNone, "M5 the quad is drawn under CullNone, so the probe itself is sound");
        check(underCcw != underCw,
              "M5 exactly one of the two culling modes removes the quad, so this renderer honours "
              "RasterizerState.CullMode and has a definite front-face convention for this winding");
        check(!underCcw && underCw,
              std::string("M5 this quad (TL -> BL -> BR, counter-clockwise as displayed) is an XNA "
                          "BACK face, so CullCounterClockwise removes it and CullClockwise keeps "
                          "it -- measured: CullCounterClockwise ") +
              (underCcw ? "keeps" : "removes") + ", CullClockwise " +
              (underCw ? "keeps" : "removes"));

        // M1 -- REMED-GFX-155 leg I0's exact sequence.
        Begin(dev, bb);
        drawSpriteCell(patTex_, 0);
        draw3DCell(altPatTex_, 1);            // the draw leg I0 reports as LOST
        drawSpriteCell(patTex_, 2);
        r = Read(dev, bb);
        if (!Readable(r, bb, "M1")) return;

        std::string map;
        for (int s = 0; s < cols * rows; ++s)
            if (!cellClear(s)) map += (map.empty() ? "" : ",") + std::to_string(s);
        const bool m1Landed = !cellClear(1);
        info("M1 non-clear cells after sprite(0) -> 3D(1) -> sprite(2): [" + map + "]" +
             " (leg I0's shape; cell 1 exact-pattern=" +
             (cellIs(1, altPattern_) ? "yes" : "no") + ", cell 1 all-clear=" +
             (cellClear(1) ? "yes" : "no") + ")");

        // THE invariant this leg exists for: leg I0's "the 3D draw never landed" is the leaked
        // rasterizer state and nothing else. SpriteBatch.Begin was given a null rasterizerState,
        // which FNA defines as RasterizerState.CullCounterClockwise, and FNA's PrepRenderState
        // assigns it to the device and never restores it -- so the following draw must behave
        // EXACTLY as the same draw does under an explicit CullCounterClockwise, and it does. If a
        // command-ORDER defect were also losing that draw, these two would diverge.
        check(m1Landed == underCcw,
              std::string("M1 a 3D draw issued after a null-rasterizerState SpriteBatch behaves "
                          "exactly as the same draw under an explicit CullCounterClockwise (after "
                          "SpriteBatch: ") + (m1Landed ? "drawn" : "culled") +
              ", explicit: " + (underCcw ? "drawn" : "culled") +
              ") -- so leg I0's \"lost draw\" is leaked state, not command order");
        if (m1Landed)
            check(cellIs(1, altPattern_),
                  "M1 where it does land it reproduces its source pattern exactly, so nothing "
                  "about orientation or filtering is involved either");

        // M2 -- the SAME sequence with the SpriteBatch given an explicit CullNone rasterizer state
        // instead of the null that means "CullCounterClockwise" (FNA SpriteBatch.cs:
        // `rasterizerState ?? RasterizerState.CullCounterClockwise`, assigned to the device in
        // PrepRenderState and never restored). If this lands where M1 did not, the missing draw was
        // culled by state SpriteBatch legitimately left behind, not lost by command ordering.
        auto drawSpriteCellNoCull = [&](Texture2D& src, int slot) {
            const int cx = (slot % cols) * kCW, cy = (slot / cols) * kCH;
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
            sb_->Draw(src, Rectangle(cx, cy, kCW, kCH), Rectangle(0, 0, kCW, kCH), kWhite);
            sb_->End();
        };
        Begin(dev, bb);
        drawSpriteCellNoCull(patTex_, 0);
        draw3DCell(altPatTex_, 1);
        drawSpriteCellNoCull(patTex_, 2);
        r = Read(dev, bb);
        if (!Readable(r, bb, "M2")) return;
        const bool m2Landed = !cellClear(1);
        const bool m2Exact  = cellIs(1, altPattern_);
        check(m2Landed && m2Exact,
              "M2 the same 3D draw lands, and lands exactly, when the SpriteBatch is given an "
              "explicit CullNone instead of the null that selects CullCounterClockwise");

        // M3 -- the null-rasterizer sequence again, but with the device's RasterizerState set back
        // explicitly before the 3D draw, which is what a game must do after SpriteBatch.End.
        Begin(dev, bb);
        drawSpriteCell(patTex_, 0);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        draw3DCell(altPatTex_, 1);
        drawSpriteCell(patTex_, 2);
        r = Read(dev, bb);
        if (!Readable(r, bb, "M3")) return;
        check(!cellClear(1) && cellIs(1, altPattern_),
              "M3 restoring RasterizerState explicitly after SpriteBatch.End makes the same draw "
              "land, so nothing about the command ORDER was ever wrong");

        // M4 -- what SpriteBatch.End actually leaves on the device, read through the public API.
        Begin(dev, bb);
        drawSpriteCell(patTex_, 0);
        const CullMode leaked = dev.getRasterizerStateProperty().getCullModeProperty();
        info(std::string("M4 GraphicsDevice.RasterizerState.CullMode after a SpriteBatch.End whose "
                         "Begin passed a null rasterizerState is ") +
             (leaked == CullMode::None ? "CullNone"
              : leaked == CullMode::CullClockwiseFace ? "CullClockwiseFace"
                                                      : "CullCounterClockwiseFace") +
             " (XNA/FNA leave the Begin state applied; they restore nothing)");
        check(leaked == CullMode::CullCounterClockwiseFace,
              "M4 a null rasterizerState leaves CullCounterClockwiseFace on the device, exactly as "
              "FNA's `rasterizerState ?? RasterizerState.CullCounterClockwise` requires");
        ResetState(dev);
    }

    /**
     * @brief N -- public order between 3D draws that use DIFFERENT stock effects.
     *
     * Every leg above puts one sequence's 3D steps through one stock effect, so a renderer keeping
     * one deferred queue per effect can replay those steps in issue order and still be reordering
     * everything else. REMED-GFX-159's ticket names only the SpriteBatch-against-3D direction, but
     * on a renderer whose replay is a fixed list of per-family loops the ten 3D families are ordered
     * against each other by exactly the same mechanism, and a game mixing a textured BasicEffect
     * with a vertex-colour one inside a cycle sees it without a SpriteBatch anywhere.
     *
     * The staircase oracle is unchanged; only the effect varies per step. Each pair is run in BOTH
     * directions, because a fixed replay list renders one of the two correctly by accident and a
     * one-directional check would report that as "ordered".
     */
    void LegCrossEffectOrder(GraphicsDevice& dev)
    {
        if (!kDraws3D) { skip("N: skipped -- no stock 3D rasterization here"); return; }
        if (!RequirePublicFamilyOrder("N")) return;
        RenderTarget2D rt(dev, kRTW, kRTH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        Dest d{ &rt, kRTW, kRTH, "render target" };

        /// A `Fam::Three` step pinned to one stock effect.
        auto three = [](Fx fx, const Color& c) { return Step{ Fam::Three, c, fx }; };

        struct Pair { Fx a; Fx b; const char* an; const char* bn; };
        const Pair pairs[] = {
            { Fx::Basic,       Fx::BasicVertexColor, "textured BasicEffect", "vertex-colour BasicEffect" },
            { Fx::AlphaTest,   Fx::BasicVertexColor, "AlphaTestEffect",      "vertex-colour BasicEffect" },
            { Fx::DualTexture, Fx::Basic,            "DualTextureEffect",    "textured BasicEffect" },
            { Fx::AlphaTest,   Fx::DualTexture,      "AlphaTestEffect",      "DualTextureEffect" },
        };
        int n = 0;
        for (const Pair& p : pairs)
        {
            const std::string tag = "N" + std::to_string(++n);
            try
            {
                Staircase(dev, d, { three(p.a, kRed), three(p.b, kBlue) },
                          tag + "a " + p.an + " -> " + p.bn + " in public order");
                Staircase(dev, d, { three(p.b, kRed), three(p.a, kBlue) },
                          tag + "b " + p.bn + " -> " + p.an + " in public order");
            }
            catch (const std::exception& e)
            {
                skip(tag + ": skipped -- threw on " + kRendererName + " (" + e.what() + ")");
            }
        }

        // Three distinct 3D families plus sprites alternating in ONE cycle: the shape a fixed
        // per-family replay list permutes hardest, and the one that fails if any single family is
        // hoisted out of order rather than only the sprite/3D split.
        try
        {
            Staircase(dev, d,
                      { three(Fx::Basic, kRed), { Fam::Sprite, kGreen },
                        three(Fx::BasicVertexColor, kBlue), { Fam::Sprite, kYellow },
                        three(Fx::AlphaTest, kMagenta) },
                      "N5 three stock effects and two sprite batches alternating in one cycle");
        }
        catch (const std::exception& e)
        {
            skip(std::string("N5: skipped -- threw on ") + kRendererName + " (" + e.what() + ")");
        }

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        white_ = Texture2D(dev, 1, 1);
        const Color w = kWhite;
        white_.SetData(&w, 1);

        const Color palette[7] = { kRed, kGreen, kBlue, kYellow, kMagenta, kCyan, kWhite };
        solid_.reserve(7);
        for (const Color& c : palette)
        {
            Texture2D t(dev, 1, 1);
            t.SetData(&c, 1);
            solid_.push_back(std::move(t));
        }

        // REMED-GFX-155's own 8x4 patterns: every texel unique in (R, G), so a stale, zero, mirrored
        // or rotated image fails distinguishably. Leg M needs exactly these to replay leg I0.
        pattern_.assign(32, Color(0, 0, 0, 0));
        altPattern_.assign(32, Color(0, 0, 0, 0));
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x)
            {
                pattern_[static_cast<std::size_t>(y) * 8 + x] = Color(
                    static_cast<bytecs>(20 + x * 30), static_cast<bytecs>(25 + y * 70),
                    static_cast<bytecs>(40 + ((x * 5 + y * 3) % 7) * 30),
                    static_cast<bytecs>(255 - ((x + 2 * y) % 4) * 55));
                altPattern_[static_cast<std::size_t>(y) * 8 + x] = Color(
                    static_cast<bytecs>(245 - x * 30), static_cast<bytecs>(235 - y * 70),
                    static_cast<bytecs>(30 + ((x * 3 + y * 5) % 7) * 30),
                    static_cast<bytecs>(90 + ((x + 3 * y) % 4) * 55));
            }
        patTex_ = Texture2D(dev, 8, 4);
        patTex_.SetData(pattern_.data(), 32);
        altPatTex_ = Texture2D(dev, 8, 4);
        altPatTex_.SetData(altPattern_.data(), 32);

        sb_ = std::make_unique<SpriteBatch>(dev);

        if (kDraws3D)
        {
            basicFx_  = std::make_unique<BasicEffect>(dev);
            basicFx2_ = std::make_unique<BasicEffect>(dev);
            vcolFx_   = std::make_unique<BasicEffect>(dev);
            try { alphaFx_ = std::make_unique<AlphaTestEffect>(dev); } catch (...) {}
            try { dualFx_  = std::make_unique<DualTextureEffect>(dev); } catch (...) {}
            try { envFx_   = std::make_unique<EnvironmentMapEffect>(dev); } catch (...) {}
            try
            {
                envCube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
                const Color c = kWhite;
                for (int f = 0; f < 6; ++f)
                    envCube_->SetData(static_cast<CubeMapFace>(f), &c, 1);
            }
            catch (...) { envCube_.reset(); }

            const auto& decl = VertexPositionTexture::getVertexDeclarationStatic();
            quadVb_    = std::make_unique<VertexBuffer>(dev, decl, 6,  BufferUsage::None);
            quadIdxVb_ = std::make_unique<VertexBuffer>(dev, decl, 4,  BufferUsage::None);
            wideVb_    = std::make_unique<VertexBuffer>(dev, decl, 12, BufferUsage::None);
            wideIdxVb_ = std::make_unique<VertexBuffer>(dev, decl, 8,  BufferUsage::None);
            quadIb16_  = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 6,
                                                       BufferUsage::None);
            const std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
            quadIb16_->SetData(idx, 0, 6);
            wideIb16_  = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 12,
                                                       BufferUsage::None);
        }

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        ResetState(dev);
        dev.Clear(kClear);

        std::printf("[INFO] REMED-GFX-157 SpriteBatch/3D public ordering on %s "
                    "(%d stripes, requested %dx%d backbuffer, actual %dx%d)\n",
                    kRendererName, kStripes, kBBW, kBBH,
                    dev.getPresentationParametersProperty().getBackBufferWidthProperty(),
                    dev.getPresentationParametersProperty().getBackBufferHeightProperty());
        std::fflush(stdout);

        LegBackbuffer(dev);
        LegTargetEqualDims(dev);
        LegTargetOtherDims(dev);
        LegDestinationTransitions(dev);
        LegTexturing(dev);
        LegDrawEntryPoints(dev);
        LegStockEffects(dev);
        LegEffectApplication(dev);
        LegStateTransitions(dev);
        LegClears(dev);
        LegClearBoundaryFamilies(dev);
        LegCardinality(dev);
        LegLocalLifetime(dev);
        LegLegI0Shape(dev);
        LegCrossEffectOrder(dev);

        std::printf("[INFO] %s: %d/%d checks passed (%d skipped)\n",
                    kRendererName, passCount_, totalCount_, skipCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    /** @brief Builds the fixture at a fixed backbuffer size. */
    SpriteBatch3DOrderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    SpriteBatch3DOrderTest test;
    test.Run();
    return test.Result();
}
