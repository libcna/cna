// SPDX-License-Identifier: MS-PL
// REMED-GFX-136: a RenderTargetCube's public RenderTargetUsage must reach the renderer that decides
// what happens to the face's existing colour when it is bound.
//
// `IGraphicsRenderer::CreateRenderTarget2D` has carried a `preserveContents` parameter since the
// renderer interface existed; `CreateRenderTargetCube(size, depthFormat, mipMap, multiSampleCount)`
// never did. A RenderTargetCube stores a real `usage_` and reports it through
// `RenderTargetUsage` -- and then throws it away at the one call that builds the GPU resource. Two
// renderers had to invent an answer and both invented the same one: `VulkanRenderTargetCubeRenderer`
// built every face framebuffer against `GetOrCreateRTRenderPass(depthFmt, /*discardContents=*/true)`
// and `WebGPURenderer::RenderPendingDrawsToRenderTargetCubeFace` set
// `colorAttachment.loadOp = WGPULoadOp_Clear` unconditionally, each with a source comment naming
// the missing parameter. So a `PreserveContents` cube face was wiped on every bind cycle.
//
// This file is the oracle for that. It reuses REMED-GFX-134's asymmetric face producer verbatim --
// drawn geometry, never Clear() (REMED-GFX-129 separately owns an explicit Clear() being dropped on
// a preserving target, and a uniform clear colour cannot detect a flip, a wrong face or a stale
// face anyway), a five-region pattern whose six colours rotate by face index, from a 0/255-only
// palette that is an exact fixed point of sRGB encoding so every comparison here is byte-exact even
// on WebGPU's sRGB cube target.
//
// The decisive shape is asymmetric on purpose: a face is painted completely, unbound, read (so the
// producer itself is proven before anything else is claimed), then rebound and given a marker over
// a SMALL corner only. Under PreserveContents the whole face must then read back as the original
// pattern with just that corner replaced. A renderer that discards passes the "the region I drew is
// correct" half and fails the other half, which is exactly the failure REMED-GFX-134 measured.
//
// What the public contract is taken to be here, and where each part comes from:
//   * PreserveContents -- the face's COLOUR survives an unbind/rebind cycle exactly. FNA's
//     GraphicsDevice.SetRenderTargets passes `usage != DiscardContents` to FNA3D as
//     "preserveTargetContents" and issues no clear.
//   * DiscardContents -- the previous colour is not preserved. CNA does not leave that undefined:
//     GraphicsDevice::SetRenderTargets already clears a DiscardContents target to (0,0,0,255) on
//     every bind, mirroring FNA's own `if (clearTarget == DiscardContents) Clear(...)`. So the
//     replacement colour IS defined by the shared layer and is asserted, per renderer.
//   * PlatformContents -- CNA maps it to preservation, i.e. FNA's own `usage != DiscardContents`
//     rule, and REMED-GFX-136 makes that ONE helper both public targets call. Before it, the two
//     halves of CNA disagreed: GraphicsDevice::SetRenderTargets only ever cleared a
//     DiscardContents target (so the shared layer already treated PlatformContents as
//     non-discarding), while RenderTarget2D passed `usage == PreserveContents` to the renderer (so
//     the renderer treated it as discarding). Whichever half won was a per-renderer accident.
//   * Depth/stencil -- a SEPARATE guarantee, not a missing one, and not this file's subject.
//     REMED-GFX-142 later established from FNA3D's own header ("Set this to 1 to store the
//     color/depth/stencil contents for future use") that RenderTargetUsage governs all three
//     aspects, and made every renderer honour that; `examples/rendertarget_depthstencil_usage_test`
//     is its oracle. What THIS file asserts about depth is only that a depth/stencil attachment
//     never corrupts the preserved COLOUR (checks T1/T2), which is exactly the independence the
//     two contracts need from each other. It said "not covered by the colour guarantee, Vulkan's
//     PreserveContents render pass uses LOAD_OP_DONT_CARE for depth and always has" while that was
//     the measured state; it is no longer, so the claim is corrected here rather than left to read
//     as a contract.
//   * First bind -- a brand-new target has no previous content, so nothing is asserted about the
//     texels a first PreserveContents bind did not draw. Only that the bind is legal and the
//     texels that WERE drawn are exact.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW  = 64;  ///< Backbuffer width.
    constexpr int kBBH  = 64;  ///< Backbuffer height.
    constexpr int kCube = 8;   ///< Cube face edge.
    constexpr int kCorner = 3; ///< Edge of each corner marker in the producer pattern.
    constexpr int kMark = 2;   ///< Edge of the small second-pass marker.
    constexpr int kMsaaRequest = 4;  ///< Multisample count requested by the MSAA check.

    /**
     * @brief What a rendered cube face's public readback must do on this renderer.
     *
     * `Exact` -- GetData returns the rendered face byte for byte.
     * `Unsupported` -- GetData raises System::NotSupportedException with the caller's destination
     * untouched. Reserved for a target this renderer genuinely cannot read back.
     */
    enum class Support
    {
        Exact,
        Unsupported,
    };

    /// The complete, reviewed per-renderer claim this file enforces.
    struct Contract
    {
        const char* name;
        bool    cubeTargetBinds;   ///< CreateRenderTargetCube() returns a real object AND it binds.
        Support readback;          ///< RenderTargetCube::GetData at mip 0.
        /// RenderTargetUsage::PreserveContents keeps a face's colour byte-exact across a full
        /// unbind/rebind cycle. This is REMED-GFX-136's subject: false is a declared defect, and
        /// after the fix no supported renderer declares false.
        bool    preserves;
        /// A DiscardContents rebind leaves the texels the new pass did not draw at the shared
        /// layer's own discard colour (0,0,0,255) -- see this file's header.
        bool    discardClearsToBlack;
        bool    msaaCubeTargets;   ///< A multisampled RenderTargetCube really engages MSAA here.
        Support msaaReadback;      ///< GetData on a MULTISAMPLED RenderTargetCube.
        /// PreserveContents on a genuinely multisampled cube target. Only consulted when
        /// `msaaCubeTargets` is true.
        bool    msaaPreserves;
        bool    mipMapCubeTargets; ///< A mipMap=true RenderTargetCube can be constructed and bound.
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef.
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless rasterizes nothing, so it has no rendered face to preserve OR to discard and its
    // readback stays the deterministic refusal REMED-GFX-130 established. The usage parameter is
    // still required to reach it (a source/compile contract), which is what the C1 property checks
    // and the creation checks cover here.
    constexpr Contract kContract{"HEADLESS", true, Support::Unsupported, false, false,
                                 true, Support::Unsupported, false, true, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr Contract kContract{"SOFTWARE", false, Support::Unsupported, false, false,
                                 false, Support::Unsupported, false, true, false};
#elif defined(CNA_RENDERER_EASYGL) && defined(CNA_GL_PROFILE_OPENGLES2)
    // The OPENGLES2 GL profile: identical to the EASYGL contract below except MSAA -- core
    // OpenGL ES 2.0 has no multisample renderbuffers/blit (docs/opengles2-renderer.md), so a
    // multisampled cube request degrades to single-sample (applied 0, `msaaCubeTargets` false,
    // same declaration shape as SKIA's truthful clamp) and `msaaPreserves` is never consulted.
    constexpr Contract kContract{"EASYGL(OPENGLES2)", true, Support::Exact, true, true,
                                 false, Support::Exact, false, true, false};
#elif defined(CNA_RENDERER_EASYGL)
    // `msaaPreserves` was false here, measured: this renderer allocated ONE multisample colour
    // renderbuffer shared by all six faces (the same allocation choice Vulkan and SdlGpu made), so
    // rebinding a multisampled face reloaded whichever face was rendered last, and check M3 proved
    // it by interleaving a second face. REMED-GFX-141 gave every face its own renderbuffer, so it
    // is now true and M2/M3 require exact content -- the boundary this file recorded is closed, and
    // examples/rendertargetcube_msaa_face_test.cpp is the fuller oracle for it.
    constexpr Contract kContract{"EASYGL", true, Support::Exact, true, true,
                                 true, Support::Exact, true, true, false};
#elif defined(CNA_RENDERER_BGFX)
    // REMED-GFX-138: GFX-154's ordered frame completion resolves the cube attachment before the
    // readback blit, so MSAA readback is exact. REMED-GFX-195 then made every cube face's
    // multisample colour storage independent, closing the A -> B -> A PreserveContents boundary.
    constexpr Contract kContract{"BGFX", true, Support::Exact, true, true,
                                 true, Support::Exact, true, true, false};
#elif defined(CNA_RENDERER_VULKAN)
    // `msaaCubeTargets` false: a cube target multisamples only when the BACKBUFFER was created
    // multisampled (Task 903's deliberate piggyback on the renderer's own sampleCount_), which this
    // test does not request.
    constexpr Contract kContract{"VULKAN", true, Support::Exact, true, true,
                                 false, Support::Exact, false, true, false};
#elif defined(CNA_RENDERER_WEBGPU)
    // `mipMapCubeTargets` true: WEBGPU-114 builds a real mip chain for a mipMap=true RenderTargetCube
    // and regenerates each face from its resolved level 0. `msaaCubeTargets` false: like EASYGL, a
    // WebGPU cube target multisamples only when the BACKBUFFER was created multisampled (the
    // per-instance multiSampleCount argument is not read, mirroring RenderTarget2D), so this file's
    // standalone MSAA request does not engage it -- the real per-face path is covered by
    // WebGPU_RenderTargetCube Check F and rendertargetcube_msaa_face_test.
    constexpr Contract kContract{"WEBGPU", true, Support::Exact, true, true,
                                 false, Support::Exact, false, true, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // `msaaPreserves` was false here: this renderer's multisampled cube target rendered into ONE
    // shared single-layer scratch texture that had to be cycled on every pass and was resolved away
    // immediately, so there was nothing per-face to load back. REMED-GFX-141 gave every face its own
    // texture, dropped the cycling and switched a preserving target to
    // SDL_GPU_STOREOP_RESOLVE_AND_STORE, so it is now true and M2/M3 require exact content.
    constexpr Contract kContract{"SDL_GPU", true, Support::Exact, true, true,
                                 true, Support::Exact, true, true, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr Contract kContract{"SDL_RENDERER", false, Support::Unsupported, false, false,
                                 false, Support::Unsupported, false, true, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", false, Support::Unsupported, false, false,
                                 false, Support::Unsupported, false, true, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", false, Support::Unsupported, false, false,
                                 false, Support::Unsupported, false, true, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    // `mipMapCubeTargets` true only in the sense that construction and level-0 rendering work:
    // D3D9RenderTargetCubeRenderer::Recreate() allocates ONE level whatever mipMap asked for
    // (REMED-GFX-139). This file only ever asserts level 0, so that boundary is untouched here.
    constexpr Contract kContract{"DIRECTX9", true, Support::Exact, true, true,
                                 false, Support::Exact, false, true, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", true, Support::Exact, true, true,
                                 true, Support::Exact, true, true, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr Contract kContract{"DIRECTX12", true, Support::Exact, true, true,
                                 true, Support::Exact, true, true, false};
#else
#error "REMED-GFX-136: this renderer has no declared RenderTargetCube usage contract."
#endif

    /// Destination pre-fills. Neither equals any pattern colour nor the discard colour.
    Color SentinelCD() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }
    Color SentinelA5() { return Color(0xA5, 0xA5, 0xA5, 0xA5); }

    /// The colour GraphicsDevice::SetRenderTargets clears a non-preserving target to on every bind.
    Color DiscardColour() { return Color(0, 0, 0, 255); }

    /// The six producer colours, all fully saturated so sRGB encoding is the identity.
    const std::array<Color, 6> kPalette = {
        Color(255,   0,   0, 255),  // red
        Color(  0, 255,   0, 255),  // green
        Color(  0,   0, 255, 255),  // blue
        Color(255, 255,   0, 255),  // yellow
        Color(255,   0, 255, 255),  // magenta
        Color(  0, 255, 255, 255),  // cyan
    };

    /**
     * @brief Colour of pattern slot @p slot on @p face, for producer round @p variant.
     *
     * Slot 0 is the base fill, 1..4 the corner markers, 5 the centre. Rotating the palette by the
     * face index makes every slot of every face differ from the same slot of every other face, so a
     * face swap or a stale face changes the colour at EVERY asserted texel. `variant` rotates by
     * two more, so re-rendering one face produces a pattern that is neither its own previous
     * content nor any other face's.
     */
    Color Slot(int face, int slot, int variant)
    {
        return kPalette[static_cast<std::size_t>((slot + face + 2 * variant) % 6)];
    }

    /// The exact texel the pattern producer puts at (@p x, @p y) of @p face in round @p variant.
    Color PatternTexel(int face, int x, int y, int variant)
    {
        const int lo = kCorner;
        const int hi = kCube - kCorner;
        if (x >= lo && x < hi && y >= lo && y < hi) return Slot(face, 5, variant);
        if (x < lo  && y < lo)  return Slot(face, 1, variant);
        if (x >= hi && y < lo)  return Slot(face, 2, variant);
        if (x < lo  && y >= hi) return Slot(face, 3, variant);
        if (x >= hi && y >= hi) return Slot(face, 4, variant);
        return Slot(face, 0, variant);
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /// One classified readback attempt: what the call did, in full, without judging it yet.
    struct Probe
    {
        bool threwNotSupported  = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> dest;
        std::size_t sentinelSurvivors = 0;
        std::size_t exact = 0;
        std::size_t window = 0;
        std::string firstMismatch;
    };
}

class RenderTargetCubeUsageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::array<std::unique_ptr<Texture2D>, 6> solids_;
    bool done_ = false;
    bool warmedUp_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void skip(const std::string& label) { check(true, label); }

    // ---------------------------------------------------------------------
    // Content producer -- drawn geometry only, never Clear()
    // ---------------------------------------------------------------------

    Texture2D& SolidFor(const Color& c)
    {
        for (std::size_t i = 0; i < kPalette.size(); ++i)
            if (kPalette[i].getRProperty() == c.getRProperty() &&
                kPalette[i].getGProperty() == c.getGProperty() &&
                kPalette[i].getBProperty() == c.getBProperty())
                return *solids_[i];
        return *solids_[0];
    }

    void DrawQuad(const Color& colour, int x, int y, int w, int h)
    {
        spriteBatch_->Draw(SolidFor(colour), Rectangle(x, y, w, h), Rectangle(0, 0, 1, 1),
                           Color::White);
    }

    void BeginQuads()
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
    }

    /// Paints the whole currently-bound target with the asymmetric five-region pattern.
    void DrawPattern(int face, int variant)
    {
        BeginQuads();
        DrawQuad(Slot(face, 0, variant), 0, 0, kCube, kCube);
        DrawQuad(Slot(face, 1, variant), 0, 0, kCorner, kCorner);
        DrawQuad(Slot(face, 2, variant), kCube - kCorner, 0, kCorner, kCorner);
        DrawQuad(Slot(face, 3, variant), 0, kCube - kCorner, kCorner, kCorner);
        DrawQuad(Slot(face, 4, variant), kCube - kCorner, kCube - kCorner, kCorner, kCorner);
        DrawQuad(Slot(face, 5, variant), kCorner, kCorner, kCube - 2 * kCorner, kCube - 2 * kCorner);
        spriteBatch_->End();
    }

    /// A complete bind/paint/unbind cycle for one face.
    void RenderFace(GraphicsDevice& dev, RenderTargetCube& cube, int face, int variant)
    {
        dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
        DrawPattern(face, variant);
        dev.SetRenderTargets({});
    }

    /// A bind/paint/unbind cycle that draws ONLY a kMark x kMark marker at (@p x, @p y).
    void MarkFace(GraphicsDevice& dev, RenderTargetCube& cube, int face, int x, int y,
                  const Color& colour)
    {
        dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
        BeginQuads();
        DrawQuad(colour, x, y, kMark, kMark);
        spriteBatch_->End();
        dev.SetRenderTargets({});
    }

    // ---------------------------------------------------------------------
    // Probes and expectations
    // ---------------------------------------------------------------------

    static Probe ProbeFace(const TextureCube& cube, int face, const Rectangle* rect, int w, int h,
                           const Color& sentinel, const std::vector<Color>& expected)
    {
        Probe p;
        p.window = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        p.dest.assign(p.window, sentinel);
        try
        {
            cube.GetData(static_cast<CubeMapFace>(face), 0, rect, p.dest.data(), 0,
                         static_cast<int>(p.window));
        }
        catch (const System::NotSupportedException&) { p.threwNotSupported = true; }
        catch (const std::exception& e) { p.threwSomethingElse = true; p.otherWhat = e.what(); }
        catch (...) { p.threwSomethingElse = true; p.otherWhat = "non-std exception"; }

        for (std::size_t i = 0; i < p.window; ++i)
        {
            const Color& got = p.dest[i];
            if (Same(got, sentinel)) ++p.sentinelSurvivors;
            if (i < expected.size() && Same(got, expected[i])) ++p.exact;
            else if (p.firstMismatch.empty() && i < expected.size())
                p.firstMismatch = " first miss at " + std::to_string(i) + " got=" + ColorText(got) +
                                  " expected=" + ColorText(expected[i]);
        }
        return p;
    }

    static Probe ProbeWholeFace(const TextureCube& cube, int face, const Color& sentinel,
                                const std::vector<Color>& expected)
    {
        return ProbeFace(cube, face, nullptr, kCube, kCube, sentinel, expected);
    }

    /**
     * @brief Closes the current GPU pass for @p face so the NEXT bind cycle is a genuinely separate
     *        one, and returns nothing.
     *
     * This exists because a preservation check that skips it is false-positive-capable on a
     * deferred renderer. Vulkan used to record exactly ONE render pass per unique render-target
     * source per flush -- `RecordCommandBuffer`'s Phase 1 collected `usedRTs` and replayed every
     * queued batch for each -- so a producer pass and a later partial pass issued into the same
     * flush window collapsed into a single pass whose one load action ran before BOTH, and the
     * producer's content survived even on a renderer that discards on every bind. **REMED-GFX-140
     * fixed that**: Phase 1 is now keyed on the bind cycle, so a rebind is a separate native pass
     * with its own load action. **SdlGpu had the identical defect by a different route** --
     * `EnsureFrameRendered` gave each unique `DrawTarget` (resource plus cube face) one
     * `SDL_BeginGPURenderPass` -- **and REMED-GFX-145 fixed it the same way.**
     *
     * The barrier stays, and is still required, because it is what makes this file's subject
     * observable at all rather than a workaround for the collapsing: a preservation check has to
     * prove that content written by an EARLIER pass is reloaded by a LATER one, and only a real
     * flush boundary guarantees the producer has been submitted before the partial pass is
     * recorded. `examples/rendertarget_pass_boundary_test.cpp` is the file that asserts the
     * boundary itself, with no barrier anywhere. The returned value is deliberately unused: this is
     * a barrier, not an assertion.
     */
    static void Settle(const TextureCube& cube, int face)
    {
        std::vector<Color> scratch(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
        try
        {
            cube.GetData(static_cast<CubeMapFace>(face), 0, nullptr, scratch.data(), 0,
                         static_cast<int>(scratch.size()));
        }
        catch (...) { /* a renderer with no readback has nothing to flush either */ }
    }

    /**
     * @brief Judges one probe against this renderer's declared readback contract.
     *
     * `Exact`: no exception and every asserted entry equals its expectation.
     * `Unsupported`: System::NotSupportedException and the whole destination still the sentinel.
     */
    void Judge(const Probe& p, Support required, const Color& sentinel, const std::string& label)
    {
        const std::string facts =
            " [threwNotSupported=" + std::string(p.threwNotSupported ? "1" : "0") +
            " threwOther=" + (p.threwSomethingElse ? ("1:" + p.otherWhat) : "0") +
            " exact=" + std::to_string(p.exact) + "/" + std::to_string(p.window) +
            " sentinelSurvivors=" + std::to_string(p.sentinelSurvivors) + p.firstMismatch + "]";
        (void) sentinel;

        if (required == Support::Exact)
            check(!p.threwNotSupported && !p.threwSomethingElse && p.exact == p.window,
                  label + " -- exact content required" + facts);
        else
            check(p.threwNotSupported && !p.threwSomethingElse && p.sentinelSurvivors == p.window,
                  label + " -- deterministic NotSupportedException with the destination untouched "
                          "required" + facts);
    }

    static std::vector<Color> ExpectedFace(int face, int variant)
    {
        std::vector<Color> e;
        e.reserve(static_cast<std::size_t>(kCube) * kCube);
        for (int y = 0; y < kCube; ++y)
            for (int x = 0; x < kCube; ++x) e.push_back(PatternTexel(face, x, y, variant));
        return e;
    }

    /// The producer pattern of @p face with a kMark x kMark @p colour marker stamped at (@p x,@p y).
    static std::vector<Color> ExpectedFaceWithMark(int face, int variant, int x, int y,
                                                   const Color& colour)
    {
        std::vector<Color> e = ExpectedFace(face, variant);
        for (int dy = 0; dy < kMark; ++dy)
            for (int dx = 0; dx < kMark; ++dx)
                e[static_cast<std::size_t>(y + dy) * kCube + (x + dx)] = colour;
        return e;
    }

    /// The whole face reading as the shared layer's discard colour except for one marker.
    static std::vector<Color> ExpectedDiscardedWithMark(int x, int y, const Color& colour)
    {
        std::vector<Color> e(static_cast<std::size_t>(kCube) * kCube, DiscardColour());
        for (int dy = 0; dy < kMark; ++dy)
            for (int dx = 0; dx < kMark; ++dx)
                e[static_cast<std::size_t>(y + dy) * kCube + (x + dx)] = colour;
        return e;
    }

    static std::vector<Color> Uniform(const Color& c, int count)
    {
        return std::vector<Color>(static_cast<std::size_t>(count), c);
    }

    /// The marker colour used for a second pass over @p face -- never one of that face's own
    /// pattern colours, so "the marker landed" and "nothing happened" are always distinguishable.
    static Color MarkColour(int face) { return kPalette[static_cast<std::size_t>((face + 3) % 6)]; }

    // =====================================================================
    // Sections
    // =====================================================================

    /// C1..C3 -- the public property itself, on every renderer including those with no cube target.
    void RunPublicPropertyChecks(GraphicsDevice& dev)
    {
        static const std::array<RenderTargetUsage, 3> kUsages = {
            RenderTargetUsage::DiscardContents,
            RenderTargetUsage::PreserveContents,
            RenderTargetUsage::PlatformContents,
        };
        static const std::array<const char*, 3> kNames = {
            "DiscardContents", "PreserveContents", "PlatformContents",
        };

        for (std::size_t i = 0; i < kUsages.size(); ++i)
        {
            bool ok = false;
            std::string what;
            try
            {
                RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                      kUsages[i]);
                ok = cube.getRenderTargetUsageProperty() == kUsages[i];
            }
            catch (const std::exception& e) { what = e.what(); }
            catch (...) { what = "non-std exception"; }
            check(ok, std::string("C") + std::to_string(i + 1) +
                      " property: a RenderTargetCube constructed with RenderTargetUsage::" +
                      kNames[i] + " reports exactly that value back [" +
                      (what.empty() ? std::string("ok=") + (ok ? "1" : "0") : "threw:" + what) + "]");
        }
    }

    /// B1 -- nothing below means anything if a cube face cannot be bound at all.
    bool RunBindingBoundary(GraphicsDevice& dev)
    {
        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        bool bound = false;
        std::string what;
        try
        {
            dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
            bound = true;
            dev.SetRenderTargets({});
        }
        catch (const std::exception& e) { what = e.what(); }
        catch (...) { what = "non-std exception"; }

        if (kContract.cubeTargetBinds)
            check(bound, "B1 binding: this renderer declares a real RenderTargetCube, so binding a "
                         "face must succeed [" + (bound ? std::string("bound") : "threw:" + what) + "]");
        else
            check(!bound, "B1 binding: this renderer creates no cube render target, so binding a "
                          "face must be refused deterministically [" +
                          (bound ? std::string("bound anyway") : "threw:" + what) + "]");
        return bound;
    }

    /**
     * @brief P1/P2 -- the finding itself.
     *
     * P1 proves the producer before anything is claimed about preservation; P2 is the decisive
     * asymmetric check. A renderer that discards on bind passes neither the untouched region nor the
     * whole-face comparison, while still drawing the marker correctly -- which is why the marker
     * region alone is never the oracle.
     */
    void RunPreserveCore(GraphicsDevice& dev)
    {
        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderFace(dev, cube, 1, 0);
        Probe first = ProbeWholeFace(cube, 1, SentinelCD(), ExpectedFace(1, 0));
        Judge(first, kContract.readback, SentinelCD(),
              "P1 preserve: the producer pass itself reads back exactly after unbind");

        MarkFace(dev, cube, 1, 0, 0, MarkColour(1));

        if (kContract.readback == Support::Unsupported)
        {
            Probe p = ProbeWholeFace(cube, 1, SentinelCD(), ExpectedFace(1, 0));
            Judge(p, kContract.readback, SentinelCD(),
                  "P2 preserve: readback stays refused on this renderer");
            skip("P3 preserve: untouched-region count skipped -- no readback on this renderer");
            return;
        }

        const std::vector<Color> expected = ExpectedFaceWithMark(1, 0, 0, 0, MarkColour(1));
        Probe after = ProbeWholeFace(cube, 1, SentinelCD(), expected);

        // Report the two halves separately: "the marker landed" is what a discarding renderer also
        // achieves, so it must never be mistaken for preservation.
        std::size_t markerExact = 0;
        std::size_t untouchedExact = 0;
        std::size_t untouchedTotal = 0;
        for (int y = 0; y < kCube; ++y)
            for (int x = 0; x < kCube; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * kCube + x;
                const bool inMark = x < kMark && y < kMark;
                if (inMark)
                {
                    if (Same(after.dest[i], expected[i])) ++markerExact;
                }
                else
                {
                    ++untouchedTotal;
                    if (Same(after.dest[i], expected[i])) ++untouchedExact;
                }
            }

        check(markerExact == static_cast<std::size_t>(kMark) * kMark,
              "P2 preserve: the second pass's marker is exact [" + std::to_string(markerExact) +
              "/" + std::to_string(kMark * kMark) + "]");

        if (kContract.preserves)
            check(untouchedExact == untouchedTotal,
                  "P3 preserve: PreserveContents keeps every texel the second pass did NOT draw "
                  "[untouched exact=" + std::to_string(untouchedExact) + "/" +
                  std::to_string(untouchedTotal) + after.firstMismatch + "]");
        else
            check(untouchedExact == 0,
                  "P3 preserve: this renderer DISCARDS a cube face on every bind cycle regardless "
                  "of PreserveContents -- recorded, not asserted away [untouched exact=" +
                  std::to_string(untouchedExact) + "/" + std::to_string(untouchedTotal) + "]");
    }

    /// P4/P5 -- face A -> face B -> face A. Preservation must be per-subresource.
    void RunFaceSequence(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("P4 sequence: skipped -- this renderer does not declare cube preservation");
            skip("P5 sequence: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderFace(dev, cube, 2, 0);   // A
        RenderFace(dev, cube, 5, 0);   // B
        Settle(cube, 2);
        Settle(cube, 5);
        MarkFace(dev, cube, 2, kCube - kMark, kCube - kMark, MarkColour(2));  // back to A

        Probe a = ProbeWholeFace(cube, 2, SentinelCD(),
                                 ExpectedFaceWithMark(2, 0, kCube - kMark, kCube - kMark,
                                                      MarkColour(2)));
        Judge(a, kContract.readback, SentinelCD(),
              "P4 sequence: face A -> face B -> face A keeps face A's own earlier content");

        Probe b = ProbeWholeFace(cube, 5, SentinelA5(), ExpectedFace(5, 0));
        Judge(b, kContract.readback, SentinelA5(),
              "P5 sequence: face B is untouched by face A being rebound and partially redrawn");
    }

    /// P6/P7 -- the preservation boundary is SetRenderTarget(nullptr), so a trip through the
    /// backbuffer and through an unrelated RenderTarget2D must both be transparent to the cube.
    void RunTargetRoundTrips(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("P6 roundtrip: skipped -- this renderer does not declare cube preservation");
            skip("P7 roundtrip: skipped -- this renderer does not declare cube preservation");
            return;
        }

        // cube -> backbuffer -> cube
        {
            RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::PreserveContents);
            RenderFace(dev, cube, 3, 0);
            Settle(cube, 3);
            BeginQuads();
            DrawQuad(kPalette[1], 0, 0, kBBW, kBBH);   // real backbuffer work in between
            spriteBatch_->End();
            MarkFace(dev, cube, 3, 0, kCube - kMark, MarkColour(3));
            Probe p = ProbeWholeFace(cube, 3, SentinelCD(),
                                     ExpectedFaceWithMark(3, 0, 0, kCube - kMark, MarkColour(3)));
            Judge(p, kContract.readback, SentinelCD(),
                  "P6 roundtrip: cube -> backbuffer -> cube preserves the face");
        }

        // cube -> RenderTarget2D -> cube
        {
            RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::PreserveContents);
            RenderTarget2D flat(dev, kCube, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            RenderFace(dev, cube, 4, 0);
            Settle(cube, 4);
            dev.SetRenderTarget(&flat);
            DrawPattern(0, 1);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            MarkFace(dev, cube, 4, kCube - kMark, 0, MarkColour(4));
            Probe p = ProbeWholeFace(cube, 4, SentinelCD(),
                                     ExpectedFaceWithMark(4, 0, kCube - kMark, 0, MarkColour(4)));
            Judge(p, kContract.readback, SentinelCD(),
                  "P7 roundtrip: cube -> RenderTarget2D -> cube preserves the face");
        }
    }

    /**
     * @brief P8/P9 -- two independent cubes with DIFFERENT usage, interleaved A -> B -> A.
     *
     * This is the deferred-capture check: a renderer that resolves the load action from whichever
     * RenderTargetUsage is live at flush time, rather than capturing it for the pass being created,
     * applies cube B's DiscardContents policy to cube A.
     */
    void RunTwoCubesDifferentUsage(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("P8 twocubes: skipped -- no cube readback on this renderer");
            skip("P9 twocubes: skipped -- no cube readback on this renderer");
            return;
        }

        RenderTargetCube keep(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderTargetCube drop(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);

        RenderFace(dev, keep, 0, 0);
        RenderFace(dev, drop, 0, 1);
        Settle(keep, 0);
        Settle(drop, 0);
        MarkFace(dev, keep, 0, 0, 0, MarkColour(0));
        MarkFace(dev, drop, 0, 0, 0, MarkColour(0));

        if (kContract.preserves)
        {
            Probe a = ProbeWholeFace(keep, 0, SentinelCD(),
                                     ExpectedFaceWithMark(0, 0, 0, 0, MarkColour(0)));
            Judge(a, kContract.readback, SentinelCD(),
                  "P8 twocubes: the PreserveContents cube is unaffected by a DiscardContents cube "
                  "being bound between its two passes");
        }
        else
        {
            skip("P8 twocubes: skipped -- this renderer does not declare cube preservation");
        }

        if (kContract.discardClearsToBlack)
        {
            Probe b = ProbeWholeFace(drop, 0, SentinelA5(),
                                     ExpectedDiscardedWithMark(0, 0, MarkColour(0)));
            Judge(b, kContract.readback, SentinelA5(),
                  "P9 twocubes: the DiscardContents cube really dropped its earlier pattern, and "
                  "the dropped texels are the shared layer's own discard colour");
        }
        else
        {
            skip("P9 twocubes: skipped -- discard replacement colour not declared here");
        }
    }

    /// P10 -- repeated binds with NO drawing at all must not consume the content either.
    void RunIdleBinds(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("P10 idle: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderFace(dev, cube, 1, 1);
        Settle(cube, 1);
        for (int i = 0; i < 3; ++i)
        {
            dev.SetRenderTarget(&cube, CubeMapFace::NegativeX);
            dev.SetRenderTargets({});
        }
        Probe p = ProbeWholeFace(cube, 1, SentinelCD(), ExpectedFace(1, 1));
        Judge(p, kContract.readback, SentinelCD(),
              "P10 idle: three bind/unbind cycles that draw nothing leave the face untouched");
    }

    /// P11 -- several partial updates accumulate; P12 -- a readback between binds is not a writer.
    void RunAccumulationAndReadbackBoundary(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("P11 accumulate: skipped -- this renderer does not declare cube preservation");
            skip("P12 accumulate: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderFace(dev, cube, 0, 0);

        struct Mark { int x; int y; Color colour; };
        const std::array<Mark, 3> marks = {
            Mark{0, 0, kPalette[3]},
            Mark{kCube - kMark, 0, kPalette[4]},
            Mark{0, kCube - kMark, kPalette[5]},
        };
        std::vector<Color> expected = ExpectedFace(0, 0);
        for (const Mark& m : marks)
        {
            MarkFace(dev, cube, 0, m.x, m.y, m.colour);
            // A readback between the bind cycles: it must observe, never consume.
            Probe mid = ProbeFace(cube, 0, nullptr, kCube, kCube, SentinelA5(), expected);
            (void) mid;
            for (int dy = 0; dy < kMark; ++dy)
                for (int dx = 0; dx < kMark; ++dx)
                    expected[static_cast<std::size_t>(m.y + dy) * kCube + (m.x + dx)] = m.colour;
        }

        Probe p = ProbeWholeFace(cube, 0, SentinelCD(), expected);
        Judge(p, kContract.readback, SentinelCD(),
              "P11 accumulate: three separate partial bind cycles all survive, over the original "
              "pattern");

        // P12: read twice in a row with nothing in between -- the second read must be identical.
        Probe again = ProbeWholeFace(cube, 0, SentinelA5(), expected);
        Judge(again, kContract.readback, SentinelA5(),
              "P12 accumulate: reading the face does not consume or wipe it");
    }

    /// F1 -- first bind after construction. No previous content exists, so only what was drawn is
    /// asserted; the rest is measured and reported, never claimed.
    void RunFirstBind(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("F1 firstbind: skipped -- no cube readback on this renderer");
            return;
        }

        RenderTargetCube fresh(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::PreserveContents);
        MarkFace(dev, fresh, 2, kCorner, kCorner, kPalette[0]);

        const Rectangle marker(kCorner, kCorner, kMark, kMark);
        Probe p = ProbeFace(fresh, 2, &marker, kMark, kMark, SentinelCD(),
                            Uniform(kPalette[0], kMark * kMark));
        Judge(p, kContract.readback, SentinelCD(),
              "F1 firstbind: a PreserveContents cube's very first bind is legal and the texels it "
              "DID draw are exact -- nothing is asserted about texels never written");
    }

    /**
     * @brief Q1 -- the SAME-flush-window shape, which every other preservation check deliberately
     *        avoids.
     *
     * Producer and partial pass are issued back to back with nothing between them. On a
     * PreserveContents target the two possible shapes are indistinguishable -- one LOAD pass
     * holding both cycles' draws produces exactly the pixels two LOAD passes would -- so what this
     * asserts is only that the composition is right either way: the producer's pattern with the
     * marker over it. It passes on a discarding renderer too, which is exactly why it is not the
     * oracle for P3, and it is NOT a licence to collapse: REMED-GFX-140 (Vulkan) and
     * REMED-GFX-145 (SdlGpu) established that every
     * bind cycle is its own logical pass and
     * `examples/rendertarget_pass_boundary_test.cpp` enforces that on a DiscardContents target,
     * where the two shapes differ.
     */
    void RunSameWindow(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("Q1 window: skipped -- no cube readback on this renderer");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderFace(dev, cube, 4, 1);
        MarkFace(dev, cube, 4, kCorner, kCorner, MarkColour(4));
        Probe p = ProbeWholeFace(cube, 4, SentinelCD(),
                                 ExpectedFaceWithMark(4, 1, kCorner, kCorner, MarkColour(4)));
        Judge(p, kContract.readback, SentinelCD(),
              "Q1 window: a producer pass and a partial pass issued with no flush between them "
              "both land");
    }

    /// I1 -- face isolation under preservation: touching face A never reaches face B.
    void RunFaceIsolation(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("I1 isolation: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        for (int face = 0; face < 6; ++face) RenderFace(dev, cube, face, 0);
        for (int face = 0; face < 6; ++face) Settle(cube, face);
        MarkFace(dev, cube, 0, 0, 0, MarkColour(0));

        std::size_t intactFaces = 0;
        std::string detail;
        for (int face = 1; face < 6; ++face)
        {
            Probe p = ProbeWholeFace(cube, face, SentinelCD(), ExpectedFace(face, 0));
            if (!p.threwNotSupported && !p.threwSomethingElse && p.exact == p.window) ++intactFaces;
            else if (detail.empty())
                detail = " face " + std::to_string(face) + " exact=" + std::to_string(p.exact) +
                         "/" + std::to_string(p.window) + p.firstMismatch;
        }
        check(intactFaces == 5,
              "I1 isolation: rebinding and partially redrawing face 0 leaves all five other faces "
              "byte-identical [" + std::to_string(intactFaces) + "/5" + detail + "]");
    }

    /// D1/D2 -- DiscardContents.
    void RunDiscardUsage(GraphicsDevice& dev)
    {
        const RenderTargetUsage usage = RenderTargetUsage::DiscardContents;
        const char* usageName = "discard";
        const char* idA = "D1";
        const char* idB = "D2";
        if (kContract.readback != Support::Exact)
        {
            skip(std::string(idA) + " " + usageName + ": skipped -- no cube readback on this renderer");
            skip(std::string(idB) + " " + usageName + ": skipped -- no cube readback on this renderer");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0, usage);
        RenderFace(dev, cube, 3, 0);
        // This readback is both an assertion and the flush barrier Settle() documents: the marker
        // pass below must be a genuinely separate GPU pass for the discard to be observable.
        Probe complete = ProbeWholeFace(cube, 3, SentinelCD(), ExpectedFace(3, 0));
        Judge(complete, kContract.readback, SentinelCD(),
              std::string(idA) + " " + usageName +
              ": a COMPLETE render into a non-preserving target is still exact");

        MarkFace(dev, cube, 3, kCorner, 0, MarkColour(3));
        Probe partial = ProbeWholeFace(cube, 3, SentinelA5(),
                                       ExpectedDiscardedWithMark(kCorner, 0, MarkColour(3)));

        std::size_t stale = 0;
        const std::vector<Color> old = ExpectedFace(3, 0);
        for (int y = 0; y < kCube; ++y)
            for (int x = 0; x < kCube; ++x)
            {
                if (x >= kCorner && x < kCorner + kMark && y < kMark) continue;
                const std::size_t i = static_cast<std::size_t>(y) * kCube + x;
                if (Same(partial.dest[i], old[i])) ++stale;
            }

        if (kContract.discardClearsToBlack)
        {
            Judge(partial, kContract.readback, SentinelA5(),
                  std::string(idB) + " " + usageName +
                  ": the previous pass's colour is gone and the untouched texels hold the shared "
                  "layer's discard colour " + ColorText(DiscardColour()));
        }
        else
        {
            check(stale == 0,
                  std::string(idB) + " " + usageName +
                  ": the previous pass's colour is honestly NOT preserved [stale texels=" +
                  std::to_string(stale) + "]");
        }
    }

    /**
     * @brief L1/L2 -- PlatformContents.
     *
     * CNA has exactly one mapping and it is FNA's own: `usage != DiscardContents` preserves. That
     * is not a new invention here -- `GraphicsDevice::SetRenderTargets` has always cleared ONLY a
     * DiscardContents target, so the shared layer already treated PlatformContents as
     * non-discarding; REMED-GFX-136 makes the RENDERER flag agree with it instead of contradicting
     * it. So PlatformContents is asserted exactly as strictly as PreserveContents.
     */
    void RunPlatformContents(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("L1 platform: skipped -- this renderer does not declare cube preservation");
            skip("L2 platform: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PlatformContents);
        RenderFace(dev, cube, 5, 1);
        Probe complete = ProbeWholeFace(cube, 5, SentinelCD(), ExpectedFace(5, 1));
        Judge(complete, kContract.readback, SentinelCD(),
              "L1 platform: a COMPLETE render into a PlatformContents target is exact");

        MarkFace(dev, cube, 5, kCorner, 0, MarkColour(5));
        Probe partial = ProbeWholeFace(cube, 5, SentinelA5(),
                                       ExpectedFaceWithMark(5, 1, kCorner, 0, MarkColour(5)));
        Judge(partial, kContract.readback, SentinelA5(),
              "L2 platform: CNA maps PlatformContents to preservation (FNA's own "
              "`usage != DiscardContents` rule), so the untouched region survives the rebind");
    }

    /// N1 -- many Preserve/Discard bind cycles in a row must stay correct and bounded.
    void RunRepeatedCycles(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("N1 cycles: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube keep(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderTargetCube drop(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        RenderFace(dev, keep, 5, 0);
        Settle(keep, 5);

        for (int i = 0; i < 8; ++i)
        {
            MarkFace(dev, drop, 4, 0, 0, kPalette[static_cast<std::size_t>(i % 6)]);
            dev.SetRenderTarget(&keep, CubeMapFace::NegativeZ);
            dev.SetRenderTargets({});
        }
        MarkFace(dev, keep, 5, kCorner, kCorner, MarkColour(5));

        Probe p = ProbeWholeFace(keep, 5, SentinelCD(),
                                 ExpectedFaceWithMark(5, 0, kCorner, kCorner, MarkColour(5)));
        Judge(p, kContract.readback, SentinelCD(),
              "N1 cycles: eight interleaved preserve/discard bind cycles leave the preserving "
              "face exact");
    }

    /// T1/T2 -- a depth/stencil attachment must not disturb the preserved COLOUR.
    void RunDepthInteraction(GraphicsDevice& dev)
    {
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("T1 depth: skipped -- this renderer does not declare cube preservation");
            skip("T2 depth: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube depth24(dev, kCube, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                                 RenderTargetUsage::PreserveContents);
        RenderFace(dev, depth24, 2, 0);
        Settle(depth24, 2);
        MarkFace(dev, depth24, 2, 0, 0, MarkColour(2));
        Probe a = ProbeWholeFace(depth24, 2, SentinelCD(),
                                 ExpectedFaceWithMark(2, 0, 0, 0, MarkColour(2)));
        Judge(a, kContract.readback, SentinelCD(),
              "T1 depth: a Depth24 cube target preserves its COLOUR across a rebind");

        RenderTargetCube depth24s8(dev, kCube, false, SurfaceFormat::Color,
                                   DepthFormat::Depth24Stencil8, 0,
                                   RenderTargetUsage::PreserveContents);
        RenderFace(dev, depth24s8, 4, 0);
        Settle(depth24s8, 4);
        MarkFace(dev, depth24s8, 4, 0, 0, MarkColour(4));
        Probe b = ProbeWholeFace(depth24s8, 4, SentinelA5(),
                                 ExpectedFaceWithMark(4, 0, 0, 0, MarkColour(4)));
        Judge(b, kContract.readback, SentinelA5(),
              "T2 depth: a Depth24Stencil8 cube target preserves its COLOUR across a rebind");
    }

    /// M1/M2 -- the multisample boundary, measured against this renderer's declared capability.
    void RunMsaa(GraphicsDevice& dev)
    {
        RenderTargetCube msaa(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None,
                              kMsaaRequest, RenderTargetUsage::PreserveContents);
        const int applied = msaa.getMultiSampleCountProperty();
        check(kContract.msaaCubeTargets == (applied > 0),
              "M1 msaa: the applied cube-target sample count matches this renderer's declared "
              "capability (requested " + std::to_string(kMsaaRequest) + ", applied " +
              std::to_string(applied) + ")");

        if (kContract.readback != Support::Exact)
        {
            skip("M2 msaa: skipped -- no cube readback on this renderer");
            skip("M3 msaa: skipped -- no cube readback on this renderer");
            return;
        }
        const Support required = applied > 0 ? kContract.msaaReadback : kContract.readback;
        if (required != Support::Exact)
        {
            skip("M2 msaa: skipped -- this renderer cannot read back a multisampled cube face");
            skip("M3 msaa: skipped -- this renderer cannot read back a multisampled cube face");
            return;
        }

        RenderFace(dev, msaa, 1, 0);
        Settle(msaa, 1);
        MarkFace(dev, msaa, 1, 0, 0, MarkColour(1));
        // Every quad is axis-aligned and full-texel, so every sample of every texel carries one
        // colour and the RESOLVED face is byte-identical to a single-sample one.
        Probe p = ProbeWholeFace(msaa, 1, SentinelCD(),
                                 ExpectedFaceWithMark(1, 0, 0, 0, MarkColour(1)));
        const bool preserveExpected = applied > 0 ? kContract.msaaPreserves : kContract.preserves;
        if (preserveExpected)
        {
            Judge(p, Support::Exact, SentinelCD(),
                  "M2 msaa: an applied-" + std::to_string(applied) +
                  "x PreserveContents cube target keeps its RESOLVED face across a rebind");
        }
        else
        {
            std::size_t stale = 0;
            const std::vector<Color> old = ExpectedFace(1, 0);
            for (std::size_t i = kMark; i < old.size(); ++i)
                if (Same(p.dest[i], old[i])) ++stale;
            check(!p.threwSomethingElse,
                  "M2 msaa: an applied-" + std::to_string(applied) +
                  "x cube target does not preserve across a rebind on this renderer -- recorded as "
                  "a declared boundary [exact=" + std::to_string(p.exact) + "/" +
                  std::to_string(p.window) + " staleTexels=" + std::to_string(stale) + "]");
        }

        // M3: whether the preserved content came from the RESOLVED face or from a multisample
        // scratch surface shared between faces. A renderer whose MSAA attachment is one shared
        // resource -- which is how EasyGL, Vulkan and SdlGpu all allocated it until REMED-GFX-141 --
        // passes M2 (one face, nothing else in between) while actually reloading the LAST face's
        // samples. Face A -> face B -> face A separates the two, and this is the check that first
        // measured the defect.
        RenderTargetCube seq(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None,
                             kMsaaRequest, RenderTargetUsage::PreserveContents);
        RenderFace(dev, seq, 0, 0);
        RenderFace(dev, seq, 3, 0);
        Settle(seq, 0);
        Settle(seq, 3);
        MarkFace(dev, seq, 0, kCube - kMark, kCube - kMark, MarkColour(0));
        Probe q = ProbeWholeFace(seq, 0, SentinelA5(),
                                 ExpectedFaceWithMark(0, 0, kCube - kMark, kCube - kMark,
                                                      MarkColour(0)));
        if (preserveExpected)
        {
            Judge(q, Support::Exact, SentinelA5(),
                  "M3 msaa: a multisampled face A -> face B -> face A sequence preserves face A's "
                  "OWN resolved colour, not face B's unresolved samples");
        }
        else
        {
            // Count how many texels came back holding the OTHER face's pattern. That is the
            // signature of a multisample attachment shared between faces being reloaded, and it is
            // what makes multisampled preservation a declared boundary rather than a guarantee.
            std::size_t otherFace = 0;
            const std::vector<Color> b = ExpectedFace(3, 0);
            for (std::size_t i = 0; i < q.dest.size() && i < b.size(); ++i)
                if (Same(q.dest[i], b[i])) ++otherFace;
            check(!q.threwSomethingElse,
                  "M3 msaa: a multisampled face A -> face B -> face A sequence does NOT preserve "
                  "face A on this renderer -- recorded as a declared boundary [faceA exact=" +
                  std::to_string(q.exact) + "/" + std::to_string(q.window) +
                  " texels holding face B=" + std::to_string(otherFace) + "]");
        }
    }

    /// X1 -- a mipMap=true cube target's level 0 is preserved like any other.
    void RunMipLevelZero(GraphicsDevice& dev)
    {
        if (!kContract.mipMapCubeTargets)
        {
            std::string what;
            try
            {
                RenderTargetCube mipped(dev, kCube, true, SurfaceFormat::Color, DepthFormat::None,
                                        0, RenderTargetUsage::PreserveContents);
                dev.SetRenderTarget(&mipped, CubeMapFace::PositiveZ);
                dev.SetRenderTargets({});
            }
            catch (const std::exception& e) { what = e.what(); }
            catch (...) { what = "non-std exception"; }
            check(!what.empty(),
                  "X1 mip: this renderer refuses a mipMap=true RenderTargetCube deterministically, "
                  "whatever its usage [" +
                  (what.empty() ? std::string("accepted it silently") : what) + "]");
            return;
        }
        if (!kContract.preserves || kContract.readback != Support::Exact)
        {
            skip("X1 mip: skipped -- this renderer does not declare cube preservation");
            return;
        }

        RenderTargetCube mipped(dev, kCube, true, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::PreserveContents);
        RenderFace(dev, mipped, 0, 0);
        Settle(mipped, 0);
        MarkFace(dev, mipped, 0, kCorner, kCorner, MarkColour(0));
        Probe p = ProbeWholeFace(mipped, 0, SentinelCD(),
                                 ExpectedFaceWithMark(0, 0, kCorner, kCorner, MarkColour(0)));
        Judge(p, kContract.readback, SentinelCD(),
              "X1 mip: level 0 of a mipMap=true PreserveContents cube target survives a rebind, "
              "mip regeneration included");
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (kContract.wantHiDefProfile)
            gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        for (std::size_t i = 0; i < kPalette.size(); ++i)
        {
            const Color& c = kPalette[i];
            solids_[i] = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
                device, 1, 1,
                std::vector<std::uint8_t>{
                    static_cast<std::uint8_t>(c.getRProperty()),
                    static_cast<std::uint8_t>(c.getGProperty()),
                    static_cast<std::uint8_t>(c.getBProperty()),
                    255,
                }));
        }
    }

    void Draw(const GameTime&) override
    {
        if (done_) { Exit(); return; }
        // Frame 0 is a deliberate warm-up: bgfx copies every createTexture payload at
        // bgfx::frame(), so without it the 1x1 producer sources would still be unuploaded during
        // the very first Draw and the whole battery would measure black. See REMED-GFX-134's own
        // identical warm-up.
        if (!warmedUp_)
        {
            warmedUp_ = true;
            BeginQuads();
            DrawQuad(kPalette[0], 0, 0, kBBW, kBBH);
            spriteBatch_->End();
            return;
        }
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        std::printf("REMED-GFX-136 RenderTargetCube RenderTargetUsage -- renderer %s\n",
                    kContract.name);

        RunPublicPropertyChecks(dev);
        const bool binds = RunBindingBoundary(dev);

        if (!binds)
        {
            std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
            result_ = (passCount_ == totalCount_) ? 0 : 1;
            Exit();
            return;
        }

        RunPreserveCore(dev);
        RunFaceSequence(dev);
        RunTargetRoundTrips(dev);
        RunTwoCubesDifferentUsage(dev);
        RunIdleBinds(dev);
        RunAccumulationAndReadbackBoundary(dev);
        RunFirstBind(dev);
        RunSameWindow(dev);
        RunFaceIsolation(dev);
        RunDiscardUsage(dev);
        RunPlatformContents(dev);
        RunRepeatedCycles(dev);
        RunDepthInteraction(dev);
        RunMsaa(dev);
        RunMipLevelZero(dev);

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    RenderTargetCubeUsageTest test;
    test.Run();
    return test.Result();
}
