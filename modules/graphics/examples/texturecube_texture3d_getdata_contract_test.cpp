// SPDX-License-Identifier: MS-PL
// REMED-GFX-130: every public TextureCube::GetData and Texture3D::GetData call must have exactly
// ONE honest outcome -- it returns the resource's real content, or it rejects the request
// deterministically. It must never fabricate content out of the shared layer's own scratch memory.
//
// This is REMED-GFX-127's finding on the two interfaces that finding deliberately did not cover.
// The cube defect (shared, renderer-neutral, in TextureCube::GetData itself):
//
//     std::vector<uint8_t> rgba(elementCount * 4);            // zero-initialized HERE
//     renderer_->GetData(face, level, x, y, w, h, rgba.data(), size);  // interface default = no-op
//     rgbaToColors(rgba, data, startIndex, elementCount);      // converted for the caller REGARDLESS
//
// and the volume defect, in Texture3D::GetData, byte for byte the same shape:
//
//     std::vector<uint8_t> rgba(elementCount * 4);
//     renderer_->GetData(level, left, top, front, w, h, depth, rgba.data(), size);
//     rgbaToColors(rgba, data, startIndex, elementCount);
//
// A renderer that overrides neither therefore does not "leave the caller's buffer untouched": it
// returns a complete, confidently-wrong, uniformly transparent-black face/volume. Headless goes one
// step further and actively `std::fill_n`s the caller's destination with zeros while its SetData
// stores nothing at all -- the same fabrication reached through a different renderer behaviour.
//
// That result is strictly worse than an empty one, because
//   * the obvious oracle "did GetData overwrite my sentinel destination?" PASSES on it, and
//   * any assertion whose expected content happens to be transparent black also PASSES on it.
// Both facts are asserted below (checks Z1/Z2) so this file records WHY sentinel mutation is not a
// valid success oracle, rather than merely avoiding it.
//
// What every renderer must satisfy after the fix:
//   * a TextureCube/Texture3D populated by SetData reads back EXACTLY -- per face, per mip, for an
//     arbitrary rectangle/box, at an arbitrary destination offset, over repeated calls;
//   * a resource this renderer cannot read back throws a deterministic System::NotSupportedException
//     WITHOUT having touched one byte of the caller's destination;
//   * nothing in between: no fabricated face, no fabricated volume, no partially written
//     destination, no silent success.
//
// Each renderer's capability is declared here explicitly (kContract) rather than inferred at
// runtime, so "this renderer cannot read a cube face back" is a reviewed claim this test enforces,
// not an escape hatch that lets any result pass.
//
// The uploaded pattern deliberately carries a per-face tag in R, an x ramp in G, a y ramp in B and
// a non-trivial alpha, plus one pure opaque red/green/blue/yellow/magenta/cyan signature texel per
// face, so a fabricated (0,0,0,0) face, a wrong face, a wrong origin, a vertically flipped face, a
// channel-swapped readback and an untouched destination all fail decisively and distinguishably.
// The volume pattern varies along X, Y *and* Z, so a flattened, zeroed, slice-duplicated,
// slice-reversed or wrong-pitch result cannot pass either.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

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
    constexpr int kBBH  = 32;  ///< Backbuffer height.
    constexpr int kCube = 8;   ///< Cube face edge at mip 0 (mip 1 = 4, mip 2 = 2, mip 3 = 1).
    constexpr int kVolW = 4;   ///< Texture3D width  at mip 0 (mip 1 = 2).
    constexpr int kVolH = 4;   ///< Texture3D height at mip 0 (mip 1 = 2).
    constexpr int kVolD = 4;   ///< Texture3D depth  at mip 0 (mip 1 = 2).

    /**
     * @brief What one public readback path on this renderer is required to do.
     *
     * `Exact` -- GetData must return the uploaded content byte for byte.
     * `Unsupported` -- GetData must throw System::NotSupportedException and leave the caller's
     * destination byte-for-byte untouched. Reserved for a resource/level this renderer genuinely
     * cannot read back, never for one it merely has not been checked on.
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
        bool    cubeHasRenderer;    ///< IGraphicsRenderer::CreateTextureCube() returns a real object.
        Support cubeLevel0;        ///< TextureCube::GetData at mip 0.
        Support cubeMip;           ///< TextureCube::GetData at mip > 0.
        bool    volumeConstructs;  ///< Texture3D's constructor succeeds on this renderer/profile.
        Support volumeLevel0;      ///< Texture3D::GetData at mip 0.
        Support volumeMip;         ///< Texture3D::GetData at mip > 0.
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef (D3D9's volume-texture gate).
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless deliberately executes no rasterization and stores no pixel data: it records API
    // usage and resource state for validation/tracing. Its cube SetData is a trace entry, not a
    // write, so there is no content it could honestly return. Texture3D is refused at construction
    // (REMED-CONTENT-004: it reports no GraphicsCapability::Texture3D).
    constexpr Contract kContract{"HEADLESS", true, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    // SOFTWARE-82 gives this renderer real 6-face RGBA8 cube storage. REMED-GFX-135 extended it to
    // every mip level TextureCube declares (previously only level 0 was allocated, so this entry
    // read `Support::Unsupported` for mips), which is what makes a mipmapped cube's LevelCount a
    // claim the storage can actually back. Texture3D is an explicit documented v1 scope boundary,
    // refused at construction.
    constexpr Contract kContract{"SOFTWARE", true, Support::Exact, Support::Exact,
                                 false, Support::Unsupported, Support::Unsupported, false};
#elif defined(CNA_RENDERER_EASYGL)
    constexpr Contract kContract{"EASYGL", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_BGFX)
    constexpr Contract kContract{"BGFX", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_VULKAN)
    constexpr Contract kContract{"VULKAN", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr Contract kContract{"WEBGPU", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr Contract kContract{"SDL_GPU", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_SKIA)
    // Skia emulates transfer/readback in bounded CPU RGBA8 storage. This does not advertise cube
    // or volume shader sampling, custom effects, or the 3D pipeline.
    constexpr Contract kContract{"SKIA", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    // 2D-only by design: CreateTextureCube()/CreateTexture3D() keep IGraphicsRenderer's own
    // nullptr-returning defaults, so a TextureCube here has no storage at all and Texture3D is
    // refused at construction (this renderer reports no GraphicsCapability::Texture3D).
    constexpr Contract kContract{"SDL_RENDERER", false, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", false, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", false, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    // plans/plan_dx9.md D9-100: GraphicsProfile.Reach does not support volume textures at all, so the
    // Texture3D half of this file needs a HiDef device to have anything to measure.
    constexpr Contract kContract{"DIRECTX9", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr Contract kContract{"DIRECTX12", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#elif defined(CNA_RENDERER_LLGL)
    constexpr Contract kContract{"LLGL", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact, false};
#else
#error "REMED-GFX-130: this renderer has no declared TextureCube/Texture3D GetData contract."
#endif

    /// Two unrelated destination pre-fills. Neither equals any pattern colour, so "still the
    /// sentinel" and "a real texel" can never be confused for one another.
    Color SentinelCD() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }
    Color SentinelA5() { return Color(0xA5, 0xA5, 0xA5, 0xA5); }

    /// The fabricated content the pre-fix shared layer produces from a no-op renderer.
    Color Fabricated() { return Color(0, 0, 0, 0); }

    /// One pure, fully opaque signature colour per CubeMapFace, written at that face's (0,0).
    Color PureFaceColour(int face)
    {
        switch (face)
        {
        case 0:  return Color(255, 0, 0, 255);      // +X red
        case 1:  return Color(0, 255, 0, 255);      // -X green
        case 2:  return Color(0, 0, 255, 255);      // +Y blue
        case 3:  return Color(255, 255, 0, 255);    // -Y yellow
        case 4:  return Color(255, 0, 255, 255);    // +Z magenta
        default: return Color(0, 255, 255, 255);    // -Z cyan
        }
    }

    /**
     * @brief The texel this test uploads to (@p face, @p level, @p x, @p y).
     *
     * R identifies the face AND the mip level, G ramps with x, B ramps with y and A is neither 0
     * nor 255. Reading the wrong face, the wrong mip, the wrong origin, a vertically flipped face
     * or a channel-swapped copy therefore all produce a different colour at the same coordinate.
     */
    Color CubeTexel(int face, int level, int x, int y)
    {
        if (level == 0 && x == 0 && y == 0) return PureFaceColour(face);
        const int r = 11 + face * 37 + level * 3;
        const int g = 9 + x * 21;
        const int b = 13 + y * 19;
        const int a = 201 - x * 5 - y * 7;
        return Color(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                     static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a));
    }

    /**
     * @brief The voxel this test uploads to (@p level, @p x, @p y, @p z).
     *
     * Every axis moves an independent channel, so a duplicated slice, a reversed slice order, a
     * flattened volume and a row-pitch/slice-pitch mix-up are each individually detectable.
     */
    Color VolumeVoxel(int level, int x, int y, int z)
    {
        const int r = 23 + z * 51 + level * 7;
        const int g = 17 + x * 43;
        const int b = 29 + y * 39;
        const int a = 197 - x * 3 - y * 5 - z * 11;
        return Color(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                     static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a));
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
        int d = base >> level;
        return d < 1 ? 1 : d;
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
    };
}

class CubeVolumeGetDataContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
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

    // ---------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------

    /**
     * @brief Uploads the full pattern for one cube mip level, one face at a time.
     *
     * REMED-GFX-135 gave SetData the same two-outcome contract GetData has, so on a renderer with no
     * cube storage this fixture now raises System::NotSupportedException instead of quietly doing
     * nothing. That rejection is the write side's own subject and is asserted in
     * texturecube_texture3d_setdata_contract_test.cpp; here it only means "there is nothing to read
     * back", which is exactly what the Unsupported contract below already expects, so it is
     * absorbed rather than allowed to abort this file.
     */
    static void UploadCubeLevel(TextureCube& cube, int level)
    {
        const int dim = MipDim(kCube, level);
        for (int face = 0; face < 6; ++face)
        {
            std::vector<Color> src;
            src.reserve(static_cast<std::size_t>(dim) * dim);
            for (int y = 0; y < dim; ++y)
                for (int x = 0; x < dim; ++x) src.push_back(CubeTexel(face, level, x, y));
            try
            {
                cube.SetData(static_cast<CubeMapFace>(face), level, nullptr, src.data(), 0,
                             static_cast<int>(src.size()));
            }
            catch (const System::NotSupportedException&) { }
        }
    }

    /// Uploads the full pattern for one Texture3D mip level in a single call. See UploadCubeLevel
    /// for why an unsupported upload is absorbed here (REMED-GFX-135).
    static void UploadVolumeLevel(Texture3D& vol, int level)
    {
        const int w = MipDim(kVolW, level);
        const int h = MipDim(kVolH, level);
        const int d = MipDim(kVolD, level);
        std::vector<Color> src;
        src.reserve(static_cast<std::size_t>(w) * h * d);
        for (int z = 0; z < d; ++z)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) src.push_back(VolumeVoxel(level, x, y, z));
        try
        {
            vol.SetData(level, 0, 0, w, h, 0, d, src.data(), 0, static_cast<int>(src.size()));
        }
        catch (const System::NotSupportedException&) { }
    }

    // ---------------------------------------------------------------------
    // Probes
    // ---------------------------------------------------------------------

    /**
     * @brief Reads a cube rectangle into a destination pre-filled with @p sentinel and records
     *        every fact needed to classify the outcome.
     *
     * @param pad Extra sentinel entries reserved BEFORE the asserted window, i.e. the startIndex
     *            the call is made with. They must survive untouched no matter what happens.
     */
    static Probe ProbeCube(const TextureCube& cube, int face, int level, const Rectangle* rect,
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
        }
        return p;
    }

    /// Volume counterpart of ProbeCube.
    static Probe ProbeVolume(const Texture3D& vol, int level, int left, int top, int right,
                             int bottom, int front, int back, int pad, const Color& sentinel,
                             const std::vector<Color>& expected)
    {
        Probe p;
        p.window = static_cast<std::size_t>(right - left) * static_cast<std::size_t>(bottom - top) *
                   static_cast<std::size_t>(back - front);
        p.dest.assign(p.window + static_cast<std::size_t>(pad) * 2, sentinel);
        try
        {
            vol.GetData(level, left, top, right, bottom, front, back, p.dest.data(), pad,
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
            " padIntact=" + std::to_string(padIntact) + "/" + std::to_string(padTotal) + "]";

        if (required == Support::Exact)
        {
            const bool ok = !p.threwNotSupported && !p.threwSomethingElse &&
                            p.exact == p.window && padIntact == padTotal;
            check(ok, label + " -- exact content required" + facts);
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
    static std::vector<Color> ExpectedCubeRect(int face, int level, int x0, int y0, int w, int h)
    {
        std::vector<Color> e;
        e.reserve(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) e.push_back(CubeTexel(face, level, x0 + x, y0 + y));
        return e;
    }

    /// Expected colours for a volume sub-box, in the slice-major/row-major order GetData writes.
    static std::vector<Color> ExpectedVolumeBox(int level, int left, int top, int right,
                                                int bottom, int front, int back)
    {
        std::vector<Color> e;
        e.reserve(static_cast<std::size_t>(right - left) * (bottom - top) * (back - front));
        for (int z = front; z < back; ++z)
            for (int y = top; y < bottom; ++y)
                for (int x = left; x < right; ++x) e.push_back(VolumeVoxel(level, x, y, z));
        return e;
    }

    // ---------------------------------------------------------------------
    // Cube checks
    // ---------------------------------------------------------------------

    void RunCubeChecks(GraphicsDevice& dev)
    {
        TextureCube cube(dev, kCube, /*mipMap=*/true, SurfaceFormat::Color);
        UploadCubeLevel(cube, 0);
        if (kContract.cubeMip == Support::Exact) UploadCubeLevel(cube, 1);

        // ---- Z1/Z2: why sentinel mutation is not a success oracle -------------------------
        //
        // Recorded permanently, on every renderer. Z1 states the rule this file enforces; Z2
        // states the pre-fix behaviour it exists to rule out.
        {
            const auto expected = ExpectedCubeRect(0, 0, 0, 0, kCube, kCube);
            Probe p = ProbeCube(cube, 0, 0, nullptr, kCube, kCube, 0, SentinelCD(), expected);
            const bool overwroteSentinel = p.sentinelSurvivors == 0;
            const bool reallyRead = p.exact == p.window;
            check(!overwroteSentinel || reallyRead,
                  "Z1 cube: 'GetData overwrote my sentinel destination' is NOT accepted as success "
                  "-- an overwritten destination must also hold the exact uploaded content "
                  "[sentinelSurvivors=" + std::to_string(p.sentinelSurvivors) +
                  " exact=" + std::to_string(p.exact) + "/" + std::to_string(p.window) + "]");
            check(p.fabricated == 0,
                  "Z2 cube: no asserted texel comes back as the fabricated (0,0,0,0) the pre-fix "
                  "shared layer produced from its own zeroed scratch buffer [fabricated=" +
                  std::to_string(p.fabricated) + "/" + std::to_string(p.window) + "]");
        }

        // ---- C1..C6: every face, full level 0, read independently --------------------------
        for (int face = 0; face < 6; ++face)
        {
            const auto expected = ExpectedCubeRect(face, 0, 0, 0, kCube, kCube);
            Probe p = ProbeCube(cube, face, 0, nullptr, kCube, kCube, 0, SentinelCD(), expected);
            Judge(p, kContract.cubeLevel0, 0, SentinelCD(),
                  "C" + std::to_string(face + 1) + " cube face " + std::to_string(face) +
                  " full level 0 (signature texel " + ColorText(PureFaceColour(face)) + ")");
        }

        // ---- C7: reading one face must NOT return another face's content -------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            bool anyFaceConfusable = false;
            for (int face = 0; face < 6; ++face)
            {
                std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
                cube.GetData(static_cast<CubeMapFace>(face), got.data(), static_cast<int>(got.size()));
                for (int other = 0; other < 6; ++other)
                {
                    if (other == face) continue;
                    if (Same(got[0], PureFaceColour(other))) anyFaceConfusable = true;
                }
            }
            check(!anyFaceConfusable,
                  "C7 cube: no face's readback returns another face's signature colour -- selecting "
                  "the wrong CubeMapFace cannot pass");
        }
        else
        {
            check(true, "C7 cube: face-confusion probe skipped -- this renderer rejects cube readback");
        }

        // ---- C8: one texel, away from every edge ------------------------------------------
        {
            const Rectangle r(5, 3, 1, 1);
            Probe p = ProbeCube(cube, 2, 0, &r, 1, 1, 0, SentinelA5(),
                                ExpectedCubeRect(2, 0, 5, 3, 1, 1));
            Judge(p, kContract.cubeLevel0, 0, SentinelA5(), "C8 cube: single texel rect (5,3,1,1)");
        }

        // ---- C9: interior rectangle ---------------------------------------------------------
        {
            const Rectangle r(2, 2, 4, 3);
            Probe p = ProbeCube(cube, 3, 0, &r, 4, 3, 0, SentinelCD(),
                                ExpectedCubeRect(3, 0, 2, 2, 4, 3));
            Judge(p, kContract.cubeLevel0, 0, SentinelCD(), "C9 cube: middle rect (2,2,4,3)");
        }

        // ---- C10: final row ----------------------------------------------------------------
        {
            const Rectangle r(0, kCube - 1, kCube, 1);
            Probe p = ProbeCube(cube, 4, 0, &r, kCube, 1, 0, SentinelCD(),
                                ExpectedCubeRect(4, 0, 0, kCube - 1, kCube, 1));
            Judge(p, kContract.cubeLevel0, 0, SentinelCD(),
                  "C10 cube: final row -- a vertically flipped face cannot pass");
        }

        // ---- C11: final column -------------------------------------------------------------
        {
            const Rectangle r(kCube - 1, 0, 1, kCube);
            Probe p = ProbeCube(cube, 5, 0, &r, 1, kCube, 0, SentinelCD(),
                                ExpectedCubeRect(5, 0, kCube - 1, 0, 1, kCube));
            Judge(p, kContract.cubeLevel0, 0, SentinelCD(),
                  "C11 cube: final column -- a horizontally flipped face cannot pass");
        }

        // ---- C12: lower-right corner texel --------------------------------------------------
        {
            const Rectangle r(kCube - 1, kCube - 1, 1, 1);
            Probe p = ProbeCube(cube, 1, 0, &r, 1, 1, 0, SentinelA5(),
                                ExpectedCubeRect(1, 0, kCube - 1, kCube - 1, 1, 1));
            Judge(p, kContract.cubeLevel0, 0, SentinelA5(), "C12 cube: lower-right corner texel");
        }

        // ---- C13: non-zero destination offset, surrounding entries untouched ----------------
        {
            const Rectangle r(1, 1, 3, 2);
            Probe p = ProbeCube(cube, 0, 0, &r, 3, 2, 5, SentinelA5(),
                                ExpectedCubeRect(0, 0, 1, 1, 3, 2));
            Judge(p, kContract.cubeLevel0, 5, SentinelA5(),
                  "C13 cube: startIndex=5 writes only the requested window; the 5 entries before "
                  "and 5 after stay sentinel");
        }

        // ---- C14: two consecutive identical calls agree -------------------------------------
        {
            const auto expected = ExpectedCubeRect(2, 0, 0, 0, kCube, kCube);
            Probe a = ProbeCube(cube, 2, 0, nullptr, kCube, kCube, 0, SentinelCD(), expected);
            Probe b = ProbeCube(cube, 2, 0, nullptr, kCube, kCube, 0, SentinelA5(), expected);
            const bool agree = a.threwNotSupported == b.threwNotSupported &&
                               a.exact == b.exact && a.window == b.window;
            check(agree, "C14 cube: two consecutive reads of the same face produce the same outcome "
                         "[first exact=" + std::to_string(a.exact) + " second exact=" +
                         std::to_string(b.exact) + "]");
        }

        // ---- C15: A -> B -> A face order ----------------------------------------------------
        {
            const auto expA = ExpectedCubeRect(0, 0, 0, 0, kCube, kCube);
            const auto expB = ExpectedCubeRect(3, 0, 0, 0, kCube, kCube);
            Probe a1 = ProbeCube(cube, 0, 0, nullptr, kCube, kCube, 0, SentinelCD(), expA);
            Probe b  = ProbeCube(cube, 3, 0, nullptr, kCube, kCube, 0, SentinelCD(), expB);
            Probe a2 = ProbeCube(cube, 0, 0, nullptr, kCube, kCube, 0, SentinelA5(), expA);
            const bool ok = a1.exact == a2.exact && a1.threwNotSupported == a2.threwNotSupported &&
                            b.threwNotSupported == a1.threwNotSupported;
            check(ok, "C15 cube: A -> B -> A face order leaves A's readback unchanged [A1 exact=" +
                      std::to_string(a1.exact) + " B exact=" + std::to_string(b.exact) +
                      " A2 exact=" + std::to_string(a2.exact) + "]");
        }

        // ---- C16: mip level 1 ---------------------------------------------------------------
        {
            const int dim = MipDim(kCube, 1);
            Probe p = ProbeCube(cube, 4, 1, nullptr, dim, dim, 0, SentinelCD(),
                                ExpectedCubeRect(4, 1, 0, 0, dim, dim));
            Judge(p, kContract.cubeMip, 0, SentinelCD(),
                  "C16 cube: mip level 1 (" + std::to_string(dim) + "x" + std::to_string(dim) + ")");
        }

        // ---- C17: mip 1 content is not mip 0 content ----------------------------------------
        if (kContract.cubeMip == Support::Exact)
        {
            const int dim = MipDim(kCube, 1);
            std::vector<Color> got(static_cast<std::size_t>(dim) * dim, SentinelCD());
            cube.GetData(CubeMapFace::PositiveZ, 1, nullptr, got.data(), 0,
                         static_cast<int>(got.size()));
            check(!Same(got[0], CubeTexel(4, 0, 0, 0)),
                  "C17 cube: a mip-1 read returns mip-1 content, not mip 0's [got=" +
                  ColorText(got[0]) + " mip0=" + ColorText(CubeTexel(4, 0, 0, 0)) + "]");
        }
        else
        {
            check(true, "C17 cube: mip-level distinctness skipped -- this renderer stores no cube mips");
        }

        // ---- C18: partial SetData, then read that sub-rect and the whole face ----------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            const Color patch(3, 240, 17, 91);
            std::vector<Color> patchSrc(6, patch);
            const Rectangle r(2, 5, 3, 2);
            cube.SetData(CubeMapFace::NegativeY, 0, &r, patchSrc.data(), 0, 6);

            std::vector<Color> got(6, SentinelCD());
            cube.GetData(CubeMapFace::NegativeY, 0, &r, got.data(), 0, 6);
            bool patchOk = true;
            for (const Color& c : got) patchOk = patchOk && Same(c, patch);

            std::vector<Color> full(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
            cube.GetData(CubeMapFace::NegativeY, full.data(), static_cast<int>(full.size()));
            bool fullOk = true;
            for (int y = 0; y < kCube; ++y)
                for (int x = 0; x < kCube; ++x)
                {
                    const bool inPatch = x >= 2 && x < 5 && y >= 5 && y < 7;
                    const Color want = inPatch ? patch : CubeTexel(3, 0, x, y);
                    if (!Same(full[static_cast<std::size_t>(y) * kCube + x], want)) fullOk = false;
                }
            check(patchOk && fullOk,
                  "C18 cube: a partial SetData is visible through both a partial and a full GetData, "
                  "and leaves every texel outside the patch unchanged");
            // Restore, so later checks still see the canonical pattern.
            UploadCubeLevel(cube, 0);
        }
        else
        {
            check(true, "C18 cube: partial SetData round-trip skipped -- this renderer rejects cube readback");
        }

        // ---- C19: two independent cubes do not alias -----------------------------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            TextureCube other(dev, kCube, false, SurfaceFormat::Color);
            std::vector<Color> flat(static_cast<std::size_t>(kCube) * kCube, Color(7, 8, 9, 10));
            other.SetData(CubeMapFace::PositiveX, flat.data(), static_cast<int>(flat.size()));

            std::vector<Color> a(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
            std::vector<Color> b(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
            cube.GetData(CubeMapFace::PositiveX, a.data(), static_cast<int>(a.size()));
            other.GetData(CubeMapFace::PositiveX, b.data(), static_cast<int>(b.size()));
            check(Same(a[0], PureFaceColour(0)) && Same(b[0], Color(7, 8, 9, 10)),
                  "C19 cube: two independent TextureCubes keep independent content [first=" +
                  ColorText(a[0]) + " second=" + ColorText(b[0]) + "]");
        }
        else
        {
            check(true, "C19 cube: independence probe skipped -- this renderer rejects cube readback");
        }

        // ---- C20: mutating the source array after SetData does not change stored content ------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            TextureCube tmp(dev, kCube, false, SurfaceFormat::Color);
            std::vector<Color> src(static_cast<std::size_t>(kCube) * kCube, Color(60, 70, 80, 90));
            tmp.SetData(CubeMapFace::NegativeZ, src.data(), static_cast<int>(src.size()));
            for (Color& c : src) c = Color(1, 2, 3, 4);
            std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
            tmp.GetData(CubeMapFace::NegativeZ, got.data(), static_cast<int>(got.size()));
            check(Same(got[0], Color(60, 70, 80, 90)),
                  "C20 cube: mutating the caller's source array after SetData does not alter the "
                  "stored face [got=" + ColorText(got[0]) + "]");
        }
        else
        {
            check(true, "C20 cube: source-mutation probe skipped -- this renderer rejects cube readback");
        }

        // ---- C21..C26: argument validation is deterministic on every renderer -------------------
        {
            std::vector<Color> buf(static_cast<std::size_t>(kCube) * kCube, SentinelCD());
            check(Throws<std::invalid_argument>([&] {
                      cube.GetData(CubeMapFace::PositiveX, nullptr, static_cast<int>(buf.size()));
                  }),
                  "C21 cube: null destination throws std::invalid_argument");
            check(Throws<std::out_of_range>([&] {
                      cube.GetData(static_cast<CubeMapFace>(-1), buf.data(), static_cast<int>(buf.size()));
                  }),
                  "C22 cube: CubeMapFace below range throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.GetData(static_cast<CubeMapFace>(6), buf.data(), static_cast<int>(buf.size()));
                  }),
                  "C23 cube: CubeMapFace above range throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.GetData(CubeMapFace::PositiveX, -1, nullptr, buf.data(), 0,
                                   static_cast<int>(buf.size()));
                  }),
                  "C24 cube: negative mip level throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      const Rectangle outside(kCube - 1, kCube - 1, 4, 4);
                      cube.GetData(CubeMapFace::PositiveX, 0, &outside, buf.data(), 0, 16);
                  }),
                  "C25 cube: a rectangle outside the face throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      const Rectangle r(0, 0, 4, 4);
                      cube.GetData(CubeMapFace::PositiveX, 0, &r, buf.data(), 0, 4);
                  }),
                  "C26 cube: elementCount below the requested region throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.GetData(CubeMapFace::PositiveX, buf.data(), -1, 4);
                  }),
                  "C27 cube: negative startIndex throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.GetData(CubeMapFace::PositiveX, buf.data(), 0, 0);
                  }),
                  "C28 cube: elementCount of 0 throws std::out_of_range");

            // Validation must run BEFORE anything is written -- the buffer is still sentinel.
            std::size_t intact = 0;
            for (const Color& c : buf) if (Same(c, SentinelCD())) ++intact;
            check(intact == buf.size(),
                  "C29 cube: every rejected call left the destination byte-for-byte untouched [" +
                  std::to_string(intact) + "/" + std::to_string(buf.size()) + "]");
        }

        // ---- C30: a disposed cube refuses deterministically -------------------------------------
        {
            TextureCube dead(dev, kCube, false, SurfaceFormat::Color);
            std::vector<Color> flat(static_cast<std::size_t>(kCube) * kCube, Color(200, 100, 50, 25));
            // REMED-GFX-135: pre-populating the doomed cube is a convenience, not the subject --
            // a renderer with no cube storage now rejects the upload rather than ignoring it.
            try { dead.SetData(CubeMapFace::PositiveX, flat.data(), static_cast<int>(flat.size())); }
            catch (const System::NotSupportedException&) { }
            dead.Dispose();
            std::vector<Color> got(static_cast<std::size_t>(kCube) * kCube, SentinelA5());
            bool threwDisposed = false;
            try { dead.GetData(CubeMapFace::PositiveX, got.data(), static_cast<int>(got.size())); }
            catch (const System::ObjectDisposedException&) { threwDisposed = true; }
            catch (...) {}
            std::size_t intact = 0;
            for (const Color& c : got) if (Same(c, SentinelA5())) ++intact;
            check(threwDisposed && intact == got.size(),
                  "C30 cube: GetData on a disposed TextureCube throws ObjectDisposedException and "
                  "leaves the destination untouched [threw=" + std::string(threwDisposed ? "1" : "0") +
                  " intact=" + std::to_string(intact) + "/" + std::to_string(got.size()) + "]");
        }
    }

    // ---------------------------------------------------------------------
    // Volume checks
    // ---------------------------------------------------------------------

    void RunVolumeChecks(GraphicsDevice& dev)
    {
        if (!kContract.volumeConstructs)
        {
            bool threw = false;
            try { Texture3D probe(dev, kVolW, kVolH, kVolD, false, SurfaceFormat::Color); }
            catch (const System::NotSupportedException&) { threw = true; }
            catch (...) {}
            check(threw,
                  "V0 volume: Texture3D construction is refused deterministically on this renderer "
                  "(no GraphicsCapability::Texture3D) -- there is no readback to fabricate");
            return;
        }

        Texture3D vol(dev, kVolW, kVolH, kVolD, /*mipMap=*/true, SurfaceFormat::Color);
        UploadVolumeLevel(vol, 0);
        if (kContract.volumeMip == Support::Exact) UploadVolumeLevel(vol, 1);

        // ---- Z3/Z4: the volume half of the sentinel-oracle record ------------------------------
        {
            const auto expected = ExpectedVolumeBox(0, 0, 0, kVolW, kVolH, 0, kVolD);
            Probe p = ProbeVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, 0, SentinelCD(), expected);
            const bool overwroteSentinel = p.sentinelSurvivors == 0;
            const bool reallyRead = p.exact == p.window;
            check(!overwroteSentinel || reallyRead,
                  "Z3 volume: 'GetData overwrote my sentinel destination' is NOT accepted as success "
                  "[sentinelSurvivors=" + std::to_string(p.sentinelSurvivors) + " exact=" +
                  std::to_string(p.exact) + "/" + std::to_string(p.window) + "]");
            check(p.fabricated == 0,
                  "Z4 volume: no asserted voxel comes back as the fabricated (0,0,0,0) [fabricated=" +
                  std::to_string(p.fabricated) + "/" + std::to_string(p.window) + "]");
        }

        // ---- V1: the complete volume -------------------------------------------------------
        {
            const auto expected = ExpectedVolumeBox(0, 0, 0, kVolW, kVolH, 0, kVolD);
            Probe p = ProbeVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, 0, SentinelCD(), expected);
            Judge(p, kContract.volumeLevel0, 0, SentinelCD(), "V1 volume: complete volume at mip 0");
        }

        // ---- V2: one voxel -----------------------------------------------------------------
        {
            Probe p = ProbeVolume(vol, 0, 2, 1, 3, 2, 2, 3, 0, SentinelA5(),
                                  ExpectedVolumeBox(0, 2, 1, 3, 2, 2, 3));
            Judge(p, kContract.volumeLevel0, 0, SentinelA5(), "V2 volume: single voxel (2,1,2)");
        }

        // ---- V3: one row (x range, single y, single z) ---------------------------------------
        {
            Probe p = ProbeVolume(vol, 0, 0, 2, kVolW, 3, 1, 2, 0, SentinelCD(),
                                  ExpectedVolumeBox(0, 0, 2, kVolW, 3, 1, 2));
            Judge(p, kContract.volumeLevel0, 0, SentinelCD(), "V3 volume: one row of slice z=1");
        }

        // ---- V4: one complete 2D slice -------------------------------------------------------
        {
            Probe p = ProbeVolume(vol, 0, 0, 0, kVolW, kVolH, 2, 3, 0, SentinelCD(),
                                  ExpectedVolumeBox(0, 0, 0, kVolW, kVolH, 2, 3));
            Judge(p, kContract.volumeLevel0, 0, SentinelCD(),
                  "V4 volume: the whole z=2 slice -- reading only slice 0 cannot pass");
        }

        // ---- V5: interior sub-box across two slices ------------------------------------------
        {
            Probe p = ProbeVolume(vol, 0, 1, 1, 3, 3, 1, 3, 0, SentinelCD(),
                                  ExpectedVolumeBox(0, 1, 1, 3, 3, 1, 3));
            Judge(p, kContract.volumeLevel0, 0, SentinelCD(),
                  "V5 volume: middle sub-box (1..3, 1..3, 1..3) -- row pitch and slice pitch must "
                  "be applied independently");
        }

        // ---- V6: last row of the last slice, and the lower-right-back corner voxel -------------
        {
            Probe row = ProbeVolume(vol, 0, 0, kVolH - 1, kVolW, kVolH, kVolD - 1, kVolD, 0,
                                    SentinelCD(),
                                    ExpectedVolumeBox(0, 0, kVolH - 1, kVolW, kVolH, kVolD - 1, kVolD));
            Judge(row, kContract.volumeLevel0, 0, SentinelCD(),
                  "V6 volume: final row of the final slice");
            Probe corner = ProbeVolume(vol, 0, kVolW - 1, kVolH - 1, kVolW, kVolH, kVolD - 1, kVolD,
                                       0, SentinelA5(),
                                       ExpectedVolumeBox(0, kVolW - 1, kVolH - 1, kVolW, kVolH,
                                                         kVolD - 1, kVolD));
            Judge(corner, kContract.volumeLevel0, 0, SentinelA5(),
                  "V7 volume: lower-right-back corner voxel");
        }

        // ---- V8: non-zero destination offset --------------------------------------------------
        {
            Probe p = ProbeVolume(vol, 0, 1, 0, 3, 2, 0, 2, 7, SentinelA5(),
                                  ExpectedVolumeBox(0, 1, 0, 3, 2, 0, 2));
            Judge(p, kContract.volumeLevel0, 7, SentinelA5(),
                  "V8 volume: startIndex=7 writes only the requested window; the 7 entries before "
                  "and 7 after stay sentinel");
        }

        // ---- V9: repeated calls agree ---------------------------------------------------------
        {
            const auto expected = ExpectedVolumeBox(0, 0, 0, kVolW, kVolH, 0, kVolD);
            Probe a = ProbeVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, 0, SentinelCD(), expected);
            Probe b = ProbeVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, 0, SentinelA5(), expected);
            check(a.threwNotSupported == b.threwNotSupported && a.exact == b.exact,
                  "V9 volume: two consecutive whole-volume reads produce the same outcome [first "
                  "exact=" + std::to_string(a.exact) + " second exact=" + std::to_string(b.exact) + "]");
        }

        // ---- V10: no slice is duplicated or reversed -------------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            std::vector<Color> got(static_cast<std::size_t>(kVolW) * kVolH * kVolD, SentinelCD());
            vol.GetData(got.data(), static_cast<int>(got.size()));
            const std::size_t slice = static_cast<std::size_t>(kVolW) * kVolH;
            bool distinct = true;
            bool ordered = true;
            for (int z = 0; z < kVolD; ++z)
            {
                if (!Same(got[static_cast<std::size_t>(z) * slice], VolumeVoxel(0, 0, 0, z)))
                    ordered = false;
                for (int other = z + 1; other < kVolD; ++other)
                    if (Same(got[static_cast<std::size_t>(z) * slice],
                             got[static_cast<std::size_t>(other) * slice]))
                        distinct = false;
            }
            check(distinct && ordered,
                  "V10 volume: every depth slice is distinct AND in front-to-back order -- a "
                  "duplicated, reversed or flattened volume cannot pass");
        }
        else
        {
            check(true, "V10 volume: slice-order probe skipped -- this renderer rejects volume readback");
        }

        // ---- V11: mip level 1 -------------------------------------------------------------------
        {
            const int w = MipDim(kVolW, 1), h = MipDim(kVolH, 1), d = MipDim(kVolD, 1);
            Probe p = ProbeVolume(vol, 1, 0, 0, w, h, 0, d, 0, SentinelCD(),
                                  ExpectedVolumeBox(1, 0, 0, w, h, 0, d));
            Judge(p, kContract.volumeMip, 0, SentinelCD(),
                  "V11 volume: mip level 1 (" + std::to_string(w) + "x" + std::to_string(h) + "x" +
                  std::to_string(d) + ")");
        }

        // ---- V12: partial SetData round-trip -----------------------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            const Color patch(5, 205, 45, 155);
            std::vector<Color> patchSrc(4, patch);
            vol.SetData(0, 1, 1, 3, 3, 2, 3, patchSrc.data(), 0, 4);

            std::vector<Color> got(4, SentinelCD());
            vol.GetData(0, 1, 1, 3, 3, 2, 3, got.data(), 0, 4);
            bool patchOk = true;
            for (const Color& c : got) patchOk = patchOk && Same(c, patch);

            std::vector<Color> full(static_cast<std::size_t>(kVolW) * kVolH * kVolD, SentinelCD());
            vol.GetData(full.data(), static_cast<int>(full.size()));
            bool fullOk = true;
            for (int z = 0; z < kVolD; ++z)
                for (int y = 0; y < kVolH; ++y)
                    for (int x = 0; x < kVolW; ++x)
                    {
                        const bool inPatch = x >= 1 && x < 3 && y >= 1 && y < 3 && z == 2;
                        const Color want = inPatch ? patch : VolumeVoxel(0, x, y, z);
                        const std::size_t i = (static_cast<std::size_t>(z) * kVolH + y) * kVolW + x;
                        if (!Same(full[i], want)) fullOk = false;
                    }
            check(patchOk && fullOk,
                  "V12 volume: a partial SetData is visible through both a partial and a full "
                  "GetData, and leaves every voxel outside the box unchanged");
            UploadVolumeLevel(vol, 0);
        }
        else
        {
            check(true, "V12 volume: partial SetData round-trip skipped -- this renderer rejects volume readback");
        }

        // ---- V13: two independent volumes do not alias ---------------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            Texture3D other(dev, kVolW, kVolH, kVolD, false, SurfaceFormat::Color);
            std::vector<Color> flat(static_cast<std::size_t>(kVolW) * kVolH * kVolD, Color(11, 22, 33, 44));
            other.SetData(flat.data(), static_cast<int>(flat.size()));

            std::vector<Color> a(static_cast<std::size_t>(kVolW) * kVolH * kVolD, SentinelCD());
            std::vector<Color> b(a.size(), SentinelCD());
            vol.GetData(a.data(), static_cast<int>(a.size()));
            other.GetData(b.data(), static_cast<int>(b.size()));
            check(Same(a[0], VolumeVoxel(0, 0, 0, 0)) && Same(b[0], Color(11, 22, 33, 44)),
                  "V13 volume: two independent Texture3Ds keep independent content [first=" +
                  ColorText(a[0]) + " second=" + ColorText(b[0]) + "]");
        }
        else
        {
            check(true, "V13 volume: independence probe skipped -- this renderer rejects volume readback");
        }

        // ---- V14..V20: argument validation ------------------------------------------------------
        {
            std::vector<Color> buf(static_cast<std::size_t>(kVolW) * kVolH * kVolD, SentinelCD());
            check(Throws<std::invalid_argument>([&] {
                      vol.GetData(nullptr, static_cast<int>(buf.size()));
                  }),
                  "V14 volume: null destination throws std::invalid_argument");
            check(Throws<std::out_of_range>([&] {
                      vol.GetData(buf.data(), 0);
                  }),
                  "V15 volume: elementCount of 0 throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.GetData(buf.data(), -1, 4);
                  }),
                  "V16 volume: negative startIndex throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.GetData(-1, 0, 0, kVolW, kVolH, 0, kVolD, buf.data(), 0,
                                  static_cast<int>(buf.size()));
                  }),
                  "V17 volume: negative mip level throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.GetData(0, 2, 0, 2, kVolH, 0, kVolD, buf.data(), 0, 4);
                  }),
                  "V18 volume: left == right throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.GetData(0, 0, 0, kVolW, kVolH, 2, 1, buf.data(), 0, 4);
                  }),
                  "V19 volume: back < front throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.GetData(0, 0, 0, kVolW, kVolH, 0, kVolD, buf.data(), 0, 2);
                  }),
                  "V20 volume: elementCount below the requested box throws std::out_of_range");

            std::size_t intact = 0;
            for (const Color& c : buf) if (Same(c, SentinelCD())) ++intact;
            check(intact == buf.size(),
                  "V21 volume: every rejected call left the destination byte-for-byte untouched [" +
                  std::to_string(intact) + "/" + std::to_string(buf.size()) + "]");
        }

        // ---- V22: a disposed volume refuses deterministically --------------------------------------
        {
            Texture3D dead(dev, kVolW, kVolH, kVolD, false, SurfaceFormat::Color);
            std::vector<Color> flat(static_cast<std::size_t>(kVolW) * kVolH * kVolD, Color(9, 8, 7, 6));
            try { dead.SetData(flat.data(), static_cast<int>(flat.size())); }
            catch (const System::NotSupportedException&) { }
            dead.Dispose();
            std::vector<Color> got(flat.size(), SentinelA5());
            bool threwDisposed = false;
            try { dead.GetData(got.data(), static_cast<int>(got.size())); }
            catch (const System::ObjectDisposedException&) { threwDisposed = true; }
            catch (...) {}
            std::size_t intact = 0;
            for (const Color& c : got) if (Same(c, SentinelA5())) ++intact;
            check(threwDisposed && intact == got.size(),
                  "V22 volume: GetData on a disposed Texture3D throws ObjectDisposedException and "
                  "leaves the destination untouched [threw=" + std::string(threwDisposed ? "1" : "0") +
                  " intact=" + std::to_string(intact) + "/" + std::to_string(got.size()) + "]");
        }
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
    }

    void Draw(const GameTime&) override
    {
        if (done_) { Exit(); return; }
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        std::printf("REMED-GFX-130 TextureCube/Texture3D GetData contract -- renderer %s\n",
                    kContract.name);

        if (kContract.cubeHasRenderer)
        {
            RunCubeChecks(dev);
        }
        else
        {
            const bool rejected = Throws<System::NotSupportedException>([&]
            {
                TextureCube cube(dev, kCube, /*mipMap=*/true, SurfaceFormat::Color);
            });
            check(rejected,
                  "C0 cube: TextureCube construction is refused deterministically when the "
                  "renderer declares no validated cube storage");
        }
        RunVolumeChecks(dev);

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    CubeVolumeGetDataContractTest test;
    test.Run();
    return test.Result();
}
