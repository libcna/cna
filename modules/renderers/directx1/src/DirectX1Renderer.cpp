#include "CNA/Internal/Renderers/DirectX1/DirectX1Renderer.hpp"

// plan_dx1.md design decision 9: <ddraw.h> (and the real <windows.h> it pulls in) is contained to
// this .cpp only -- see DirectX1Renderer.hpp's own comment. plan_dx1.md section 1's discipline:
// this file may only name v1 DirectDraw symbols (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC/
// DirectDrawCreate) -- never IDirectDraw2+/IDirectDrawSurface2+/DDSURFACEDESC2, and never anything
// from <d3d.h> (DX1-1's grep-based CTest asserts this automatically).
#include <ddraw.h>

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::DirectX1
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;

    namespace
    {
        [[noreturn]] void ThrowHr(const char* what, HRESULT hr)
        {
            throw std::runtime_error(std::string(what) + " failed: HRESULT=0x" +
                                      std::to_string(static_cast<unsigned long>(hr)));
        }

        // ---- 3D pipeline: DirectX 1 shipped no Direct3D at all. All 3D calls throw. ----
        // Callers can check GraphicsDevice::SupportsCapability(GraphicsCapability::ThreeD) ahead
        // of time instead of relying on this throw -- see SupportsCapability() in the header.
        [[noreturn]] void ThrowNo3D(const char* methodName)
        {
            throw std::runtime_error(std::string("DIRECTX1 (DirectDraw v1) does not support 3D: ") + methodName);
        }

        // ---- Phase O3: shared offscreen-surface helpers, used by both DirectX1TextureRenderer and
        // DirectX1RenderTargetRenderer (design decision 7: 32bpp DDSCAPS_OFFSCREENPLAIN only, no
        // palette/8-bit path). Kept as free functions rather than a shared base class: neither type
        // is ever named outside this .cpp (only returned polymorphically as ITextureRenderer/
        // IRenderTargetRenderer), so there is no header/ddraw.h containment reason to give them one
        // (design decision 9).
        // Real bug found and fixed (2026-07-20, reported directly by the project owner after
        // seeing the demo running: a texture that is really yellow rendered as blue/cyan). Root
        // cause: CreateOffscreenSurface never specified an explicit DDPIXELFORMAT, so real Wine
        // ddraw.dll defaults every offscreen surface's byte layout to match the CURRENT DISPLAY
        // MODE's own native format -- confirmed via a real Wine ddraw trace in this environment to
        // be R=0x00ff0000/G=0x0000ff00/B=0x000000ff, i.e. byte order (B,G,R,X) in memory, NOT the
        // (R,G,B,A) byte order every WriteSurfacePixels/FillSurfaceColor/CompositeQuad call in this
        // file assumed (matching every other CNA renderer's own ImageData::pixels convention,
        // SDL_PIXELFORMAT_RGBA32 -- see plan_freedirect.md design decision 4). Both sides of every
        // round-trip through this renderer's OWN surfaces stayed internally consistent (which is why
        // DirectX1_Smoke/DirectX1_Blend/etc.'s Clear()+GetBackBufferData() checks all still passed), but a
        // real IDirectDrawSurface::Blt() between two DIFFERENTLY-formatted surfaces performs genuine
        // pixel-format conversion -- so uploading a real RGBA8 image (a genuinely R,G,B,A-ordered
        // byte stream) directly into a (B,G,R,X)-formatted surface, then Blt()ing it toward the
        // primary, swapped red and blue on real screen output.
        //
        // First attempted fix (explicitly requesting DDPF_RGB with R,G,B masks matching (R,G,B,A)
        // memory order) was itself wrong: real Wine ddraw.dll's CreateSurface rejected it with
        // DDERR_INVALIDPIXELFORMAT -- confirmed empirically, not assumed -- this environment's
        // wined3d-backed surface creation only supports specific native formats, and the reversed
        // byte order isn't one of them. The actually-robust fix (this one): never assume a fixed
        // byte order at all. Query the REAL negotiated DDPIXELFORMAT from the first surface this
        // renderer creates (DetectChannelLayout, below) and use those byte offsets everywhere a raw
        // Lock()'d pixel is read or written (WriteSurfacePixels/ReadSurfacePixels/FillSurfaceColor/
        // SampleTexel/CompositeQuad) -- correct regardless of which native format a given Wine
        // version/environment/display depth happens to negotiate, and no format request is ever
        // rejected since none is made. All offscreen surfaces (textures, render targets, the shadow
        // backbuffer) come from the same IDirectDraw device and get the same native format, so one
        // detection, cached for this renderer instance's lifetime, is correct for all of them. The
        // primary surface is unaffected either way -- design decision 4 already established it is
        // never Lock()'d/written to directly, only ever Blt() INTO from a (now correctly
        // interpreted) shadow/offscreen surface, and real DirectDraw performs its own color
        // conversion during that Blt() regardless of the primary's own native format.
        struct Rgba8Layout
        {
            int r = 0, g = 1, b = 2, a = 3;
        };

        Rgba8Layout g_layout; // NOLINT: single renderer instance, single-threaded rendering context
        bool g_layoutDetected = false;

        // Returns which byte (0-3, little-endian) an 8-bit-wide, byte-aligned DWORD mask occupies,
        // or -1 if the mask isn't a plain byte-aligned 8-bit field (not expected for any real 32bpp
        // RGB surface format, but checked rather than assumed).
        int ByteOffsetForMask(DWORD mask)
        {
            if (mask == 0) return -1;
            int offset = 0;
            while ((mask & 0xFFu) == 0)
            {
                mask >>= 8;
                ++offset;
                if (offset > 3) return -1;
            }
            return (mask & 0xFFu) == 0xFFu ? offset : -1;
        }

        void DetectChannelLayout(LPDIRECTDRAWSURFACE surface)
        {
            if (g_layoutDetected) return;
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            if (FAILED(surface->GetSurfaceDesc(&desc))) return;

            const int r = ByteOffsetForMask(desc.ddpfPixelFormat.dwRBitMask);
            const int g = ByteOffsetForMask(desc.ddpfPixelFormat.dwGBitMask);
            const int b = ByteOffsetForMask(desc.ddpfPixelFormat.dwBBitMask);
            if (r < 0 || g < 0 || b < 0 || r == g || g == b || r == b) return; // unexpected format; keep the (R,G,B,A) default

            g_layout.r = r;
            g_layout.g = g;
            g_layout.b = b;
            g_layout.a = 0 + 1 + 2 + 3 - r - g - b; // the one leftover byte position of {0,1,2,3}
            g_layoutDetected = true;
        }

        LPDIRECTDRAWSURFACE CreateOffscreenSurface(LPDIRECTDRAW dd, int width, int height)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
            desc.dwWidth = static_cast<DWORD>(width);
            desc.dwHeight = static_cast<DWORD>(height);
            LPDIRECTDRAWSURFACE surface = nullptr;
            const HRESULT hr = dd->CreateSurface(&desc, &surface, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw::CreateSurface(offscreen)", hr);
            DetectChannelLayout(surface);
            return surface;
        }

        // Writes a full level-0 RGBA8 image into `surface` via Lock()/memcpy/Unlock(), remapping
        // each pixel's 4 bytes from canonical (R,G,B,A) source order into g_layout's real native
        // byte positions (see DetectChannelLayout's own comment for why this can't be a plain
        // memcpy). `stride` is the source row length in bytes; <=0 means tightly packed (width*4),
        // matching ITextureRenderer::UpdatePixels's own documented convention.
        void WriteSurfacePixels(LPDIRECTDRAWSURFACE surface, int width, int height,
                                const uint8_t* rgba, int stride)
        {
            if (!rgba) return;
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(write)", hr);

            auto* base = static_cast<uint8_t*>(desc.lpSurface);
            const std::size_t srcStride = stride > 0 ? static_cast<std::size_t>(stride) : static_cast<std::size_t>(width) * 4u;
            for (int row = 0; row < height; ++row)
            {
                uint8_t* dstRow = base + static_cast<std::size_t>(row) * static_cast<std::size_t>(desc.lPitch);
                const uint8_t* srcRow = rgba + static_cast<std::size_t>(row) * srcStride;
                for (int col = 0; col < width; ++col)
                {
                    uint8_t* dp = dstRow + static_cast<std::size_t>(col) * 4u;
                    const uint8_t* sp = srcRow + static_cast<std::size_t>(col) * 4u;
                    dp[g_layout.r] = sp[0]; dp[g_layout.g] = sp[1]; dp[g_layout.b] = sp[2]; dp[g_layout.a] = sp[3];
                }
            }
            surface->Unlock(desc.lpSurface);
        }

        // Reads an (x, y, w, h) region back from `surface` via Lock()/memcpy/Unlock(), remapping
        // each pixel's 4 bytes from g_layout's real native byte positions into tightly-packed
        // canonical (R,G,B,A) rows in `pixels`. Shared by ReadBackbuffer against whichever surface
        // is currently active.
        void ReadSurfacePixels(LPDIRECTDRAWSURFACE surface, int x, int y, int w, int h, uint8_t* pixels)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(read)", hr);

            const auto* base = static_cast<const uint8_t*>(desc.lpSurface);
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* srcRow = base + static_cast<std::size_t>(y + row) * static_cast<std::size_t>(desc.lPitch) +
                                        static_cast<std::size_t>(x) * 4u;
                uint8_t* dstRow = pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u;
                for (int col = 0; col < w; ++col)
                {
                    const uint8_t* sp = srcRow + static_cast<std::size_t>(col) * 4u;
                    uint8_t* dp = dstRow + static_cast<std::size_t>(col) * 4u;
                    dp[0] = sp[g_layout.r]; dp[1] = sp[g_layout.g]; dp[2] = sp[g_layout.b]; dp[3] = sp[g_layout.a];
                }
            }
            surface->Unlock(desc.lpSurface);
        }

        // Fills the full (width, height) extent of `surface` with a solid RGBA8 color via
        // Lock()/Unlock() (not DDBLT_COLORFILL -- writing all 4 channels directly avoids relying on
        // whatever a given ddraw.dll implementation's ColorFill does with the alpha channel,
        // matching DIRECTX3's own found-and-fixed lesson (plan_freedirect.md DX3-14) proactively instead of
        // re-discovering the same class of bug here). Writes into g_layout's real native byte
        // positions, not fixed (R,G,B,A) positions -- see DetectChannelLayout's own comment.
        void FillSurfaceColor(LPDIRECTDRAWSURFACE surface, int width, int height,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(clear)", hr);

            auto* base = static_cast<uint8_t*>(desc.lpSurface);
            for (int y = 0; y < height; ++y)
            {
                uint8_t* row = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(desc.lPitch);
                for (int x = 0; x < width; ++x)
                {
                    uint8_t* px = row + static_cast<std::size_t>(x) * 4u;
                    px[g_layout.r] = r; px[g_layout.g] = g; px[g_layout.b] = b; px[g_layout.a] = a;
                }
            }
            surface->Unlock(desc.lpSurface);
        }

        [[noreturn]] void ThrowMipLevelUnsupported(int level)
        {
            throw std::runtime_error(
                "DIRECTX1 (DirectDraw v1) does not support mip-level texture uploads (level " +
                std::to_string(level) + "): IDirectDrawSurface has no native mip chain or "
                "per-level LOD sampling. Use Texture2D::SetData(level=0, ...) only.");
        }

        // Real letterbox scale+offset transform between physical window pixels and logical
        // (virtual) game pixels (uniform scale to fit, centered) -- computed fresh from the real
        // physical SDL_Window size on every call, shared by Present() (the on-screen Blt
        // destination) and TransformWindowToLogical/TransformLogicalToWindow, so those three are
        // always mutually consistent and a resize/SetVirtualResolution change is correct on the
        // very next call, unlike DIRECTX3's own documented stale-scale limitation (plan_freedirect.md DX3-16).
        bool ComputeLetterbox(SDL_Window* window, int logicalWidth, int logicalHeight,
                              float& scale, float& offsetX, float& offsetY)
        {
            if (!window || logicalWidth <= 0 || logicalHeight <= 0) return false;
            int physW = 0, physH = 0;
            SDL_GetWindowSize(window, &physW, &physH);
            if (physW <= 0 || physH <= 0) return false;

            scale = std::min(static_cast<float>(physW) / static_cast<float>(logicalWidth),
                             static_cast<float>(physH) / static_cast<float>(logicalHeight));
            offsetX = (static_cast<float>(physW) - static_cast<float>(logicalWidth) * scale) * 0.5f;
            offsetY = (static_cast<float>(physH) - static_cast<float>(logicalHeight) * scale) * 0.5f;
            return true;
        }

        // ---- Phase O4: CPU 2D compositor (design decision 5) ----
        // IDirectDrawSurface::Blt/BltFast has never supported rotation in any DirectX version, so
        // every DirectDraw-family CNA renderer needs this same architecture -- ported verbatim from
        // DIRECTX3's own already-verified CompositeQuad (plan_dx1.md design decision 5), not re-derived.

        // Signed area of triangle (a, b, c) -- also the raw (un-normalized) edge function used by
        // BarycentricWeights below. Positive/negative depending on winding; consistent use of the
        // same sign in both `area` and each per-vertex edge value below makes the inside-test
        // winding-agnostic (correct for both CW and CCW quads, which rotation can produce either
        // of in screen space).
        float EdgeFunction(const Vector2& a, const Vector2& b, const Vector2& c)
        {
            return (c.X - a.X) * (b.Y - a.Y) - (c.Y - a.Y) * (b.X - a.X);
        }

        // Computes barycentric weights of point (px, py) w.r.t. triangle (a, b, c); returns false
        // if (px, py) is outside the triangle or the triangle is degenerate.
        bool BarycentricWeights(const Vector2& a, const Vector2& b, const Vector2& c,
                                float px, float py, float& w0, float& w1, float& w2)
        {
            const Vector2 p(px, py);
            const float area = EdgeFunction(a, b, c);
            if (area == 0.0f) return false;
            w0 = EdgeFunction(b, c, p) / area;
            w1 = EdgeFunction(c, a, p) / area;
            w2 = EdgeFunction(a, b, p) / area;
            return w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f;
        }

        // ---- Phase O5: real, distinct blend formulas + filter/address sampling (design decisions
        // 6/7) ----

        // Matches every other CNA renderer's Blend-enum-ordinal mapping (e.g.
        // SdlRenderer::ToSdlBlendFactor): One=0, Zero=1, SourceColor=2,
        // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
        // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9.
        enum class DirectX1BlendMode { Opaque, AlphaBlend, NonPremultiplied, Additive };

        // Detects which of the 4 BlendState presets (BlendState.cpp) the raw factors match, by
        // exact value -- not by BlendState identity (the renderer never sees a BlendState object,
        // only GraphicsDevice::ApplyBlendState's raw ints).
        // A real preset match requires BOTH the 4 factors AND both blend functions (color/alpha)
        // to match -- all 4 real BlendState.cpp presets use BlendFunction::Add (0) implicitly.
        // A custom BlendState with e.g. Opaque's exact factors but BlendFunction::Subtract is NOT
        // equivalent to Opaque and must fall through to the AlphaBlend fallback below, not be
        // misdetected as Opaque just because the factors happen to match (ported from DX3-44's own
        // found-and-fixed bug).
        DirectX1BlendMode DetectBlendMode(int colorSrc, int alphaSrc, int colorDst, int alphaDst,
                                     int colorFunc, int alphaFunc)
        {
            const bool bothAdd = (colorFunc == 0 && alphaFunc == 0);
            // NonPremultiplied: ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, ColorDst=InvSrcAlpha, AlphaDst=InvSrcAlpha
            if (bothAdd && colorSrc == 4 && alphaSrc == 4 && colorDst == 5 && alphaDst == 5) return DirectX1BlendMode::NonPremultiplied;
            // Additive: ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, ColorDst=One, AlphaDst=One
            if (bothAdd && colorSrc == 4 && alphaSrc == 4 && colorDst == 0 && alphaDst == 0) return DirectX1BlendMode::Additive;
            // Opaque: ColorSrc=One, AlphaSrc=One, ColorDst=Zero, AlphaDst=Zero
            if (bothAdd && colorSrc == 0 && alphaSrc == 0 && colorDst == 1 && alphaDst == 1) return DirectX1BlendMode::Opaque;
            // AlphaBlend: ColorSrc=One, AlphaSrc=One, ColorDst=InvSrcAlpha, AlphaDst=InvSrcAlpha --
            // and the fallback for any other/custom factor+op combination (including a factor
            // match with a non-Add function), same recorded scope limitation SOFTWARE/DIRECTX3 already
            // made (no general blend-equation interpreter in v1).
            return DirectX1BlendMode::AlphaBlend;
        }

        // TextureAddressMode raw int convention (matches ISpriteBatchRenderer::SetSamplerAddressMode's
        // own doc): 0=Wrap, 1=Clamp, 2=Mirror. Maps a possibly out-of-[0,size) integer texel
        // coordinate into range accordingly.
        int WrapCoord(int coord, int size, int addressMode)
        {
            if (addressMode == 0) // Wrap
            {
                int m = coord % size;
                if (m < 0) m += size;
                return m;
            }
            if (addressMode == 2) // Mirror
            {
                const int period = 2 * size;
                int m = coord % period;
                if (m < 0) m += period;
                return m < size ? m : (period - 1 - m);
            }
            return std::clamp(coord, 0, size - 1); // Clamp (default)
        }

        // Samples `srcBase` at normalized (u, v), honoring `filter` (0=Linear/bilinear, else
        // Point/nearest -- matches ISpriteBatchRenderer::SetSamplerFilter's own doc) and
        // `addressU`/`addressV`. Writes 4 bytes into `out` in canonical (R,G,B,A) order, remapped
        // from g_layout's real native byte positions in the source surface -- see
        // DetectChannelLayout's own comment.
        void SampleTexel(const uint8_t* srcBase, int srcPitch, int srcW, int srcH,
                         float u, float v, int filter, int addressU, int addressV, uint8_t out[4])
        {
            const auto texelAt = [&](int xi, int yi) -> const uint8_t*
            {
                const int sx = WrapCoord(xi, srcW, addressU);
                const int sy = WrapCoord(yi, srcH, addressV);
                return srcBase + static_cast<std::size_t>(sy) * static_cast<std::size_t>(srcPitch) +
                       static_cast<std::size_t>(sx) * 4u;
            };
            const int channelOffset[4] = {g_layout.r, g_layout.g, g_layout.b, g_layout.a};

            if (filter == 0) // Linear (bilinear)
            {
                const float fx = u * static_cast<float>(srcW) - 0.5f;
                const float fy = v * static_cast<float>(srcH) - 0.5f;
                const int x0 = static_cast<int>(std::floor(fx));
                const int y0 = static_cast<int>(std::floor(fy));
                const float tx = fx - static_cast<float>(x0);
                const float ty = fy - static_cast<float>(y0);

                const uint8_t* p00 = texelAt(x0, y0);
                const uint8_t* p10 = texelAt(x0 + 1, y0);
                const uint8_t* p01 = texelAt(x0, y0 + 1);
                const uint8_t* p11 = texelAt(x0 + 1, y0 + 1);
                for (int c = 0; c < 4; ++c)
                {
                    const int o = channelOffset[c];
                    const float top = static_cast<float>(p00[o]) * (1.0f - tx) + static_cast<float>(p10[o]) * tx;
                    const float bot = static_cast<float>(p01[o]) * (1.0f - tx) + static_cast<float>(p11[o]) * tx;
                    out[c] = static_cast<uint8_t>(std::clamp(top * (1.0f - ty) + bot * ty, 0.0f, 255.0f));
                }
            }
            else // Point (nearest)
            {
                const uint8_t* p = texelAt(static_cast<int>(std::floor(u * static_cast<float>(srcW))),
                                           static_cast<int>(std::floor(v * static_cast<float>(srcH))));
                out[0] = p[channelOffset[0]]; out[1] = p[channelOffset[1]];
                out[2] = p[channelOffset[2]]; out[3] = p[channelOffset[3]];
            }
        }

        // Manages the Lock()/Unlock() lifetime of a single IDirectDrawSurface across many
        // consecutive CompositeQuad calls that target/sample the SAME surface, instead of every
        // Draw() paying for its own Lock()+Unlock() round-trip (real bug found and fixed: reported
        // directly by the project owner as visible stutter running the 2D demo, which draws dozens
        // of independently-rotating sprites per frame -- every one of them was Lock()ing and
        // Unlock()ing both the shared destination surface AND the (usually shared) source texture
        // surface separately, and each Lock()/Unlock() is a real COM call Wine has to translate,
        // not a cheap pointer fetch). EnsureLocked() is a no-op if already locked to the same
        // surface; Unlock() must be called before any real Blt()/BltFast() call touches this same
        // surface (DirectDraw does not allow blitting to/from a currently-locked surface), and at
        // Begin()/End() batch boundaries.
        struct LockedSurfaceCache
        {
            LPDIRECTDRAWSURFACE surface = nullptr;
            DDSURFACEDESC desc{};
            bool locked = false;

            void EnsureLocked(LPDIRECTDRAWSURFACE s, const char* what)
            {
                if (locked && surface == s) return;
                Unlock();
                DDSURFACEDESC d{};
                d.dwSize = sizeof(DDSURFACEDESC);
                const HRESULT hr = s->Lock(nullptr, &d, DDLOCK_WAIT, nullptr);
                if (FAILED(hr)) ThrowHr(what, hr);
                surface = s;
                desc = d;
                locked = true;
            }

            void Unlock()
            {
                if (!locked) return;
                surface->Unlock(desc.lpSurface);
                locked = false;
                surface = nullptr;
            }
        };

        // Composites a textured, tinted quad (corners[0..3]/uvs[0..3], same winding order: TL, TR,
        // BR, BL) as two triangles (0,1,2) and (2,3,0) into an already-locked `dstBase`, sampling an
        // already-locked `srcBase` per `filter`/`addressU`/`addressV` and blending per
        // `blendMode`'s real, distinct Opaque/AlphaBlend/NonPremultiplied/Additive formula -- the
        // exact factors BlendState.cpp's own presets specify, not a single baseline approximation.
        // Does NOT Lock()/Unlock() either surface itself -- see LockedSurfaceCache's own comment
        // for why callers manage that lifetime across a whole batch of draws instead.
        void CompositeQuad(uint8_t* dstBase, int dstPitch, int dstW, int dstH,
                           const uint8_t* srcBase, int srcPitch, int srcW, int srcH,
                           const Vector2 corners[4], const Vector2 uvs[4],
                           float tintR, float tintG, float tintB, float tintA,
                           DirectX1BlendMode blendMode, int filter, int addressU, int addressV)
        {
            float minX = corners[0].X, maxX = corners[0].X, minY = corners[0].Y, maxY = corners[0].Y;
            for (int i = 1; i < 4; ++i)
            {
                minX = std::min(minX, corners[i].X); maxX = std::max(maxX, corners[i].X);
                minY = std::min(minY, corners[i].Y); maxY = std::max(maxY, corners[i].Y);
            }
            const int x0 = std::clamp(static_cast<int>(std::floor(minX)), 0, dstW);
            const int x1 = std::clamp(static_cast<int>(std::ceil(maxX)), 0, dstW);
            const int y0 = std::clamp(static_cast<int>(std::floor(minY)), 0, dstH);
            const int y1 = std::clamp(static_cast<int>(std::ceil(maxY)), 0, dstH);

            static constexpr int kTriangles[2][3] = {{0, 1, 2}, {2, 3, 0}};

            for (int y = y0; y < y1; ++y)
            {
                for (int x = x0; x < x1; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;

                    for (const auto& tri : kTriangles)
                    {
                        float w0, w1, w2;
                        if (!BarycentricWeights(corners[tri[0]], corners[tri[1]], corners[tri[2]], px, py, w0, w1, w2))
                            continue;

                        const float u = w0 * uvs[tri[0]].X + w1 * uvs[tri[1]].X + w2 * uvs[tri[2]].X;
                        const float v = w0 * uvs[tri[0]].Y + w1 * uvs[tri[1]].Y + w2 * uvs[tri[2]].Y;

                        uint8_t sp[4];
                        SampleTexel(srcBase, srcPitch, srcW, srcH, u, v, filter, addressU, addressV, sp);
                        const float srcR = static_cast<float>(sp[0]) / 255.0f * tintR;
                        const float srcG = static_cast<float>(sp[1]) / 255.0f * tintG;
                        const float srcB = static_cast<float>(sp[2]) / 255.0f * tintB;
                        const float srcA = static_cast<float>(sp[3]) / 255.0f * tintA;

                        uint8_t* dp = dstBase + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstPitch) +
                                     static_cast<std::size_t>(x) * 4u;
                        // Destination byte positions -- remapped from canonical (R,G,B,A) via
                        // g_layout, same reasoning as SampleTexel's own remap (DetectChannelLayout).
                        const int rO = g_layout.r, gO = g_layout.g, bO = g_layout.b, aO = g_layout.a;

                        switch (blendMode)
                        {
                        case DirectX1BlendMode::Opaque:
                            // Direct overwrite -- source alpha is not part of the blend equation
                            // at all (ColorSrcBlend=One, ColorDstBlend=Zero).
                            dp[rO] = static_cast<uint8_t>(std::clamp(srcR * 255.0f, 0.0f, 255.0f));
                            dp[gO] = static_cast<uint8_t>(std::clamp(srcG * 255.0f, 0.0f, 255.0f));
                            dp[bO] = static_cast<uint8_t>(std::clamp(srcB * 255.0f, 0.0f, 255.0f));
                            dp[aO] = static_cast<uint8_t>(std::clamp(srcA * 255.0f, 0.0f, 255.0f));
                            break;
                        case DirectX1BlendMode::AlphaBlend:
                        {
                            // Premultiplied convention (ColorSrcBlend=One, ColorDstBlend=
                            // InverseSourceAlpha): the source color is used as-is, NOT multiplied
                            // by srcAlpha again -- SpriteBatch's default preset assumes
                            // already-premultiplied source pixels.
                            const float invA = 1.0f - srcA;
                            dp[rO] = static_cast<uint8_t>(std::clamp(srcR * 255.0f + static_cast<float>(dp[rO]) * invA, 0.0f, 255.0f));
                            dp[gO] = static_cast<uint8_t>(std::clamp(srcG * 255.0f + static_cast<float>(dp[gO]) * invA, 0.0f, 255.0f));
                            dp[bO] = static_cast<uint8_t>(std::clamp(srcB * 255.0f + static_cast<float>(dp[bO]) * invA, 0.0f, 255.0f));
                            dp[aO] = static_cast<uint8_t>(std::clamp(srcA * 255.0f + static_cast<float>(dp[aO]) * invA, 0.0f, 255.0f));
                            break;
                        }
                        case DirectX1BlendMode::NonPremultiplied:
                        {
                            // Straight alpha (ColorSrcBlend=SourceAlpha, ColorDstBlend=
                            // InverseSourceAlpha): out = src*srcAlpha + dst*(1-srcAlpha).
                            const float invA = 1.0f - srcA;
                            dp[rO] = static_cast<uint8_t>(std::clamp(srcR * 255.0f * srcA + static_cast<float>(dp[rO]) * invA, 0.0f, 255.0f));
                            dp[gO] = static_cast<uint8_t>(std::clamp(srcG * 255.0f * srcA + static_cast<float>(dp[gO]) * invA, 0.0f, 255.0f));
                            dp[bO] = static_cast<uint8_t>(std::clamp(srcB * 255.0f * srcA + static_cast<float>(dp[bO]) * invA, 0.0f, 255.0f));
                            dp[aO] = static_cast<uint8_t>(std::clamp(srcA * 255.0f * srcA + static_cast<float>(dp[aO]) * invA, 0.0f, 255.0f));
                            break;
                        }
                        case DirectX1BlendMode::Additive:
                            // ColorSrcBlend=SourceAlpha, ColorDstBlend=One: saturating add, no
                            // destination attenuation at all.
                            dp[rO] = static_cast<uint8_t>(std::clamp(srcR * 255.0f * srcA + static_cast<float>(dp[rO]), 0.0f, 255.0f));
                            dp[gO] = static_cast<uint8_t>(std::clamp(srcG * 255.0f * srcA + static_cast<float>(dp[gO]), 0.0f, 255.0f));
                            dp[bO] = static_cast<uint8_t>(std::clamp(srcB * 255.0f * srcA + static_cast<float>(dp[bO]), 0.0f, 255.0f));
                            dp[aO] = static_cast<uint8_t>(std::clamp(srcA * 255.0f * srcA + static_cast<float>(dp[aO]), 0.0f, 255.0f));
                            break;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Common accessor so DirectX1SpriteBatchRenderer can reach the underlying IDirectDrawSurface* of
    // either concrete renderer it might be asked to sample from (a plain texture, or a former
    // render target now being sampled as one) without needing to know which. Never named outside
    // this .cpp, same reasoning as DirectX1TextureRenderer/DirectX1RenderTargetRenderer themselves.
    class DirectX1SurfaceOwner
    {
    public:
        virtual ~DirectX1SurfaceOwner() = default;
        [[nodiscard]] virtual LPDIRECTDRAWSURFACE Surface() const = 0;
    };

    struct DirectX1Renderer::Impl
    {
        // NOTE: SDL_Window is NOT owned by the renderer -- same convention as every other
        // window-based CNA renderer (GraphicsDevice/platform layer owns it).
        SDL_Window* window = nullptr;
        // Design decision 3: the real Win32 HWND behind `window`, obtained once at construction --
        // needed directly (not just via reinterpret_cast, unlike DIRECTX3's free-direct hack) for
        // Present()'s GetClientRect/ClientToScreen calls.
        HWND hwnd = nullptr;

        LPDIRECTDRAW dd = nullptr;
        // The real DirectDraw primary surface -- desktop-sized (DX1-0b finding: with no
        // SetDisplayMode call, DirectDraw hands back the whole display, not "this window"), created
        // once at construction and never recreated. Never Lock()'d directly for pixel access; it is
        // written to only via Present()'s single Blt().
        LPDIRECTDRAWSURFACE primary = nullptr;
        // DX1-owned "shadow backbuffer": a Lockable offscreen surface, sized to the logical/virtual
        // resolution, that Clear() and (from Phase O4 on) SpriteBatch draws always composite into.
        LPDIRECTDRAWSURFACE backBuffer = nullptr;

        // Phase O3: the offscreen surface owned by the currently-bound DirectX1RenderTargetRenderer, or
        // nullptr when no custom render target is bound (i.e. the shadow backbuffer is active). Set
        // directly by DirectX1RenderTargetRenderer::BindAsRenderTarget/UnbindAsRenderTarget via a pointer
        // to this field (passed at construction) -- kept as a plain LPDIRECTDRAWSURFACE rather than
        // a DirectX1RenderTargetRenderer* so DirectX1RenderTargetRenderer never needs to name this private Impl
        // type (it is defined later in this file, after Impl).
        LPDIRECTDRAWSURFACE currentTargetSurface = nullptr;
        // Width/height of currentTargetSurface, kept alongside it (Phase O4: the SpriteBatch
        // compositor needs the active destination's own bounds, which can differ from
        // logicalWidth/logicalHeight when a custom-sized render target is bound).
        int currentTargetWidth = 0;
        int currentTargetHeight = 0;

        int logicalWidth = 0;
        int logicalHeight = 0;
        CnaPresentationMode presentationMode = CnaPresentationMode::Overscan;

        // Phase O5 (design decision 6): the real, distinct blend mode detected from
        // ApplyBlendState's raw factors (DetectBlendMode) -- gates the SpriteBatch identity fast
        // path (design decision 5, Opaque only) and selects CompositeQuad's per-formula math.
        // Default AlphaBlend matches SpriteBatch::Begin()'s own default blend state.
        DirectX1BlendMode currentBlendMode = DirectX1BlendMode::AlphaBlend;

        // Resolves to whichever surface Clear()/ReadBackbuffer() should currently target: the
        // bound render target's surface if one is bound, else the shadow backbuffer. Present()
        // deliberately does NOT go through this -- it always Blt()s from the real shadow
        // backbuffer, matching FNA's own backbuffer-vs-render-target separation (a game must
        // SetRenderTarget(null) before presenting, same as every other CNA renderer).
        [[nodiscard]] LPDIRECTDRAWSURFACE ActiveSurface() const
        {
            return currentTargetSurface ? currentTargetSurface : backBuffer;
        }

        // Phase O4: the active destination's own bounds -- the bound render target's size if one
        // is bound, else the device's logical size.
        void ActiveSurfaceSize(int& w, int& h) const
        {
            if (currentTargetSurface) { w = currentTargetWidth; h = currentTargetHeight; }
            else { w = logicalWidth; h = logicalHeight; }
        }

        ~Impl()
        {
            if (backBuffer) backBuffer->Release();
            if (primary) primary->Release();
            if (dd) dd->Release();
        }

        // Design decision 4 / DX1-0b: the primary surface is created exactly once, with no
        // SetDisplayMode call (windowed DDSCL_NORMAL never needs one) and no explicit
        // width/height (it takes on the current desktop mode).
        void CreatePrimary()
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_CAPS;
            desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
            const HRESULT hr = dd->CreateSurface(&desc, &primary, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw::CreateSurface(primary)", hr);
        }

        // Releases the current shadow backbuffer (if any) and recreates it at (width, height).
        // Unlike DIRECTX3's own CreateSurfaces (which also recreated the primary via SetDisplayMode
        // every time), the primary here never changes size or needs recreation -- design decision
        // 4 -- so SetVirtualResolution only ever touches this shadow buffer.
        void CreateBackBuffer(int width, int height)
        {
            if (backBuffer) { backBuffer->Release(); backBuffer = nullptr; }

            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
            desc.dwWidth = static_cast<DWORD>(width);
            desc.dwHeight = static_cast<DWORD>(height);
            const HRESULT hr = dd->CreateSurface(&desc, &backBuffer, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw::CreateSurface(shadow backbuffer)", hr);
            DetectChannelLayout(backBuffer);

            logicalWidth = width;
            logicalHeight = height;
        }
    };

    DirectX1Renderer::DirectX1Renderer(const GraphicsRendererCreateArgs& args)
        : impl_(std::make_unique<Impl>())
    {
        if (!args.window) throw std::runtime_error("DirectX1Renderer initialized with null window.");
        impl_->window = args.window;
        impl_->presentationMode = args.presentationMode;

        // Design decision 3: a real Win32 HWND, obtained the same way DirectX9Renderer.cpp does
        // -- CNA's MinGW/Wine SDL3 build uses its genuine win32 video renderer, so this is a real
        // window handle, never free-direct's reinterpret_cast<HWND>(sdlWindow) hack DIRECTX3 needs.
        impl_->hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(impl_->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!impl_->hwnd)
            throw std::runtime_error("DirectX1Renderer: could not obtain a real HWND from the SDL window.");

        HRESULT hr = DirectDrawCreate(nullptr, &impl_->dd, nullptr);
        if (FAILED(hr)) ThrowHr("DirectDrawCreate", hr);

        // Design decision 4 / DX1-0c: windowed (DDSCL_NORMAL) mode never calls SetDisplayMode --
        // that call is exclusive-fullscreen-only in the real historical DirectDraw programming
        // model, confirmed both by reading ddraw.h and empirically at the DX1-0 spike.
        hr = impl_->dd->SetCooperativeLevel(impl_->hwnd, DDSCL_NORMAL);
        if (FAILED(hr)) ThrowHr("IDirectDraw::SetCooperativeLevel", hr);

        impl_->CreatePrimary();

        const int width = args.virtualWidth > 0 ? args.virtualWidth : 640;
        const int height = args.virtualHeight > 0 ? args.virtualHeight : 480;
        impl_->CreateBackBuffer(width, height);
    }

    DirectX1Renderer::~DirectX1Renderer() = default;

    void DirectX1Renderer::Clear(float r, float g, float b, float a)
    {
        int width = 0, height = 0;
        impl_->ActiveSurfaceSize(width, height);
        FillSurfaceColor(impl_->ActiveSurface(), width, height,
                         static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f)));
    }

    void DirectX1Renderer::Present()
    {
        // DX1-0b finding: the primary surface is desktop-sized, not window-sized, so the Blt()
        // destination rect must be this window's own client area translated to screen coordinates
        // (GetClientRect + ClientToScreen), letterbox-scaled (ComputeLetterbox) to fit -- computed
        // fresh every call, so a resize/SetVirtualResolution change is correct on the very next
        // Present(), unlike DIRECTX3's own documented stale-scale limitation.
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
        {
            scale = 1.0f;
            offsetX = 0.0f;
            offsetY = 0.0f;
        }

        POINT topLeft{0, 0};
        if (!ClientToScreen(impl_->hwnd, &topLeft))
            throw std::runtime_error("DirectX1Renderer::Present: ClientToScreen failed");

        RECT destRect{};
        destRect.left = topLeft.x + static_cast<LONG>(offsetX);
        destRect.top = topLeft.y + static_cast<LONG>(offsetY);
        destRect.right = destRect.left + static_cast<LONG>(static_cast<float>(impl_->logicalWidth) * scale);
        destRect.bottom = destRect.top + static_cast<LONG>(static_cast<float>(impl_->logicalHeight) * scale);

        const HRESULT hr = impl_->primary->Blt(&destRect, impl_->backBuffer, nullptr, DDBLT_WAIT, nullptr);
        if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Blt(present)", hr);
    }

    void DirectX1Renderer::GetViewportSize(int& width, int& height)
    {
        width = impl_->logicalWidth;
        height = impl_->logicalHeight;
    }

    void DirectX1Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // Reads from whichever surface is currently active -- the bound render target's surface if
        // one is bound (SetRenderTarget2D), else the shadow backbuffer. Never through the real
        // physical presentation path, so this is exact-pixel regardless of window size/letterboxing
        // (there is no scaling between the shadow buffer and the values read here; scaling only
        // happens later, once, when Present() Blt()s onto the primary).
        ReadSurfacePixels(impl_->ActiveSurface(), x, y, w, h, pixels);
    }

    void DirectX1Renderer::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        if (width == impl_->logicalWidth && height == impl_->logicalHeight) return;
        impl_->CreateBackBuffer(width, height);
    }

    void DirectX1Renderer::SetPresentationMode(int mode)
    {
        impl_->presentationMode = static_cast<CnaPresentationMode>(mode);
        // Present() always applies a letterbox-equivalent uniform scale (ComputeLetterbox) --
        // Stretch/Overscan/NativeBackBuffer are not yet distinguished (🟨, same honest scope
        // DX3-16 recorded). The mode is still stored so GetViewportSize()/logical-resolution
        // bookkeeping stays consistent with what the game requested.
    }

    bool DirectX1Renderer::TransformWindowToLogical(float windowX, float windowY,
                                                       float& logX, float& logY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
            return false;
        logX = (windowX - offsetX) / scale;
        logY = (windowY - offsetY) / scale;
        return true;
    }

    bool DirectX1Renderer::TransformLogicalToWindow(float logX, float logY,
                                                       float& windowX, float& windowY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
            return false;
        windowX = logX * scale + offsetX;
        windowY = logY * scale + offsetY;
        return true;
    }

    // ---- Phase O3: textures and render targets ----
    // Both classes below are never named outside this .cpp (only returned polymorphically), so
    // <ddraw.h> stays fully contained here.

    class DirectX1TextureRenderer : public ITextureRenderer, public DirectX1SurfaceOwner
    {
    public:
        DirectX1TextureRenderer(LPDIRECTDRAW dd, int width, int height)
            : width_(width), height_(height), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        DirectX1TextureRenderer(LPDIRECTDRAW dd, const ImageData& data)
            : DirectX1TextureRenderer(dd, data.width, data.height)
        {
            WriteSurfacePixels(surface_, width_, height_, data.pixels.data(), width_ * 4);
        }

        ~DirectX1TextureRenderer() override { if (surface_) surface_->Release(); }

        DirectX1TextureRenderer(const DirectX1TextureRenderer&) = delete;
        DirectX1TextureRenderer& operator=(const DirectX1TextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            WriteSurfacePixels(surface_, width_, height_, rgba, stride);
        }

        // No native mip chain on IDirectDrawSurface -- level 0 is unaffected (always routed
        // through UpdatePixels by Texture2D::SetData), level>0 throws honestly rather than
        // silently discarding the upload, matching SDL_RENDERER/DIRECTX3's own precedent.
        void UpdatePixelsLevel(int level, const uint8_t*, int, int) override
        {
            ThrowMipLevelUnsupported(level);
        }

        [[nodiscard]] LPDIRECTDRAWSURFACE Surface() const override { return surface_; }

    protected:
        int width_ = 0;
        int height_ = 0;
        LPDIRECTDRAWSURFACE surface_ = nullptr;
    };

    class DirectX1RenderTargetRenderer final : public IRenderTargetRenderer, public DirectX1SurfaceOwner
    {
    public:
        DirectX1RenderTargetRenderer(LPDIRECTDRAWSURFACE* currentTargetSlot, int* currentTargetWidthSlot,
                               int* currentTargetHeightSlot, LPDIRECTDRAW dd,
                               int width, int height, int multiSampleCount)
            : currentTargetSlot_(currentTargetSlot), currentTargetWidthSlot_(currentTargetWidthSlot),
              currentTargetHeightSlot_(currentTargetHeightSlot), width_(width), height_(height),
              multiSampleCount_(multiSampleCount), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        ~DirectX1RenderTargetRenderer() override
        {
            UnbindAsRenderTarget();
            if (surface_) surface_->Release();
        }

        DirectX1RenderTargetRenderer(const DirectX1RenderTargetRenderer&) = delete;
        DirectX1RenderTargetRenderer& operator=(const DirectX1RenderTargetRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            WriteSurfacePixels(surface_, width_, height_, rgba, stride);
        }

        void UpdatePixelsLevel(int level, const uint8_t*, int, int) override
        {
            ThrowMipLevelUnsupported(level);
        }

        void BindAsRenderTarget() override
        {
            *currentTargetSlot_ = surface_;
            *currentTargetWidthSlot_ = width_;
            *currentTargetHeightSlot_ = height_;
        }

        void UnbindAsRenderTarget() override
        {
            if (*currentTargetSlot_ == surface_)
            {
                *currentTargetSlot_ = nullptr;
                *currentTargetWidthSlot_ = 0;
                *currentTargetHeightSlot_ = 0;
            }
        }

        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        // IDirectDrawSurface has no depth-buffer concept at all, regardless of what DepthFormat
        // was requested -- always false, same reasoning SDL_RENDERER/DIRECTX3 already use.
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return false;
        }

        [[nodiscard]] LPDIRECTDRAWSURFACE Surface() const override { return surface_; }

    private:
        LPDIRECTDRAWSURFACE* currentTargetSlot_;
        int* currentTargetWidthSlot_;
        int* currentTargetHeightSlot_;
        int width_ = 0;
        int height_ = 0;
        int multiSampleCount_ = 0;
        LPDIRECTDRAWSURFACE surface_ = nullptr;
    };

    std::unique_ptr<ITextureRenderer> DirectX1Renderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<DirectX1TextureRenderer>(impl_->dd, data);
    }

    std::unique_ptr<IRenderTargetRenderer> DirectX1Renderer::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int multiSampleCount)
    {
        return std::make_unique<DirectX1RenderTargetRenderer>(&impl_->currentTargetSurface,
                                                         &impl_->currentTargetWidth, &impl_->currentTargetHeight,
                                                         impl_->dd, w, h, multiSampleCount);
    }

    void DirectX1Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt)
            rt->BindAsRenderTarget();
        else
        {
            impl_->currentTargetSurface = nullptr;
            impl_->currentTargetWidth = 0;
            impl_->currentTargetHeight = 0;
        }
    }

    void DirectX1Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        // DirectDraw has no multi-render-target concept -- single active surface only.
        if (count > 1)
            throw std::runtime_error(
                "DIRECTX1 (DirectDraw v1) does not support multiple simultaneous render targets (MRT): "
                "requested " + std::to_string(count) + ", but IDirectDrawSurface supports exactly "
                "one active render target at a time.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "DIRECTX1 (DirectDraw v1) does not support RenderTargetCube face bindings.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    // ---- Phase O4: the CPU compositor / SpriteBatch draw path (design decision 5) ----
    // Never named outside this .cpp, same reasoning as DirectX1TextureRenderer/DirectX1RenderTargetRenderer.

    class DirectX1SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        DirectX1SpriteBatchRenderer(std::function<LPDIRECTDRAWSURFACE()> getActiveSurface,
                              std::function<void(int&, int&)> getActiveSurfaceSize,
                              std::function<DirectX1BlendMode()> getBlendMode)
            : getActiveSurface_(std::move(getActiveSurface)),
              getActiveSurfaceSize_(std::move(getActiveSurfaceSize)),
              getBlendMode_(std::move(getBlendMode))
        {
        }

        void Begin() override
        {
            if (begun_)
                throw std::runtime_error("DirectX1SpriteBatchRenderer::Begin: Begin() called without a matching End()");
            begun_ = true;
        }

        void End() override
        {
            if (!begun_)
                throw std::runtime_error("DirectX1SpriteBatchRenderer::End: End() called without a matching Begin()");
            begun_ = false;
            // See LockedSurfaceCache's own comment: never leave a Lock() held past the batch that
            // needed it.
            dstLock_.Unlock();
            srcLock_.Unlock();
        }

        // Applied as a point transform (z=0) directly on the already-screen-space quad corners,
        // same convention SOFTWARE/DIRECTX3's own SetTransformMatrix uses.
        void SetTransformMatrix(const Matrix& m) override { transformMatrix_ = m; }

        // No programmable shader stage exists on this renderer.
        void SetCustomEffect(Effect* effect) override
        {
            if (effect != nullptr)
                throw std::runtime_error(
                    "DIRECTX1 (DirectDraw v1) does not support custom SpriteBatch Effects: no "
                    "programmable shader stage exists on this renderer.");
        }

        // 0=Linear (bilinear), else Point/nearest -- matches ISpriteBatchRenderer's own documented
        // convention.
        void SetSamplerFilter(int textureFilter) override { filter_ = textureFilter; }

        // Raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror).
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            addressU_ = addressU;
            addressV_ = addressV;
        }

        void Draw(const ITextureRenderer& texture, float x, float y) override
        {
            Draw(texture,
                 Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                 Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
                 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                 const Rectangle& sourceRectangle, const Color& color) override
        {
            Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
                 SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                 const Rectangle& sourceRectangle, const Color& color, float rotation,
                 const Vector2& origin, SpriteEffects effects, float /*layerDepth*/) override
        {
            if (!begun_)
                throw std::runtime_error("DirectX1SpriteBatchRenderer::Draw: Draw() called before Begin()");

            const auto* owner = dynamic_cast<const DirectX1SurfaceOwner*>(&texture);
            if (!owner)
                throw std::runtime_error(
                    "DirectX1SpriteBatchRenderer::Draw: texture renderer is not a DIRECTX1 surface (created by "
                    "a different graphics renderer?)");
            LPDIRECTDRAWSURFACE srcSurface = owner->Surface();
            LPDIRECTDRAWSURFACE dstSurface = getActiveSurface_();
            int dstW = 0, dstH = 0;
            getActiveSurfaceSize_(dstW, dstH);

            const int texW = std::max(1, texture.GetWidth());
            const int texH = std::max(1, texture.GetHeight());
            float u1 = static_cast<float>(sourceRectangle.X) / static_cast<float>(texW);
            float v1 = static_cast<float>(sourceRectangle.Y) / static_cast<float>(texH);
            float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / static_cast<float>(texW);
            float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / static_cast<float>(texH);
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

            const float dw = static_cast<float>(destinationRectangle.Width);
            const float dh = static_cast<float>(destinationRectangle.Height);
            const float sw = static_cast<float>(std::max(1, sourceRectangle.Width));
            const float sh = static_cast<float>(std::max(1, sourceRectangle.Height));
            const float scaleX = dw / sw;
            const float scaleY = dh / sh;

            // Identity fast path -- a real BltFast straight copy, no CPU compositing at all. Only
            // when every parameter would be a visual no-op relative to a plain copy: 1:1 scale, no
            // rotation, no origin offset, no flip, opaque white tint, no custom transform, and the
            // currently-applied blend state really is Opaque (design decision 5).
            const bool isIdentityGeometry =
                rotation == 0.0f && scaleX == 1.0f && scaleY == 1.0f &&
                origin.X == 0.0f && origin.Y == 0.0f && effects == SpriteEffects::None &&
                transformMatrix_ == Matrix::getIdentityProperty();
            const bool isWhiteTint =
                color.getRProperty() == 255 && color.getGProperty() == 255 &&
                color.getBProperty() == 255 && color.getAProperty() == 255;
            if (isIdentityGeometry && isWhiteTint && getBlendMode_() == DirectX1BlendMode::Opaque)
            {
                // A real Blt/BltFast call cannot target a surface that's currently Lock()'d --
                // release both caches first (a cheap no-op if neither is actually holding a lock).
                dstLock_.Unlock();
                srcLock_.Unlock();
                RECT srcRect{};
                srcRect.left = sourceRectangle.X;
                srcRect.top = sourceRectangle.Y;
                srcRect.right = sourceRectangle.X + sourceRectangle.Width;
                srcRect.bottom = sourceRectangle.Y + sourceRectangle.Height;
                const HRESULT hr = dstSurface->BltFast(static_cast<DWORD>(destinationRectangle.X),
                                                       static_cast<DWORD>(destinationRectangle.Y),
                                                       srcSurface, &srcRect, DDBLTFAST_NOCOLORKEY);
                if (FAILED(hr)) ThrowHr("IDirectDrawSurface::BltFast(identity)", hr);
                return;
            }

            // General path -- per-pixel CPU compositing. Quad-corner placement (rotation about
            // origin, then scale) reuses the exact formula SoftwareSpriteBatchRenderer::Draw()/DIRECTX3
            // already use (design decision 5: consume SpriteBatch's already-computed
            // position/rotation/flip, don't re-derive the pivot math).
            const float dx = static_cast<float>(destinationRectangle.X);
            const float dy = static_cast<float>(destinationRectangle.Y);
            const float ox = origin.X, oy = origin.Y;

            const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
            const float p1x = (sw - ox) * scaleX,   p1y = (0.0f - oy) * scaleY;
            const float p2x = (sw - ox) * scaleX,   p2y = (sh - oy) * scaleY;
            const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

            const float cosR = std::cos(rotation);
            const float sinR = std::sin(rotation);

            const auto placeCorner = [&](float px, float py) -> Vector2
            {
                const float rx = dx + px * cosR - py * sinR;
                const float ry = dy + px * sinR + py * cosR;
                const Vector3 transformed = Vector3::Transform(Vector3(rx, ry, 0.0f), transformMatrix_);
                return Vector2(transformed.X, transformed.Y);
            };

            const Vector2 corners[4] = { placeCorner(p0x, p0y), placeCorner(p1x, p1y),
                                        placeCorner(p2x, p2y), placeCorner(p3x, p3y) };
            const Vector2 uvs[4] = { Vector2(u1, v1), Vector2(u2, v1), Vector2(u2, v2), Vector2(u1, v2) };

            // Lock once, reused across every consecutive general-path draw that targets/samples
            // the same surfaces (the common case: many sprites sharing one destination and one
            // texture per frame) -- see LockedSurfaceCache's own comment for why this matters.
            dstLock_.EnsureLocked(dstSurface, "IDirectDrawSurface::Lock(compositor dst)");
            srcLock_.EnsureLocked(srcSurface, "IDirectDrawSurface::Lock(compositor src)");

            CompositeQuad(static_cast<uint8_t*>(dstLock_.desc.lpSurface), dstLock_.desc.lPitch, dstW, dstH,
                         static_cast<const uint8_t*>(srcLock_.desc.lpSurface), srcLock_.desc.lPitch, texW, texH,
                         corners, uvs,
                         static_cast<float>(color.getRProperty()) / 255.0f,
                         static_cast<float>(color.getGProperty()) / 255.0f,
                         static_cast<float>(color.getBProperty()) / 255.0f,
                         static_cast<float>(color.getAProperty()) / 255.0f,
                         getBlendMode_(), filter_, addressU_, addressV_);
        }

    private:
        std::function<LPDIRECTDRAWSURFACE()> getActiveSurface_;
        std::function<void(int&, int&)> getActiveSurfaceSize_;
        std::function<DirectX1BlendMode()> getBlendMode_;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        // Defaults match SpriteBatch::Begin()'s own documented default sampler state
        // ("AlphaBlend, LinearClamp, no depth"): Linear filter (0), Clamp address mode (1) on
        // both axes.
        int filter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        LockedSurfaceCache dstLock_;
        LockedSurfaceCache srcLock_;
    };

    std::unique_ptr<ISpriteBatchRenderer> DirectX1Renderer::CreateSpriteBatch()
    {
        Impl* impl = impl_.get();
        return std::make_unique<DirectX1SpriteBatchRenderer>(
            [impl]() { return impl->ActiveSurface(); },
            [impl](int& w, int& h) { impl->ActiveSurfaceSize(w, h); },
            [impl]() { return impl->currentBlendMode; });
    }

    void DirectX1Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                             int colorDstBlend, int alphaDstBlend,
                                             int colorBlendFunc, int alphaBlendFunc,
                                             const BlendWriteState& /*writeState*/)
    {
        // REMED-GFX-077: BlendState's ColorWriteChannels0-3 and MultiSampleMask are genuinely
        // inexpressible at this DirectX era -- no colour-write-enable or coverage-sample-mask
        // render state exists at all -- so the write state is accepted and ignored: a documented
        // capability gap of this Historical renderer, not a silent drop of expressible state.
        impl_->currentBlendMode = DetectBlendMode(colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                                                  colorBlendFunc, alphaBlendFunc);
    }

    void DirectX1Renderer::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void DirectX1Renderer::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void DirectX1Renderer::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void DirectX1Renderer::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void DirectX1Renderer::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil"); }
    void DirectX1Renderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil"); }
    void DirectX1Renderer::SetDepthTestEnabled(bool)  { ThrowNo3D("SetDepthTestEnabled"); }
    void DirectX1Renderer::SetBlendEnabled(bool)      { ThrowNo3D("SetBlendEnabled"); }
    void DirectX1Renderer::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferRenderer> DirectX1Renderer::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferRenderer> DirectX1Renderer::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16");
    }

    void DirectX1Renderer::DrawColoredPrimitives(const IVertexBufferRenderer&,
                                                   const Matrix&, const Matrix&, const Matrix&,
                                                   PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }

    void DirectX1Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&,
                                                          const IIndexBufferRenderer&,
                                                          const Matrix&, const Matrix&, const Matrix&,
                                                          PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_DIRECTX1
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<DirectX1::DirectX1Renderer>(args);
    }
#endif
}
