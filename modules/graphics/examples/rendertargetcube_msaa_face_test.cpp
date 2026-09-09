// SPDX-License-Identifier: MS-PL
// REMED-GFX-141: every face of a MULTISAMPLED RenderTargetCube must own its multisample colour
// state, so a PreserveContents face reloads ITS OWN samples and resolves into ITS OWN cube layer.
//
// REMED-GFX-136 gave RenderTargetCube a real RenderTargetUsage and then measured the one place it
// still could not be honoured: three renderers allocated exactly ONE multisample colour attachment
// per cube target and pointed all six faces at it, because only one face is ever rendered at a time.
//
//   * `EasyGLRenderTargetCubeRenderer::msaaColorRbo_`  -- one multisample renderbuffer.
//   * `VulkanRenderTargetCubeRenderer::msaaColorImage_` -- one single-layer TRANSIENT_ATTACHMENT
//     image, and `GetOrCreateRTRenderPassMsaa` had no LOAD variant at all.
//   * `SdlGpuRenderTargetCubeState::msaaTexture`       -- one single-layer scratch texture that had
//     to be cycled (wiped) on every pass.
//
// That is enough to PRODUCE a face. It leaves nothing per-face to load back: rebinding face A finds
// face B's samples, or a cycled/cleared scratch, in the storage the load action reads. The
// single-sample cube layer still holds the correct A, which is why a full redraw and an immediate
// readback both pass -- the defect only appears when a face is revisited for a PARTIAL update after
// another face has been rendered. D3D11 and D3D12, whose multisampled cube resource is a real
// six-slice array with one per-slice view, preserve exactly.
//
// This file is the oracle for that. It reuses REMED-GFX-134/136's asymmetric five-region face
// producer verbatim -- drawn geometry, never Clear(), a 0/255-only palette that is an exact fixed
// point of sRGB encoding so every comparison is byte-exact -- and adds the shape the shared
// attachment cannot survive:
//
//   1. paint face A completely;
//   2. paint face B completely;
//   3. rebind face A and draw ONLY a small marker;
//   4. read the WHOLE of face A.
//
// Under PreserveContents the answer must be A's own pattern with just that marker replaced, and no
// texel anywhere may carry B's pattern. A renderer reloading the shared attachment passes "the marker
// landed" and fails everything else.
//
// Nothing forces a flush between the producer and the rebind: no Settle(), no Present, no
// intermediate GetData, no wait. REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) made every public
// bind cycle its own native pass, so the whole public sequence is queued first and read once at the
// end. A readback barrier in the middle would hide exactly the resolve-ordering half of this
// finding.
//
// Boundaries this file does NOT cross:
//   * Depth/stencil preservation is REMED-GFX-142's. Here depth only has to not corrupt colour.
//   * Only mip level 0 is asserted (REMED-GFX-138/139 own cube mip storage).
//   * Cube SAMPLING orientation is untouched; the resolved bytes GetData returns are the subject.
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
    constexpr int kBBW    = 64;  ///< Backbuffer width.
    constexpr int kBBH    = 64;  ///< Backbuffer height.
    constexpr int kCube   = 8;   ///< Cube face edge.
    constexpr int kCorner = 3;   ///< Edge of each corner marker in the producer pattern.
    constexpr int kMark   = 2;   ///< Edge of the small partial-update marker.
    constexpr int kMsaaRequest = 4;  ///< Multisample count every MSAA check requests.

    /**
     * @brief What a rendered cube face's public readback must do on this renderer.
     *
     * `Exact` -- GetData returns the resolved face byte for byte.
     * `Unsupported` -- GetData raises System::NotSupportedException with the caller's destination
     * untouched (REMED-GFX-127/130's contract).
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
        /// CreateRenderTargetCube() returns a real object AND a face binds. Everything below is
        /// skipped when false.
        bool    cubeTargets;
        /// RenderTargetCube::GetData at level 0 on a SINGLE-SAMPLE target.
        Support readback;
        /**
         * A `multiSampleCount = 4` RenderTargetCube really reports an applied count > 0 here.
         * False is a declared capability boundary, not a defect: WebGPU ignores the parameter
         * (WEBGPU-114's own scope), D3D9's cube target allocates no multisampling at all, and
         * Software/SDL_Renderer/Canvas/DIRECTX3 have no cube target to multisample.
         */
        bool    msaaEngages;
        /**
         * GetData on a MULTISAMPLED cube target, and therefore this file's whole claim about it:
         * where this is `Exact`, every multisampled preservation check below REQUIRES exact
         * content -- that is REMED-GFX-141's post-fix contract, stated as one falsifiable value per
         * renderer rather than per check. `Unsupported` remains the honest no-content outcome on
         * other routes.
         */
        Support msaaReadback;
        /**
         * Ask for a multisampled BACKBUFFER. Only true where that is the sole way a cube target can
         * engage MSAA at all: `VulkanRenderTargetCubeRenderer` gates its MSAA resources on the
         * renderer's own `sampleCount_`, so `multiSampleCount = 4` reports 0 unless the device
         * itself was created multisampled.
         */
        bool    preferMultiSampling;
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef.
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless rasterizes nothing, so it owns no face colour at all and its readback stays
    // REMED-GFX-130's deterministic refusal. The whole public sequence must still be legal.
    constexpr Contract kContract{"HEADLESS", true, Support::Unsupported, true,
                                 Support::Unsupported, false, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr Contract kContract{"SOFTWARE", false, Support::Unsupported, false,
                                 Support::Unsupported, false, false};
#elif defined(CNA_RENDERER_EASYGL)
    // Pre-fix: ONE multisample renderbuffer for the whole cube. Post-fix: six, one per face,
    // re-attached to the render FBO on every bind.
    constexpr Contract kContract{"EASYGL", true, Support::Exact, true,
                                 Support::Exact, false, false};
#elif defined(CNA_RENDERER_BGFX)
    // REMED-GFX-138 makes the resolved readback real. REMED-GFX-195 closes the separately exposed
    // Bgfx face-aliasing defect, so the same direct oracle now requires exact per-face contents.
    constexpr Contract kContract{"BGFX", true, Support::Exact, true,
                                 Support::Exact, false, false};
#elif defined(CNA_RENDERER_VULKAN)
    // Pre-fix: ONE single-layer TRANSIENT_ATTACHMENT image AND no LOAD variant of the MSAA render
    // pass. Post-fix: a six-layer multisampled image with one per-layer view per face, plus a
    // LOAD/STORE MSAA render pass for preserving targets.
    constexpr Contract kContract{"VULKAN", true, Support::Exact, true,
                                 Support::Exact, true, false};
#elif defined(CNA_RENDERER_WEBGPU)
    // `msaaEngages` TRUE as of WEBGPU-165: CreateRenderTargetCube used to ignore multiSampleCount
    // outright -- every pipeline baked one renderer-global sample count until WEBGPU-197, so a cube
    // that asked for its own would have been pipeline-incompatible -- and there was no multisampled
    // cube storage to share or isolate. It now allocates a per-face multisampled attachment at its
    // own applied count.
    constexpr Contract kContract{"WEBGPU", true, Support::Exact, true,
                                 Support::Exact, false, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // Pre-fix: ONE single-layer scratch texture that had to be cycled on every pass. Post-fix: six
    // single-layer multisampled textures (SDL_gpu forbids sample_count > 1 on an array texture), no
    // cycling, LOAD + RESOLVE_AND_STORE for a preserving target.
    constexpr Contract kContract{"SDL_GPU", true, Support::Exact, true,
                                 Support::Exact, false, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr Contract kContract{"SDL_RENDERER", false, Support::Unsupported, false,
                                 Support::Unsupported, false, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", false, Support::Unsupported, false,
                                 Support::Unsupported, false, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", false, Support::Unsupported, false,
                                 Support::Unsupported, false, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    // `msaaEngages` false: D3D9RenderTargetCubeRenderer::Recreate() allocates a plain
    // D3DUSAGE_RENDERTARGET cube texture and GetMultiSampleCount() reports 0 by construction.
    constexpr Contract kContract{"DIRECTX9", true, Support::Exact, false,
                                 Support::Exact, false, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    // Already correct: a six-slice DXGI_SAMPLE_DESC array with one Texture2DMSArray RTV per slice.
    constexpr Contract kContract{"DIRECTX11", true, Support::Exact, true,
                                 Support::Exact, false, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    // Already correct: DepthOrArraySize = 6 at the requested sample count, one
    // D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY view per face.
    constexpr Contract kContract{"DIRECTX12", true, Support::Exact, true,
                                 Support::Exact, false, false};
#elif defined(CNA_RENDERER_SOKOL)
    // `msaaEngages` false: sokol_gfx's own validation layer hard-rejects a CUBE image with
    // sample_count > 1 (VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE) -- a permanent API boundary
    // this renderer cannot cross, unlike RenderTarget2D's real MSAA support. `readback` Exact as of
    // plans/plan_sokol.md SOKOL-38: SokolRenderTargetCubeRenderer::GetData now round-trips a single-sample
    // face's content via a throwaway GL FBO around the raw GL texture sg_gl_query_image_info()
    // exposes; `msaaReadback` stays Unsupported since a multisampled cube can never exist here at
    // all (nothing to read back).
    constexpr Contract kContract{"SOKOL", true, Support::Exact, false,
                                 Support::Unsupported, false, false};
#elif defined(CNA_RENDERER_LLGL)
    // LLGL allocates one anonymous multisampled colour attachment per face and resolves every
    // attachment into the corresponding layer of the shared cube texture. GetData reads that
    // resolved layer, so both single-sample and multisampled face contents are exact.
    constexpr Contract kContract{"LLGL", true, Support::Exact, true,
                                 Support::Exact, false, false};
#else
#error "REMED-GFX-141: this renderer has no declared multisampled RenderTargetCube contract."
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

    /// Colour of pattern slot @p slot on @p face for producer round @p variant. Rotating by the
    /// face index makes every slot of every face differ from the same slot of every other face, so
    /// a stale face changes the colour at EVERY asserted texel.
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

    /// Where each face's partial update lands. Distinct per face, so a marker written into the
    /// wrong face is visible as a missing marker AND as an unexpected one.
    constexpr std::array<std::array<int, 2>, 6> kMarkSpot = {{
        {{0, 0}}, {{kCube - kMark, 0}}, {{0, kCube - kMark}},
        {{kCube - kMark, kCube - kMark}}, {{kCorner, 0}}, {{0, kCorner}},
    }};

    /// The marker colour for @p face -- never that face's base fill, so "the marker landed" and
    /// "nothing happened" are always distinguishable.
    Color MarkColour(int face) { return kPalette[static_cast<std::size_t>((face + 3) % 6)]; }

    /// Producer order, revisit order and read order are deliberately three different permutations.
    constexpr std::array<int, 6> kProduceOrder = {0, 1, 2, 3, 4, 5};
    constexpr std::array<int, 6> kRevisitOrder = {3, 0, 5, 1, 4, 2};
    constexpr std::array<int, 6> kReadOrder    = {4, 2, 0, 5, 1, 3};

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

class RenderTargetCubeMsaaFaceTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::array<std::unique_ptr<Texture2D>, 6> solids_;
    bool done_ = false;
    bool warmedUp_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;
    /// Applied sample count of the last multisampled cube target created, for message text.
    int applied_ = 0;

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

    /// A bind/paint/unbind cycle that draws ONLY that face's kMark x kMark marker.
    void MarkFace(GraphicsDevice& dev, RenderTargetCube& cube, int face)
    {
        dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
        BeginQuads();
        DrawQuad(MarkColour(face), kMarkSpot[static_cast<std::size_t>(face)][0],
                 kMarkSpot[static_cast<std::size_t>(face)][1], kMark, kMark);
        spriteBatch_->End();
        dev.SetRenderTargets({});
    }

    // ---------------------------------------------------------------------
    // Probes and expectations
    // ---------------------------------------------------------------------

    static Probe ProbeWholeFace(const TextureCube& cube, int face, const Color& sentinel,
                                const std::vector<Color>& expected)
    {
        Probe p;
        p.window = static_cast<std::size_t>(kCube) * kCube;
        p.dest.assign(p.window, sentinel);
        try
        {
            cube.GetData(static_cast<CubeMapFace>(face), 0, nullptr, p.dest.data(), 0,
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

    /// Judges one probe against a required readback contract.
    void Judge(const Probe& p, Support required, const std::string& label)
    {
        const std::string facts =
            " [threwNotSupported=" + std::string(p.threwNotSupported ? "1" : "0") +
            " threwOther=" + (p.threwSomethingElse ? ("1:" + p.otherWhat) : "0") +
            " exact=" + std::to_string(p.exact) + "/" + std::to_string(p.window) +
            " sentinelSurvivors=" + std::to_string(p.sentinelSurvivors) + p.firstMismatch + "]";

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

    /// The producer pattern of @p face with that face's own marker stamped on it.
    static std::vector<Color> ExpectedFaceMarked(int face, int variant)
    {
        std::vector<Color> e = ExpectedFace(face, variant);
        const int mx = kMarkSpot[static_cast<std::size_t>(face)][0];
        const int my = kMarkSpot[static_cast<std::size_t>(face)][1];
        for (int dy = 0; dy < kMark; ++dy)
            for (int dx = 0; dx < kMark; ++dx)
                e[static_cast<std::size_t>(my + dy) * kCube + (mx + dx)] = MarkColour(face);
        return e;
    }

    /// The whole face at the shared layer's discard colour except for that face's own marker.
    static std::vector<Color> ExpectedDiscardedMarked(int face)
    {
        std::vector<Color> e(static_cast<std::size_t>(kCube) * kCube, DiscardColour());
        const int mx = kMarkSpot[static_cast<std::size_t>(face)][0];
        const int my = kMarkSpot[static_cast<std::size_t>(face)][1];
        for (int dy = 0; dy < kMark; ++dy)
            for (int dx = 0; dx < kMark; ++dx)
                e[static_cast<std::size_t>(my + dy) * kCube + (mx + dx)] = MarkColour(face);
        return e;
    }

    /**
     * @brief How many texels of @p got carry face @p otherFace's producer pattern WHERE THE
     *        EXPECTED ANSWER IS SOMETHING ELSE.
     *
     * The direct signature of a multisample attachment shared between faces. Texels where the two
     * faces happen to agree are excluded, so this counts only real contamination and is exactly 0
     * on a correct renderer.
     */
    static std::size_t CountStaleFrom(const std::vector<Color>& got,
                                      const std::vector<Color>& expected,
                                      int otherFace, int variant)
    {
        const std::vector<Color> other = ExpectedFace(otherFace, variant);
        std::size_t n = 0;
        for (std::size_t i = 0; i < got.size() && i < other.size() && i < expected.size(); ++i)
            if (!Same(other[i], expected[i]) && Same(got[i], other[i])) ++n;
        return n;
    }

    std::unique_ptr<RenderTargetCube> MakeCube(GraphicsDevice& dev, RenderTargetUsage usage,
                                               int samples, DepthFormat depth = DepthFormat::None)
    {
        return std::make_unique<RenderTargetCube>(dev, kCube, false, SurfaceFormat::Color, depth,
                                                  samples, usage);
    }

    // =====================================================================
    // Sections
    // =====================================================================

    /// M1 -- nothing below means anything unless the requested sample count really applied.
    bool RunCapability(GraphicsDevice& dev)
    {
        auto msaa = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        applied_ = msaa->getMultiSampleCountProperty();
        check(kContract.msaaEngages == (applied_ > 0),
              "M1 capability: a multiSampleCount=" + std::to_string(kMsaaRequest) +
              " RenderTargetCube applies exactly this renderer's declared capability (applied " +
              std::to_string(applied_) + ", declared " +
              (kContract.msaaEngages ? "multisampled" : "single-sample") + ")");
        return applied_ > 0;
    }

    /// The readback contract that applies to a multisampled target on this renderer.
    static Support MsaaRequired()
    {
        return kContract.msaaReadback;
    }

    /**
     * @brief The canonical REMED-GFX-141 reproduction: A, then B, then a partial update of A.
     *
     * Queued with nothing in between -- no readback, no Present, no flush -- and read only at the
     * end. Both faces are checked: A must be its own pattern plus its own marker, and B must be
     * untouched, so a fix that merely stopped A from reading B (by wiping something) cannot pass.
     */
    void RunCanonical(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        RenderFace(dev, *cube, 0, 0);
        RenderFace(dev, *cube, 1, 0);
        MarkFace(dev, *cube, 0);

        const std::vector<Color> expected = ExpectedFaceMarked(0, 0);
        Probe a = ProbeWholeFace(*cube, 0, SentinelCD(), expected);
        const std::size_t stale = CountStaleFrom(a.dest, expected, 1, 0);
        Judge(a, MsaaRequired(),
              "F1 canonical: an applied-" + std::to_string(applied_) +
              "x PreserveContents cube keeps face 0's OWN resolved pattern under a partial update "
              "issued after face 1 was rendered");
        if (MsaaRequired() == Support::Exact)
            check(stale == 0,
                  "F1b canonical: not one texel of face 0 came back holding face 1's pattern "
                  "[contaminated texels = " + std::to_string(stale) + "]");
        else
            skip("F1b canonical: skipped -- no multisampled cube readback on this renderer");

        Probe b = ProbeWholeFace(*cube, 1, SentinelA5(), ExpectedFace(1, 0));
        Judge(b, MsaaRequired(),
              "F2 canonical: face 1 is untouched by face 0's later partial update");
    }

    /**
     * @brief F3 -- all six faces, produced, revisited and read in three different orders.
     *
     * The single decisive check for face identity: with one shared attachment, five of the six
     * faces come back holding somebody else's samples.
     */
    void RunAllSixFaces(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        for (int face : kProduceOrder) RenderFace(dev, *cube, face, 0);
        for (int face : kRevisitOrder) MarkFace(dev, *cube, face);

        std::size_t exactFaces = 0;
        std::string detail;
        for (int face : kReadOrder)
        {
            const std::vector<Color> expected = ExpectedFaceMarked(face, 0);
            Probe p = ProbeWholeFace(*cube, face, SentinelCD(), expected);
            Judge(p, MsaaRequired(),
                  "F3." + std::to_string(face) + " six faces: face " + std::to_string(face) +
                  " survives its own partial update after all six were rendered and all six "
                  "revisited");
            if (p.exact == p.window) ++exactFaces;
            else if (MsaaRequired() == Support::Exact)
            {
                for (int other = 0; other < 6; ++other)
                    if (other != face)
                    {
                        const std::size_t n = CountStaleFrom(p.dest, expected, other, 0);
                        if (n > 0)
                            detail += " face" + std::to_string(face) + "<-" + std::to_string(other) +
                                      "=" + std::to_string(n);
                    }
            }
        }
        if (MsaaRequired() == Support::Exact)
            check(exactFaces == 6,
                  "F4 six faces: all six faces are simultaneously live and independent [exact "
                  "faces = " + std::to_string(exactFaces) + "/6" + detail + "]");
        else
            skip("F4 six faces: skipped -- no multisampled cube readback on this renderer");
    }

    /// F5 -- the sample-count-one control. The same sequence at samples=0 must be exact, and stays
    /// exact whatever REMED-GFX-141 changed about the multisample path.
    void RunSingleSampleControl(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, 0);
        check(cube->getMultiSampleCountProperty() == 0,
              "F5a single-sample: a multiSampleCount=0 cube target reports 0 [" +
              std::to_string(cube->getMultiSampleCountProperty()) + "]");
        for (int face : kProduceOrder) RenderFace(dev, *cube, face, 0);
        for (int face : kRevisitOrder) MarkFace(dev, *cube, face);
        for (int face : kReadOrder)
        {
            Probe p = ProbeWholeFace(*cube, face, SentinelA5(), ExpectedFaceMarked(face, 0));
            Judge(p, kContract.readback,
                  "F5." + std::to_string(face) + " single-sample: face " + std::to_string(face) +
                  " of a SINGLE-SAMPLE PreserveContents cube is unchanged by this task");
        }
    }

    /// F6 -- DiscardContents must NOT gain a preservation guarantee from the fix.
    void RunDiscard(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::DiscardContents, kMsaaRequest);
        RenderFace(dev, *cube, 2, 0);
        RenderFace(dev, *cube, 5, 0);
        MarkFace(dev, *cube, 2);
        Probe p = ProbeWholeFace(*cube, 2, SentinelCD(), ExpectedDiscardedMarked(2));
        Judge(p, MsaaRequired(),
              "F6 discard: an applied-" + std::to_string(applied_) +
              "x DiscardContents cube face reads as the shared layer's discard colour outside the "
              "new partial draw -- preservation must NOT leak into it");
    }

    /// F7 -- PlatformContents follows FNA's `usage != DiscardContents` rule, i.e. it preserves.
    void RunPlatform(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::PlatformContents, kMsaaRequest);
        RenderFace(dev, *cube, 4, 0);
        RenderFace(dev, *cube, 1, 0);
        MarkFace(dev, *cube, 4);
        Probe p = ProbeWholeFace(*cube, 4, SentinelA5(), ExpectedFaceMarked(4, 0));
        Judge(p, MsaaRequired(),
              "F7 platform: an applied-" + std::to_string(applied_) +
              "x PlatformContents cube face preserves exactly like PreserveContents");
    }

    /// F8 -- one face only. The degenerate case a shared attachment also passes, kept so the fix
    /// cannot regress it.
    void RunOneFaceOnly(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        RenderFace(dev, *cube, 3, 0);
        MarkFace(dev, *cube, 3);
        Probe p = ProbeWholeFace(*cube, 3, SentinelCD(), ExpectedFaceMarked(3, 0));
        Judge(p, MsaaRequired(),
              "F8 one face: a multisampled cube that only ever binds ONE face preserves it");
    }

    /// F9 -- two independent multisampled cube targets interleaved. Neither may see the other's
    /// samples; a per-target allocation that accidentally became per-device would fail here.
    void RunTwoCubes(GraphicsDevice& dev)
    {
        auto a = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        auto b = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        RenderFace(dev, *a, 0, 0);
        RenderFace(dev, *b, 0, 1);
        RenderFace(dev, *a, 2, 0);
        RenderFace(dev, *b, 2, 1);
        MarkFace(dev, *a, 0);
        MarkFace(dev, *b, 0);

        Probe pa = ProbeWholeFace(*a, 0, SentinelCD(), ExpectedFaceMarked(0, 0));
        Judge(pa, MsaaRequired(),
              "F9a two cubes: cube A's face 0 keeps its own content across an interleaved second "
              "multisampled cube target");
        Probe pb = ProbeWholeFace(*b, 0, SentinelA5(), ExpectedFaceMarked(0, 1));
        Judge(pb, MsaaRequired(),
              "F9b two cubes: cube B's face 0 keeps its own content, which is a DIFFERENT pattern "
              "variant from cube A's");
    }

    /// F10 -- cube -> RenderTarget2D -> cube, and F11 -- cube -> backbuffer -> cube. Neither
    /// intervening target may disturb a multisampled cube face.
    void RunRoundTrips(GraphicsDevice& dev)
    {
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
            RenderTarget2D flat(dev, kCube, kCube, false, SurfaceFormat::Color, DepthFormat::None,
                                0, RenderTargetUsage::DiscardContents);
            RenderFace(dev, *cube, 1, 0);
            dev.SetRenderTarget(&flat);
            BeginQuads();
            DrawQuad(kPalette[2], 0, 0, kCube, kCube);
            spriteBatch_->End();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            MarkFace(dev, *cube, 1);
            Probe p = ProbeWholeFace(*cube, 1, SentinelCD(), ExpectedFaceMarked(1, 0));
            Judge(p, MsaaRequired(),
                  "F10 round trip: cube -> RenderTarget2D -> cube keeps the multisampled face");
        }
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
            RenderFace(dev, *cube, 5, 0);
            BeginQuads();
            DrawQuad(kPalette[1], 0, 0, kBBW, kBBH);
            spriteBatch_->End();
            MarkFace(dev, *cube, 5);
            Probe p = ProbeWholeFace(*cube, 5, SentinelA5(), ExpectedFaceMarked(5, 0));
            Judge(p, MsaaRequired(),
                  "F11 round trip: cube -> backbuffer -> cube keeps the multisampled face");
        }
    }

    /// F12 -- repeated bind cycles of the same face. Each cycle accumulates onto the previous one,
    /// so a per-bind reallocation or a per-bind wipe shows up immediately.
    void RunRepeatedCycles(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        RenderFace(dev, *cube, 0, 0);
        RenderFace(dev, *cube, 3, 0);
        // Four more cycles of face 0, each drawing one 1x1 texel of the same marker colour, so the
        // final expectation is face 0's pattern plus a kMark x kMark block built one texel at a
        // time. Any cycle that loses the previous cycles' output fails.
        const int mx = kMarkSpot[0][0];
        const int my = kMarkSpot[0][1];
        for (int i = 0; i < 4; ++i)
        {
            dev.SetRenderTarget(cube.get(), static_cast<CubeMapFace>(0));
            BeginQuads();
            DrawQuad(MarkColour(0), mx + (i % kMark), my + (i / kMark), 1, 1);
            spriteBatch_->End();
            dev.SetRenderTargets({});
        }
        Probe p = ProbeWholeFace(*cube, 0, SentinelCD(), ExpectedFaceMarked(0, 0));
        Judge(p, MsaaRequired(),
              "F12 repeated cycles: four consecutive PreserveContents bind cycles of one "
              "multisampled face each accumulate onto the previous one");
    }

    /// F13 -- disposal and recreation. A fresh target after a disposed one must not inherit
    /// anything, and must itself preserve.
    void RunDisposalAndRecreation(GraphicsDevice& dev)
    {
        {
            auto doomed = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
            RenderFace(dev, *doomed, 2, 1);
            RenderFace(dev, *doomed, 4, 1);
            doomed->Dispose();
        }
        auto fresh = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        RenderFace(dev, *fresh, 2, 0);
        RenderFace(dev, *fresh, 4, 0);
        MarkFace(dev, *fresh, 2);
        Probe p = ProbeWholeFace(*fresh, 2, SentinelA5(), ExpectedFaceMarked(2, 0));
        Judge(p, MsaaRequired(),
              "F13 recreation: a multisampled cube target created after another was disposed "
              "preserves its own faces and inherits nothing");
    }

    /// F14/F15/F16 -- every public depth format must leave the preserved COLOUR independent.
    /// REMED-GFX-142 owns
    /// depth preservation itself; nothing is claimed about depth here.
    void RunDepthBackedControls(GraphicsDevice& dev)
    {
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest,
                                 DepthFormat::Depth16);
            RenderFace(dev, *cube, 2, 0);
            RenderFace(dev, *cube, 4, 0);
            MarkFace(dev, *cube, 2);
            Probe p = ProbeWholeFace(*cube, 2, SentinelA5(), ExpectedFaceMarked(2, 0));
            Judge(p, MsaaRequired(),
                  "F14 depth: a Depth16-backed multisampled cube preserves its COLOUR across a "
                  "face switch and a partial update");
        }
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest,
                                 DepthFormat::Depth24);
            RenderFace(dev, *cube, 0, 0);
            RenderFace(dev, *cube, 5, 0);
            MarkFace(dev, *cube, 0);
            Probe p = ProbeWholeFace(*cube, 0, SentinelCD(), ExpectedFaceMarked(0, 0));
            Judge(p, MsaaRequired(),
                  "F15 depth: a Depth24-backed multisampled cube preserves its COLOUR across a "
                  "face switch and a partial update");
        }
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest,
                                 DepthFormat::Depth24Stencil8);
            RenderFace(dev, *cube, 3, 0);
            RenderFace(dev, *cube, 1, 0);
            MarkFace(dev, *cube, 3);
            Probe p = ProbeWholeFace(*cube, 3, SentinelA5(), ExpectedFaceMarked(3, 0));
            Judge(p, MsaaRequired(),
                  "F16 depth: a Depth24Stencil8-backed multisampled cube preserves its COLOUR the "
                  "same way");
        }
    }

    /// F17 -- repeated readback of the same face is stable, and reading one face neither consumes
    /// nor disturbs another. The resolve must be idempotent, not a one-shot drain.
    void RunReadbackStability(GraphicsDevice& dev)
    {
        auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents, kMsaaRequest);
        RenderFace(dev, *cube, 1, 0);
        RenderFace(dev, *cube, 4, 0);
        MarkFace(dev, *cube, 1);
        Probe first  = ProbeWholeFace(*cube, 1, SentinelCD(), ExpectedFaceMarked(1, 0));
        Probe other  = ProbeWholeFace(*cube, 4, SentinelA5(), ExpectedFace(4, 0));
        Probe second = ProbeWholeFace(*cube, 1, SentinelA5(), ExpectedFaceMarked(1, 0));
        Judge(first, MsaaRequired(), "F17a stability: the first read of face 1 is exact");
        Judge(other, MsaaRequired(), "F17b stability: reading face 4 in between is exact");
        Judge(second, MsaaRequired(),
              "F17c stability: re-reading face 1 after face 4 returns the identical bytes");
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (kContract.preferMultiSampling)
            gdm_->setPreferMultiSamplingProperty(true);
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
        // Frame 0 is a deliberate warm-up: bgfx copies every createTexture payload at bgfx::frame(),
        // so without it the 1x1 producer sources would still be unuploaded during the very first
        // Draw and the whole battery would measure black. Same warm-up REMED-GFX-134/136 use.
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
        std::printf("REMED-GFX-141 multisampled RenderTargetCube face isolation -- renderer %s\n",
                    kContract.name);

        if (!kContract.cubeTargets)
        {
            // No cube target at all: the only claim is that asking for one multisampled is
            // deterministic, whatever the answer, and that nothing below is silently skipped as if
            // it had passed.
            std::string what;
            bool created = false;
            try
            {
                RenderTargetCube cube(dev, kCube, false, SurfaceFormat::Color, DepthFormat::None,
                                      kMsaaRequest, RenderTargetUsage::PreserveContents);
                dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
                dev.SetRenderTargets({});
                created = true;
            }
            catch (const std::exception& e) { what = e.what(); }
            catch (...) { what = "non-std exception"; }
            check(!created && !what.empty(),
                  "E1 unsupported: this renderer refuses a multisampled RenderTargetCube "
                  "deterministically [" +
                  (created ? std::string("accepted and bound it") : what) + "]");
            std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
            result_ = (passCount_ == totalCount_) ? 0 : 1;
            Exit();
            return;
        }

        const bool msaa = RunCapability(dev);

        // The single-sample control runs everywhere a cube target exists, including where MSAA
        // never engages -- it is what proves this task changed nothing outside the MSAA path.
        RunSingleSampleControl(dev);

        if (!msaa)
        {
            skip("F1-F17: skipped -- a multisampled RenderTargetCube does not engage MSAA here "
                 "(declared capability boundary, see M1)");
        }
        else
        {
            RunCanonical(dev);
            RunAllSixFaces(dev);
            RunDiscard(dev);
            RunPlatform(dev);
            RunOneFaceOnly(dev);
            RunTwoCubes(dev);
            RunRoundTrips(dev);
            RunRepeatedCycles(dev);
            RunDisposalAndRecreation(dev);
            RunDepthBackedControls(dev);
            RunReadbackStability(dev);
        }

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    RenderTargetCubeMsaaFaceTest test;
    test.Run();
    return test.Result();
}
