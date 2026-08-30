// SPDX-License-Identifier: MS-PL
// REMED-GFX-134: a RenderTargetCube's rendered colour must have exactly ONE honest public outcome
// -- `RenderTargetCube::GetData` returns the face that was really rendered, or it rejects the
// request deterministically. Never a fabricated face, never another face's content, never a
// mirrored or rotated one, never an unresolved multisample surface.
//
// REMED-GFX-130 made the *reporting* honest: `ITextureCubeRenderer::GetData`'s default became
// `return false`, so a renderer with no rendered-cube readback raises System::NotSupportedException
// instead of handing back the shared layer's own zeroed scratch buffer. What it deliberately did
// NOT do was implement the missing readback, and it recorded the consequence: only SdlGpu and
// WebGPU could answer at all, while EasyGL, Vulkan, Bgfx, D3D9, D3D11 and D3D12 -- each of which
// already owns the exact staging/blit/copy mechanism its plain-TextureCube sibling uses in the same
// file -- refused. This file is the oracle for closing that gap.
//
// Why the content producer is drawn geometry and never Clear():
//   * REMED-GFX-129 separately records a Vulkan PreserveContents Clear defect, so a Clear-based
//     oracle would confuse two findings;
//   * a uniform clear colour cannot detect a vertical flip, a horizontal flip, a 180-degree
//     rotation, a wrong face, a stale previous face, or an unresolved multisample surface.
// Every face is therefore painted by SpriteBatch quads into an asymmetric five-region pattern
// (upper-left, upper-right, lower-left, lower-right, centre, plus the surrounding base), and the
// six regions of face `f` are a rotation of the same six pure colours by `f`, so ANY face swap
// changes the colour at EVERY asserted texel.
//
// The palette is deliberately restricted to fully saturated 0/255 channel values. Those are exact
// fixed points of sRGB encoding, so the one renderer whose cube target is sRGB-encoded
// (REMED-GFX-096 already records WebGPU's) needs no tolerance, no colour-space model and no
// weakened assertion here -- every comparison in this file is byte-exact.
//
// The public row order is not assumed. `RenderTarget2D::GetData` is the established, already
// REMED-GFX-127-verified normalization for "what did rendering put at the top of this target", so
// check O1 renders the identical pattern into a RenderTarget2D of the same size, reads it back, and
// requires the cube-face readback to agree with it. A renderer whose native cube readback is
// bottom-up (REMED-GFX-067's originBottomLeft class of question) must normalize exactly once, and
// checks R4/R5/R6 -- final row, final column, lower-right corner -- fail loudly if it normalizes
// twice, or not at all, or on the wrong axis.
//
// Sentinels are used, but never as the success oracle: checks Z1/Z2 record permanently that "the
// call overwrote my destination" and "the destination is no longer the sentinel" both PASS on the
// pre-REMED-GFX-130 fabricated transparent-black face, which is why every other check here asserts
// exact rendered content instead.
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
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

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
    constexpr int kCube = 8;   ///< Cube face edge at mip 0 (mip 1 = 4, mip 2 = 2, mip 3 = 1).
    constexpr int kCorner = 3; ///< Edge of each corner marker.
    constexpr int kMsaaRequest = 4;  ///< Multisample count requested by the MSAA check.

    /**
     * @brief What one public RenderTargetCube readback path on this renderer is required to do.
     *
     * `Exact` -- GetData must return the rendered face byte for byte.
     * `Unsupported` -- GetData must throw System::NotSupportedException and leave the caller's
     * destination byte-for-byte untouched. Reserved for a target this renderer genuinely cannot read
     * back, never for one that merely has not been checked.
     */
    enum class Support
    {
        Exact,
        Unsupported,
    };

    /**
     * @brief What a `mipMap=true` RenderTargetCube really is on this renderer.
     *
     * `Real` -- the mip chain exists and its levels above 0 hold rendered content.
     * `RefusedAtCreation` -- constructing or first using one throws, rather than under-delivering
     * the chain `RenderTargetCube` has already told the XNA layer to expect.
     * `LevelsWithoutStorage` -- construction succeeds and `LevelCount` reports the full chain, but
     * only level 0 was actually allocated. Recorded as an independent finding; what this file
     * enforces is that the missing levels are REFUSED by GetData rather than answered from level 0.
     */
    enum class MipTargets
    {
        Real,
        RefusedAtCreation,
        LevelsWithoutStorage,
    };

    /// The complete, reviewed per-renderer claim this file enforces.
    struct Contract
    {
        const char* name;
        bool    cubeTargetBinds;   ///< CreateRenderTargetCube() returns a real object AND it binds.
        Support level0;            ///< RenderTargetCube::GetData at mip 0.
        Support mipLevel;          ///< RenderTargetCube::GetData at mip > 0 on a mipMap=true target.
        bool    rt2dReadback;      ///< RenderTarget2D::GetData works -- the orientation reference.
        bool    msaaCubeTargets;   ///< A multisampled RenderTargetCube really engages MSAA here.
        Support msaaReadback;      ///< GetData on a MULTISAMPLED RenderTargetCube.
        MipTargets mipMapCubeTargets; ///< What a mipMap=true RenderTargetCube really is here.
        bool    preservesOnRebind; ///< RenderTargetUsage::PreserveContents survives a rebind cycle.
        bool    exactAlpha;        ///< A non-255 rendered alpha survives readback exactly.
        bool    rtCubeSetData;     ///< RenderTargetCube::SetData stores pixels (REMED-GFX-135).
        /// True when an UPLOADED face reads back vertically mirrored, i.e. this renderer's rendering
        /// and its CPU upload write the same face's rows in opposite order. See check W1.
        bool    rtCubeUploadMirrored;
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef.
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless executes no rasterization at all: CreateRenderTargetCube returns a real object so
    // binding/tracing works, but nothing is ever drawn into it, so there is no rendered face it
    // could honestly return. REMED-GFX-130 already deleted the zero-filled buffers that used to
    // make its cube readback look like one.
    // `msaaCubeTargets` true: this renderer records and reports the requested sample count as
    // applied, which is what a state-tracking diagnostic renderer is for -- it is a bookkeeping
    // claim, not a rasterized one, and there is no resolved surface behind it to read (hence
    // `msaaReadback` Unsupported, like every other readback here).
    constexpr Contract kContract{"HEADLESS", true, Support::Unsupported, Support::Unsupported,
                                 false, true, Support::Unsupported, MipTargets::Real, true, false, false, false, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    // Cube-map render targets are an explicit documented v1 scope boundary here
    // (CreateRenderTargetCube keeps IGraphicsRenderer's nullptr default), so binding one is refused
    // by GraphicsDevice::SetRenderTargets and there is no resource to read.
    constexpr Contract kContract{"SOFTWARE", false, Support::Unsupported, Support::Unsupported,
                                 true, false, Support::Unsupported, MipTargets::Real, true, false, false, false, false};
#elif defined(CNA_RENDERER_EASYGL) && defined(CNA_GL_PROFILE_OPENGLES2)
    // The OPENGLES2 GL profile of the EasyGL family: identical to the EASYGL contract below
    // except MSAA -- core OpenGL ES 2.0 has no multisample renderbuffers/blit
    // (docs/opengles2-renderer.md), so a multisampled cube request degrades to single-sample
    // (applied 0, `msaaCubeTargets` false) and the "multisampled" readback probe reads the same
    // ordinary single-sample face exactly.
    constexpr Contract kContract{"EASYGL(OPENGLES2)", true, Support::Exact, Support::Exact,
                                 true, false, Support::Exact, MipTargets::Real, true, true, true, true, false};
#elif defined(CNA_RENDERER_EASYGL)
    constexpr Contract kContract{"EASYGL", true, Support::Exact, Support::Exact,
                                 true, true, Support::Exact, MipTargets::Real, true, true, true, true, false};
#elif defined(CNA_RENDERER_BGFX)
    // REMED-GFX-138: GFX-154's ordered completion now exposes both bgfx's resolved cube level 0
    // and every auto-generated mip before the readback blit. The combined MSAA+mip path is exact
    // too, so both formerly Unsupported declarations are deliberately re-armed.
    constexpr Contract kContract{"BGFX", true, Support::Exact, Support::Exact,
                                 true, true, Support::Exact, MipTargets::Real, true, true, false, false, false};
#elif defined(CNA_RENDERER_VULKAN)
    // `msaaCubeTargets` false: Task 903 deliberately piggybacks a cube target's sample count on
    // the renderer's own global `sampleCount_`, so a cube target multisamples only when the
    // BACKBUFFER was created multisampled -- which this test does not request. The applied count is
    // still asserted against this claim below rather than assumed.
    // `preservesOnRebind` true since REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube now
    // carries a `preserveContents` flag, so VulkanRenderTargetCubeRenderer builds its face
    // framebuffers against GetOrCreateRTRenderPass(depthFmt, !preserveContents) instead of
    // hardcoding the discard variant. examples/rendertargetcube_usage_test.cpp is that finding's
    // own full battery; what U1/U2 below keep is the single check that a preserved face is exactly
    // what GetData reports.
    constexpr Contract kContract{"VULKAN", true, Support::Exact, Support::Exact,
                                 true, false, Support::Exact, MipTargets::Real, true, true, false, false, false};
#elif defined(CNA_RENDERER_WEBGPU)
    // `mipMapCubeTargets` Real / `mipLevel` Exact: WEBGPU-114 builds a real mip chain for a
    // mipMap=true RenderTargetCube -- the colour texture carries the full chain and each face is
    // regenerated from its resolved level 0 after that face's render pass (GenerateMipsForLayer),
    // so GetData at level > 0 returns real downsampled content. `preservesOnRebind` true since
    // REMED-GFX-136: RenderPendingDrawsToRenderTargetCubeFace() no longer hardcodes WGPULoadOp_Clear,
    // it uses the same `clearPending || !preserveContents` choice as its RenderTarget2D sibling.
    constexpr Contract kContract{"WEBGPU", true, Support::Exact, Support::Exact,
                                 true, false, Support::Exact, MipTargets::Real, true, true, false, false, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr Contract kContract{"SDL_GPU", true, Support::Exact, Support::Exact,
                                 true, true, Support::Exact, MipTargets::Real, true, true, false, false, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    // 2D-only by design: CreateRenderTargetCube keeps IGraphicsRenderer's nullptr default.
    constexpr Contract kContract{"SDL_RENDERER", false, Support::Unsupported, Support::Unsupported,
                                 true, false, Support::Unsupported, MipTargets::Real, true, false, false, false, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", false, Support::Unsupported, Support::Unsupported,
                                 true, false, Support::Unsupported, MipTargets::Real, true, false, false, false, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", false, Support::Unsupported, Support::Unsupported,
                                 true, false, Support::Unsupported, MipTargets::Real, true, false, false, false, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    // D3D9RenderTargetCubeRenderer reports GetMultiSampleCount() == 0 unconditionally and its
    // Recreate() allocates exactly ONE level whatever `mipMap` asked for, so this target is
    // single-sample and level-0-only -- while RenderTargetCube::LevelCount still reports the whole
    // chain. `MipTargets::LevelsWithoutStorage` records that mismatch as an independent finding and
    // pins what GetData must do about it: refuse the levels that were never allocated.
    constexpr Contract kContract{"DIRECTX9", true, Support::Exact, Support::Unsupported,
                                 true, false, Support::Exact, MipTargets::LevelsWithoutStorage, true, true, false, false, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", true, Support::Exact, Support::Exact,
                                 true, true, Support::Exact, MipTargets::Real, true, true, false, false, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr Contract kContract{"DIRECTX12", true, Support::Exact, Support::Exact,
                                 true, true, Support::Exact, MipTargets::Real, true, true, false, false, false};
#else
#error "REMED-GFX-134: this renderer has no declared RenderTargetCube GetData contract."
#endif

    /// Two unrelated destination pre-fills. Neither equals any pattern colour, so "still the
    /// sentinel" and "a real texel" can never be confused for one another.
    Color SentinelCD() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }
    Color SentinelA5() { return Color(0xA5, 0xA5, 0xA5, 0xA5); }

    /// The fabricated content a no-op renderer produced through the pre-REMED-GFX-130 shared layer.
    Color Fabricated() { return Color(0, 0, 0, 0); }

    /**
     * @brief The six pattern colours, all fully saturated so sRGB encoding is the identity.
     *
     * None is black, so a fabricated (0,0,0,0) face fails at every asserted texel rather than
     * accidentally matching one.
     */
    const std::array<Color, 6> kPalette = {
        Color(255,   0,   0, 255),  // red
        Color(  0, 255,   0, 255),  // green
        Color(  0,   0, 255, 255),  // blue
        Color(255, 255,   0, 255),  // yellow
        Color(255,   0, 255, 255),  // magenta
        Color(  0, 255, 255, 255),  // cyan
    };

    /// Alpha of the centre marker. Non-trivial (neither 0 nor 255) and exactly representable in an
    /// 8-bit UNORM target, so a renderer that drops the rendered alpha channel is detectable.
    constexpr std::uint8_t kCentreAlpha = 128;

    /**
     * @brief Colour of pattern slot @p slot on @p face, for producer round @p variant.
     *
     * Slot 0 is the base fill, 1..4 are the upper-left/upper-right/lower-left/lower-right corner
     * markers and 5 is the centre marker. Rotating the palette by the face index makes every slot
     * of every face differ from the same slot of every other face, so a face swap, a face reuse or
     * a stale previous face changes the colour at EVERY asserted texel rather than at one probe.
     * `variant` rotates by two more, so re-rendering the same face produces a pattern that is
     * neither its own previous content nor any other face's.
     */
    Color Slot(int face, int slot, int variant)
    {
        Color c = kPalette[static_cast<std::size_t>((slot + face + 2 * variant) % 6)];
        if (slot == 5 && kContract.exactAlpha)
            return Color(c.getRProperty(), c.getGProperty(), c.getBProperty(), kCentreAlpha);
        return c;
    }

    /// The exact texel the pattern producer puts at (@p x, @p y) of @p face in round @p variant.
    Color PatternTexel(int face, int x, int y, int variant)
    {
        const int lo = kCorner;              // 3
        const int hi = kCube - kCorner;      // 5
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

    int MipDim(int base, int level)
    {
        const int d = base >> level;
        return d < 1 ? 1 : d;
    }

    int MipLevelCount(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = s / 2 < 1 ? 1 : s / 2; ++levels; }
        return levels;
    }

    /// One classified readback attempt: what the call did, in full, without judging it yet.
    struct Probe
    {
        bool threwNotSupported  = false;  ///< the deterministic "cannot read this back" rejection
        bool threwSomethingElse = false;  ///< any other exception
        std::string otherWhat;
        std::vector<Color> dest;          ///< the WHOLE destination buffer after the call
        std::size_t sentinelSurvivors = 0;///< entries in the asserted window still holding sentinel
        std::size_t fabricated = 0;       ///< entries in the asserted window equal to (0,0,0,0)
        std::size_t exact = 0;            ///< entries in the asserted window equal to expectation
        std::size_t window = 0;           ///< size of the asserted window
        std::string firstMismatch;        ///< "index got=... expected=..." for the first miss
    };
}

class RenderTargetCubeGetDataContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    /// One opaque 1x1 source texture per palette entry, plus one per palette entry at kCentreAlpha.
    std::array<std::unique_ptr<Texture2D>, 12> solids_;
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

    template <typename ExceptionT, typename Fn>
    static bool Throws(Fn&& fn)
    {
        try { fn(); }
        catch (const ExceptionT&) { return true; }
        catch (...) { return false; }
        return false;
    }

    template <typename Fn>
    static bool ThrowsAnything(Fn&& fn)
    {
        try { fn(); }
        catch (...) { return true; }
        return false;
    }

    // ---------------------------------------------------------------------
    // Content producer
    // ---------------------------------------------------------------------

    Texture2D& SolidFor(const Color& c)
    {
        for (std::size_t i = 0; i < kPalette.size(); ++i)
            if (kPalette[i].getRProperty() == c.getRProperty() &&
                kPalette[i].getGProperty() == c.getGProperty() &&
                kPalette[i].getBProperty() == c.getBProperty())
            {
                const std::size_t index = (c.getAProperty() == 255) ? i : i + kPalette.size();
                return *solids_[index];
            }
        return *solids_[0];
    }

    void DrawQuad(const Color& colour, int x, int y, int w, int h)
    {
        spriteBatch_->Draw(SolidFor(colour), Rectangle(x, y, w, h), Rectangle(0, 0, 1, 1),
                           Color::White);
    }

    /// Paints the whole currently-bound target with the asymmetric five-region pattern. Drawn
    /// geometry only -- never Clear (see this file's header for why).
    void DrawPattern(int face, int variant)
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        DrawQuad(Slot(face, 0, variant), 0, 0, kCube, kCube);
        DrawQuad(Slot(face, 1, variant), 0, 0, kCorner, kCorner);
        DrawQuad(Slot(face, 2, variant), kCube - kCorner, 0, kCorner, kCorner);
        DrawQuad(Slot(face, 3, variant), 0, kCube - kCorner, kCorner, kCorner);
        DrawQuad(Slot(face, 4, variant), kCube - kCorner, kCube - kCorner, kCorner, kCorner);
        DrawQuad(Slot(face, 5, variant), kCorner, kCorner, kCube - 2 * kCorner, kCube - 2 * kCorner);
        spriteBatch_->End();
    }

    /// Paints the whole currently-bound target one uniform colour, still with a drawn quad.
    void DrawUniform(const Color& colour, int size)
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        DrawQuad(colour, 0, 0, size, size);
        spriteBatch_->End();
    }

    void RenderFace(GraphicsDevice& dev, RenderTargetCube& cube, int face, int variant)
    {
        dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
        DrawPattern(face, variant);
        dev.SetRenderTargets({});
    }

    // ---------------------------------------------------------------------
    // Probes
    // ---------------------------------------------------------------------

    /**
     * @brief Reads a cube-face rectangle into a destination pre-filled with @p sentinel and records
     *        every fact needed to classify the outcome.
     *
     * @param pad Extra sentinel entries reserved BEFORE the asserted window, i.e. the startIndex
     *            the call is made with. `pad` more are reserved after it. Both must survive
     *            untouched no matter what happens.
     */
    static Probe ProbeFace(const TextureCube& cube, int face, int level, const Rectangle* rect,
                           int w, int h, int pad, const Color& sentinel,
                           const std::vector<Color>& expected)
    {
        Probe p;
        p.window = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        p.dest.assign(p.window + static_cast<std::size_t>(pad) * 2, sentinel);
        try
        {
            cube.GetData(static_cast<CubeMapFace>(face), level, rect, p.dest.data(), pad,
                         static_cast<int>(p.window));
        }
        catch (const System::NotSupportedException&) { p.threwNotSupported = true; }
        catch (const std::exception& e) { p.threwSomethingElse = true; p.otherWhat = e.what(); }
        catch (...) { p.threwSomethingElse = true; p.otherWhat = "non-std exception"; }

        for (std::size_t i = 0; i < p.window; ++i)
        {
            const Color& got = p.dest[static_cast<std::size_t>(pad) + i];
            if (Same(got, sentinel)) ++p.sentinelSurvivors;
            if (Same(got, Fabricated())) ++p.fabricated;
            if (i < expected.size() && Same(got, expected[i])) ++p.exact;
            else if (p.firstMismatch.empty() && i < expected.size())
                p.firstMismatch = " first miss at " + std::to_string(i) + " got=" + ColorText(got) +
                                  " expected=" + ColorText(expected[i]);
        }
        return p;
    }

    /**
     * @brief Judges one probe against this renderer's declared contract.
     *
     * `Exact`: no exception, every asserted entry equals its expectation, and every padding entry
     * outside the asserted window is still the sentinel.
     * `Unsupported`: System::NotSupportedException and the WHOLE destination -- window and padding
     * alike -- byte-for-byte still the sentinel. A fabricated or partially written destination
     * fails either way.
     */
    void Judge(const Probe& p, Support required, int pad, const Color& sentinel,
               const std::string& label)
    {
        std::size_t padIntact = 0;
        const std::size_t padTotal = static_cast<std::size_t>(pad) * 2;
        for (std::size_t i = 0; i < p.dest.size(); ++i)
        {
            const bool inWindow = i >= static_cast<std::size_t>(pad) &&
                                  i < static_cast<std::size_t>(pad) + p.window;
            if (!inWindow && Same(p.dest[i], sentinel)) ++padIntact;
        }

        const std::string facts =
            " [threwNotSupported=" + std::string(p.threwNotSupported ? "1" : "0") +
            " threwOther=" + (p.threwSomethingElse ? ("1:" + p.otherWhat) : "0") +
            " exact=" + std::to_string(p.exact) + "/" + std::to_string(p.window) +
            " fabricated=" + std::to_string(p.fabricated) +
            " sentinelSurvivors=" + std::to_string(p.sentinelSurvivors) +
            " padIntact=" + std::to_string(padIntact) + "/" + std::to_string(padTotal) +
            p.firstMismatch + "]";

        if (required == Support::Exact)
        {
            const bool ok = !p.threwNotSupported && !p.threwSomethingElse &&
                            p.exact == p.window && padIntact == padTotal;
            check(ok, label + " -- exact rendered content required" + facts);
        }
        else
        {
            const bool ok = p.threwNotSupported && !p.threwSomethingElse &&
                            p.sentinelSurvivors == p.window && padIntact == padTotal;
            check(ok, label + " -- deterministic NotSupportedException with the destination "
                              "untouched required" + facts);
        }
    }

    /// Expected colours for a cube sub-rectangle, in the row-major order GetData writes them.
    static std::vector<Color> ExpectedRect(int face, int x0, int y0, int w, int h, int variant)
    {
        std::vector<Color> e;
        e.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) e.push_back(PatternTexel(face, x0 + x, y0 + y, variant));
        return e;
    }

    static std::vector<Color> ExpectedFace(int face, int variant)
    {
        return ExpectedRect(face, 0, 0, kCube, kCube, variant);
    }

    static std::vector<Color> Uniform(const Color& c, int count)
    {
        return std::vector<Color>(static_cast<std::size_t>(count), c);
    }

    // =====================================================================
    // Sections
    // =====================================================================

    /// O1 -- the orientation reference. RenderTarget2D::GetData is the established normalization
    /// for "what did rendering put at the top of this target" (REMED-GFX-127), so the cube faces
    /// below are required to agree with what it reports for the identical draw.
    std::vector<Color> RunOrientationReference(GraphicsDevice& dev)
    {
        std::vector<Color> reference;
        if (!kContract.rt2dReadback)
        {
            check(true, "O1 reference: RenderTarget2D readback is unavailable on this renderer -- "
                        "no orientation reference to establish");
            return reference;
        }

        RenderTarget2D flat(dev, kCube, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(&flat);
        DrawPattern(0, 0);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
        bool threw = false;
        std::string what;
        try { flat.GetData(got.data(), 0, static_cast<int>(got.size())); }
        catch (const std::exception& e) { threw = true; what = e.what(); }

        const std::vector<Color> expected = ExpectedFace(0, 0);
        std::size_t exact = 0;
        std::string firstMiss;
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            if (Same(got[i], expected[i])) ++exact;
            else if (firstMiss.empty())
                firstMiss = " first miss at " + std::to_string(i) + " got=" + ColorText(got[i]) +
                            " expected=" + ColorText(expected[i]);
        }
        check(!threw && exact == got.size(),
              "O1 reference: the identical pattern rendered into a RenderTarget2D reads back "
              "top-row-first and exact, establishing the row order every cube face below must "
              "match [threw=" + (threw ? ("1:" + what) : std::string("0")) +
              " exact=" + std::to_string(exact) + "/" + std::to_string(got.size()) + firstMiss + "]");
        if (!threw) reference = got;
        return reference;
    }

    /// The whole file's subject: a cube target that cannot even be bound has nothing to read back.
    bool RunBindingBoundary(GraphicsDevice& dev, RenderTargetCube& cube)
    {
        bool bound = false;
        std::string what;
        try
        {
            dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
            bound = true;
            dev.SetRenderTargets({});
        }
        catch (const std::exception& e) { what = e.what(); }

        if (kContract.cubeTargetBinds)
        {
            check(bound, "B1 binding: this renderer declares a real RenderTargetCube, so binding a "
                         "face must succeed [" + (bound ? std::string("bound") : "threw:" + what) + "]");
        }
        else
        {
            check(!bound, "B1 binding: this renderer creates no cube render target, so binding a "
                          "face must be refused deterministically [" +
                              (bound ? std::string("bound anyway") : "threw:" + what) + "]");
        }
        return bound;
    }

    void RunFaceChecks(GraphicsDevice& dev, RenderTargetCube& cube,
                       const std::vector<Color>& reference)
    {
        // Render all six faces first, then read them back in a DIFFERENT order, so a renderer that
        // only ever answers for the most recently bound face cannot pass.
        for (int face = 0; face < 6; ++face) RenderFace(dev, cube, face, 0);

        static const std::array<int, 6> kReadOrder = {3, 0, 5, 1, 4, 2};

        // ---- Z1/Z2: why sentinel mutation is not a success oracle -------------------------
        {
            Probe p = ProbeFace(cube, 0, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(0, 0));
            const bool overwroteSentinel = p.sentinelSurvivors == 0;
            const bool reallyRead = p.exact == p.window;
            check(!overwroteSentinel || reallyRead,
                  "Z1: 'GetData overwrote my sentinel destination' is NOT accepted as success -- an "
                  "overwritten destination must also hold the exact rendered face "
                  "[sentinelSurvivors=" + std::to_string(p.sentinelSurvivors) +
                  " exact=" + std::to_string(p.exact) + "/" + std::to_string(p.window) + "]");
            check(p.fabricated == 0,
                  "Z2: no asserted texel comes back as the fabricated (0,0,0,0) the pre-fix shared "
                  "layer produced from its own zeroed scratch buffer [fabricated=" +
                  std::to_string(p.fabricated) + "/" + std::to_string(p.window) + "]");
        }

        for (std::size_t i = 0; i < kReadOrder.size(); ++i)
        {
            const int face = kReadOrder[i];
            Probe p = ProbeFace(cube, face, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(face, 0));
            Judge(p, kContract.level0, 0, SentinelCD(),
                  "F" + std::to_string(i + 1) + " face " + std::to_string(face) +
                  " full level 0, read " + std::to_string(i + 1) + "th of six in a shuffled order "
                  "(base slot " + ColorText(Slot(face, 0, 0)) + ")");
        }

        // ---- F7: one face's readback must never be another face's content ------------------
        if (kContract.level0 == Support::Exact)
        {
            std::string confusion;
            for (int face = 0; face < 6; ++face)
            {
                std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
                try
                {
                    cube.GetData(static_cast<CubeMapFace>(face), got.data(),
                                 static_cast<int>(got.size()));
                }
                catch (const std::exception& e)
                {
                    confusion += " face " + std::to_string(face) + " threw:" + e.what();
                    continue;
                }
                for (int other = 0; other < 6; ++other)
                {
                    if (other == face) continue;
                    if (Same(got[0], PatternTexel(other, 0, 0, 0)))
                        confusion += " face " + std::to_string(face) + " returned face " +
                                     std::to_string(other) + "'s corner";
                }
            }
            check(confusion.empty(),
                  "F7: no face's readback returns another face's pattern -- a wrong array layer, a "
                  "wrong D3D subresource index or a stale previous face cannot pass" + confusion);
        }
        else
        {
            check(true, "F7: face-confusion probe skipped -- this renderer rejects cube readback");
        }

        // ---- F8: the cube face agrees with the RenderTarget2D orientation reference ---------
        if (kContract.level0 == Support::Exact && !reference.empty())
        {
            std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
            std::string what;
            try { cube.GetData(CubeMapFace::PositiveX, got.data(), static_cast<int>(got.size())); }
            catch (const std::exception& e) { what = std::string(" threw:") + e.what(); }
            std::size_t same = 0;
            for (std::size_t i = 0; i < got.size(); ++i)
                if (Same(got[i], reference[i])) ++same;
            check(what.empty() && same == got.size(),
                  "F8 orientation: face +X reads back identical to the RenderTarget2D reference of "
                  "the same draw -- one normalization, on the right axis [" +
                  std::to_string(same) + "/" + std::to_string(got.size()) + what + "]");
        }
        else
        {
            check(true, "F8 orientation: reference comparison skipped -- no cube readback or no "
                        "RenderTarget2D reference on this renderer");
        }

        // ---- T1: face A -> face B -> face A ------------------------------------------------
        RenderFace(dev, cube, 0, 0);
        RenderFace(dev, cube, 1, 0);
        RenderFace(dev, cube, 0, 1);
        {
            Probe a = ProbeFace(cube, 0, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(0, 1));
            Judge(a, kContract.level0, 0, SentinelCD(),
                  "T1 A->B->A: face 0 holds its SECOND pattern, not its first and not face 1's");
            Probe b = ProbeFace(cube, 1, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(1, 0));
            Judge(b, kContract.level0, 0, SentinelCD(),
                  "T2 A->B->A: face 1 still holds its own pattern after face 0 was re-rendered");
        }
    }

    void RunRangeChecks(GraphicsDevice& dev, RenderTargetCube& cube)
    {
        constexpr int kFace = 2;
        RenderFace(dev, cube, kFace, 0);

        {
            Probe p = ProbeFace(cube, kFace, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(kFace, 0));
            Judge(p, kContract.level0, 0, SentinelCD(), "R1 range: complete face");
        }
        {
            const Rectangle r(5, 3, 1, 1);
            Probe p = ProbeFace(cube, kFace, 0, &r, 1, 1, 0, SentinelA5(),
                                ExpectedRect(kFace, 5, 3, 1, 1, 0));
            Judge(p, kContract.level0, 0, SentinelA5(), "R2 range: single texel (5,3,1,1)");
        }
        {
            const Rectangle r(2, 2, 4, 3);
            Probe p = ProbeFace(cube, kFace, 0, &r, 4, 3, 0, SentinelCD(),
                                ExpectedRect(kFace, 2, 2, 4, 3, 0));
            Judge(p, kContract.level0, 0, SentinelCD(), "R3 range: middle rect (2,2,4,3)");
        }
        {
            const Rectangle r(0, kCube - 1, kCube, 1);
            Probe p = ProbeFace(cube, kFace, 0, &r, kCube, 1, 0, SentinelCD(),
                                ExpectedRect(kFace, 0, kCube - 1, kCube, 1, 0));
            Judge(p, kContract.level0, 0, SentinelCD(),
                  "R4 range: final row -- a vertically flipped face cannot pass");
        }
        {
            const Rectangle r(kCube - 1, 0, 1, kCube);
            Probe p = ProbeFace(cube, kFace, 0, &r, 1, kCube, 0, SentinelCD(),
                                ExpectedRect(kFace, kCube - 1, 0, 1, kCube, 0));
            Judge(p, kContract.level0, 0, SentinelCD(),
                  "R5 range: final column -- a horizontally flipped face cannot pass");
        }
        {
            const Rectangle r(kCube - 1, kCube - 1, 1, 1);
            Probe p = ProbeFace(cube, kFace, 0, &r, 1, 1, 0, SentinelA5(),
                                ExpectedRect(kFace, kCube - 1, kCube - 1, 1, 1, 0));
            Judge(p, kContract.level0, 0, SentinelA5(),
                  "R6 range: lower-right corner -- a 180-degree rotated face cannot pass");
        }
        {
            Probe p = ProbeFace(cube, kFace, 0, nullptr, kCube, kCube, 5, SentinelCD(),
                                ExpectedFace(kFace, 0));
            Judge(p, kContract.level0, 5, SentinelCD(),
                  "R7 range: complete face at destination startIndex 5, surrounding sentinels intact");
        }
        {
            Probe first = ProbeFace(cube, kFace, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                    ExpectedFace(kFace, 0));
            Probe again = ProbeFace(cube, kFace, 0, nullptr, kCube, kCube, 0, SentinelA5(),
                                    ExpectedFace(kFace, 0));
            Judge(again, kContract.level0, 0, SentinelA5(),
                  "R8 range: a repeated readback returns the same content, from a different sentinel");
            if (kContract.level0 == Support::Exact)
            {
                std::size_t identical = 0;
                for (std::size_t i = 0; i < first.dest.size() && i < again.dest.size(); ++i)
                    if (Same(first.dest[i], again.dest[i])) ++identical;
                check(identical == first.dest.size(),
                      "R9 range: two consecutive readbacks are byte-identical [" +
                      std::to_string(identical) + "/" + std::to_string(first.dest.size()) + "]");
            }
            else
            {
                check(true, "R9 range: repeat-stability probe skipped -- readback is rejected here");
            }
        }
    }

    void RunRejectionChecks(GraphicsDevice& dev, RenderTargetCube& cube)
    {
        (void)dev;
        std::vector<Color> buf(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
        check(Throws<std::out_of_range>([&] {
                  cube.GetData(static_cast<CubeMapFace>(6), 0, nullptr, buf.data(), 0, 4);
              }),
              "X1 reject: an out-of-range CubeMapFace throws std::out_of_range");
        check(Throws<std::out_of_range>([&] {
                  cube.GetData(CubeMapFace::PositiveX, -1, nullptr, buf.data(), 0, 4);
              }),
              "X2 reject: a negative mip level throws std::out_of_range");
        {
            const Rectangle r(kCube - 1, kCube - 1, 2, 2);
            check(Throws<std::out_of_range>([&] {
                      cube.GetData(CubeMapFace::PositiveX, 0, &r, buf.data(), 0, 4);
                  }),
                  "X3 reject: a rectangle leaving the face throws std::out_of_range");
        }
        check(Throws<std::out_of_range>([&] {
                  cube.GetData(CubeMapFace::PositiveX, 0, nullptr, buf.data(), 0, 4);
              }),
              "X4 reject: elementCount below the requested region throws std::out_of_range");
        check(Throws<std::invalid_argument>([&] {
                  cube.GetData(CubeMapFace::PositiveX, 0, nullptr, static_cast<Color*>(nullptr), 0,
                               kCube * kCube);
              }),
              "X5 reject: a null destination throws std::invalid_argument");

        std::size_t intact = 0;
        for (const Color& c : buf) if (Same(c, SentinelCD())) ++intact;
        check(intact == buf.size(),
              "X6 reject: every rejected call left the destination byte-for-byte untouched [" +
              std::to_string(intact) + "/" + std::to_string(buf.size()) + "]");

        // A mip level this target genuinely does not have must be refused, never answered with
        // level 0's content.
        {
            Probe p = ProbeFace(cube, 0, 4, nullptr, 1, 1, 0, SentinelA5(),
                                Uniform(SentinelA5(), 1));
            check(p.threwNotSupported && !p.threwSomethingElse && p.sentinelSurvivors == 1,
                  "X7 reject: a mip level this non-mipmapped target never allocated is refused with "
                  "NotSupportedException, not answered with level 0 [threwNotSupported=" +
                  std::string(p.threwNotSupported ? "1" : "0") + " threwOther=" +
                  (p.threwSomethingElse ? ("1:" + p.otherWhat) : "0") + " sentinelSurvivors=" +
                  std::to_string(p.sentinelSurvivors) + "]");
        }
    }

    void RunActiveTargetChecks(GraphicsDevice& dev, RenderTargetCube& cube)
    {
        RenderFace(dev, cube, 4, 0);

        dev.SetRenderTarget(&cube, CubeMapFace::PositiveZ);   // face 4
        check(Throws<System::InvalidOperationException>([&] {
                  dev.getTexturesProperty()(0, &cube);
              }),
              "A1 active: sampling the ACTIVE cube target through the texture collection is "
              "rejected (REMED-GFX-039)");
        check(Throws<System::InvalidOperationException>([&] { cube.Dispose(); }),
              "A2 active: disposing the ACTIVE cube target is rejected (REMED-GFX-039)");

        // GetData while the same face is bound. CNA establishes no rejection here for
        // RenderTarget2D, so the only thing that is never acceptable is a fabricated face.
        {
            Probe p = ProbeFace(cube, 4, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(4, 0));
            const bool honest = (p.exact == p.window) ||
                                (p.threwNotSupported && p.sentinelSurvivors == p.window);
            check(honest && p.fabricated == 0 && !p.threwSomethingElse,
                  "A3 active: GetData while the SAME face is bound returns the live face or refuses "
                  "-- never a fabricated one, and never a face this readback itself wiped [exact=" +
                  std::to_string(p.exact) + "/" + std::to_string(p.window) + " fabricated=" +
                  std::to_string(p.fabricated) + " sentinelSurvivors=" +
                  std::to_string(p.sentinelSurvivors) +
                  " threwNotSupported=" + std::string(p.threwNotSupported ? "1" : "0") +
                  " threwOther=" + (p.threwSomethingElse ? ("1:" + p.otherWhat) : "0") +
                  p.firstMismatch + "]");
        }

        // GetData for ANOTHER face while this one is bound.
        RenderFace(dev, cube, 5, 0);
        dev.SetRenderTarget(&cube, CubeMapFace::PositiveZ);   // bind face 4 again
        {
            Probe p = ProbeFace(cube, 5, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(5, 0));
            const bool honest = (p.exact == p.window) ||
                                (p.threwNotSupported && p.sentinelSurvivors == p.window);
            check(honest && p.fabricated == 0 && !p.threwSomethingElse,
                  "A4 active: GetData for ANOTHER face while face 4 is bound returns that face or "
                  "refuses -- never face 4's content and never a fabricated one [exact=" +
                  std::to_string(p.exact) + "/" + std::to_string(p.window) + " fabricated=" +
                  std::to_string(p.fabricated) + " sentinelSurvivors=" +
                  std::to_string(p.sentinelSurvivors) + p.firstMismatch + "]");
        }
        dev.SetRenderTargets({});

        // GetData while an unrelated RenderTarget2D is bound.
        if (kContract.rt2dReadback)
        {
            RenderTarget2D other(dev, kCube, kCube, false, SurfaceFormat::Color, DepthFormat::None,
                                 0, RenderTargetUsage::PreserveContents);
            dev.SetRenderTarget(&other);
            Probe p = ProbeFace(cube, 5, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(5, 0));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Judge(p, kContract.level0, 0, SentinelCD(),
                  "A5 active: GetData on an idle cube while an unrelated RenderTarget2D is bound");
        }
        else
        {
            check(true, "A5 active: skipped -- no RenderTarget2D on this renderer");
        }

        // And after a plain unbind.
        {
            Probe p = ProbeFace(cube, 5, 0, nullptr, kCube, kCube, 0, SentinelA5(),
                                ExpectedFace(5, 0));
            Judge(p, kContract.level0, 0, SentinelA5(), "A6 active: GetData after a plain unbind");
        }
    }

    void RunSwitchingChecks(GraphicsDevice& dev, RenderTargetCube& cube)
    {
        // cube face -> backbuffer -> cube face
        RenderFace(dev, cube, 0, 0);
        DrawUniform(kPalette[3], kBBW);   // a backbuffer draw between the two cube reads
        {
            Probe p = ProbeFace(cube, 0, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(0, 0));
            Judge(p, kContract.level0, 0, SentinelCD(),
                  "S1 switch: cube face -> backbuffer -> read keeps the face's own content");
        }

        // cube face -> RenderTarget2D -> cube face
        if (kContract.rt2dReadback)
        {
            RenderTarget2D flat(dev, kCube, kCube, false, SurfaceFormat::Color, DepthFormat::None,
                                0, RenderTargetUsage::PreserveContents);
            dev.SetRenderTarget(&flat);
            DrawPattern(3, 1);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            Probe p = ProbeFace(cube, 0, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                ExpectedFace(0, 0));
            Judge(p, kContract.level0, 0, SentinelCD(),
                  "S2 switch: a RenderTarget2D drawn between bind and read does not alias the cube");

            std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
            bool threw = false;
            try { flat.GetData(got.data(), 0, static_cast<int>(got.size())); }
            catch (...) { threw = true; }
            const std::vector<Color> expected = ExpectedFace(3, 1);
            std::size_t exact = 0;
            for (std::size_t i = 0; i < got.size(); ++i) if (Same(got[i], expected[i])) ++exact;
            check(!threw && exact == got.size(),
                  "S3 switch: the RenderTarget2D itself still holds its own content after the cube "
                  "readback [exact=" + std::to_string(exact) + "/" + std::to_string(got.size()) + "]");
        }
        else
        {
            check(true, "S2 switch: skipped -- no RenderTarget2D on this renderer");
            check(true, "S3 switch: skipped -- no RenderTarget2D on this renderer");
        }

        // Two independent cube targets must not alias.
        {
            RenderTargetCube second(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                    RenderTargetUsage::PreserveContents);
            if (kContract.cubeTargetBinds)
            {
                RenderFace(dev, cube, 2, 0);
                RenderFace(dev, second, 2, 1);
                Probe a = ProbeFace(cube, 2, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                    ExpectedFace(2, 0));
                Judge(a, kContract.level0, 0, SentinelCD(),
                      "S4 switch: cube A face 2 keeps its own pattern after cube B rendered face 2");
                Probe b = ProbeFace(second, 2, 0, nullptr, kCube, kCube, 0, SentinelA5(),
                                    ExpectedFace(2, 1));
                Judge(b, kContract.level0, 0, SentinelA5(),
                      "S5 switch: cube B face 2 holds its own, different pattern");
            }
            else
            {
                check(true, "S4 switch: skipped -- no cube render target on this renderer");
                check(true, "S5 switch: skipped -- no cube render target on this renderer");
            }
        }
    }

    void RunUsageChecks(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargetBinds)
        {
            check(true, "U1 usage: skipped -- no cube render target on this renderer");
            check(true, "U2 usage: skipped -- no cube render target on this renderer");
            return;
        }

        // PreserveContents: render the pattern, unbind, read, rebind, draw a small marker over one
        // corner, unbind, read again. Both the untouched pattern and the marker must be present.
        {
            RenderTargetCube preserve(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                      RenderTargetUsage::PreserveContents);
            RenderFace(dev, preserve, 1, 0);
            Probe before = ProbeFace(preserve, 1, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                     ExpectedFace(1, 0));
            Judge(before, kContract.level0, 0, SentinelCD(),
                  "U1 usage: PreserveContents holds the rendered pattern after unbind");

            dev.SetRenderTarget(&preserve, CubeMapFace::NegativeX);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth,
                                &noCull);
            DrawQuad(kPalette[0], 0, 0, 2, 2);
            spriteBatch_->End();
            dev.SetRenderTargets({});

            if (kContract.preservesOnRebind)
            {
                std::vector<Color> expected = ExpectedFace(1, 0);
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 2; ++x)
                        expected[static_cast<std::size_t>(y) * kCube + x] = kPalette[0];
                Probe after = ProbeFace(preserve, 1, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                        expected);
                Judge(after, kContract.level0, 0, SentinelCD(),
                      "U2 usage: PreserveContents keeps the untouched region and shows the second "
                      "draw");
            }
            else
            {
                // This renderer drops the previous bind cycle's content even under
                // PreserveContents, so the untouched region is NOT asserted -- claiming it would be
                // claiming preservation this renderer does not provide. What IS still required is
                // that the region actually drawn this cycle reads back exactly, and that the
                // dropped region is a real cleared colour rather than a fabricated (0,0,0,0).
                const Rectangle marker(0, 0, 2, 2);
                Probe drawn = ProbeFace(preserve, 1, 0, &marker, 2, 2, 0, SentinelCD(),
                                        Uniform(kPalette[0], 4));
                Judge(drawn, kContract.level0, 0, SentinelCD(),
                      "U2 usage: this renderer discards on every cube bind cycle regardless of "
                      "PreserveContents -- only the region drawn in the last cycle is asserted");
                Probe whole = ProbeFace(preserve, 1, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                        ExpectedFace(1, 0));
                check(whole.fabricated == 0,
                      "U2b usage: the region this renderer dropped comes back as its real cleared "
                      "colour, never as the fabricated (0,0,0,0) [fabricated=" +
                      std::to_string(whole.fabricated) + "/" + std::to_string(whole.window) + "]");
            }
        }

        // DiscardContents: its contract explicitly permits the previous frame to be dropped, so
        // only the newly drawn content is asserted -- what happened to the rest is reported.
        {
            RenderTargetCube discard(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                     RenderTargetUsage::DiscardContents);
            RenderFace(dev, discard, 1, 0);
            dev.SetRenderTarget(&discard, CubeMapFace::NegativeX);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth,
                                &noCull);
            DrawQuad(kPalette[0], 0, 0, 2, 2);
            spriteBatch_->End();
            dev.SetRenderTargets({});

            const Rectangle marker(0, 0, 2, 2);
            Probe p = ProbeFace(discard, 1, 0, &marker, 2, 2, 0, SentinelCD(),
                                Uniform(kPalette[0], 4));
            Judge(p, kContract.level0, 0, SentinelCD(),
                  "U3 usage: DiscardContents returns the region actually drawn in the last pass");
        }
    }

    void RunDepthChecks(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargetBinds)
        {
            check(true, "D1 depth: skipped -- no cube render target on this renderer");
            check(true, "D2 depth: skipped -- no cube render target on this renderer");
            return;
        }

        RenderTargetCube depth24(dev, kCube, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                                 RenderTargetUsage::PreserveContents);
        RenderFace(dev, depth24, 3, 0);
        Probe a = ProbeFace(depth24, 3, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                            ExpectedFace(3, 0));
        Judge(a, kContract.level0, 0, SentinelCD(),
              "D1 depth: a Depth24 cube target's COLOUR readback returns colour, never depth bytes");

        RenderTargetCube depth24s8(dev, kCube, false, SurfaceFormat::Color,
                                   DepthFormat::Depth24Stencil8, 0,
                                   RenderTargetUsage::PreserveContents);
        RenderFace(dev, depth24s8, 4, 0);
        Probe b = ProbeFace(depth24s8, 4, 0, nullptr, kCube, kCube, 0, SentinelA5(),
                            ExpectedFace(4, 0));
        Judge(b, kContract.level0, 0, SentinelA5(),
              "D2 depth: a Depth24Stencil8 cube target's COLOUR readback is unaffected by the "
              "depth/stencil attachment");
    }

    void RunMsaaChecks(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargetBinds)
        {
            check(true, "M1 msaa: skipped -- no cube render target on this renderer");
            check(true, "M2 msaa: skipped -- no cube render target on this renderer");
            return;
        }

        RenderTargetCube msaa(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None,
                              kMsaaRequest, RenderTargetUsage::PreserveContents);
        const int applied = msaa.getMultiSampleCountProperty();
        check(kContract.msaaCubeTargets == (applied > 0),
              "M1 msaa: the applied cube-target sample count matches this renderer's declared "
              "capability (requested " + std::to_string(kMsaaRequest) + ", applied " +
              std::to_string(applied) + ", declared msaaCubeTargets=" +
              std::string(kContract.msaaCubeTargets ? "true" : "false") + ")");

        // A face is painted with axis-aligned full-texel quads, so every sample of every texel
        // carries the same colour and the RESOLVED result is byte-identical to the single-sample
        // one. Reading the raw multisample surface instead, or skipping the resolve, does not
        // produce that. The same assertion is made at applied == 0, where it is simply the
        // single-sample path -- so this check is never skipped.
        RenderFace(dev, msaa, 2, 0);
        Probe p = ProbeFace(msaa, 2, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                            ExpectedFace(2, 0));
        Judge(p, applied > 0 ? kContract.msaaReadback : kContract.level0, 0, SentinelCD(),
              "M2 msaa: an applied-" + std::to_string(applied) +
              "x cube target reads back its RESOLVED face");
    }

    void RunMipChecks(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargetBinds)
        {
            check(true, "L1 mip: skipped -- no cube render target on this renderer");
            check(true, "L2 mip: skipped -- no cube render target on this renderer");
            check(true, "L3 mip: skipped -- no cube render target on this renderer");
            return;
        }

        if (kContract.mipMapCubeTargets == MipTargets::RefusedAtCreation)
        {
            // A renderer that cannot build the mip chain RenderTargetCube's own LevelCount already
            // promised may refuse it -- what it must not do is accept the request and silently
            // under-deliver. Whether the refusal lands at construction or at first use, it has to
            // be an exception rather than a target whose declared levels have no storage.
            std::string what;
            try
            {
                RenderTargetCube mipped(dev, kCube, true, SurfaceFormat::Color, DepthFormat::None,
                                        0, RenderTargetUsage::PreserveContents);
                dev.SetRenderTarget(&mipped, CubeMapFace::NegativeY);
                DrawUniform(kPalette[4], kCube);
                dev.SetRenderTargets({});
            }
            catch (const std::exception& e) { what = e.what(); }
            catch (...) { what = "non-std exception"; }
            check(!what.empty(),
                  "L1 mip: this renderer refuses a mipMap=true RenderTargetCube deterministically "
                  "rather than reporting levels it has no storage for [" +
                  (what.empty() ? std::string("accepted it silently") : what) + "]");
            check(true, "L2 mip: level-count probe skipped -- no mipmapped cube target here");
            check(true, "L3 mip: level-1 readback skipped -- no mipmapped cube target here");
            return;
        }

        RenderTargetCube mipped(dev, kCube, true, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::PreserveContents);
        // A UNIFORM face colour makes the level-1 expectation independent of whichever box/linear
        // filter each renderer's mip generation uses, so this asserts real downsampled content
        // without asserting a filter this project never specified.
        dev.SetRenderTarget(&mipped, CubeMapFace::NegativeY);
        DrawUniform(kPalette[4], kCube);
        dev.SetRenderTargets({});

        Probe l0 = ProbeFace(mipped, 3, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                             Uniform(kPalette[4], kCube * kCube));
        Judge(l0, kContract.level0, 0, SentinelCD(),
              "L1 mip: a mipMap=true cube target's level 0 reads back exactly");

        const int levelCount = mipped.getLevelCountProperty();
        check(levelCount == MipLevelCount(kCube),
              "L2 mip: a mipMap=true " + std::to_string(kCube) + "x" + std::to_string(kCube) +
              " cube target reports " + std::to_string(MipLevelCount(kCube)) + " levels [got " +
              std::to_string(levelCount) + "]");

        const int dim = MipDim(kCube, 1);
        Probe l1 = ProbeFace(mipped, 3, 1, nullptr, dim, dim, 0, SentinelA5(),
                             Uniform(kPalette[4], dim * dim));
        Judge(l1, kContract.mipLevel, 0, SentinelA5(),
              kContract.mipMapCubeTargets == MipTargets::LevelsWithoutStorage
                  ? std::string("L3 mip: this renderer allocates ONLY level 0 while LevelCount "
                                "reports the whole chain -- the levels it never allocated must be "
                                "REFUSED, never answered from level 0")
                  : std::string("L3 mip: level 1 of a mipMap=true cube target"));
    }

    /**
     * @brief W1 -- how a face UPLOADED through the inherited TextureCube::SetData relates to the
     *        same face read back.
     *
     * EasyGL and Skia implement this upload; other renderers inherit the deterministic refusal.
     * Where it is implemented, the round trip is measured rather than assumed because rendered
     * and uploaded writers need not share row orientation. EasyGL intentionally records a mirrored
     * upload/readback relationship; Skia's canonical CPU transfer shadow records the same order.
     */
    void RunUploadRoundTrip(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargetBinds)
        {
            check(true, "W1 upload: skipped -- no cube render target on this renderer");
            return;
        }

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        std::vector<Color> src;
        src.reserve(static_cast<std::size_t>(kCube) * kCube);
        for (int y = 0; y < kCube; ++y)
            for (int x = 0; x < kCube; ++x) src.push_back(PatternTexel(2, x, y, 0));

        bool refused = false;
        try
        {
            cube.SetData(CubeMapFace::PositiveY, 0, nullptr, src.data(), 0,
                         static_cast<int>(src.size()));
        }
        catch (const System::NotSupportedException&) { refused = true; }

        if (!kContract.rtCubeSetData)
        {
            check(refused,
                  "W1 upload: this renderer's RenderTargetCube refuses SetData (REMED-GFX-135), so "
                  "there is no uploaded face to round-trip");
            return;
        }

        Probe p = ProbeFace(cube, 2, 0, nullptr, kCube, kCube, 0, SentinelCD(), src);
        std::size_t mirrored = 0;
        for (int y = 0; y < kCube; ++y)
            for (int x = 0; x < kCube; ++x)
            {
                const Color& got = p.dest[static_cast<std::size_t>(y) * kCube + x];
                if (Same(got, src[static_cast<std::size_t>(kCube - 1 - y) * kCube + x])) ++mirrored;
            }
        const bool sameOrder = !refused && p.exact == p.window;
        const bool flipped   = !refused && mirrored == p.window;
        check(kContract.rtCubeUploadMirrored ? flipped : sameOrder,
              "W1 upload: an uploaded face reads back " +
              std::string(kContract.rtCubeUploadMirrored ? "vertically mirrored"
                                                         : "in the same row order") +
              " -- the measured relationship must match this renderer's declared row convention "
              "[sameOrder=" + std::to_string(p.exact) + "/" + std::to_string(p.window) +
              " mirrored=" + std::to_string(mirrored) + "/" + std::to_string(p.window) +
              " refused=" + std::string(refused ? "1" : "0") + "]");
    }

    void RunDisposalChecks(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargetBinds)
        {
            check(true, "P1 dispose: skipped -- no cube render target on this renderer");
            return;
        }

        RenderTargetCube keep(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderTargetCube dead(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        RenderFace(dev, keep, 1, 0);
        RenderFace(dev, dead, 2, 0);
        dead.Dispose();

        std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelA5());
        bool threwDisposed = false;
        try { dead.GetData(CubeMapFace::PositiveY, got.data(), static_cast<int>(got.size())); }
        catch (const System::ObjectDisposedException&) { threwDisposed = true; }
        catch (...) {}
        std::size_t intact = 0;
        for (const Color& c : got) if (Same(c, SentinelA5())) ++intact;
        check(threwDisposed && intact == got.size(),
              "P1 dispose: GetData on a disposed RenderTargetCube throws ObjectDisposedException "
              "and leaves the destination untouched [threw=" +
              std::string(threwDisposed ? "1" : "0") + " intact=" + std::to_string(intact) + "/" +
              std::to_string(got.size()) + "]");

        check(!ThrowsAnything([&] { dead.Dispose(); }),
              "P2 dispose: a second Dispose() on the same target stays harmless");

        Probe survivor = ProbeFace(keep, 1, 0, nullptr, kCube, kCube, 0, SentinelCD(),
                                   ExpectedFace(1, 0));
        Judge(survivor, kContract.level0, 0, SentinelCD(),
              "P3 dispose: an independent cube target is still fully readable after its sibling was "
              "disposed");
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
            for (int variant = 0; variant < 2; ++variant)
            {
                const std::uint8_t alpha = variant == 0 ? 255 : kCentreAlpha;
                solids_[i + static_cast<std::size_t>(variant) * kPalette.size()] =
                    std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
                        device, 1, 1,
                        std::vector<std::uint8_t>{
                            static_cast<std::uint8_t>(c.getRProperty()),
                            static_cast<std::uint8_t>(c.getGProperty()),
                            static_cast<std::uint8_t>(c.getBProperty()),
                            alpha,
                        }));
            }
        }
    }

    void Draw(const GameTime&) override
    {
        if (done_) { Exit(); return; }
        // Frame 0 is a deliberate warm-up: a renderer that stages resource uploads until the next
        // frame boundary (bgfx copies every createTexture payload at bgfx::frame()) would otherwise
        // have every 1x1 source texture still unuploaded during the very first Draw, and the whole
        // battery -- including the RenderTarget2D orientation reference -- would measure black
        // instead of the pattern. One throwaway frame makes the producer identical on every
        // renderer.
        if (!warmedUp_)
        {
            warmedUp_ = true;
            DrawUniform(kPalette[0], kBBW);
            return;
        }
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        std::printf("REMED-GFX-134 RenderTargetCube GetData contract -- renderer %s\n",
                    kContract.name);

        const std::vector<Color> reference = RunOrientationReference(dev);

        RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        const bool binds = RunBindingBoundary(dev, cube);

        if (binds)
        {
            RunFaceChecks(dev, cube, reference);
            RunRangeChecks(dev, cube);
            RunActiveTargetChecks(dev, cube);
            RunSwitchingChecks(dev, cube);
        }
        else
        {
            // The readback contract still has to hold on a cube target with no renderer at all:
            // it must refuse, with the caller's destination untouched.
            Probe p = ProbeFace(cube, 0, 0, nullptr, kCube, kCube, 3, SentinelCD(),
                                ExpectedFace(0, 0));
            Judge(p, kContract.level0, 3, SentinelCD(),
                  "F1 face: a RenderTargetCube with no renderer resource refuses readback");
        }
        RunRejectionChecks(dev, cube);
        RunUsageChecks(dev);
        RunDepthChecks(dev);
        RunMsaaChecks(dev);
        RunMipChecks(dev);
        RunUploadRoundTrip(dev);
        RunDisposalChecks(dev);

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    RenderTargetCubeGetDataContractTest test;
    test.Run();
    return test.Result();
}
