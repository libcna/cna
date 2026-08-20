#include "CNA/Internal/Renderers/DirectX3/DirectX3Renderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

// plans/plan_dx3.md: real DirectX 3 graphics renderer (temporarily named DIRECTX3 -- see plans/plan_dx3.md's own
// status note for why -- pending a still-not-executed rename of the existing, shipping
// ../free-direct-backed DIRECTX3 renderer to FREE_DIRECT). <ddraw.h> (and the real <windows.h> it pulls
// in) is contained to this .cpp only -- see DirectX3Renderer.hpp's own comment. This file's 2D
// layer (mechanically ported from DIRECTX2's own, plans/plan_dx2.md, with one upgrade: the DirectDraw object
// itself is IDirectDraw2 -- QueryInterface'd off a v1 DirectDrawCreate() result, plans/plan_dx3.md
// design decision 2) may name IDirectDraw2/IDirectDrawSurface/DDSURFACEDESC -- never
// IDirectDraw3+/IDirectDrawSurface2+/DDSURFACEDESC2 (that boundary is this renderer's own,
// plans/plan_dx3.md section 1). <d3d.h>/IDirect3D2/IDirect3DDevice2 are not forbidden -- this renderer's
// 3D layer (a verbatim port of DIRECTX2's own already-proven 3D layer, including Phase O9's CPU
// lighting) is built on them -- only the literal execute-buffer surface
// (IDirect3DDevice::Execute/D3DEXECUTEBUFFERDESC/IDirect3DExecuteBuffer/D3DOP_*/the un-versioned
// IDirect3D/IDirect3DDevice) is permanently forbidden, asserted by the DirectX3_ExecuteBufferDiscipline
// CTest (scripts/check-directx3-execute-buffer-discipline.sh, plans/plan_dx3.md design decision 9).
#include <ddraw.h>
#include <d3d.h>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::DirectX3
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

        // ---- Phase O3: shared offscreen-surface helpers, used by both DirectX3TextureRenderer and
        // DirectX3RenderTargetRenderer (design decision 7: 32bpp DDSCAPS_OFFSCREENPLAIN only, no
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
        // the engine's canonical RGBA32 byte layout). Both sides of every
        // round-trip through this renderer's OWN surfaces stayed internally consistent (which is why
        // DirectX3_Smoke/DirectX3_Blend/etc.'s Clear()+GetBackBufferData() checks all still passed), but a
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

        // Phase O4 (dx2_spike9_dualcap_texture.cpp finding): DDSCAPS_TEXTURE is added alongside
        // DDSCAPS_OFFSCREENPLAIN so any texture/render-target surface can ALSO be bound as a real
        // Direct3D v2 texture (IDirect3DTexture2, QueryInterface'd on demand at draw time -- see
        // DrawPrimitivesEx) for GpuDrawParams::texture0 sampling, without needing a second surface.
        // Spike-confirmed: the combined caps are accepted with no explicit pixel format (same
        // "let Wine pick the native format" convention this helper already used), and the SAME
        // surface instance supports both a plain 2D Lock()-style write/Blt (already exercised by
        // Phase O2/O3's own tests) and 3D IDirect3DTexture2 sampling correctly.
        LPDIRECTDRAWSURFACE CreateOffscreenSurface(LPDIRECTDRAW2 dd, int width, int height)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_TEXTURE;
            desc.dwWidth = static_cast<DWORD>(width);
            desc.dwHeight = static_cast<DWORD>(height);
            LPDIRECTDRAWSURFACE surface = nullptr;
            const HRESULT hr = dd->CreateSurface(&desc, &surface, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw2::CreateSurface(offscreen)", hr);
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
        // matching DIRECTX3's own found-and-fixed lesson (plans/plan_freedirect.md DX3-14) proactively instead of
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

        // Phase O3 (plans/plan_dx2.md design decision 5, DX2-0's dx2_spike8_zclear.cpp finding):
        // IDirect3DViewport(2)::Clear() has no depth/color VALUE parameter at all (only
        // IDirect3DViewport3::Clear2, DIRECTX5+, takes an explicit dvZ) -- so ClearDepth/
        // ClearColorAndDepth's arbitrary caller-requested depth value cannot go through it. Mirrors
        // FillSurfaceColor's own direct Lock()+fill approach instead, confirmed by spike to be
        // genuinely respected by the real depth test: a manually-written Z-buffer value blocks a
        // subsequent draw exactly as a real draw's own Z-write would. A 16-bit DDSCAPS_ZBUFFER
        // surface stores a plain unsigned value per pixel, 0=near/0xFFFF=far, linearly mapped over
        // the viewport's dvMinZ..dvMaxZ (0..1 here) -- matches XNA/D3D's own post-divide depth
        // convention with no remap needed.
        void FillZBuffer16(LPDIRECTDRAWSURFACE zbuf, int width, int height, float depth)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = zbuf->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(z-clear)", hr);

            const uint16_t value = static_cast<uint16_t>(std::clamp(depth, 0.0f, 1.0f) * 65535.0f);
            auto* base = static_cast<uint8_t*>(desc.lpSurface);
            for (int y = 0; y < height; ++y)
            {
                auto* row = reinterpret_cast<uint16_t*>(base + static_cast<std::size_t>(y) * static_cast<std::size_t>(desc.lPitch));
                for (int x = 0; x < width; ++x) row[x] = value;
            }
            zbuf->Unlock(desc.lpSurface);
        }

        [[noreturn]] void ThrowMipLevelUnsupported(int level)
        {
            throw std::runtime_error(
                "DIRECTX3 (DirectDraw v2) does not support mip-level texture uploads (level " +
                std::to_string(level) + "): IDirectDrawSurface has no native mip chain or "
                "per-level LOD sampling. Use Texture2D::SetData(level=0, ...) only.");
        }

        // Real letterbox scale+offset transform between physical window pixels and logical
        // (virtual) game pixels (uniform scale to fit, centered) -- computed fresh from the real
        // physical drawable size in the current surface snapshot, shared by Present() (the on-screen Blt
        // destination) and TransformWindowToLogical/TransformLogicalToWindow, so those three are
        // always mutually consistent and a resize/SetVirtualResolution change is correct on the
        // very next call, unlike DIRECTX3's own documented stale-scale limitation (plans/plan_freedirect.md DX3-16).
        bool ComputeLetterbox(const PlatformRendererSurfaceState& surface,
                              int logicalWidth, int logicalHeight,
                              float& scale, float& offsetX, float& offsetY)
        {
            if (logicalWidth <= 0 || logicalHeight <= 0) return false;
            const auto drawableSize = surface.GetDrawableSize();
            const int physW = drawableSize.width;
            const int physH = drawableSize.height;
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
        // DIRECTX3's own already-verified CompositeQuad (plans/plan_dx1.md design decision 5), not re-derived.

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
        // the common Blend enum): One=0, Zero=1, SourceColor=2,
        // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
        // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9.
        enum class DirectX3BlendMode { Opaque, AlphaBlend, NonPremultiplied, Additive };

        // Detects which of the 4 BlendState presets (BlendState.cpp) the raw factors match, by
        // exact value -- not by BlendState identity (the renderer never sees a BlendState object,
        // only GraphicsDevice::ApplyBlendState's raw ints).
        // A real preset match requires BOTH the 4 factors AND both blend functions (color/alpha)
        // to match -- all 4 real BlendState.cpp presets use BlendFunction::Add (0) implicitly.
        // A custom BlendState with e.g. Opaque's exact factors but BlendFunction::Subtract is NOT
        // equivalent to Opaque and must fall through to the AlphaBlend fallback below, not be
        // misdetected as Opaque just because the factors happen to match (ported from DX3-44's own
        // found-and-fixed bug).
        DirectX3BlendMode DetectBlendMode(int colorSrc, int alphaSrc, int colorDst, int alphaDst,
                                     int colorFunc, int alphaFunc)
        {
            const bool bothAdd = (colorFunc == 0 && alphaFunc == 0);
            // NonPremultiplied: ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, ColorDst=InvSrcAlpha, AlphaDst=InvSrcAlpha
            if (bothAdd && colorSrc == 4 && alphaSrc == 4 && colorDst == 5 && alphaDst == 5) return DirectX3BlendMode::NonPremultiplied;
            // Additive: ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, ColorDst=One, AlphaDst=One
            if (bothAdd && colorSrc == 4 && alphaSrc == 4 && colorDst == 0 && alphaDst == 0) return DirectX3BlendMode::Additive;
            // Opaque: ColorSrc=One, AlphaSrc=One, ColorDst=Zero, AlphaDst=Zero
            if (bothAdd && colorSrc == 0 && alphaSrc == 0 && colorDst == 1 && alphaDst == 1) return DirectX3BlendMode::Opaque;
            // AlphaBlend: ColorSrc=One, AlphaSrc=One, ColorDst=InvSrcAlpha, AlphaDst=InvSrcAlpha --
            // and the fallback for any other/custom factor+op combination (including a factor
            // match with a non-Add function), same recorded scope limitation SOFTWARE/DIRECTX3 already
            // made (no general blend-equation interpreter in v1).
            return DirectX3BlendMode::AlphaBlend;
        }

        // ---- Phase O6 (design decision 10): raw XNA int -> real D3D v1/v2 render-state value
        // mapping, modeled on D3D9StateMapping.cpp's own per-enum switch style. Two real
        // architectural gaps found while writing these, both documented rather than silently
        // dropped: (1) D3D v1/v2 has NO separate alpha blend-factor/op pair (D3DRENDERSTATE_
        // SRCBLENDALPHA/BLENDOP don't exist in d3dtypes.h at this era -- confirmed by inspection,
        // not assumed) -- alphaSrcBlend/alphaDstBlend/colorBlendFunc/alphaBlendFunc are accepted
        // and ignored (decision 7's pattern), only colorSrcBlend/colorDstBlend map to real state.
        // (2) D3DRENDERSTATE_TEXTUREADDRESS is a SINGLE combined U+V mode (no separate ADDRESSU/
        // ADDRESSV render states exist either) -- addressV is accepted and ignored, only addressU
        // maps to real state, a real lossy-mapping case documented per design decision 10's own
        // "document any lossy mapping" instruction.
        using Microsoft::Xna::Framework::Graphics::Blend;
        using Microsoft::Xna::Framework::Graphics::CompareFunction;
        using Microsoft::Xna::Framework::Graphics::CullMode;
        using Microsoft::Xna::Framework::Graphics::FillMode;
        using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
        using Microsoft::Xna::Framework::Graphics::TextureFilter;

        DWORD DirectX3BlendToD3D(int blend)
        {
            switch (static_cast<Blend>(blend))
            {
                case Blend::One:                     return D3DBLEND_ONE;
                case Blend::Zero:                     return D3DBLEND_ZERO;
                case Blend::SourceColor:             return D3DBLEND_SRCCOLOR;
                case Blend::InverseSourceColor:      return D3DBLEND_INVSRCCOLOR;
                case Blend::SourceAlpha:             return D3DBLEND_SRCALPHA;
                case Blend::InverseSourceAlpha:      return D3DBLEND_INVSRCALPHA;
                case Blend::DestinationColor:        return D3DBLEND_DESTCOLOR;
                case Blend::InverseDestinationColor: return D3DBLEND_INVDESTCOLOR;
                case Blend::DestinationAlpha:        return D3DBLEND_DESTALPHA;
                case Blend::InverseDestinationAlpha: return D3DBLEND_INVDESTALPHA;
                case Blend::SourceAlphaSaturation:   return D3DBLEND_SRCALPHASAT;
                // BlendFactor/InverseBlendFactor (a constant-color blend factor) has no D3D v1/v2
                // equivalent render state (D3DRENDERSTATE_BLENDFACTOR doesn't exist at this era) --
                // falls back to ONE, matching D3D9StateMapping's own "default: ONE" fallback shape.
                default:                               return D3DBLEND_ONE;
            }
        }

        DWORD DirectX3CompareFunctionToD3D(int compareFunction)
        {
            switch (static_cast<CompareFunction>(compareFunction))
            {
                case CompareFunction::Always:        return D3DCMP_ALWAYS;
                case CompareFunction::Never:         return D3DCMP_NEVER;
                case CompareFunction::Less:          return D3DCMP_LESS;
                case CompareFunction::LessEqual:     return D3DCMP_LESSEQUAL;
                case CompareFunction::Equal:         return D3DCMP_EQUAL;
                case CompareFunction::GreaterEqual:  return D3DCMP_GREATEREQUAL;
                case CompareFunction::Greater:       return D3DCMP_GREATER;
                case CompareFunction::NotEqual:      return D3DCMP_NOTEQUAL;
                default:                               return D3DCMP_ALWAYS;
            }
        }

        DWORD DirectX3CullModeToD3D(int cullMode)
        {
            switch (static_cast<CullMode>(cullMode))
            {
                case CullMode::None:                     return D3DCULL_NONE;
                case CullMode::CullClockwiseFace:        return D3DCULL_CW;
                case CullMode::CullCounterClockwiseFace: return D3DCULL_CCW;
                default:                                   return D3DCULL_NONE;
            }
        }

        DWORD DirectX3FillModeToD3D(int fillMode)
        {
            switch (static_cast<FillMode>(fillMode))
            {
                case FillMode::Solid:      return D3DFILL_SOLID;
                case FillMode::WireFrame:  return D3DFILL_WIREFRAME;
                default:                     return D3DFILL_SOLID;
            }
        }

        DWORD DirectX3TextureAddressModeToD3D(int addressMode)
        {
            switch (static_cast<TextureAddressMode>(addressMode))
            {
                case TextureAddressMode::Wrap:   return D3DTADDRESS_WRAP;
                case TextureAddressMode::Clamp:  return D3DTADDRESS_CLAMP;
                case TextureAddressMode::Mirror: return D3DTADDRESS_MIRROR;
                default:                           return D3DTADDRESS_WRAP;
            }
        }

        struct DirectX3FilterPair { DWORD mag; DWORD min; };

        // No mip-filter concept is mapped -- D3D v1/v2 (and this renderer's textures) have no real
        // mip chain at all (design decision, ported from DIRECTX1/DIRECTX3's own identical "no native mip
        // chain" finding), so every "MipPoint"/"MipLinear" variant collapses to its own mag/min
        // pair with the mip distinction simply not represented -- a real, documented simplification.
        DirectX3FilterPair DirectX3TextureFilterToD3D(int textureFilter)
        {
            switch (static_cast<TextureFilter>(textureFilter))
            {
                case TextureFilter::Linear:                        return {D3DTFG_LINEAR, D3DTFN_LINEAR};
                case TextureFilter::Point:                          return {D3DTFG_POINT, D3DTFN_POINT};
                case TextureFilter::Anisotropic:                    return {D3DTFG_ANISOTROPIC, D3DTFN_ANISOTROPIC};
                case TextureFilter::LinearMipPoint:                 return {D3DTFG_LINEAR, D3DTFN_LINEAR};
                case TextureFilter::PointMipLinear:                 return {D3DTFG_POINT, D3DTFN_POINT};
                case TextureFilter::MinLinearMagPointMipLinear:     return {D3DTFG_POINT, D3DTFN_LINEAR};
                case TextureFilter::MinLinearMagPointMipPoint:      return {D3DTFG_POINT, D3DTFN_LINEAR};
                case TextureFilter::MinPointMagLinearMipLinear:     return {D3DTFG_LINEAR, D3DTFN_POINT};
                case TextureFilter::MinPointMagLinearMipPoint:      return {D3DTFG_LINEAR, D3DTFN_POINT};
                default:                                              return {D3DTFG_LINEAR, D3DTFN_LINEAR};
            }
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
                           DirectX3BlendMode blendMode, int filter, int addressU, int addressV)
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
                        case DirectX3BlendMode::Opaque:
                            // Direct overwrite -- source alpha is not part of the blend equation
                            // at all (ColorSrcBlend=One, ColorDstBlend=Zero).
                            dp[rO] = static_cast<uint8_t>(std::clamp(srcR * 255.0f, 0.0f, 255.0f));
                            dp[gO] = static_cast<uint8_t>(std::clamp(srcG * 255.0f, 0.0f, 255.0f));
                            dp[bO] = static_cast<uint8_t>(std::clamp(srcB * 255.0f, 0.0f, 255.0f));
                            dp[aO] = static_cast<uint8_t>(std::clamp(srcA * 255.0f, 0.0f, 255.0f));
                            break;
                        case DirectX3BlendMode::AlphaBlend:
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
                        case DirectX3BlendMode::NonPremultiplied:
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
                        case DirectX3BlendMode::Additive:
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

    // Common accessor so DirectX3SpriteBatchRenderer can reach the underlying IDirectDrawSurface* of
    // either concrete renderer it might be asked to sample from (a plain texture, or a former
    // render target now being sampled as one) without needing to know which. Never named outside
    // this .cpp, same reasoning as DirectX3TextureRenderer/DirectX3RenderTargetRenderer themselves.
    class DirectX3SurfaceOwner
    {
    public:
        virtual ~DirectX3SurfaceOwner() = default;
        [[nodiscard]] virtual LPDIRECTDRAWSURFACE Surface() const = 0;
    };

    struct DirectX3Renderer::Impl
    {
        explicit Impl(const RendererSurfaceInfo& surfaceInfo)
            : surface(surfaceInfo, "DirectX3Renderer")
        {
        }

        PlatformRendererSurfaceState surface;
        HWND hwnd = nullptr;

        // Design decision 2 (plans/plan_dx3.md): IDirectDraw2, not v1 -- upgraded via QueryInterface
        // immediately after DirectDrawCreate() (see the constructor); every other DirectDraw call
        // this renderer makes goes through this v2 pointer, spike-confirmed (DX30-0c) to behave
        // identically to v1 for all of them.
        LPDIRECTDRAW2 dd = nullptr;
        // The real DirectDraw primary surface -- desktop-sized (DX2-0b finding: with no
        // SetDisplayMode call, DirectDraw hands back the whole display, not "this window"), created
        // once at construction and never recreated. Never Lock()'d directly for pixel access; it is
        // written to only via Present()'s single Blt().
        LPDIRECTDRAWSURFACE primary = nullptr;
        // This renderer's own "shadow backbuffer": a Lockable offscreen surface, sized to the
        // logical/virtual resolution, that Clear() and SpriteBatch draws always composite into.
        LPDIRECTDRAWSURFACE backBuffer = nullptr;

        // Phase O3 (plans/plan_dx2.md design decisions 3/5): the real Direct3D v2 device, built directly
        // on the shadow backbuffer (design decision 4's DDSCAPS_3DDEVICE flag). d3d2 only needs `dd`
        // and is created once, reused across backbuffer resizes; zbuffer/device2/viewport2 are all
        // tied to the specific backBuffer surface instance and must be torn down and recreated
        // whenever CreateBackBuffer() replaces it (DX2-0 spike: IDirect3DDevice2 has no
        // SetRenderTarget-equivalent to rebind an existing device to a new surface).
        LPDIRECT3D2 d3d2 = nullptr;
        LPDIRECT3DDEVICE2 device2 = nullptr;
        LPDIRECT3DVIEWPORT2 viewport2 = nullptr;
        // 16-bit DDSCAPS_ZBUFFER surface, attached to backBuffer via AddAttachedSurface, sized to
        // match it exactly (design decision 5).
        LPDIRECTDRAWSURFACE zbuffer = nullptr;

        // Phase O3: the offscreen surface owned by the currently-bound DirectX3RenderTargetRenderer, or
        // nullptr when no custom render target is bound (i.e. the shadow backbuffer is active). Set
        // directly by DirectX3RenderTargetRenderer::BindAsRenderTarget/UnbindAsRenderTarget via a pointer
        // to this field (passed at construction) -- kept as a plain LPDIRECTDRAWSURFACE rather than
        // a DirectX3RenderTargetRenderer* so DirectX3RenderTargetRenderer never needs to name this private Impl
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
        DirectX3BlendMode currentBlendMode = DirectX3BlendMode::AlphaBlend;

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
            Release3DDevice();
            if (d3d2) d3d2->Release();
            if (zbuffer) zbuffer->Release();
            if (backBuffer) backBuffer->Release();
            if (primary) primary->Release();
            if (dd) dd->Release();
        }

        // Tears down device2/viewport2 (but not d3d2 or zbuffer -- callers that recreate the
        // z-buffer/backbuffer release those separately) -- shared by ~Impl() and CreateBackBuffer()
        // before rebuilding against the new backbuffer surface.
        void Release3DDevice()
        {
            if (viewport2)
            {
                if (device2) device2->DeleteViewport(viewport2);
                viewport2->Release();
                viewport2 = nullptr;
            }
            if (device2) { device2->Release(); device2 = nullptr; }
        }

        // Phase O3 (design decisions 3-5): builds the real Direct3D v2 device against the current
        // backBuffer surface -- a 16-bit DDSCAPS_ZBUFFER surface sized to match, a software RGB
        // device (IID_IDirect3DRGBDevice -- DX2-0 spike: routes the same as IID_IDirect3DHALDevice
        // in this environment; RGB is the historically-correct no-hardware-required choice), and a
        // full-surface IDirect3DViewport2. D3DRENDERSTATE_LIGHTING is set to FALSE once here, not
        // per-draw: Phase O4's CPU transform pipeline always submits already-lit D3DTLVERTEX data
        // (design decision 6), so real Direct3D's fixed-function lighting must never re-light it.
        void Create3DDevice(int width, int height)
        {
            Release3DDevice();
            if (zbuffer) { zbuffer->Release(); zbuffer = nullptr; }

            if (!d3d2)
            {
                const HRESULT hr = dd->QueryInterface(IID_IDirect3D2, reinterpret_cast<void**>(&d3d2));
                if (FAILED(hr)) ThrowHr("IDirectDraw2::QueryInterface(IID_IDirect3D2)", hr);
            }

            DDSURFACEDESC zDesc{};
            zDesc.dwSize = sizeof(DDSURFACEDESC);
            zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
            zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
            zDesc.dwWidth = static_cast<DWORD>(width);
            zDesc.dwHeight = static_cast<DWORD>(height);
            zDesc.dwZBufferBitDepth = 16;
            HRESULT hr = dd->CreateSurface(&zDesc, &zbuffer, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw2::CreateSurface(z-buffer)", hr);
            hr = backBuffer->AddAttachedSurface(zbuffer);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::AddAttachedSurface(z-buffer)", hr);

            hr = d3d2->CreateDevice(IID_IDirect3DRGBDevice, backBuffer, &device2);
            if (FAILED(hr)) ThrowHr("IDirect3D2::CreateDevice(IID_IDirect3DRGBDevice)", hr);

            hr = d3d2->CreateViewport(&viewport2, nullptr);
            if (FAILED(hr)) ThrowHr("IDirect3D2::CreateViewport", hr);
            hr = device2->AddViewport(viewport2);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice2::AddViewport", hr);

            D3DVIEWPORT2 vp{};
            vp.dwSize = sizeof(D3DVIEWPORT2);
            vp.dwX = 0; vp.dwY = 0;
            vp.dwWidth = static_cast<DWORD>(width);
            vp.dwHeight = static_cast<DWORD>(height);
            vp.dvClipX = -1.0f; vp.dvClipY = 1.0f;
            vp.dvClipWidth = 2.0f; vp.dvClipHeight = 2.0f;
            vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
            hr = viewport2->SetViewport2(&vp);
            if (FAILED(hr)) ThrowHr("IDirect3DViewport2::SetViewport2", hr);
            hr = device2->SetCurrentViewport(viewport2);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice2::SetCurrentViewport", hr);

            device2->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
            // Phase O4 safe default: real Direct3D's own default cull mode was never spike-verified
            // against this renderer's triangle-winding conventions (every DX2-0 spike explicitly
            // set D3DCULL_NONE rather than relying on the default). Cull-none never discards a
            // triangle due to winding, so it is a safe interim default until Phase O6 wires
            // ApplyRasterizerState to select the real per-draw cull mode from RasterizerState.
            device2->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
            // Phase O4 safe default, matching real XNA's own DepthStencilState.Default
            // (DepthBufferEnable=true, DepthBufferFunction=CompareFunction.LessEqual,
            // DepthBufferWriteEnable=true) rather than relying on whatever Direct3D's own
            // undocumented device default happens to be -- explicit until Phase O6 wires
            // ApplyDepthStencilState to select the real per-draw depth state from
            // GraphicsDevice.DepthStencilState.
            device2->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
            device2->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
            device2->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
        }

        // Design decision 4 / DX2-0b: the primary surface is created exactly once, with no
        // SetDisplayMode call (windowed DDSCL_NORMAL never needs one) and no explicit
        // width/height (it takes on the current desktop mode).
        void CreatePrimary()
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_CAPS;
            desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
            const HRESULT hr = dd->CreateSurface(&desc, &primary, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw2::CreateSurface(primary)", hr);
        }

        // Releases the current shadow backbuffer (if any) and recreates it at (width, height).
        // Unlike DIRECTX3's own CreateSurfaces (which also recreated the primary via SetDisplayMode
        // every time), the primary here never changes size or needs recreation -- design decision
        // 4 -- so SetVirtualResolution only ever touches this shadow buffer.
        // plans/plan_dx2.md design decision 4: unlike DIRECTX1's plain DDSCAPS_OFFSCREENPLAIN, this ONE
        // surface (the shadow backbuffer that Clear()/Present()/the default backbuffer all use) is
        // also flagged DDSCAPS_3DDEVICE, so a later phase (O3) can attach a DDSCAPS_ZBUFFER surface
        // and create a real Direct3D device against it -- 2D SpriteBatch draws and 3D
        // DrawIndexedPrimitive draws then land on the same surface and composite naturally within a
        // frame, matching real XNA's single-backbuffer model. Textures/render targets created via
        // CreateOffscreenSurface() above are NOT given this flag -- only this one surface is.
        void CreateBackBuffer(int width, int height)
        {
            if (backBuffer) { backBuffer->Release(); backBuffer = nullptr; }

            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
            desc.dwWidth = static_cast<DWORD>(width);
            desc.dwHeight = static_cast<DWORD>(height);
            const HRESULT hr = dd->CreateSurface(&desc, &backBuffer, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw2::CreateSurface(shadow backbuffer)", hr);
            DetectChannelLayout(backBuffer);

            logicalWidth = width;
            logicalHeight = height;

            Create3DDevice(width, height);
        }
    };

    DirectX3Renderer::DirectX3Renderer(const GraphicsRendererCreateArgs& args)
        : impl_(std::make_unique<Impl>(args.surface))
    {
        impl_->presentationMode = args.presentationMode;

        CNA::Platform::Win32NativeWindow nativeWindow;
        if (!CNA::Platform::TryGetWin32(impl_->surface.GetNativeHandle(), nativeWindow))
            throw std::runtime_error("DirectX3Renderer requires a Win32 native window.");
        impl_->hwnd = static_cast<HWND>(nativeWindow.hwnd);

        // Design decision 2 (plans/plan_dx3.md), spike-confirmed (DX30-0a/DX30-0c): DirectDrawCreate
        // only ever hands back a v1 IDirectDraw object -- this renderer immediately upgrades it to
        // IDirectDraw2 via QueryInterface and releases the v1 pointer, using the v2 object for
        // everything from this point on (confirmed to work identically to v1 for every call this
        // renderer's 2D layer makes).
        LPDIRECTDRAW ddV1 = nullptr;
        HRESULT hr = DirectDrawCreate(nullptr, &ddV1, nullptr);
        if (FAILED(hr)) ThrowHr("DirectDrawCreate", hr);
        hr = ddV1->QueryInterface(IID_IDirectDraw2, reinterpret_cast<void**>(&impl_->dd));
        ddV1->Release();
        if (FAILED(hr)) ThrowHr("IDirectDraw::QueryInterface(IID_IDirectDraw2)", hr);

        // Design decision 4 / DX2-0c: windowed (DDSCL_NORMAL) mode never calls SetDisplayMode --
        // that call is exclusive-fullscreen-only in the real historical DirectDraw programming
        // model, confirmed both by reading ddraw.h and empirically at the DX2-0/DX30-0 spikes
        // (DX30-0d: IDirectDraw2::SetDisplayMode's wider, refresh-rate-adding signature returns
        // E_NOTIMPL in windowed mode here, exactly as expected -- dead code for this renderer
        // family regardless of DirectDraw version).
        hr = impl_->dd->SetCooperativeLevel(impl_->hwnd, DDSCL_NORMAL);
        if (FAILED(hr)) ThrowHr("IDirectDraw2::SetCooperativeLevel", hr);

        impl_->CreatePrimary();

        const int width = args.virtualWidth > 0 ? args.virtualWidth : 640;
        const int height = args.virtualHeight > 0 ? args.virtualHeight : 480;
        impl_->CreateBackBuffer(width, height);
    }

    DirectX3Renderer::~DirectX3Renderer() = default;

    void DirectX3Renderer::Clear(float r, float g, float b, float a)
    {
        int width = 0, height = 0;
        impl_->ActiveSurfaceSize(width, height);
        FillSurfaceColor(impl_->ActiveSurface(), width, height,
                         static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f)));
    }

    void DirectX3Renderer::Present()
    {
        // DX2-0b finding: the primary surface is desktop-sized, not window-sized, so the Blt()
        // destination rect must be this window's own client area translated to screen coordinates
        // (GetClientRect + ClientToScreen), letterbox-scaled (ComputeLetterbox) to fit -- computed
        // fresh every call, so a resize/SetVirtualResolution change is correct on the very next
        // Present(), unlike DIRECTX3's own documented stale-scale limitation.
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->surface, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
        {
            scale = 1.0f;
            offsetX = 0.0f;
            offsetY = 0.0f;
        }

        POINT topLeft{0, 0};
        if (!ClientToScreen(impl_->hwnd, &topLeft))
            throw std::runtime_error("DirectX3Renderer::Present: ClientToScreen failed");

        RECT destRect{};
        destRect.left = topLeft.x + static_cast<LONG>(offsetX);
        destRect.top = topLeft.y + static_cast<LONG>(offsetY);
        destRect.right = destRect.left + static_cast<LONG>(static_cast<float>(impl_->logicalWidth) * scale);
        destRect.bottom = destRect.top + static_cast<LONG>(static_cast<float>(impl_->logicalHeight) * scale);

        const HRESULT hr = impl_->primary->Blt(&destRect, impl_->backBuffer, nullptr, DDBLT_WAIT, nullptr);
        if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Blt(present)", hr);
    }

    void DirectX3Renderer::GetViewportSize(int& width, int& height)
    {
        width = impl_->logicalWidth;
        height = impl_->logicalHeight;
    }

    void DirectX3Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // Reads from whichever surface is currently active -- the bound render target's surface if
        // one is bound (SetRenderTarget2D), else the shadow backbuffer. Never through the real
        // physical presentation path, so this is exact-pixel regardless of window size/letterboxing
        // (there is no scaling between the shadow buffer and the values read here; scaling only
        // happens later, once, when Present() Blt()s onto the primary).
        ReadSurfacePixels(impl_->ActiveSurface(), x, y, w, h, pixels);
    }

    void DirectX3Renderer::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        if (width == impl_->logicalWidth && height == impl_->logicalHeight) return;
        impl_->CreateBackBuffer(width, height);
    }

    void DirectX3Renderer::SetPresentationMode(int mode)
    {
        impl_->presentationMode = static_cast<CnaPresentationMode>(mode);
        // Present() always applies a letterbox-equivalent uniform scale (ComputeLetterbox) --
        // Stretch/Overscan/NativeBackBuffer are not yet distinguished (🟨, same honest scope
        // DX3-16 recorded). The mode is still stored so GetViewportSize()/logical-resolution
        // bookkeeping stays consistent with what the game requested.
    }

    void DirectX3Renderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        impl_->surface.Update(surface);
    }

    bool DirectX3Renderer::TransformWindowToLogical(float windowX, float windowY,
                                                       float& logX, float& logY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->surface, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
            return false;
        logX = (impl_->surface.WindowToDrawable(windowX) - offsetX) / scale;
        logY = (impl_->surface.WindowToDrawable(windowY) - offsetY) / scale;
        return true;
    }

    bool DirectX3Renderer::TransformLogicalToWindow(float logX, float logY,
                                                       float& windowX, float& windowY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->surface, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
            return false;
        windowX = impl_->surface.DrawableToWindow(logX * scale + offsetX);
        windowY = impl_->surface.DrawableToWindow(logY * scale + offsetY);
        return true;
    }

    // ---- Phase O5 (design decision 8): VertexBuffer/IndexBuffer renderers ----
    // Plain CPU-side storage (a std::vector<uint8_t> holding raw vertex/index bytes), matching
    // SoftwareVertexBufferRenderer/SoftwareIndexBufferRenderer's own identical approach exactly --
    // Phase O4's CPU transform pipeline reads directly from these buffers each draw, so there is
    // no GPU-side vertex buffer object to upload to (IDirect3DVertexBuffer doesn't exist until
    // DIRECTX6 anyway, per docs/directx-legacy-renderers-analysis.md section 3.1's table).

    class DirectX3VertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit DirectX3VertexBufferRenderer(int vertexCapacity) : capacity_(vertexCapacity) {}

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override
        {
            if (vertex_count < 0 || vertex_count > capacity_)
                throw std::runtime_error("DirectX3VertexBufferRenderer::SetData: vertex_count exceeds capacity");
            if (stride_in_bytes == 0)
                throw std::runtime_error("DirectX3VertexBufferRenderer::SetData: stride_in_bytes must be > 0");

            vertexCount_ = vertex_count;
            stride_ = stride_in_bytes;
            const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
            data_.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + byteCount);
        }

        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions) override
        {
            SetData(data, vertex_count, stride_in_bytes);
        }

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }
        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }
        [[nodiscard]] const std::vector<uint8_t>& Data() const { return data_; }

        // REMED-GFX-DECL-GUARD: the draw routes infer attribute byte offsets from the stride
        // alone (REMED-GFX-217), so the declaration is remembered rather than discarded and the
        // Draw*Ex routes refuse one those offsets would silently reinterpret.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        /// The declaration this buffer carries, for REMED-GFX-DECL-GUARD's fidelity check.
        [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& Declaration() const
        {
            return declaration_;
        }

    private:
        int capacity_ = 0;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<uint8_t> data_;
    };

    class DirectX3IndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        DirectX3IndexBufferRenderer(int indexCapacity, bool thirtyTwoBit)
            : capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
        {
        }

        void SetData16(const void* data, int index_count) override { Upload(data, index_count, false); }
        void SetData32(const void* data, int index_count) override { Upload(data, index_count, true); }
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions) override
        { Upload(data, index_count, false); }
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions) override
        { Upload(data, index_count, true); }

        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }
        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] const std::vector<uint8_t>& Data() const { return data_; }

    private:
        void Upload(const void* data, int index_count, bool dataIsThirtyTwoBit)
        {
            if (index_count < 0 || index_count > capacity_)
                throw std::runtime_error("DirectX3IndexBufferRenderer: index_count exceeds capacity");
            if (dataIsThirtyTwoBit != thirtyTwoBit_)
                throw std::runtime_error("DirectX3IndexBufferRenderer: SetData bit-width does not match the buffer's declared width");

            indexCount_ = index_count;
            const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(uint32_t) : sizeof(uint16_t);
            const std::size_t byteCount = static_cast<std::size_t>(index_count) * elementSize;
            data_.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + byteCount);
        }

        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<uint8_t> data_;
    };

    // ---- Phase O4 (design decision 6): CPU transform + clip pipeline, ported from
    // SoftwareRenderer.cpp -- computes world*view*projection on the CPU, transforms each
    // vertex into clip space, clips against the near plane (Sutherland-Hodgman, single plane),
    // then perspective-divides the POSITION ONLY and packs into a real D3DTLVERTEX for submission
    // via IDirect3DDevice2::DrawPrimitive/DrawIndexedPrimitive. Simplified from Software's own
    // ClipVertex/RasterVertex: no world-space position/normal fields are carried (design decision
    // 7 scopes lighting/fog/envMap/skinning out of this renderer's v1), and color/uv are NOT
    // premultiplied by invW the way Software's RasterVertex is -- real Direct3D's rasterizer
    // already performs perspective-correct attribute interpolation internally via rhw, so
    // premultiplying here would double-apply the correction (a load-bearing distinction found and
    // documented before this code was written, see plans/plan_dx2.md design decision 6).

    /// One vertex in clip space (before the perspective divide), matching
    /// SoftwareRenderer.cpp's own ClipVertex (position + un-premultiplied color/uv only --
    /// no world-space position/normal, unlike Software's, since envMap/skinning are out of scope).
    /// `sr`/`sg`/`sb` (Phase O9, plans/plan_dx2.md design decision 13): the specular highlight
    /// contribution, additive, packed into D3DTLVERTEX::specular and composited by real
    /// D3DRENDERSTATE_SPECULARENABLE hardware AFTER the texture-modulate stage -- zero for every
    /// draw except a lit DrawPrimitivesEx/DrawIndexedPrimitivesEx (stride 32/52, lightingEnabled).
    struct DirectX3ClipVertex
    {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        float u = 0.0f, v = 0.0f;
        float sr = 0.0f, sg = 0.0f, sb = 0.0f;
    };

    DirectX3ClipVertex DirectX3LerpClipVertex(const DirectX3ClipVertex& a, const DirectX3ClipVertex& b, float t)
    {
        DirectX3ClipVertex out;
        out.x = a.x + t * (b.x - a.x);
        out.y = a.y + t * (b.y - a.y);
        out.z = a.z + t * (b.z - a.z);
        out.w = a.w + t * (b.w - a.w);
        out.r = a.r + t * (b.r - a.r);
        out.g = a.g + t * (b.g - a.g);
        out.b = a.b + t * (b.b - a.b);
        out.a = a.a + t * (b.a - a.a);
        out.u = a.u + t * (b.u - a.u);
        out.v = a.v + t * (b.v - a.v);
        out.sr = a.sr + t * (b.sr - a.sr);
        out.sg = a.sg + t * (b.sg - a.sg);
        out.sb = a.sb + t * (b.sb - a.sb);
        return out;
    }

    /// Ported verbatim from SoftwareRenderer.cpp's ClipTriangleNearPlane (SOFTWARE-83):
    /// Sutherland-Hodgman clip against the single near-plane half-space `w > kNearEpsilon`.
    /// Returns 0 (fully behind, discarded), 3 (no clip needed / one corner clipped), or 4 (two
    /// corners clipped, forming a quad -- caller fans it into 2 triangles). Preserves winding.
    int DirectX3ClipTriangleNearPlane(const DirectX3ClipVertex verts[3], DirectX3ClipVertex out[4])
    {
        constexpr float kNearEpsilon = 1e-5f;
        int count = 0;
        for (int i = 0; i < 3; ++i)
        {
            const DirectX3ClipVertex& cur = verts[i];
            const DirectX3ClipVertex& prev = verts[(i + 2) % 3];
            const bool curIn = cur.w > kNearEpsilon;
            const bool prevIn = prev.w > kNearEpsilon;
            if (curIn != prevIn)
            {
                const float t = (kNearEpsilon - prev.w) / (cur.w - prev.w);
                out[count++] = DirectX3LerpClipVertex(prev, cur, t);
            }
            if (curIn)
                out[count++] = cur;
        }
        return count;
    }

    /// Transforms a VertexPositionColor vertex (Position@0, Color@12 -- DrawColoredPrimitives/
    /// DrawIndexedColoredPrimitives's own fixed layout) into clip space. Ported from
    /// SoftwareRenderer.cpp's BuildPositionColorClipVertex.
    DirectX3ClipVertex DirectX3BuildPositionColorClipVertex(const uint8_t* raw, const Matrix& combined)
    {
        Vector3 position;
        std::memcpy(&position, raw, sizeof(Vector3));
        const Vector4 clip = Vector4::Transform(position, combined);

        DirectX3ClipVertex out;
        out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;
        const uint8_t* colorBytes = raw + sizeof(Vector3);
        out.r = colorBytes[0] / 255.0f;
        out.g = colorBytes[1] / 255.0f;
        out.b = colorBytes[2] / 255.0f;
        out.a = colorBytes[3] / 255.0f;
        return out;
    }

    /// Result of DirectX3ComputeVertexLighting: the diffuse (base vertex color) and specular (additive,
    /// D3DTLVERTEX::specular) contributions, both already RGB (alpha handled separately by the
    /// caller -- diffuseColor's own alpha channel).
    struct DirectX3LitColor
    {
        float diffuseR = 0.0f, diffuseG = 0.0f, diffuseB = 0.0f;
        float specR = 0.0f, specG = 0.0f, specB = 0.0f;
    };

    /// CPU-side BasicEffect-style per-vertex lighting (Phase O9, plans/plan_dx2.md design decision 13):
    /// ambient + up to 3 directional lights, Lambertian diffuse + Blinn-Phong specular. Ported
    /// from EasyGLRenderer.cpp's EnsureLit3DVertexLitProgram() GLSL (CNA's default
    /// per-vertex-lit path -- BasicEffect::preferPerPixelLighting_ defaults to false, matching
    /// real XNA) and BasicEffect::FillGpuDrawParams()'s field semantics -- not re-derived. Only
    /// called for stride==32/52 (the layouts that carry a normal) when params.lightingEnabled.
    DirectX3LitColor DirectX3ComputeVertexLighting(const Vector3& localPosition, const Vector3& localNormal,
                                         const Matrix& world, const GpuDrawParams& params)
    {
        const Vector3 worldPos = Vector3::Transform(localPosition, world);

        // Normal matrix = transpose(inverse(World 3x3)), cofactor/determinant shortcut ported
        // verbatim from EasyGLRenderer.cpp's own Task-398 fix -- correct for non-uniform-
        // scale World transforms, unlike using the raw upper-left 3x3 directly. Reads
        // GpuDrawParams::worldColMajor (already provided for exactly this).
        const float* w = params.worldColMajor;
        const float a = w[0], d = w[1], g = w[2];
        const float b = w[4], e = w[5], h = w[6];
        const float c = w[8], f = w[9], i = w[10];
        const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
        const float nm[9] = {
            (e * i - f * h) * invDet, -(b * i - c * h) * invDet, (b * f - c * e) * invDet,
            -(d * i - f * g) * invDet, (a * i - c * g) * invDet, -(a * f - c * d) * invDet,
            (d * h - e * g) * invDet, -(a * h - b * g) * invDet, (a * e - b * d) * invDet,
        };
        Vector3 normal(
            nm[0] * localNormal.X + nm[1] * localNormal.Y + nm[2] * localNormal.Z,
            nm[3] * localNormal.X + nm[4] * localNormal.Y + nm[5] * localNormal.Z,
            nm[6] * localNormal.X + nm[7] * localNormal.Y + nm[8] * localNormal.Z);
        normal = Vector3::Normalize(normal);

        const Vector3 eyePos(params.eyePositionWorld[0], params.eyePositionWorld[1], params.eyePositionWorld[2]);
        const Vector3 eyeDir = Vector3::Normalize(eyePos - worldPos);

        const float* lightDirs[3]      = {params.light0Dir, params.light1Dir, params.light2Dir};
        const float* lightDiffuses[3]  = {params.light0Diffuse, params.light1Diffuse, params.light2Diffuse};
        const float* lightSpeculars[3] = {params.light0Specular, params.light1Specular, params.light2Specular};

        float lightSum[3] = {params.ambientColor[0], params.ambientColor[1], params.ambientColor[2]};
        float specSum[3]  = {0.0f, 0.0f, 0.0f};

        for (int li = 0; li < 3; ++li)
        {
            const Vector3 lightDir(lightDirs[li][0], lightDirs[li][1], lightDirs[li][2]);
            const float dotL = Vector3::Dot(normal, Vector3(-lightDir.X, -lightDir.Y, -lightDir.Z));
            const float ndotL = std::max(dotL, 0.0f);
            const float zeroL = (dotL >= 0.0f) ? 1.0f : 0.0f;
            lightSum[0] += lightDiffuses[li][0] * ndotL;
            lightSum[1] += lightDiffuses[li][1] * ndotL;
            lightSum[2] += lightDiffuses[li][2] * ndotL;

            const Vector3 half = Vector3::Normalize(Vector3(eyeDir.X - lightDir.X, eyeDir.Y - lightDir.Y, eyeDir.Z - lightDir.Z));
            const float specTerm = std::pow(std::max(Vector3::Dot(half, normal), 0.0f) * zeroL, params.specularPower);
            specSum[0] += specTerm * lightSpeculars[li][0];
            specSum[1] += specTerm * lightSpeculars[li][1];
            specSum[2] += specTerm * lightSpeculars[li][2];
        }

        DirectX3LitColor out;
        out.diffuseR = lightSum[0] * params.diffuseColor[0] + params.emissiveColor[0];
        out.diffuseG = lightSum[1] * params.diffuseColor[1] + params.emissiveColor[1];
        out.diffuseB = lightSum[2] * params.diffuseColor[2] + params.emissiveColor[2];
        out.specR = specSum[0] * params.specularColor[0];
        out.specG = specSum[1] * params.specularColor[1];
        out.specB = specSum[2] * params.specularColor[2];
        return out;
    }

    /// Stride-dispatched vertex transform for the DrawPrimitivesEx/DrawIndexedPrimitivesEx path
    /// (design decision 7's simplified scope -- no skinning bone-blend, no envMap world-space
    /// output, matching this renderer's v1 boundary): 16=VertexPositionColor, 20=
    /// VertexPositionTexture, 24=VertexPositionColorTexture, 32=VertexPositionNormalTexture, 52=
    /// VertexPositionNormalTextureSkinned (bone weights/indices are read but ignored -- renders as
    /// if unskinned, matching the "accept and ignore" pattern design decision 7 documents).
    /// `vertexColorEnabled` mirrors GpuDrawParams' own flag -- when false, vertex color is forced
    /// to opaque white so only texture modulation and the material-less diffuse pass-through
    /// apply. Phase O9 (design decision 13): when params.lightingEnabled and the stride carries a
    /// normal (32/52), DirectX3ComputeVertexLighting() replaces the raw/white vertex color with real
    /// ambient+directional lighting instead -- `vertexColorEnabled` is irrelevant in that case
    /// (neither of those two strides carries a per-vertex diffuse channel to begin with).
    DirectX3ClipVertex DirectX3BuildGenericClipVertex(const uint8_t* raw, std::size_t stride, const Matrix& combined,
                                            const Matrix& world, const GpuDrawParams& params)
    {
        Vector3 position;
        std::memcpy(&position, raw, sizeof(Vector3));
        const Vector4 clip = Vector4::Transform(position, combined);

        DirectX3ClipVertex out;
        out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;

        bool lit = false;

        if (stride == 16)
        {
            out.r = raw[12] / 255.0f; out.g = raw[13] / 255.0f; out.b = raw[14] / 255.0f; out.a = raw[15] / 255.0f;
        }
        else if (stride == 20)
        {
            std::memcpy(&out.u, raw + 12, sizeof(float));
            std::memcpy(&out.v, raw + 16, sizeof(float));
        }
        else if (stride == 24)
        {
            out.r = raw[12] / 255.0f; out.g = raw[13] / 255.0f; out.b = raw[14] / 255.0f; out.a = raw[15] / 255.0f;
            std::memcpy(&out.u, raw + 16, sizeof(float));
            std::memcpy(&out.v, raw + 20, sizeof(float));
        }
        else if (stride == 32 || stride == 52)
        {
            // VertexPositionNormalTexture(Skinned): Position@0, Normal@12, TextureCoordinate@24;
            // stride 52's BlendWeight@32/BlendIndices@48 are ignored (design decision 7).
            std::memcpy(&out.u, raw + 24, sizeof(float));
            std::memcpy(&out.v, raw + 28, sizeof(float));

            if (params.lightingEnabled)
            {
                Vector3 normal;
                std::memcpy(&normal, raw + 12, sizeof(Vector3));
                const DirectX3LitColor litColor = DirectX3ComputeVertexLighting(position, normal, world, params);
                out.r = litColor.diffuseR; out.g = litColor.diffuseG; out.b = litColor.diffuseB;
                out.a = params.diffuseColor[3];
                out.sr = litColor.specR; out.sg = litColor.specG; out.sb = litColor.specB;
                lit = true;
            }
        }

        if (!lit && !params.vertexColorEnabled)
        {
            out.r = out.g = out.b = out.a = 1.0f;
        }
        return out;
    }

    /// Perspective-divides the POSITION ONLY and maps into a real D3DTLVERTEX ready for
    /// DrawPrimitive/DrawIndexedPrimitive. Deliberately does NOT premultiply color/uv by invW --
    /// see this section's own header comment and plans/plan_dx2.md design decision 6 for why.
    D3DTLVERTEX DirectX3ClipVertexToD3DTLVERTEX(const DirectX3ClipVertex& cv, int viewportWidth, int viewportHeight)
    {
        const float invW = 1.0f / cv.w;
        const float ndcX = cv.x * invW;
        const float ndcY = cv.y * invW;
        const float ndcZ = cv.z * invW;

        D3DTLVERTEX out{};
        out.sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth);
        out.sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(viewportHeight);
        out.sz = ndcZ;
        out.rhw = invW;
        const auto channel = [](float c) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f);
        };
        out.color = (static_cast<D3DCOLOR>(channel(cv.a)) << 24) |
                    (static_cast<D3DCOLOR>(channel(cv.r)) << 16) |
                    (static_cast<D3DCOLOR>(channel(cv.g)) << 8) |
                    static_cast<D3DCOLOR>(channel(cv.b));
        // Phase O9 (design decision 13): specular highlight, composited by real Direct3D
        // fixed-function hardware AFTER the texture-modulate stage when
        // D3DRENDERSTATE_SPECULARENABLE is set (spike-confirmed real, dx2_spike10). Alpha is
        // unused by that compositing stage -- always full, harmless.
        out.specular = (static_cast<D3DCOLOR>(0xFFu) << 24) |
                       (static_cast<D3DCOLOR>(channel(cv.sr)) << 16) |
                       (static_cast<D3DCOLOR>(channel(cv.sg)) << 8) |
                       static_cast<D3DCOLOR>(channel(cv.sb));
        out.tu = cv.u;
        out.tv = cv.v;
        return out;
    }

    /// Resolves a GpuDrawParams::texture0-style ITextureRenderer* into a real D3DTEXTUREHANDLE for
    /// the current device, or 0 (no texture) when `texture` is null. Fetched fresh on every draw
    /// (not cached) rather than stored on DirectX3TextureRenderer: a handle is only valid for the device
    /// it was obtained against, and this renderer's device2 is torn down and recreated whenever the
    /// backbuffer is resized (Create3DDevice) -- caching would need explicit invalidation on
    /// resize for no real benefit, since QueryInterface+GetHandle is a cheap COM call.
    D3DTEXTUREHANDLE DirectX3ResolveTextureHandle(const ITextureRenderer* texture, LPDIRECT3DDEVICE2 device2)
    {
        if (!texture) return 0;
        const auto* owner = dynamic_cast<const DirectX3SurfaceOwner*>(texture);
        if (!owner)
            throw std::runtime_error(
                "DirectX3Renderer: texture0 is not a DIRECTX3 surface (created by a different graphics renderer?)");

        LPDIRECT3DTEXTURE2 tex2 = nullptr;
        HRESULT hr = owner->Surface()->QueryInterface(IID_IDirect3DTexture2, reinterpret_cast<void**>(&tex2));
        if (FAILED(hr)) ThrowHr("IDirectDrawSurface::QueryInterface(IID_IDirect3DTexture2)", hr);

        D3DTEXTUREHANDLE handle = 0;
        hr = tex2->GetHandle(device2, &handle);
        tex2->Release();
        if (FAILED(hr)) ThrowHr("IDirect3DTexture2::GetHandle", hr);
        return handle;
    }

    /// Shared core for all 4 draw entry points: clips each triangle (DirectX3ClipTriangleNearPlane),
    /// packs the result into D3DTLVERTEX (DirectX3ClipVertexToD3DTLVERTEX), and submits via a single
    /// DrawIndexedPrimitive call -- used even for CNA's non-indexed draw calls
    /// (DrawColoredPrimitives/DrawPrimitivesEx) since near-plane clipping can turn one triangle
    /// into a quad (2 triangles sharing 2 vertices), which an index buffer expresses without
    /// vertex duplication. `fetchVertex(i)` returns the i-th vertex in sequential triangle-list
    /// order (i in [0, primitiveCount*3)); the caller supplies whichever stride/index-buffer
    /// resolution its own entry point needs. `texHandle` is applied via
    /// D3DRENDERSTATE_TEXTUREHANDLE every call (0 = no texture) since it is a per-draw state,
    /// unlike D3DRENDERSTATE_LIGHTING (set once at device creation, plans/plan_dx2.md design decision 6).
    /// `specularEnabled` (Phase O9, design decision 13): sets D3DRENDERSTATE_SPECULARENABLE per
    /// draw -- true only for a lit DrawPrimitivesEx/DrawIndexedPrimitivesEx call, spike-confirmed
    /// (dx2_spike10) to genuinely composite D3DTLVERTEX::specular additively after the
    /// texture-modulate stage; DrawColoredPrimitives/DrawIndexedColoredPrimitives always pass
    /// false (their vertices' specular channel is always zero anyway, but leaving the render
    /// state off avoids an unnecessary per-draw state change on the no-lighting-concept path).
    void SubmitDx30Primitives(LPDIRECT3DDEVICE2 device2, int viewportWidth, int viewportHeight,
                             const std::function<DirectX3ClipVertex(int)>& fetchVertex,
                             int primitiveCount, D3DTEXTUREHANDLE texHandle, bool specularEnabled)
    {
        std::vector<D3DTLVERTEX> verts;
        std::vector<WORD> indices;
        verts.reserve(static_cast<std::size_t>(primitiveCount) * 3);
        indices.reserve(static_cast<std::size_t>(primitiveCount) * 3);

        for (int i = 0; i < primitiveCount; ++i)
        {
            DirectX3ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
                cv[k] = fetchVertex(i * 3 + k);

            DirectX3ClipVertex clipped[4];
            const int clippedCount = DirectX3ClipTriangleNearPlane(cv, clipped);
            if (clippedCount == 0) continue;

            const auto baseIdx = static_cast<WORD>(verts.size());
            for (int k = 0; k < clippedCount; ++k)
                verts.push_back(DirectX3ClipVertexToD3DTLVERTEX(clipped[k], viewportWidth, viewportHeight));

            indices.push_back(static_cast<WORD>(baseIdx + 0));
            indices.push_back(static_cast<WORD>(baseIdx + 1));
            indices.push_back(static_cast<WORD>(baseIdx + 2));
            if (clippedCount == 4)
            {
                indices.push_back(static_cast<WORD>(baseIdx + 0));
                indices.push_back(static_cast<WORD>(baseIdx + 2));
                indices.push_back(static_cast<WORD>(baseIdx + 3));
            }
        }

        if (indices.empty()) return;  // every triangle fully clipped by the near plane

        HRESULT hr = device2->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, texHandle);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice2::SetRenderState(TEXTUREHANDLE)", hr);
        hr = device2->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATE);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice2::SetRenderState(TEXTUREMAPBLEND)", hr);
        hr = device2->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, specularEnabled ? TRUE : FALSE);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice2::SetRenderState(SPECULARENABLE)", hr);

        hr = device2->BeginScene();
        if (FAILED(hr)) ThrowHr("IDirect3DDevice2::BeginScene", hr);
        hr = device2->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX,
                                           verts.data(), static_cast<DWORD>(verts.size()),
                                           indices.data(), static_cast<DWORD>(indices.size()), 0);
        if (FAILED(hr))
        {
            device2->EndScene();
            ThrowHr("IDirect3DDevice2::DrawIndexedPrimitive", hr);
        }
        hr = device2->EndScene();
        if (FAILED(hr)) ThrowHr("IDirect3DDevice2::EndScene", hr);
    }

    /// Common precondition checks shared by all 4 draw entry points (design decision 4: 3D drawing
    /// is scoped to the default backbuffer only -- device2 is never bound to a custom render
    /// target's surface).
    void DirectX3CheckDrawPreconditions(const char* who, bool customRenderTargetBound,
                                   PrimitiveType primitive, int primitiveCount)
    {
        if (customRenderTargetBound)
            throw std::runtime_error(std::string("DirectX3Renderer::") + who +
                ": 3D drawing is not supported while a custom RenderTarget2D is bound "
                "(design decision 4 -- 3D is scoped to the default backbuffer only)");
        if (primitiveCount <= 0)
            throw std::runtime_error(std::string("DirectX3Renderer::") + who + ": primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error(std::string("DirectX3Renderer::") + who + ": only TriangleList is supported in v1");
    }

    // ---- Phase O3: textures and render targets ----
    // Both classes below are never named outside this .cpp (only returned polymorphically), so
    // <ddraw.h> stays fully contained here.

    class DirectX3TextureRenderer : public ITextureRenderer, public DirectX3SurfaceOwner
    {
    public:
        DirectX3TextureRenderer(LPDIRECTDRAW2 dd, int width, int height)
            : width_(width), height_(height), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        DirectX3TextureRenderer(LPDIRECTDRAW2 dd, const ImageData& data)
            : DirectX3TextureRenderer(dd, data.width, data.height)
        {
            WriteSurfacePixels(surface_, width_, height_, data.pixels.data(), width_ * 4);
        }

        ~DirectX3TextureRenderer() override { if (surface_) surface_->Release(); }

        DirectX3TextureRenderer(const DirectX3TextureRenderer&) = delete;
        DirectX3TextureRenderer& operator=(const DirectX3TextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            WriteSurfacePixels(surface_, width_, height_, rgba, stride);
        }

        // No native mip chain on IDirectDrawSurface -- level 0 is unaffected (always routed
        // through UpdatePixels by Texture2D::SetData), level>0 throws honestly rather than
        // silently discarding the upload, matching the other 2D backends' precedent.
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

    class DirectX3RenderTargetRenderer final : public IRenderTargetRenderer, public DirectX3SurfaceOwner
    {
    public:
        DirectX3RenderTargetRenderer(LPDIRECTDRAWSURFACE* currentTargetSlot, int* currentTargetWidthSlot,
                               int* currentTargetHeightSlot, LPDIRECTDRAW2 dd,
                               int width, int height, int multiSampleCount)
            : currentTargetSlot_(currentTargetSlot), currentTargetWidthSlot_(currentTargetWidthSlot),
              currentTargetHeightSlot_(currentTargetHeightSlot), width_(width), height_(height),
              multiSampleCount_(multiSampleCount), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        ~DirectX3RenderTargetRenderer() override
        {
            UnbindAsRenderTarget();
            if (surface_) surface_->Release();
        }

        DirectX3RenderTargetRenderer(const DirectX3RenderTargetRenderer&) = delete;
        DirectX3RenderTargetRenderer& operator=(const DirectX3RenderTargetRenderer&) = delete;

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
        // was requested -- always false, as in the other depth-less 2D backends.
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

    std::unique_ptr<ITextureRenderer> DirectX3Renderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<DirectX3TextureRenderer>(impl_->dd, data);
    }

    std::unique_ptr<IRenderTargetRenderer> DirectX3Renderer::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int multiSampleCount)
    {
        return std::make_unique<DirectX3RenderTargetRenderer>(&impl_->currentTargetSurface,
                                                         &impl_->currentTargetWidth, &impl_->currentTargetHeight,
                                                         impl_->dd, w, h, multiSampleCount);
    }

    void DirectX3Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
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

    void DirectX3Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        // DirectDraw has no multi-render-target concept -- single active surface only.
        if (count > 1)
            throw std::runtime_error(
                "DIRECTX3 (DirectDraw v2) does not support multiple simultaneous render targets (MRT): "
                "requested " + std::to_string(count) + ", but IDirectDrawSurface supports exactly "
                "one active render target at a time.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "DIRECTX3 (DirectDraw v2) does not support RenderTargetCube face bindings.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    // ---- Phase O4: the CPU compositor / SpriteBatch draw path (design decision 5) ----
    // Never named outside this .cpp, same reasoning as DirectX3TextureRenderer/DirectX3RenderTargetRenderer.

    class DirectX3SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        DirectX3SpriteBatchRenderer(std::function<LPDIRECTDRAWSURFACE()> getActiveSurface,
                              std::function<void(int&, int&)> getActiveSurfaceSize,
                              std::function<DirectX3BlendMode()> getBlendMode)
            : getActiveSurface_(std::move(getActiveSurface)),
              getActiveSurfaceSize_(std::move(getActiveSurfaceSize)),
              getBlendMode_(std::move(getBlendMode))
        {
        }

        void Begin() override
        {
            if (begun_)
                throw std::runtime_error("DirectX3SpriteBatchRenderer::Begin: Begin() called without a matching End()");
            begun_ = true;
        }

        void End() override
        {
            if (!begun_)
                throw std::runtime_error("DirectX3SpriteBatchRenderer::End: End() called without a matching Begin()");
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
                    "DIRECTX3 (DirectDraw v2) does not support custom SpriteBatch Effects: no "
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
                throw std::runtime_error("DirectX3SpriteBatchRenderer::Draw: Draw() called before Begin()");

            const auto* owner = dynamic_cast<const DirectX3SurfaceOwner*>(&texture);
            if (!owner)
                throw std::runtime_error(
                    "DirectX3SpriteBatchRenderer::Draw: texture renderer is not a DIRECTX3 surface (created by "
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
            if (isIdentityGeometry && isWhiteTint && getBlendMode_() == DirectX3BlendMode::Opaque)
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
        std::function<DirectX3BlendMode()> getBlendMode_;
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

    std::unique_ptr<ISpriteBatchRenderer> DirectX3Renderer::CreateSpriteBatch()
    {
        Impl* impl = impl_.get();
        return std::make_unique<DirectX3SpriteBatchRenderer>(
            [impl]() { return impl->ActiveSurface(); },
            [impl](int& w, int& h) { impl->ActiveSurfaceSize(w, h); },
            [impl]() { return impl->currentBlendMode; });
    }

    void DirectX3Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
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

        // Phase O6: real 3D blend state. D3D v1/v2 has no separate alpha blend-factor/op pair or
        // blend-equation render state (confirmed absent from d3dtypes.h by inspection) --
        // alphaSrcBlend/alphaDstBlend/colorBlendFunc/alphaBlendFunc are accepted and ignored.
        impl_->device2->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
        impl_->device2->SetRenderState(D3DRENDERSTATE_SRCBLEND, DirectX3BlendToD3D(colorSrcBlend));
        impl_->device2->SetRenderState(D3DRENDERSTATE_DESTBLEND, DirectX3BlendToD3D(colorDstBlend));
    }

    void DirectX3Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                    bool /*stencilEnable*/, int /*stencilFunc*/,
                                                    int /*stencilPass*/, int /*stencilFail*/, int /*stencilDepthFail*/,
                                                    int /*stencilMask*/, int /*stencilWriteMask*/, int /*referenceStencil*/,
                                                    bool /*twoSidedStencilMode*/,
                                                    int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                    int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        // Phase O6 (design decision 7): only depth is real at this DirectX era -- every stencil
        // parameter is accepted and silently ignored (no real stencil buffer/ops exist until DIRECTX6).
        impl_->device2->SetRenderState(D3DRENDERSTATE_ZENABLE, depthEnable ? D3DZB_TRUE : D3DZB_FALSE);
        impl_->device2->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, depthWriteEnable ? TRUE : FALSE);
        impl_->device2->SetRenderState(D3DRENDERSTATE_ZFUNC, DirectX3CompareFunctionToD3D(depthFunc));
    }

    void DirectX3Renderer::ApplyRasterizerState(int cullMode, int fillMode, bool /*scissorTestEnable*/,
                                                  float /*depthBias*/, float /*slopeScaleDepthBias*/)
    {
        // Phase O6: scissorTestEnable/depthBias/slopeScaleDepthBias are accepted and ignored -- no
        // scissor test or depth-bias render state exists in d3dtypes.h at this DirectX era
        // (confirmed by inspection, matching design decision 7's pattern).
        impl_->device2->SetRenderState(D3DRENDERSTATE_CULLMODE, DirectX3CullModeToD3D(cullMode));
        impl_->device2->SetRenderState(D3DRENDERSTATE_FILLMODE, DirectX3FillModeToD3D(fillMode));
    }

    void DirectX3Renderer::ApplySamplerState(int slot, int filter, int addressU, int /*addressV*/,
                                               int maxAnisotropy)
    {
        // Phase O6: D3D v1/v2 has exactly one texture stage (no multitexture, decision 7) and no
        // per-slot sampler state concept at all -- only slot 0 is honored, matching the single
        // combined D3DRENDERSTATE_TEXTUREMAG/MIN/ADDRESS/ANISOTROPY render states that exist.
        // addressV is accepted and ignored: D3DRENDERSTATE_TEXTUREADDRESS is a single combined U+V
        // mode, there is no separate per-axis render state to set it on.
        if (slot != 0) return;

        const DirectX3FilterPair filterPair = DirectX3TextureFilterToD3D(filter);
        impl_->device2->SetRenderState(D3DRENDERSTATE_TEXTUREMAG, filterPair.mag);
        impl_->device2->SetRenderState(D3DRENDERSTATE_TEXTUREMIN, filterPair.min);
        impl_->device2->SetRenderState(D3DRENDERSTATE_TEXTUREADDRESS, DirectX3TextureAddressModeToD3D(addressU));
        impl_->device2->SetRenderState(D3DRENDERSTATE_ANISOTROPY, static_cast<DWORD>(maxAnisotropy));
    }

    // Phase O3 (design decision 5): color goes through the same ActiveSurface() path Clear(r,g,b,a)
    // already uses; depth always targets impl_->zbuffer directly, since only the shadow backbuffer
    // (design decision 4) ever has a Z-buffer attached in this v1 -- a custom RenderTarget2D bound
    // at the same time has no 3D capability at all (Phase O3/O4's scope), so this combination is
    // out of scope, not specially guarded against here.
    void DirectX3Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }

    void DirectX3Renderer::ClearDepth(float depth)
    {
        int width = 0, height = 0;
        impl_->ActiveSurfaceSize(width, height);
        FillZBuffer16(impl_->zbuffer, width, height, depth);
    }

    // Design decision 7: no real stencil buffer exists at this DirectX era (DIRECTX6+) -- stencil clears
    // are accepted and silently ignored, never thrown, matching the "accept and ignore" pattern
    // docs/directx-legacy-renderers-analysis.md documents as already blessed elsewhere in this
    // family. A pure stencil-only clear is therefore a complete no-op.
    void DirectX3Renderer::ClearStencil(int) {}
    void DirectX3Renderer::ClearDepthAndStencil(float depth, int) { ClearDepth(depth); }
    void DirectX3Renderer::ClearColorAndStencil(float r, float g, float b, float a, int) { Clear(r, g, b, a); }
    void DirectX3Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int)
    {
        ClearColorAndDepth(r, g, b, a, depth);
    }
    void DirectX3Renderer::SetDepthTestEnabled(bool enabled)
    {
        impl_->device2->SetRenderState(D3DRENDERSTATE_ZENABLE, enabled ? D3DZB_TRUE : D3DZB_FALSE);
    }

    // Deliberate no-op, matching D3D9's/D3D11's/D3D12's own identical choice: a bare "enable
    // blending" has no defined blend factors in XNA -- real blend configuration always arrives via
    // ApplyBlendState(), which already unconditionally enables blending
    // (D3DRENDERSTATE_ALPHABLENDENABLE) whenever it's called.
    void DirectX3Renderer::SetBlendEnabled(bool) {}

    void DirectX3Renderer::SetDepthWriteEnabled(bool enabled)
    {
        impl_->device2->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, enabled ? TRUE : FALSE);
    }

    std::unique_ptr<IVertexBufferRenderer> DirectX3Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<DirectX3VertexBufferRenderer>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> DirectX3Renderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<DirectX3IndexBufferRenderer>(index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> DirectX3Renderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<DirectX3IndexBufferRenderer>(index_capacity, true);
    }

    void DirectX3Renderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                                   PrimitiveType primitive, int primitiveCount)
    {
        DirectX3CheckDrawPreconditions("DrawColoredPrimitives", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (primitiveCount * 3 > vb.GetVertexCount())
            throw std::runtime_error(
                "DirectX3Renderer::DrawColoredPrimitives: primitiveCount needs more vertices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX3VertexBufferRenderer&>(vb);
        const uint8_t* base = dxVb.Data().data();
        const std::size_t stride = dxVb.Stride();
        const Matrix combined = world * view * projection;
        int vw = 0, vh = 0;
        impl_->ActiveSurfaceSize(vw, vh);

        SubmitDx30Primitives(impl_->device2, vw, vh,
            [&](int i) { return DirectX3BuildPositionColorClipVertex(base + static_cast<std::size_t>(i) * stride, combined); },
            primitiveCount, 0, false);
    }

    void DirectX3Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                          const IIndexBufferRenderer& ib,
                                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                                          PrimitiveType primitive, int primitiveCount)
    {
        DirectX3CheckDrawPreconditions("DrawIndexedColoredPrimitives", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (primitiveCount * 3 > ib.GetIndexCount())
            throw std::runtime_error(
                "DirectX3Renderer::DrawIndexedColoredPrimitives: primitiveCount needs more indices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX3VertexBufferRenderer&>(vb);
        const auto& dxIb = static_cast<const DirectX3IndexBufferRenderer&>(ib);
        const uint8_t* vbBase = dxVb.Data().data();
        const std::size_t stride = dxVb.Stride();
        const uint8_t* ibBase = dxIb.Data().data();
        const bool thirtyTwoBit = dxIb.IsThirtyTwoBit();
        const Matrix combined = world * view * projection;
        int vw = 0, vh = 0;
        impl_->ActiveSurfaceSize(vw, vh);

        const auto readIndex = [&](int i) -> uint32_t {
            if (thirtyTwoBit)
            {
                uint32_t v;
                std::memcpy(&v, ibBase + static_cast<std::size_t>(i) * sizeof(uint32_t), sizeof(uint32_t));
                return v;
            }
            uint16_t v;
            std::memcpy(&v, ibBase + static_cast<std::size_t>(i) * sizeof(uint16_t), sizeof(uint16_t));
            return v;
        };

        SubmitDx30Primitives(impl_->device2, vw, vh,
            [&](int i) {
                const uint32_t idx = readIndex(i);
                return DirectX3BuildPositionColorClipVertex(vbBase + static_cast<std::size_t>(idx) * stride, combined);
            },
            primitiveCount, 0, false);
    }

    // REMED-GFX-DECL-GUARD: the declaration-fidelity boundary. The Draw*Ex routes below read
    // their attributes at byte offsets chosen by the stride alone (REMED-GFX-217), so a
    // declaration those offsets cannot represent is refused before any vertex is fetched. A
    // stride outside this renderer's own supported set is left to its established out-of-table
    // rejection, which is already loud and deterministic.
    static void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& dxVb = static_cast<const DirectX3VertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            dxVb.Declaration(), static_cast<int>(dxVb.Stride()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "DIRECTX3", route);
    }

    void DirectX3Renderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount,
                                              const GpuDrawParams& params)
    {
        RequireFaithfulDeclarationEXT(vb, "ordinary-nonindexed");
        DirectX3CheckDrawPreconditions("DrawPrimitivesEx", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (params.textureEnabled && params.texture0 == nullptr)
            throw std::runtime_error("DirectX3Renderer::DrawPrimitivesEx: textureEnabled=true but texture0 is null");
        if (params.vertexStart + primitiveCount * 3 > vb.GetVertexCount())
            throw std::runtime_error(
                "DirectX3Renderer::DrawPrimitivesEx: vertexStart + primitiveCount needs more vertices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX3VertexBufferRenderer&>(vb);
        const std::size_t stride = dxVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 && stride != 52)
            throw std::runtime_error(
                "DirectX3Renderer::DrawPrimitivesEx: unsupported vertex stride (only 16/20/24/32/52 supported in v1)");

        const uint8_t* base = dxVb.Data().data();
        const Matrix combined = world * view * projection;
        int vw = 0, vh = 0;
        impl_->ActiveSurfaceSize(vw, vh);
        const D3DTEXTUREHANDLE texHandle = params.textureEnabled
            ? DirectX3ResolveTextureHandle(params.texture0, impl_->device2) : 0;
        const int vertexStart = params.vertexStart;

        SubmitDx30Primitives(impl_->device2, vw, vh,
            [&](int i) { return DirectX3BuildGenericClipVertex(base + static_cast<std::size_t>(vertexStart + i) * stride,
                                                          stride, combined, world, params); },
            primitiveCount, texHandle, params.lightingEnabled);
    }

    void DirectX3Renderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount,
                                                     const GpuDrawParams& params)
    {
        RequireFaithfulDeclarationEXT(vb, "ordinary-indexed");
        DirectX3CheckDrawPreconditions("DrawIndexedPrimitivesEx", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (params.textureEnabled && params.texture0 == nullptr)
            throw std::runtime_error("DirectX3Renderer::DrawIndexedPrimitivesEx: textureEnabled=true but texture0 is null");
        if (params.startIndex + primitiveCount * 3 > ib.GetIndexCount())
            throw std::runtime_error(
                "DirectX3Renderer::DrawIndexedPrimitivesEx: startIndex + primitiveCount needs more indices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX3VertexBufferRenderer&>(vb);
        const auto& dxIb = static_cast<const DirectX3IndexBufferRenderer&>(ib);
        const std::size_t stride = dxVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 && stride != 52)
            throw std::runtime_error(
                "DirectX3Renderer::DrawIndexedPrimitivesEx: unsupported vertex stride (only 16/20/24/32/52 supported in v1)");

        const uint8_t* vbBase = dxVb.Data().data();
        const uint8_t* ibBase = dxIb.Data().data();
        const bool thirtyTwoBit = dxIb.IsThirtyTwoBit();
        const Matrix combined = world * view * projection;
        int vw = 0, vh = 0;
        impl_->ActiveSurfaceSize(vw, vh);
        const D3DTEXTUREHANDLE texHandle = params.textureEnabled
            ? DirectX3ResolveTextureHandle(params.texture0, impl_->device2) : 0;
        const int startIndex = params.startIndex;
        const int baseVertex = params.baseVertex;

        const auto readIndex = [&](int i) -> uint32_t {
            if (thirtyTwoBit)
            {
                uint32_t v;
                std::memcpy(&v, ibBase + static_cast<std::size_t>(i) * sizeof(uint32_t), sizeof(uint32_t));
                return v;
            }
            uint16_t v;
            std::memcpy(&v, ibBase + static_cast<std::size_t>(i) * sizeof(uint16_t), sizeof(uint16_t));
            return v;
        };

        SubmitDx30Primitives(impl_->device2, vw, vh,
            [&](int i) {
                const uint32_t idx = readIndex(startIndex + i) + static_cast<uint32_t>(baseVertex);
                return DirectX3BuildGenericClipVertex(vbBase + static_cast<std::size_t>(idx) * stride, stride,
                                                 combined, world, params);
            },
            primitiveCount, texHandle, params.lightingEnabled);
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_DIRECTX3
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace DirectX3 { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> DirectX3::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<DirectX3::DirectX3Renderer>(args);
    }
#endif
}
