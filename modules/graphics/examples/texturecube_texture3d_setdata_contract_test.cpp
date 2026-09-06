// SPDX-License-Identifier: MS-PL
// REMED-GFX-135: every public TextureCube::SetData and Texture3D::SetData call must have exactly
// ONE honest outcome -- the complete requested region is stored, or the request is rejected
// deterministically. It must never return normally after storing nothing, or after storing only
// part of what was asked for.
//
// This is the WRITE half of REMED-GFX-127/REMED-GFX-130. Those findings gave the READ side two
// outcomes; the write side kept the old shape, renderer-neutral, in the shared layer itself:
//
//     const auto rgba = colorsToRgba(data, startIndex, elementCount);
//     if (renderer_)                                   // <-- silently skipped when there is none
//         renderer_->SetData(face, level, x, y, w, h, rgba.data(), size);   // <-- `void`
//
// and, byte for byte the same shape, in Texture3D::SetDataPointerEXT:
//
//     if (renderer_)
//         renderer_->SetData(level, left, top, front, w, h, depth, data, dataLength);
//
// `void` leaves an implementation no way to say "I stored nothing", and `if (renderer_)` turns
// "this renderer creates no cube/volume resource at all" into a successful-looking call. Three
// distinct silent-discard routes are reachable through the public API today:
//
//   * NO RESOURCE -- SDL_Renderer, Canvas and DIRECTX3 keep IGraphicsRenderer::CreateTextureCube's
//     nullptr default, so `renderer_` is null and the upload is dropped by the `if` itself;
//   * NO STORAGE -- Headless's cube renderer validates its arguments, records a trace entry and
//     stores nothing, and RenderTargetCube renderers without an explicit upload override inherit
//     IRenderTargetCubeRenderer::SetData's deterministic refusal;
//   * NO LEVEL -- Software's cube renderer returns early for any `level != 0`, because it allocated
//     storage for level 0 only while TextureCube still reports the full LevelCount.
//
// After REMED-GFX-130 the reachable end state is worse than merely losing the data: a caller can
// upload a face, read it back, be told "this renderer cannot read a cube face back to the CPU", and
// still never learn that the write went nowhere either. Check Z2 below states that exact
// inconsistency as a runtime-derived invariant -- readback support and write support must agree --
// so it cannot come back on any renderer, present or future.
//
// What every renderer must satisfy after the fix:
//   * a supported SetData stores EXACTLY the requested region -- per face, per mip, for an
//     arbitrary rectangle/box, from an arbitrary source offset, over repeated calls -- and changes
//     nothing outside it;
//   * an unsupported SetData throws a deterministic System::NotSupportedException;
//   * SetData on a disposed resource throws System::ObjectDisposedException;
//   * nothing in between: no silent no-op, no partial store reported as a complete write.
//
// Every claimed success is verified through REMED-GFX-130's honest GetData contract, never through
// "the call returned" -- check Z1 records why that oracle is invalid and enforces the real one.
//
// Each renderer's capability is declared here explicitly (kContract) rather than inferred at
// runtime, so "this renderer cannot store a cube face" is a reviewed claim this test enforces, not
// an escape hatch that lets any result pass.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#if defined(CNA_RENDERER_VULKAN)
// plan_vulkan.md VULKAN-406: reading a cube face is the one thing this file does that touches an
// image ARRAY LAYER other than 0, and a layer nobody has written is exactly the case a renderer can
// leave in an undefined image layout while still returning the right bytes. The Khronos layer is
// the only observer of that, so the Vulkan build of this fixture judges it.
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#endif

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
     * @brief What one public upload path on this renderer is required to do.
     *
     * `Exact` -- SetData must store the complete requested region, byte for byte, and REMED-GFX-130's
     * GetData must then return it.
     * `Unsupported` -- SetData must throw System::NotSupportedException. Reserved for a
     * resource/level this renderer genuinely cannot store, never for one it merely has not been
     * checked on.
     * `AcceptedRowMirrored` -- SetData stores the complete region and REMED-GFX-134's readback
     * returns it, but with the ROWS MIRRORED. Used for exactly one entry -- EasyGL's
     * RenderTargetCube -- and never as a fallback. It is a byte-exact assertion, not a weaker one:
     * this renderer's rasterizer fills a rendered face bottom-up (which is why
     * EasyGLRenderTargetCubeRenderer::GetData normalizes) while glTexSubImage2D writes source row 0
     * into texel row 0 like every other renderer's upload, so the two writers of one face disagree
     * on row order. REMED-GFX-134's own check W1 records that asymmetry as an enforced fact; this
     * entry is the write side of the same statement. Before REMED-GFX-134 there was no public
     * readback of a RenderTargetCube outside SdlGpu/WebGPU, so this entry could only assert "did
     * not throw" -- which a SetData that stored nothing would also have satisfied.
     */
    enum class Support
    {
        Exact,
        Unsupported,
        AcceptedRowMirrored,
    };

    /// The complete, reviewed per-renderer claim this file enforces.
    struct Contract
    {
        const char* name;
        bool    cubeHasRenderer;    ///< IGraphicsRenderer::CreateTextureCube() returns a real object.
        Support cubeLevel0;        ///< TextureCube::SetData at mip 0.
        Support cubeMip;           ///< TextureCube::SetData at mip > 0.
        bool    volumeConstructs;  ///< Texture3D's constructor succeeds on this renderer/profile.
        Support volumeLevel0;      ///< Texture3D::SetData at mip 0.
        Support volumeMip;         ///< Texture3D::SetData at mip > 0.
        Support rtCube;            ///< RenderTargetCube::SetData (inherited from TextureCube).
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef (D3D9's volume-texture gate).
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless deliberately executes no rasterization and stores no pixel data: it records API
    // usage and resource state for validation/tracing. Its cube SetData is a trace entry, not a
    // write -- REMED-GFX-130 already deleted the zero-filled buffers that used to make its GetData
    // look like a readback -- so accepting an upload here would be accept-and-discard by
    // definition. Texture3D is refused at construction (REMED-CONTENT-004).
    constexpr Contract kContract{"HEADLESS", true, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    // SOFTWARE-82 gives this renderer real 6-face RGBA8 cube storage. REMED-GFX-135 extends it to
    // every mip level TextureCube declares, so a mipmapped cube's LevelCount is no longer a claim
    // the storage cannot back (a mipmapped cube .xnb loads and reads back completely here now).
    // Texture3D remains an explicit documented v1 scope boundary, refused at construction, and
    // cube-map render targets are equally out of scope (CreateRenderTargetCube returns nullptr).
    constexpr Contract kContract{"SOFTWARE", true, Support::Exact, Support::Exact,
                                 false, Support::Unsupported, Support::Unsupported,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_EASYGL)
    // EasyGL uploads into the shared GL cube texture and normalizes its differing row convention.
    constexpr Contract kContract{"EASYGL", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::AcceptedRowMirrored, false};
#elif defined(CNA_RENDERER_BGFX)
    constexpr Contract kContract{"BGFX", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_VULKAN)
    constexpr Contract kContract{"VULKAN", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr Contract kContract{"WEBGPU", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr Contract kContract{"SDL_GPU", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    // 2D-only by design: CreateTextureCube()/CreateTexture3D()/CreateRenderTargetCube() all keep
    // IGraphicsRenderer's own nullptr-returning defaults, so no cube/volume storage exists at all
    // and Texture3D is refused at construction.
    constexpr Contract kContract{"SDL_RENDERER", false, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", false, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", false, Support::Unsupported, Support::Unsupported,
                                 false, Support::Unsupported, Support::Unsupported,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    // plans/plan_dx9.md D9-100: GraphicsProfile.Reach does not support volume textures at all, so the
    // Texture3D half of this file needs a HiDef device to have anything to measure.
    constexpr Contract kContract{"DIRECTX9", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    // D3D12TextureCubeRenderer's own constructor pins mipLevels_ to 1 whatever `mipMap` says (its
    // header states so explicitly), so a cube mip level has no subresource to be written into here
    // -- declared Unsupported rather than assumed, since this renderer's Game-harness tests are
    // compile-verified only under this dev loop's Wine dxgi.dll. Texture3D does build its full
    // chain.
    constexpr Contract kContract{"DIRECTX12", true, Support::Exact, Support::Unsupported,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, false};
#elif defined(CNA_RENDERER_LLGL)
    // The pinned OpenGL render system cannot sample cubes, so LLGL keeps exact transfer-only CPU
    // face storage; Texture3D remains a native LLGL texture with exact mip transfers.
    constexpr Contract kContract{"LLGL", true, Support::Exact, Support::Exact,
                                 true, Support::Exact, Support::Exact,
                                 Support::Unsupported, false};
#else
#error "REMED-GFX-135: this renderer has no declared TextureCube/Texture3D SetData contract."
#endif

    /// Destination pre-fill for readbacks. Equal to no pattern colour, so "still the sentinel" and
    /// "a real texel" can never be confused for one another.
    Color Sentinel() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }

    /// Source-array padding. Must never appear in the resource: it marks source elements OUTSIDE
    /// the [startIndex, startIndex + region) window the call is allowed to read.
    Color Poison() { return Color(0x7E, 0x11, 0x33, 0x5C); }

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
     * @brief The texel this test uploads to (@p face, @p level, @p x, @p y) in generation @p gen.
     *
     * R identifies the face, the mip level AND the generation, G ramps with x, B ramps with y and A
     * is neither 0 nor 255. Writing the wrong face, the wrong mip, the wrong origin, a vertically
     * flipped face, a channel-swapped copy or a stale generation therefore all produce a different
     * colour at the same coordinate.
     */
    Color CubeTexel(int face, int level, int x, int y, int gen = 0)
    {
        if (level == 0 && gen == 0 && x == 0 && y == 0) return PureFaceColour(face);
        const int r = 11 + face * 37 + level * 3 + gen * 61;
        const int g = 9 + x * 21;
        const int b = 13 + y * 19;
        const int a = 201 - x * 5 - y * 7;
        return Color(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                     static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a));
    }

    /**
     * @brief The voxel this test uploads to (@p level, @p x, @p y, @p z) in generation @p gen.
     *
     * Every axis moves an independent channel, so a duplicated slice, a reversed slice order, a
     * flattened volume and a row-pitch/slice-pitch mix-up are each individually detectable.
     */
    Color VolumeVoxel(int level, int x, int y, int z, int gen = 0)
    {
        const int r = 23 + z * 51 + level * 7 + gen * 89;
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

    /// What one SetData attempt did, in full, without judging it yet.
    struct WriteProbe
    {
        bool returnedNormally   = false;  ///< the call completed with no exception at all
        bool threwNotSupported  = false;  ///< the deterministic "cannot store this" rejection
        bool threwDisposed      = false;  ///< System::ObjectDisposedException
        bool threwSomethingElse = false;  ///< any other exception
        std::string otherWhat;
    };

    /// What one GetData oracle call produced.
    struct ReadProbe
    {
        bool read = false;                ///< readback completed and `data` holds real content
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> data;
    };
}

class CubeVolumeSetDataContractTest : public Game
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
    // Probes
    // ---------------------------------------------------------------------

    /// Issues one TextureCube::SetData and records only what the call itself did.
    static WriteProbe WriteCube(TextureCube& cube, int face, int level, const Rectangle* rect,
                                const std::vector<Color>& src, int startIndex, int elementCount)
    {
        WriteProbe p;
        try
        {
            cube.SetData(static_cast<CubeMapFace>(face), level, rect, src.data(), startIndex,
                         elementCount);
            p.returnedNormally = true;
        }
        catch (const System::ObjectDisposedException&) { p.threwDisposed = true; }
        catch (const System::NotSupportedException&)   { p.threwNotSupported = true; }
        catch (const std::exception& e) { p.threwSomethingElse = true; p.otherWhat = e.what(); }
        catch (...) { p.threwSomethingElse = true; p.otherWhat = "non-std exception"; }
        return p;
    }

    /// Volume counterpart of WriteCube.
    static WriteProbe WriteVolume(Texture3D& vol, int level, int left, int top, int right,
                                  int bottom, int front, int back, const std::vector<Color>& src,
                                  int startIndex, int elementCount)
    {
        WriteProbe p;
        try
        {
            vol.SetData(level, left, top, right, bottom, front, back, src.data(), startIndex,
                        elementCount);
            p.returnedNormally = true;
        }
        catch (const System::ObjectDisposedException&) { p.threwDisposed = true; }
        catch (const System::NotSupportedException&)   { p.threwNotSupported = true; }
        catch (const std::exception& e) { p.threwSomethingElse = true; p.otherWhat = e.what(); }
        catch (...) { p.threwSomethingElse = true; p.otherWhat = "non-std exception"; }
        return p;
    }

    /// REMED-GFX-130's honest readback, used here purely as this file's success oracle.
    static ReadProbe ReadCube(const TextureCube& cube, int face, int level, const Rectangle* rect,
                              int w, int h)
    {
        ReadProbe p;
        p.data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), Sentinel());
        try
        {
            cube.GetData(static_cast<CubeMapFace>(face), level, rect, p.data.data(), 0,
                         static_cast<int>(p.data.size()));
            p.read = true;
        }
        catch (const System::NotSupportedException&) { p.threwNotSupported = true; }
        catch (const std::exception& e) { p.threwSomethingElse = true; p.otherWhat = e.what(); }
        catch (...) { p.threwSomethingElse = true; p.otherWhat = "non-std exception"; }
        return p;
    }

    /// Volume counterpart of ReadCube.
    static ReadProbe ReadVolume(const Texture3D& vol, int level, int left, int top, int right,
                                int bottom, int front, int back)
    {
        ReadProbe p;
        p.data.assign(static_cast<std::size_t>(right - left) *
                      static_cast<std::size_t>(bottom - top) *
                      static_cast<std::size_t>(back - front), Sentinel());
        try
        {
            vol.GetData(level, left, top, right, bottom, front, back, p.data.data(), 0,
                        static_cast<int>(p.data.size()));
            p.read = true;
        }
        catch (const System::NotSupportedException&) { p.threwNotSupported = true; }
        catch (const std::exception& e) { p.threwSomethingElse = true; p.otherWhat = e.what(); }
        catch (...) { p.threwSomethingElse = true; p.otherWhat = "non-std exception"; }
        return p;
    }

    // ---------------------------------------------------------------------
    // Judgement
    // ---------------------------------------------------------------------

    static std::string WriteFacts(const WriteProbe& w)
    {
        return " [returned=" + std::string(w.returnedNormally ? "1" : "0") +
               " notSupported=" + (w.threwNotSupported ? "1" : "0") +
               " disposed=" + (w.threwDisposed ? "1" : "0") +
               " other=" + (w.threwSomethingElse ? ("1:" + w.otherWhat) : "0") + "]";
    }

    /// Counts how many of @p got equal @p want, and whether any entry is still the sentinel or the
    /// source array's out-of-window Poison().
    struct Compare
    {
        std::size_t exact = 0;
        std::size_t sentinelSurvivors = 0;
        std::size_t poisoned = 0;
        std::size_t total = 0;
    };

    static Compare CompareContent(const std::vector<Color>& got, const std::vector<Color>& want)
    {
        Compare c;
        c.total = got.size();
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            if (i < want.size() && Same(got[i], want[i])) ++c.exact;
            if (Same(got[i], Sentinel())) ++c.sentinelSurvivors;
            if (Same(got[i], Poison()))   ++c.poisoned;
        }
        return c;
    }

    static std::string CompareFacts(const Compare& c)
    {
        return " [exact=" + std::to_string(c.exact) + "/" + std::to_string(c.total) +
               " sentinelSurvivors=" + std::to_string(c.sentinelSurvivors) +
               " poisoned=" + std::to_string(c.poisoned) + "]";
    }

    /**
     * @brief Judges one write + its readback oracle against this renderer's declared contract.
     *
     * `Exact`: the write returned normally, the readback succeeded, and every entry equals its
     * expectation. "The write returned" alone is never enough -- that is the whole point of Z1.
     * `Unsupported`: the write threw System::NotSupportedException and nothing else.
     */
    void JudgeWrite(const WriteProbe& w, const ReadProbe& r, const std::vector<Color>& want,
                    Support required, const std::string& label)
    {
        if (required == Support::Unsupported)
        {
            const bool ok = w.threwNotSupported && !w.returnedNormally &&
                            !w.threwSomethingElse && !w.threwDisposed;
            check(ok, label + " -- deterministic NotSupportedException required" + WriteFacts(w));
            return;
        }
        if (required == Support::AcceptedRowMirrored)
        {
            std::vector<Color> mirrored = want;
            const std::size_t rows = want.empty() ? 0 : want.size() / static_cast<std::size_t>(kCube);
            for (std::size_t row = 0; row < rows; ++row)
                for (std::size_t col = 0; col < static_cast<std::size_t>(kCube); ++col)
                    mirrored[row * kCube + col] = want[(rows - 1 - row) * kCube + col];
            const Compare m = CompareContent(r.data, mirrored);
            const bool ok = w.returnedNormally && !w.threwSomethingElse && !w.threwNotSupported &&
                            r.read && m.exact == m.total && m.poisoned == 0;
            check(ok, label + " -- complete stored region required, read back with the rows "
                              "mirrored (see this file's Support::AcceptedRowMirrored comment)" +
                      WriteFacts(w) + CompareFacts(m));
            return;
        }

        const Compare c = CompareContent(r.data, want);
        const bool ok = w.returnedNormally && !w.threwSomethingElse && !w.threwNotSupported &&
                        r.read && c.exact == c.total && c.poisoned == 0;
        check(ok, label + " -- complete stored region required" + WriteFacts(w) + CompareFacts(c));
    }

    // ---------------------------------------------------------------------
    // Fixtures
    // ---------------------------------------------------------------------

    /// The full pattern for one cube face at one mip level, row-major, top row first.
    static std::vector<Color> CubeFacePattern(int face, int level, int gen = 0)
    {
        const int dim = MipDim(kCube, level);
        std::vector<Color> src;
        src.reserve(static_cast<std::size_t>(dim) * dim);
        for (int y = 0; y < dim; ++y)
            for (int x = 0; x < dim; ++x) src.push_back(CubeTexel(face, level, x, y, gen));
        return src;
    }

    /// The full pattern for one Texture3D mip level, slice-major then row-major.
    static std::vector<Color> VolumePattern(int level, int gen = 0)
    {
        const int w = MipDim(kVolW, level);
        const int h = MipDim(kVolH, level);
        const int d = MipDim(kVolD, level);
        std::vector<Color> src;
        src.reserve(static_cast<std::size_t>(w) * h * d);
        for (int z = 0; z < d; ++z)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) src.push_back(VolumeVoxel(level, x, y, z, gen));
        return src;
    }

    /// Expected colours for a cube sub-rectangle, in the row-major order GetData returns them.
    static std::vector<Color> ExpectedCubeRect(int face, int level, int x0, int y0, int w, int h,
                                               int gen = 0)
    {
        std::vector<Color> e;
        e.reserve(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) e.push_back(CubeTexel(face, level, x0 + x, y0 + y, gen));
        return e;
    }

    /// Expected colours for a volume sub-box, in the slice-major/row-major order GetData returns.
    static std::vector<Color> ExpectedVolumeBox(int level, int left, int top, int right,
                                                int bottom, int front, int back, int gen = 0)
    {
        std::vector<Color> e;
        e.reserve(static_cast<std::size_t>(right - left) * (bottom - top) * (back - front));
        for (int z = front; z < back; ++z)
            for (int y = top; y < bottom; ++y)
                for (int x = left; x < right; ++x) e.push_back(VolumeVoxel(level, x, y, z, gen));
        return e;
    }

    /// Wraps @p body in Poison() padding on both sides, so a call that reads outside its own
    /// [startIndex, startIndex + region) window shows up in the resource as Poison().
    static std::vector<Color> PaddedSource(const std::vector<Color>& body, int pad)
    {
        std::vector<Color> src(body.size() + static_cast<std::size_t>(pad) * 2, Poison());
        for (std::size_t i = 0; i < body.size(); ++i) src[static_cast<std::size_t>(pad) + i] = body[i];
        return src;
    }

    /// Uploads every face of one cube mip level, one call per face, and reports whether every
    /// single call returned normally.
    static bool UploadCubeLevel(TextureCube& cube, int level, int gen = 0)
    {
        bool allReturned = true;
        for (int face = 0; face < 6; ++face)
        {
            const std::vector<Color> src = CubeFacePattern(face, level, gen);
            const WriteProbe w = WriteCube(cube, face, level, nullptr, src, 0,
                                           static_cast<int>(src.size()));
            allReturned = allReturned && w.returnedNormally;
        }
        return allReturned;
    }

    // =====================================================================
    // Cube checks
    // =====================================================================

    void RunCubeChecks(GraphicsDevice& dev)
    {
        TextureCube cube(dev, kCube, /*mipMap=*/true, SurfaceFormat::Color);

        // ---- Z1/Z2: why "the call returned" is not a success oracle ------------------------
        //
        // Recorded permanently, on every renderer, and derived at RUNTIME rather than from
        // kContract -- these two are the finding itself, not a per-renderer capability claim.
        {
            const std::vector<Color> src = CubeFacePattern(0, 0);
            const WriteProbe w = WriteCube(cube, 0, 0, nullptr, src, 0, static_cast<int>(src.size()));
            const ReadProbe  r = ReadCube(cube, 0, 0, nullptr, kCube, kCube);
            const Compare    c = CompareContent(r.data, src);

            // Z1: an upload that returned normally must actually be there.
            check(!w.returnedNormally || (r.read && c.exact == c.total),
                  "Z1 cube: a SetData that returns normally must be readable back EXACTLY -- "
                  "'the call did not throw' is not accepted as proof the data was stored" +
                  WriteFacts(w) + CompareFacts(c));

            // Z2: the reachable inconsistency REMED-GFX-135 was raised for. Post-REMED-GFX-130 a
            // caller could upload a face, have the readback rejected, and never learn the write
            // was dropped too. Write support and read support must agree.
            check(w.returnedNormally == r.read,
                  "Z2 cube: SetData and GetData agree about whether this resource has storage -- a "
                  "renderer that cannot return a face must not have accepted one either" +
                  WriteFacts(w) + " [readBack=" + std::string(r.read ? "1" : "0") +
                  " readNotSupported=" + (r.threwNotSupported ? "1" : "0") + "]");
        }

        // ---- C1..C6: every face uploaded independently, full level 0 -----------------------
        for (int face = 0; face < 6; ++face)
        {
            const std::vector<Color> src = CubeFacePattern(face, 0);
            const WriteProbe w = WriteCube(cube, face, 0, nullptr, src, 0, static_cast<int>(src.size()));
            const ReadProbe  r = ReadCube(cube, face, 0, nullptr, kCube, kCube);
            JudgeWrite(w, r, src, kContract.cubeLevel0,
                       "C" + std::to_string(face + 1) + " cube face " + std::to_string(face) +
                       " full level 0 upload (signature texel " + ColorText(PureFaceColour(face)) + ")");
        }

        // ---- C7: writing one face must NOT land in another face ----------------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            bool anyFaceConfusable = false;
            for (int face = 0; face < 6; ++face)
            {
                const ReadProbe r = ReadCube(cube, face, 0, nullptr, kCube, kCube);
                if (!r.read) { anyFaceConfusable = true; break; }
                for (int other = 0; other < 6; ++other)
                {
                    if (other == face) continue;
                    if (Same(r.data[0], PureFaceColour(other))) anyFaceConfusable = true;
                }
            }
            check(!anyFaceConfusable,
                  "C7 cube: after six independent uploads no face holds another face's signature "
                  "colour -- storing into the wrong CubeMapFace cannot pass");
        }
        else
        {
            check(true, "C7 cube: face-confusion probe skipped -- this renderer stores no cube face");
        }

        // ---- C8: single texel, away from every edge, leaves the rest of the face intact -----
        if (kContract.cubeLevel0 == Support::Exact)
        {
            const Rectangle r(5, 3, 1, 1);
            const std::vector<Color> patch{Color(3, 240, 17, 129)};
            const WriteProbe w = WriteCube(cube, 2, 0, &r, patch, 0, 1);
            const ReadProbe  got = ReadCube(cube, 2, 0, nullptr, kCube, kCube);

            std::vector<Color> want = CubeFacePattern(2, 0);
            want[static_cast<std::size_t>(3) * kCube + 5] = patch[0];
            const Compare c = CompareContent(got.data, want);
            check(w.returnedNormally && got.read && c.exact == c.total,
                  "C8 cube: a 1x1 SetData at (5,3) stores exactly that texel and leaves every "
                  "other texel of the face unchanged" + WriteFacts(w) + CompareFacts(c));
        }
        else
        {
            const Rectangle r(5, 3, 1, 1);
            const std::vector<Color> patch{Color(3, 240, 17, 129)};
            const WriteProbe w = WriteCube(cube, 2, 0, &r, patch, 0, 1);
            JudgeWrite(w, {}, {}, Support::Unsupported, "C8 cube: 1x1 rect upload");
        }

        // ---- C9: interior rectangle -------------------------------------------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            const Rectangle r(2, 2, 4, 3);
            std::vector<Color> patch;
            for (int i = 0; i < 12; ++i)
                patch.push_back(Color(static_cast<std::uint8_t>(40 + i * 7),
                                      static_cast<std::uint8_t>(90 + i * 3),
                                      static_cast<std::uint8_t>(200 - i * 9),
                                      static_cast<std::uint8_t>(60 + i * 11)));
            const WriteProbe w = WriteCube(cube, 3, 0, &r, patch, 0, 12);
            const ReadProbe  got = ReadCube(cube, 3, 0, nullptr, kCube, kCube);

            std::vector<Color> want = CubeFacePattern(3, 0);
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 4; ++x)
                    want[static_cast<std::size_t>(2 + y) * kCube + (2 + x)] =
                        patch[static_cast<std::size_t>(y) * 4 + x];
            const Compare c = CompareContent(got.data, want);
            check(w.returnedNormally && got.read && c.exact == c.total,
                  "C9 cube: a middle 4x3 rect at (2,2) stores exactly those texels and nothing "
                  "around them" + WriteFacts(w) + CompareFacts(c));
        }
        else
        {
            check(true, "C9 cube: middle-rect probe skipped -- this renderer stores no cube face");
        }

        // ---- C10/C11/C12: final row, final column, lower-right corner ----------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            struct Edge { Rectangle rect; int w; int h; const char* what; };
            const Edge edges[3] = {
                {Rectangle(0, kCube - 1, kCube, 1), kCube, 1,
                 "C10 cube: final row -- a vertically flipped upload cannot pass"},
                {Rectangle(kCube - 1, 0, 1, kCube), 1, kCube,
                 "C11 cube: final column -- a horizontally flipped upload cannot pass"},
                {Rectangle(kCube - 1, kCube - 1, 1, 1), 1, 1,
                 "C12 cube: lower-right corner texel"},
            };
            for (int e = 0; e < 3; ++e)
            {
                const int face = 4;
                std::vector<Color> patch;
                const int n = edges[e].w * edges[e].h;
                for (int i = 0; i < n; ++i)
                    patch.push_back(Color(static_cast<std::uint8_t>(7 + e * 31 + i * 5),
                                          static_cast<std::uint8_t>(19 + i * 13),
                                          static_cast<std::uint8_t>(233 - i * 3),
                                          static_cast<std::uint8_t>(101 + e * 17)));
                const WriteProbe w = WriteCube(cube, face, 0, &edges[e].rect, patch, 0, n);
                const ReadProbe  got = ReadCube(cube, face, 0, &edges[e].rect, edges[e].w, edges[e].h);
                JudgeWrite(w, got, patch, Support::Exact, edges[e].what);
            }
            // Restore face 4 so later whole-face expectations stay valid.
            UploadCubeLevel(cube, 0);
        }
        else
        {
            check(true, "C10-C12 cube: edge-rect probes skipped -- this renderer stores no cube face");
        }

        // ---- C13: non-zero source startIndex, and source elements outside the window --------
        {
            const Rectangle r(1, 1, 3, 2);
            std::vector<Color> body;
            for (int i = 0; i < 6; ++i)
                body.push_back(Color(static_cast<std::uint8_t>(60 + i * 9),
                                     static_cast<std::uint8_t>(140 - i * 6),
                                     static_cast<std::uint8_t>(25 + i * 21),
                                     static_cast<std::uint8_t>(180 - i * 4)));
            const int pad = 5;
            const std::vector<Color> src = PaddedSource(body, pad);
            const WriteProbe w = WriteCube(cube, 1, 0, &r, src, pad, 6);
            const ReadProbe  got = ReadCube(cube, 1, 0, &r, 3, 2);
            JudgeWrite(w, got, body, kContract.cubeLevel0,
                       "C13 cube: startIndex=" + std::to_string(pad) + " uploads exactly the source "
                       "window -- the Poison() elements on both sides of it must never appear");
        }

        // ---- C14: elementCount larger than the region uploads the region only --------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            const Rectangle r(0, 0, 2, 2);
            std::vector<Color> src;
            for (int i = 0; i < 4; ++i)
                src.push_back(Color(static_cast<std::uint8_t>(200 - i * 11),
                                    static_cast<std::uint8_t>(31 + i * 17),
                                    static_cast<std::uint8_t>(77 + i * 5),
                                    static_cast<std::uint8_t>(211 - i * 9)));
            for (int i = 0; i < 3; ++i) src.push_back(Poison());   // beyond the 2x2 region
            const WriteProbe w = WriteCube(cube, 0, 0, &r, src, 0, 7);
            const ReadProbe  got = ReadCube(cube, 0, 0, &r, 2, 2);
            const std::vector<Color> want(src.begin(), src.begin() + 4);
            JudgeWrite(w, got, want, Support::Exact,
                       "C14 cube: elementCount above the requested region stores the region only");
            UploadCubeLevel(cube, 0);
        }
        else
        {
            check(true, "C14 cube: oversized-elementCount probe skipped -- no cube storage here");
        }

        // ---- C15: repeated updates A -> B -> A ---------------------------------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            bool ok = true;
            std::string detail;
            for (int gen : {1, 0})
            {
                UploadCubeLevel(cube, 0, gen);
                for (int face = 0; face < 6 && ok; ++face)
                {
                    const ReadProbe r = ReadCube(cube, face, 0, nullptr, kCube, kCube);
                    const Compare   c = CompareContent(r.data, CubeFacePattern(face, 0, gen));
                    if (!r.read || c.exact != c.total)
                    {
                        ok = false;
                        detail = " (gen " + std::to_string(gen) + ", face " + std::to_string(face) +
                                 CompareFacts(c) + ")";
                    }
                }
            }
            check(ok, "C15 cube: A -> B -> A repeated uploads each fully replace the previous "
                      "generation on every face" + detail);
        }
        else
        {
            check(true, "C15 cube: repeated-update probe skipped -- no cube storage here");
        }

        // ---- C16: mip level > 0 -------------------------------------------------------------
        {
            const int level = 1;
            const int dim = MipDim(kCube, level);
            const std::vector<Color> src = CubeFacePattern(2, level);
            const WriteProbe w = WriteCube(cube, 2, level, nullptr, src, 0, static_cast<int>(src.size()));
            const ReadProbe  got = ReadCube(cube, 2, level, nullptr, dim, dim);
            JudgeWrite(w, got, src, kContract.cubeMip,
                       "C16 cube: mip level 1 (" + std::to_string(dim) + "x" + std::to_string(dim) +
                       ") upload");
        }

        // ---- C17: an unsupported mip must not damage level 0 ---------------------------------
        if (kContract.cubeLevel0 == Support::Exact && kContract.cubeMip == Support::Unsupported)
        {
            const std::vector<Color> src = CubeFacePattern(0, 1);
            const WriteProbe w = WriteCube(cube, 0, 1, nullptr, src, 0, static_cast<int>(src.size()));
            const ReadProbe  got = ReadCube(cube, 0, 0, nullptr, kCube, kCube);
            const Compare    c = CompareContent(got.data, CubeFacePattern(0, 0));
            check(w.threwNotSupported && got.read && c.exact == c.total,
                  "C17 cube: a rejected mip upload leaves level 0 byte-for-byte intact" +
                  WriteFacts(w) + CompareFacts(c));
        }
        else
        {
            check(true, "C17 cube: rejected-mip atomicity probe not applicable on this renderer");
        }

        // ---- C18: two independent cubes do not alias ------------------------------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            TextureCube other(dev, kCube, false, SurfaceFormat::Color);
            const std::vector<Color> flat(static_cast<std::size_t>(kCube) * kCube,
                                          Color(11, 22, 33, 44));
            const WriteProbe w = WriteCube(other, 0, 0, nullptr, flat, 0, static_cast<int>(flat.size()));
            const ReadProbe  a = ReadCube(cube, 0, 0, nullptr, kCube, kCube);
            const ReadProbe  b = ReadCube(other, 0, 0, nullptr, kCube, kCube);
            check(w.returnedNormally && a.read && b.read &&
                  Same(a.data[0], PureFaceColour(0)) && Same(b.data[0], Color(11, 22, 33, 44)),
                  "C18 cube: writing one TextureCube does not touch another [first=" +
                  (a.read ? ColorText(a.data[0]) : std::string("<rejected>")) + " second=" +
                  (b.read ? ColorText(b.data[0]) : std::string("<rejected>")) + "]");
        }
        else
        {
            check(true, "C18 cube: independence probe skipped -- no cube storage here");
        }

        // ---- C19: mutating the source after SetData must not change the resource -------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            std::vector<Color> src = CubeFacePattern(5, 0, 1);
            const std::vector<Color> asUploaded = src;
            const WriteProbe w = WriteCube(cube, 5, 0, nullptr, src, 0, static_cast<int>(src.size()));
            for (auto& c : src) c = Poison();                 // caller's buffer dies right here
            const ReadProbe got = ReadCube(cube, 5, 0, nullptr, kCube, kCube);
            const Compare   c = CompareContent(got.data, asUploaded);
            check(w.returnedNormally && got.read && c.exact == c.total && c.poisoned == 0,
                  "C19 cube: overwriting the caller's source array after SetData returns does not "
                  "change the stored face -- the upload does not depend on caller memory" +
                  CompareFacts(c));
            UploadCubeLevel(cube, 0);
        }
        else
        {
            check(true, "C19 cube: source-lifetime probe skipped -- no cube storage here");
        }

        // ---- C20..C26: argument validation, and nothing stored by a rejected call -------------
        {
            const std::vector<Color> src = CubeFacePattern(0, 0);
            check(Throws<std::invalid_argument>([&] {
                      cube.SetData(CubeMapFace::PositiveX, static_cast<const Color*>(nullptr), kCube * kCube);
                  }),
                  "C20 cube: null source throws std::invalid_argument");
            check(Throws<std::out_of_range>([&] {
                      cube.SetData(CubeMapFace::PositiveX, src.data(), 0);
                  }),
                  "C21 cube: elementCount of 0 throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.SetData(CubeMapFace::PositiveX, src.data(), -1, kCube * kCube);
                  }),
                  "C22 cube: negative startIndex throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.SetData(CubeMapFace::PositiveX, -1, nullptr, src.data(), 0, kCube * kCube);
                  }),
                  "C23 cube: negative mip level throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      const Rectangle bad(kCube - 1, 0, 4, 4);
                      cube.SetData(CubeMapFace::PositiveX, 0, &bad, src.data(), 0, 16);
                  }),
                  "C24 cube: a rectangle crossing the face edge throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.SetData(CubeMapFace::PositiveX, 0, nullptr, src.data(), 0, 4);
                  }),
                  "C25 cube: elementCount below the requested region throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      cube.SetData(static_cast<CubeMapFace>(9), src.data(), kCube * kCube);
                  }),
                  "C26 cube: an out-of-range CubeMapFace throws std::out_of_range");

            if (kContract.cubeLevel0 == Support::Exact)
            {
                const ReadProbe got = ReadCube(cube, 0, 0, nullptr, kCube, kCube);
                const Compare   c = CompareContent(got.data, CubeFacePattern(0, 0));
                check(got.read && c.exact == c.total,
                      "C27 cube: every rejected call above stored nothing -- face 0 still holds its "
                      "previous content exactly" + CompareFacts(c));
            }
            else
            {
                check(true, "C27 cube: post-rejection content probe skipped -- no cube storage here");
            }
        }

        // ---- C28: valid -> invalid -> valid, the resource stays usable ------------------------
        if (kContract.cubeLevel0 == Support::Exact)
        {
            const std::vector<Color> a = CubeFacePattern(1, 0, 2);
            const WriteProbe wa = WriteCube(cube, 1, 0, nullptr, a, 0, static_cast<int>(a.size()));

            // An invalid write B: a rectangle that runs off the face.
            const Rectangle bad(kCube - 2, kCube - 2, 4, 4);
            bool rejectedB = false;
            try { cube.SetData(CubeMapFace::NegativeX, 0, &bad, a.data(), 0, 16); }
            catch (const std::out_of_range&) { rejectedB = true; }
            catch (...) {}

            const ReadProbe afterB = ReadCube(cube, 1, 0, nullptr, kCube, kCube);
            const Compare   cb = CompareContent(afterB.data, a);

            const std::vector<Color> cpat = CubeFacePattern(1, 0, 3);
            const WriteProbe wc = WriteCube(cube, 1, 0, nullptr, cpat, 0, static_cast<int>(cpat.size()));
            const ReadProbe  afterC = ReadCube(cube, 1, 0, nullptr, kCube, kCube);
            const Compare    cc = CompareContent(afterC.data, cpat);

            check(wa.returnedNormally && rejectedB && afterB.read && cb.exact == cb.total &&
                  wc.returnedNormally && afterC.read && cc.exact == cc.total,
                  "C28 cube: write A -> rejected write B -> write C leaves A intact through the "
                  "rejection and the resource fully usable afterwards" + CompareFacts(cb) +
                  CompareFacts(cc));
            UploadCubeLevel(cube, 0);
        }
        else
        {
            check(true, "C28 cube: valid/invalid/valid probe skipped -- no cube storage here");
        }

        // ---- C29: a disposed cube refuses deterministically -----------------------------------
        {
            TextureCube dead(dev, kCube, false, SurfaceFormat::Color);
            const std::vector<Color> flat(static_cast<std::size_t>(kCube) * kCube, Color(9, 8, 7, 6));
            (void)WriteCube(dead, 0, 0, nullptr, flat, 0, static_cast<int>(flat.size()));
            dead.Dispose();
            const WriteProbe w = WriteCube(dead, 0, 0, nullptr, flat, 0, static_cast<int>(flat.size()));
            check(w.threwDisposed && !w.returnedNormally,
                  "C29 cube: SetData on a disposed TextureCube throws ObjectDisposedException "
                  "instead of silently doing nothing" + WriteFacts(w));

            dead.Dispose();   // REMED-GFX-039: repeated Dispose stays harmless
            const WriteProbe w2 = WriteCube(dead, 0, 0, nullptr, flat, 0, static_cast<int>(flat.size()));
            check(w2.threwDisposed && !w2.returnedNormally,
                  "C30 cube: a second Dispose() does not change the refusal" + WriteFacts(w2));
        }
    }

    // =====================================================================
    // RenderTargetCube check
    // =====================================================================

    void RunRenderTargetCubeCheck(GraphicsDevice& dev)
    {
        // RenderTargetCube IS a TextureCube, so it inherits SetData. Every renderer except EasyGL
        // leaves IRenderTargetCubeRenderer::SetData at its `{}` no-op body, which is the same
        // accept-and-discard this finding is about, reached through inheritance instead of a null
        // renderer. Constructing one can legitimately fail on a renderer that has no cube-map render
        // target at all -- that is not what is under test here, so it is reported and skipped.
        std::unique_ptr<RenderTargetCube> rt;
        try
        {
            rt = std::make_unique<RenderTargetCube>(dev, kCube, false, SurfaceFormat::Color,
                                                    DepthFormat::None, 0,
                                                    RenderTargetUsage::DiscardContents);
        }
        catch (const std::exception& e)
        {
            check(true, std::string("R1 rtcube: RenderTargetCube construction is refused on this "
                                    "renderer -- inherited SetData is unreachable [") + e.what() + "]");
            return;
        }

        // REMED-GFX-134 gave this resource a public readback, so the upload is now VERIFIED here
        // rather than merely "did not throw". A uniform colour cannot tell a stored face from a
        // dropped one, so the source carries the same per-texel pattern the plain-cube checks use.
        //
        // plan_vulkan.md VULKAN-406: ALL SIX faces, not just face 2. One face cannot tell a cube
        // whose layers are each independently initialised from one that got the single layer this
        // check happened to touch -- which is the shape of the defect that row names, and face 2 is
        // simply the one it named because it was the only one asked for.
        for (int face = 0; face < 6; ++face)
        {
            const std::vector<Color> src = CubeFacePattern(face, 0);
            const WriteProbe w = WriteCube(*rt, face, 0, nullptr, src, 0, static_cast<int>(src.size()));
            const ReadProbe r = ReadCube(*rt, face, 0, nullptr, kCube, kCube);
            JudgeWrite(w, r, src, kContract.rtCube,
                       "R1 rtcube face " + std::to_string(face) +
                           ": RenderTargetCube::SetData must either store the face or refuse -- "
                           "never accept it and drop it");
        }
    }

    /**
     * @brief VULKAN-406 -- the Khronos layer's verdict on every cube, volume and cube-target
     *        subresource this fixture just touched.
     *
     * The layer's own liveness is asserted first: an empty message list from a layer that never
     * loaded is not evidence. Compiled only for the Vulkan build; the thirteen other renderers
     * registering this source are unaffected.
     */
    void CheckValidationClean()
    {
#if defined(CNA_RENDERER_VULKAN)
        using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
        check(VulkanRenderer::IsValidationActiveEXT(),
              "V1 VK_LAYER_KHRONOS_validation is loaded, so the count below means something");
        auto* vk = dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
        if (!vk) { check(false, "V2 Vulkan renderer not reachable"); return; }
        const auto& msgs = vk->GetValidationMessagesEXT();
        check(msgs.empty(),
              "V2 no Vulkan validation message" +
                  (msgs.empty() ? std::string{} : std::string(" -- first: ") + msgs.front()));
#endif
    }

    // =====================================================================
    // Volume checks
    // =====================================================================

    void RunVolumeChecks(GraphicsDevice& dev)
    {
        if (!kContract.volumeConstructs)
        {
            bool threw = false;
            try { Texture3D probe(dev, kVolW, kVolH, kVolD, false, SurfaceFormat::Color); }
            catch (const System::NotSupportedException&) { threw = true; }
            catch (...) {}
            check(threw,
                  "V1 volume: Texture3D construction is refused on this renderer, so no upload can "
                  "reach a resource that does not exist (REMED-CONTENT-004)");
            return;
        }

        Texture3D vol(dev, kVolW, kVolH, kVolD, /*mipMap=*/true, SurfaceFormat::Color);

        // ---- Z3/Z4: the volume half of Z1/Z2, runtime-derived ------------------------------
        {
            const std::vector<Color> src = VolumePattern(0);
            const WriteProbe w = WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, src, 0,
                                             static_cast<int>(src.size()));
            const ReadProbe  r = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
            const Compare    c = CompareContent(r.data, src);
            check(!w.returnedNormally || (r.read && c.exact == c.total),
                  "Z3 volume: a SetData that returns normally must be readable back EXACTLY" +
                  WriteFacts(w) + CompareFacts(c));
            check(w.returnedNormally == r.read,
                  "Z4 volume: SetData and GetData agree about whether this resource has storage" +
                  WriteFacts(w) + " [readBack=" + std::string(r.read ? "1" : "0") + "]");
        }

        // ---- V2: full volume, every slice distinct -----------------------------------------
        {
            const std::vector<Color> src = VolumePattern(0);
            const WriteProbe w = WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, src, 0,
                                             static_cast<int>(src.size()));
            const ReadProbe  r = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
            JudgeWrite(w, r, src, kContract.volumeLevel0,
                       "V2 volume: a full-volume upload stores every voxel -- a flattened, "
                       "slice-duplicated or slice-reversed store cannot pass");
        }

        // ---- V3: each Z slice read back on its own -----------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            bool ok = true;
            std::string detail;
            for (int z = 0; z < kVolD && ok; ++z)
            {
                const ReadProbe r = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, z, z + 1);
                const Compare   c = CompareContent(r.data,
                                                   ExpectedVolumeBox(0, 0, 0, kVolW, kVolH, z, z + 1));
                if (!r.read || c.exact != c.total)
                {
                    ok = false;
                    detail = " (slice " + std::to_string(z) + CompareFacts(c) + ")";
                }
            }
            check(ok, "V3 volume: every depth slice holds its own distinct content" + detail);
        }
        else
        {
            check(true, "V3 volume: per-slice probe skipped -- no volume storage here");
        }

        // ---- V4: interior sub-box, nothing outside it changes -------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            const Color patch(5, 205, 45, 155);
            const std::vector<Color> patchSrc(4, patch);
            const WriteProbe w = WriteVolume(vol, 0, 1, 1, 3, 3, 2, 3, patchSrc, 0, 4);
            const ReadProbe  full = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);

            std::vector<Color> want = VolumePattern(0);
            for (int y = 1; y < 3; ++y)
                for (int x = 1; x < 3; ++x)
                    want[(static_cast<std::size_t>(2) * kVolH + y) * kVolW + x] = patch;
            const Compare c = CompareContent(full.data, want);
            check(w.returnedNormally && full.read && c.exact == c.total,
                  "V4 volume: a 2x2x1 sub-box at (1,1,2) stores exactly those voxels and leaves "
                  "every voxel outside the box unchanged" + WriteFacts(w) + CompareFacts(c));

            // Restore for later whole-volume expectations.
            const std::vector<Color> src = VolumePattern(0);
            (void)WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, src, 0, static_cast<int>(src.size()));
        }
        else
        {
            check(true, "V4 volume: sub-box probe skipped -- no volume storage here");
        }

        // ---- V5: single voxel, and V6: the final slice -------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            const std::vector<Color> one{Color(250, 12, 99, 143)};
            const WriteProbe w5 = WriteVolume(vol, 0, 2, 1, 3, 2, 3, 4, one, 0, 1);
            const ReadProbe  r5 = ReadVolume(vol, 0, 2, 1, 3, 2, 3, 4);
            JudgeWrite(w5, r5, one, Support::Exact,
                       "V5 volume: a single voxel at (2,1,3) -- front/back is a Z RANGE, not a count");

            std::vector<Color> lastSlice;
            for (int i = 0; i < kVolW * kVolH; ++i)
                lastSlice.push_back(Color(static_cast<std::uint8_t>(30 + i * 6),
                                          static_cast<std::uint8_t>(210 - i * 4),
                                          static_cast<std::uint8_t>(55 + i * 9),
                                          static_cast<std::uint8_t>(170 - i * 2)));
            const WriteProbe w6 = WriteVolume(vol, 0, 0, 0, kVolW, kVolH, kVolD - 1, kVolD,
                                              lastSlice, 0, kVolW * kVolH);
            const ReadProbe  r6 = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, kVolD - 1, kVolD);
            JudgeWrite(w6, r6, lastSlice, Support::Exact,
                       "V6 volume: the final depth slice -- a reversed Z convention cannot pass");

            const std::vector<Color> src = VolumePattern(0);
            (void)WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, src, 0, static_cast<int>(src.size()));
        }
        else
        {
            check(true, "V5-V6 volume: voxel/slice probes skipped -- no volume storage here");
        }

        // ---- V7: non-zero source startIndex --------------------------------------------------
        {
            const std::vector<Color> body = ExpectedVolumeBox(0, 1, 0, 3, 2, 1, 3, 4);
            const int pad = 7;
            const std::vector<Color> src = PaddedSource(body, pad);
            const WriteProbe w = WriteVolume(vol, 0, 1, 0, 3, 2, 1, 3, src, pad,
                                             static_cast<int>(body.size()));
            const ReadProbe  r = ReadVolume(vol, 0, 1, 0, 3, 2, 1, 3);
            JudgeWrite(w, r, body, kContract.volumeLevel0,
                       "V7 volume: startIndex=" + std::to_string(pad) + " uploads exactly the source "
                       "window -- the Poison() elements around it must never appear");
            if (kContract.volumeLevel0 == Support::Exact)
            {
                const std::vector<Color> restore = VolumePattern(0);
                (void)WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, restore, 0,
                                  static_cast<int>(restore.size()));
            }
        }

        // ---- V8: repeated updates A -> B -> A -------------------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            bool ok = true;
            std::string detail;
            for (int gen : {1, 0})
            {
                const std::vector<Color> src = VolumePattern(0, gen);
                (void)WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, src, 0,
                                  static_cast<int>(src.size()));
                const ReadProbe r = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
                const Compare   c = CompareContent(r.data, src);
                if (!r.read || c.exact != c.total)
                {
                    ok = false;
                    detail = " (gen " + std::to_string(gen) + CompareFacts(c) + ")";
                }
            }
            check(ok, "V8 volume: A -> B -> A repeated uploads each fully replace the previous "
                      "generation" + detail);
        }
        else
        {
            check(true, "V8 volume: repeated-update probe skipped -- no volume storage here");
        }

        // ---- V9: mip level > 0 -----------------------------------------------------------------
        {
            const int level = 1;
            const int w = MipDim(kVolW, level);
            const int h = MipDim(kVolH, level);
            const int d = MipDim(kVolD, level);
            const std::vector<Color> src = VolumePattern(level);
            const WriteProbe wp = WriteVolume(vol, level, 0, 0, w, h, 0, d, src, 0,
                                              static_cast<int>(src.size()));
            const ReadProbe  r = ReadVolume(vol, level, 0, 0, w, h, 0, d);
            JudgeWrite(wp, r, src, kContract.volumeMip,
                       "V9 volume: mip level 1 (" + std::to_string(w) + "x" + std::to_string(h) +
                       "x" + std::to_string(d) + ") upload");
        }

        // ---- V10: two independent volumes do not alias ------------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            Texture3D other(dev, kVolW, kVolH, kVolD, false, SurfaceFormat::Color);
            const std::vector<Color> flat(static_cast<std::size_t>(kVolW) * kVolH * kVolD,
                                          Color(11, 22, 33, 44));
            const WriteProbe w = WriteVolume(other, 0, 0, 0, kVolW, kVolH, 0, kVolD, flat, 0,
                                             static_cast<int>(flat.size()));
            const ReadProbe a = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
            const ReadProbe b = ReadVolume(other, 0, 0, 0, kVolW, kVolH, 0, kVolD);
            check(w.returnedNormally && a.read && b.read &&
                  Same(a.data[0], VolumeVoxel(0, 0, 0, 0)) && Same(b.data[0], Color(11, 22, 33, 44)),
                  "V10 volume: writing one Texture3D does not touch another [first=" +
                  (a.read ? ColorText(a.data[0]) : std::string("<rejected>")) + " second=" +
                  (b.read ? ColorText(b.data[0]) : std::string("<rejected>")) + "]");
        }
        else
        {
            check(true, "V10 volume: independence probe skipped -- no volume storage here");
        }

        // ---- V11: source lifetime ------------------------------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            std::vector<Color> src = VolumePattern(0, 4);
            const std::vector<Color> asUploaded = src;
            const WriteProbe w = WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, src, 0,
                                             static_cast<int>(src.size()));
            for (auto& c : src) c = Poison();
            const ReadProbe r = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
            const Compare   c = CompareContent(r.data, asUploaded);
            check(w.returnedNormally && r.read && c.exact == c.total && c.poisoned == 0,
                  "V11 volume: overwriting the caller's source array after SetData returns does not "
                  "change the stored volume" + CompareFacts(c));
            const std::vector<Color> restore = VolumePattern(0);
            (void)WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, restore, 0,
                              static_cast<int>(restore.size()));
        }
        else
        {
            check(true, "V11 volume: source-lifetime probe skipped -- no volume storage here");
        }

        // ---- V12..V18: argument validation ------------------------------------------------------
        {
            const std::vector<Color> src = VolumePattern(0);
            check(Throws<std::invalid_argument>([&] {
                      vol.SetData(nullptr, kVolW * kVolH * kVolD);
                  }),
                  "V12 volume: null source throws std::invalid_argument");
            check(Throws<std::out_of_range>([&] { vol.SetData(src.data(), 0); }),
                  "V13 volume: elementCount of 0 throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.SetData(src.data(), -1, kVolW * kVolH * kVolD);
                  }),
                  "V14 volume: negative startIndex throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.SetData(-1, 0, 0, kVolW, kVolH, 0, kVolD, src.data(), 0,
                                  static_cast<int>(src.size()));
                  }),
                  "V15 volume: negative mip level throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.SetData(0, 2, 0, 2, kVolH, 0, kVolD, src.data(), 0, 4);
                  }),
                  "V16 volume: left == right throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.SetData(0, 0, 0, kVolW, kVolH, 2, 1, src.data(), 0, 4);
                  }),
                  "V17 volume: back < front throws std::out_of_range");
            check(Throws<std::out_of_range>([&] {
                      vol.SetData(0, 0, 0, kVolW, kVolH, 0, kVolD, src.data(), 0, 2);
                  }),
                  "V18 volume: elementCount below the requested box throws std::out_of_range");

            if (kContract.volumeLevel0 == Support::Exact)
            {
                const ReadProbe r = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
                const Compare   c = CompareContent(r.data, VolumePattern(0));
                check(r.read && c.exact == c.total,
                      "V19 volume: every rejected call above stored nothing -- the volume still "
                      "holds its previous content exactly" + CompareFacts(c));
            }
            else
            {
                check(true, "V19 volume: post-rejection content probe skipped -- no volume storage");
            }
        }

        // ---- V20: valid -> invalid -> valid -----------------------------------------------------
        if (kContract.volumeLevel0 == Support::Exact)
        {
            const std::vector<Color> a = VolumePattern(0, 5);
            const WriteProbe wa = WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, a, 0,
                                              static_cast<int>(a.size()));
            bool rejectedB = false;
            try { vol.SetData(0, 0, 0, kVolW, kVolH, 3, 2, a.data(), 0, 4); }
            catch (const std::out_of_range&) { rejectedB = true; }
            catch (...) {}
            const ReadProbe afterB = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
            const Compare   cb = CompareContent(afterB.data, a);

            const std::vector<Color> cpat = VolumePattern(0, 6);
            const WriteProbe wc = WriteVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD, cpat, 0,
                                              static_cast<int>(cpat.size()));
            const ReadProbe  afterC = ReadVolume(vol, 0, 0, 0, kVolW, kVolH, 0, kVolD);
            const Compare    cc = CompareContent(afterC.data, cpat);
            check(wa.returnedNormally && rejectedB && afterB.read && cb.exact == cb.total &&
                  wc.returnedNormally && afterC.read && cc.exact == cc.total,
                  "V20 volume: write A -> rejected write B -> write C leaves A intact through the "
                  "rejection and the resource fully usable afterwards" + CompareFacts(cb) +
                  CompareFacts(cc));
        }
        else
        {
            check(true, "V20 volume: valid/invalid/valid probe skipped -- no volume storage here");
        }

        // ---- V21: a disposed volume refuses deterministically -------------------------------------
        {
            Texture3D dead(dev, kVolW, kVolH, kVolD, false, SurfaceFormat::Color);
            const std::vector<Color> flat(static_cast<std::size_t>(kVolW) * kVolH * kVolD,
                                          Color(9, 8, 7, 6));
            (void)WriteVolume(dead, 0, 0, 0, kVolW, kVolH, 0, kVolD, flat, 0,
                              static_cast<int>(flat.size()));
            dead.Dispose();
            const WriteProbe w = WriteVolume(dead, 0, 0, 0, kVolW, kVolH, 0, kVolD, flat, 0,
                                             static_cast<int>(flat.size()));
            check(w.threwDisposed && !w.returnedNormally,
                  "V21 volume: SetData on a disposed Texture3D throws ObjectDisposedException "
                  "instead of silently doing nothing" + WriteFacts(w));

            dead.Dispose();
            const WriteProbe w2 = WriteVolume(dead, 0, 0, 0, kVolW, kVolH, 0, kVolD, flat, 0,
                                              static_cast<int>(flat.size()));
            check(w2.threwDisposed && !w2.returnedNormally,
                  "V22 volume: a second Dispose() does not change the refusal" + WriteFacts(w2));
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
        std::printf("REMED-GFX-135 TextureCube/Texture3D SetData contract -- renderer %s\n",
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
        RunRenderTargetCubeCheck(dev);
        RunVolumeChecks(dev);
        CheckValidationClean();

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    CubeVolumeSetDataContractTest test;
    test.Run();
    return test.Result();
}
