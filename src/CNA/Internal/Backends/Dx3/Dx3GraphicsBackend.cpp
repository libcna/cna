#include "CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp"

// plan_dx3.md Design decision 9: <ddraw.h> (and the <windows.h> compatibility shim it pulls in
// from free-api) is contained to this .cpp only -- see Dx3GraphicsBackend.hpp's own comment for
// why this backend goes further than D3D11/D3D12's precedent and keeps it out of its own header
// too (the fopen -> free_api_fopen macro leak risk).
#include <ddraw.h>

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Dx3
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Backends;

    namespace
    {
        [[noreturn]] void ThrowHr(const char* what, HRESULT hr)
        {
            throw std::runtime_error(std::string(what) + " failed: HRESULT=0x" +
                                      std::to_string(static_cast<unsigned long>(hr)));
        }

        // ---- 3D pipeline: DirectDraw is 2D-only. All 3D calls throw. ----
        // Callers can check GraphicsDevice::SupportsCapability(GraphicsCapability::ThreeD) ahead
        // of time instead of relying on this throw -- see SupportsCapability() in the header.
        [[noreturn]] void ThrowNo3D(const char* methodName)
        {
            throw std::runtime_error(std::string("DX3 (DirectDraw) does not support 3D: ") + methodName);
        }

        // ---- Phase X3: shared offscreen-surface helpers, used by both Dx3TextureBackend and
        // Dx3RenderTargetBackend (same underlying DirectDraw mechanism -- design decision 4:
        // 32bpp DDSCAPS_OFFSCREENPLAIN only, no palette/8-bit path). Kept as free functions rather
        // than a shared base class: neither type is ever named outside this .cpp (only returned
        // polymorphically as ITextureBackend/IRenderTargetBackend), so there is no header/ddraw.h
        // containment reason to give them one (design decision 9).
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
            return surface;
        }

        // Writes a full level-0 RGBA8 image into `surface` via Lock()/memcpy/Unlock(). `stride` is
        // the source row length in bytes; <=0 means tightly packed (width*4), matching
        // ITextureBackend::UpdatePixels's own documented convention.
        void WriteSurfacePixels(LPDIRECTDRAWSURFACE surface, int width, int height,
                                const uint8_t* rgba, int stride)
        {
            if (!rgba) return;
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, 0, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(write)", hr);

            auto* base = static_cast<uint8_t*>(desc.lpSurface);
            const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
            const std::size_t srcStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(base + static_cast<std::size_t>(row) * static_cast<std::size_t>(desc.lPitch),
                           rgba + static_cast<std::size_t>(row) * srcStride, rowBytes);
            }
            surface->Unlock(desc.lpSurface);
        }

        // Reads an (x, y, w, h) region back from `surface` via Lock()/memcpy/Unlock(), writing
        // tightly-packed RGBA8 rows into `pixels`. Shared by ReadBackbuffer (DX3-26) against
        // whichever surface is currently active.
        void ReadSurfacePixels(LPDIRECTDRAWSURFACE surface, int x, int y, int w, int h, uint8_t* pixels)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, 0, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(read)", hr);

            const auto* base = static_cast<const uint8_t*>(desc.lpSurface);
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* src = base + static_cast<std::size_t>(y + row) * static_cast<std::size_t>(desc.lPitch) +
                                      static_cast<std::size_t>(x) * 4u;
                std::memcpy(pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u, src,
                           static_cast<std::size_t>(w) * 4u);
            }
            surface->Unlock(desc.lpSurface);
        }

        // Fills the full (width, height) extent of `surface` with a solid RGBA8 color via
        // Lock()/Unlock(). Used by Clear() instead of DDBLT_COLORFILL: free-direct's own
        // FillColor() hardcodes the written alpha byte to 255 unconditionally (confirmed in
        // ../free-direct/src/directdraw/DirectDraw.cpp), so a ColorFill-based Clear() can never
        // honor a requested alpha other than fully opaque. This writes all 4 channels directly,
        // matching the requested color exactly.
        void FillSurfaceColor(LPDIRECTDRAWSURFACE surface, int width, int height,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, 0, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(clear)", hr);

            auto* base = static_cast<uint8_t*>(desc.lpSurface);
            for (int y = 0; y < height; ++y)
            {
                uint8_t* row = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(desc.lPitch);
                for (int x = 0; x < width; ++x)
                {
                    uint8_t* px = row + static_cast<std::size_t>(x) * 4u;
                    px[0] = r; px[1] = g; px[2] = b; px[3] = a;
                }
            }
            surface->Unlock(desc.lpSurface);
        }

        [[noreturn]] void ThrowMipLevelUnsupported(int level)
        {
            throw std::runtime_error(
                "DX3 (DirectDraw) does not support mip-level texture uploads (level " +
                std::to_string(level) + "): IDirectDrawSurface has no native mip chain or "
                "per-level LOD sampling. Use Texture2D::SetData(level=0, ...) only.");
        }

        // ---- Phase X4: CPU 2D compositor (design decision 5) ----

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

        // ---- Phase X5: real, distinct blend formulas + filter/address sampling (design decisions
        // 6/7) ----

        // Matches every other CNA backend's Blend-enum-ordinal mapping (e.g.
        // SdlGraphicsBackend::ToSdlBlendFactor): One=0, Zero=1, SourceColor=2,
        // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
        // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9.
        enum class Dx3BlendMode { Opaque, AlphaBlend, NonPremultiplied, Additive };

        // Detects which of the 4 BlendState presets (BlendState.cpp) the raw factors match, by
        // exact value -- not by BlendState identity (the backend never sees a BlendState object,
        // only GraphicsDevice::ApplyBlendState's raw ints).
        // A real preset match requires BOTH the 4 factors AND both blend functions (color/alpha)
        // to match -- all 4 real BlendState.cpp presets use BlendFunction::Add (0) implicitly
        // (their default-constructed value; none of the static presets override it). A custom
        // BlendState with e.g. Opaque's exact factors but BlendFunction::Subtract is NOT
        // equivalent to Opaque and must fall through to the DX3-44 AlphaBlend fallback below, not
        // be misdetected as Opaque just because the factors happen to match.
        Dx3BlendMode DetectBlendMode(int colorSrc, int alphaSrc, int colorDst, int alphaDst,
                                     int colorFunc, int alphaFunc)
        {
            const bool bothAdd = (colorFunc == 0 && alphaFunc == 0);
            // NonPremultiplied: ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, ColorDst=InvSrcAlpha, AlphaDst=InvSrcAlpha
            if (bothAdd && colorSrc == 4 && alphaSrc == 4 && colorDst == 5 && alphaDst == 5) return Dx3BlendMode::NonPremultiplied;
            // Additive: ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, ColorDst=One, AlphaDst=One
            if (bothAdd && colorSrc == 4 && alphaSrc == 4 && colorDst == 0 && alphaDst == 0) return Dx3BlendMode::Additive;
            // Opaque: ColorSrc=One, AlphaSrc=One, ColorDst=Zero, AlphaDst=Zero
            if (bothAdd && colorSrc == 0 && alphaSrc == 0 && colorDst == 1 && alphaDst == 1) return Dx3BlendMode::Opaque;
            // AlphaBlend: ColorSrc=One, AlphaSrc=One, ColorDst=InvSrcAlpha, AlphaDst=InvSrcAlpha --
            // and DX3-44's fallback for any other/custom factor+op combination (including a
            // factor match with a non-Add function), same recorded scope limitation SOFTWARE's
            // design decision 7 already made (no general blend-equation interpreter in v1).
            return Dx3BlendMode::AlphaBlend;
        }

        // TextureAddressMode raw int convention (matches ISpriteBatchBackend::SetSamplerAddressMode's
        // own doc): 0=Wrap, 1=Clamp, 2=Mirror. Maps a possibly out-of-[0,size) integer texel
        // coordinate into range accordingly (DX3-46).
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
        // Point/nearest -- matches ISpriteBatchBackend::SetSamplerFilter's own doc, DX3-45) and
        // `addressU`/`addressV` (DX3-46). Writes 4 raw bytes (RGBA8) into `out`.
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
                    const float top = static_cast<float>(p00[c]) * (1.0f - tx) + static_cast<float>(p10[c]) * tx;
                    const float bot = static_cast<float>(p01[c]) * (1.0f - tx) + static_cast<float>(p11[c]) * tx;
                    out[c] = static_cast<uint8_t>(std::clamp(top * (1.0f - ty) + bot * ty, 0.0f, 255.0f));
                }
            }
            else // Point (nearest)
            {
                const uint8_t* p = texelAt(static_cast<int>(std::floor(u * static_cast<float>(srcW))),
                                           static_cast<int>(std::floor(v * static_cast<float>(srcH))));
                out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
            }
        }

        // Composites a textured, tinted quad (corners[0..3]/uvs[0..3], same winding order: TL, TR,
        // BR, BL) as two triangles (0,1,2) and (2,3,0) into `dstSurface`, sampling `srcSurface` per
        // `filter`/`addressU`/`addressV` (DX3-45/46) and blending per `blendMode`'s real, distinct
        // Opaque/AlphaBlend/NonPremultiplied/Additive formula (DX3-40..44) -- the exact factors
        // BlendState.cpp's own presets specify, not a single baseline approximation.
        void CompositeQuad(LPDIRECTDRAWSURFACE dstSurface, int dstW, int dstH,
                           LPDIRECTDRAWSURFACE srcSurface, int srcW, int srcH,
                           const Vector2 corners[4], const Vector2 uvs[4],
                           float tintR, float tintG, float tintB, float tintA,
                           Dx3BlendMode blendMode, int filter, int addressU, int addressV)
        {
            DDSURFACEDESC dstDesc{};
            dstDesc.dwSize = sizeof(DDSURFACEDESC);
            HRESULT hr = dstSurface->Lock(nullptr, &dstDesc, 0, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(compositor dst)", hr);

            DDSURFACEDESC srcDesc{};
            srcDesc.dwSize = sizeof(DDSURFACEDESC);
            hr = srcSurface->Lock(nullptr, &srcDesc, 0, nullptr);
            if (FAILED(hr))
            {
                dstSurface->Unlock(dstDesc.lpSurface);
                ThrowHr("IDirectDrawSurface::Lock(compositor src)", hr);
            }

            auto* dstBase = static_cast<uint8_t*>(dstDesc.lpSurface);
            const auto* srcBase = static_cast<const uint8_t*>(srcDesc.lpSurface);

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
                        SampleTexel(srcBase, srcDesc.lPitch, srcW, srcH, u, v, filter, addressU, addressV, sp);
                        const float srcR = static_cast<float>(sp[0]) / 255.0f * tintR;
                        const float srcG = static_cast<float>(sp[1]) / 255.0f * tintG;
                        const float srcB = static_cast<float>(sp[2]) / 255.0f * tintB;
                        const float srcA = static_cast<float>(sp[3]) / 255.0f * tintA;

                        uint8_t* dp = dstBase + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstDesc.lPitch) +
                                     static_cast<std::size_t>(x) * 4u;

                        switch (blendMode)
                        {
                        case Dx3BlendMode::Opaque:
                            // Direct overwrite -- source alpha is not part of the blend equation
                            // at all (ColorSrcBlend=One, ColorDstBlend=Zero).
                            dp[0] = static_cast<uint8_t>(std::clamp(srcR * 255.0f, 0.0f, 255.0f));
                            dp[1] = static_cast<uint8_t>(std::clamp(srcG * 255.0f, 0.0f, 255.0f));
                            dp[2] = static_cast<uint8_t>(std::clamp(srcB * 255.0f, 0.0f, 255.0f));
                            dp[3] = static_cast<uint8_t>(std::clamp(srcA * 255.0f, 0.0f, 255.0f));
                            break;
                        case Dx3BlendMode::AlphaBlend:
                        {
                            // Premultiplied convention (ColorSrcBlend=One, ColorDstBlend=
                            // InverseSourceAlpha): the source color is used as-is, NOT multiplied
                            // by srcAlpha again -- SpriteBatch's default preset assumes
                            // already-premultiplied source pixels.
                            const float invA = 1.0f - srcA;
                            dp[0] = static_cast<uint8_t>(std::clamp(srcR * 255.0f + static_cast<float>(dp[0]) * invA, 0.0f, 255.0f));
                            dp[1] = static_cast<uint8_t>(std::clamp(srcG * 255.0f + static_cast<float>(dp[1]) * invA, 0.0f, 255.0f));
                            dp[2] = static_cast<uint8_t>(std::clamp(srcB * 255.0f + static_cast<float>(dp[2]) * invA, 0.0f, 255.0f));
                            dp[3] = static_cast<uint8_t>(std::clamp(srcA * 255.0f + static_cast<float>(dp[3]) * invA, 0.0f, 255.0f));
                            break;
                        }
                        case Dx3BlendMode::NonPremultiplied:
                        {
                            // Straight alpha (ColorSrcBlend=SourceAlpha, ColorDstBlend=
                            // InverseSourceAlpha): out = src*srcAlpha + dst*(1-srcAlpha).
                            const float invA = 1.0f - srcA;
                            dp[0] = static_cast<uint8_t>(std::clamp(srcR * 255.0f * srcA + static_cast<float>(dp[0]) * invA, 0.0f, 255.0f));
                            dp[1] = static_cast<uint8_t>(std::clamp(srcG * 255.0f * srcA + static_cast<float>(dp[1]) * invA, 0.0f, 255.0f));
                            dp[2] = static_cast<uint8_t>(std::clamp(srcB * 255.0f * srcA + static_cast<float>(dp[2]) * invA, 0.0f, 255.0f));
                            dp[3] = static_cast<uint8_t>(std::clamp(srcA * 255.0f * srcA + static_cast<float>(dp[3]) * invA, 0.0f, 255.0f));
                            break;
                        }
                        case Dx3BlendMode::Additive:
                            // ColorSrcBlend=SourceAlpha, ColorDstBlend=One: saturating add, no
                            // destination attenuation at all.
                            dp[0] = static_cast<uint8_t>(std::clamp(srcR * 255.0f * srcA + static_cast<float>(dp[0]), 0.0f, 255.0f));
                            dp[1] = static_cast<uint8_t>(std::clamp(srcG * 255.0f * srcA + static_cast<float>(dp[1]), 0.0f, 255.0f));
                            dp[2] = static_cast<uint8_t>(std::clamp(srcB * 255.0f * srcA + static_cast<float>(dp[2]), 0.0f, 255.0f));
                            dp[3] = static_cast<uint8_t>(std::clamp(srcA * 255.0f * srcA + static_cast<float>(dp[3]), 0.0f, 255.0f));
                            break;
                        }
                        break;
                    }
                }
            }

            srcSurface->Unlock(srcDesc.lpSurface);
            dstSurface->Unlock(dstDesc.lpSurface);
        }
    }

    // Common accessor so Dx3SpriteBatchBackend can reach the underlying IDirectDrawSurface* of
    // either concrete backend it might be asked to sample from (a plain texture, or a former
    // render target now being sampled as one) without needing to know which. Never named outside
    // this .cpp, same reasoning as Dx3TextureBackend/Dx3RenderTargetBackend themselves.
    class Dx3SurfaceOwner
    {
    public:
        virtual ~Dx3SurfaceOwner() = default;
        [[nodiscard]] virtual LPDIRECTDRAWSURFACE Surface() const = 0;
    };

    struct Dx3GraphicsBackend::Impl
    {
        // NOTE: SDL_Window is NOT owned by the backend -- same convention as every other
        // window-based CNA backend (GraphicsDevice/platform layer owns it).
        SDL_Window* window = nullptr;

        LPDIRECTDRAW dd = nullptr;
        // The real DirectDraw primary surface. Never Lock()'d directly for pixel access --
        // free-direct's own Lock() never exposes a writable pointer for a primary surface (see
        // Dx3GraphicsBackend.hpp's class comment). Written to only via the single identity Blt()
        // in Present().
        LPDIRECTDRAWSURFACE primary = nullptr;
        // DX3-owned "shadow backbuffer": a Lockable offscreen surface, sized to the logical/virtual
        // resolution, that Clear() and (from Phase X4 on) SpriteBatch draws always composite into.
        LPDIRECTDRAWSURFACE backBuffer = nullptr;

        // Phase X3: the offscreen surface owned by the currently-bound Dx3RenderTargetBackend, or
        // nullptr when no custom render target is bound (i.e. the shadow backbuffer is active).
        // Set directly by Dx3RenderTargetBackend::BindAsRenderTarget/UnbindAsRenderTarget via a
        // pointer to this field (passed at construction) -- kept as a plain LPDIRECTDRAWSURFACE
        // rather than a Dx3RenderTargetBackend* so Dx3RenderTargetBackend never needs to name this
        // private Impl type (it is defined later in this file, after Impl).
        LPDIRECTDRAWSURFACE currentTargetSurface = nullptr;
        // Width/height of currentTargetSurface, kept alongside it (Phase X4: the SpriteBatch
        // compositor needs the active destination's own bounds, which can differ from
        // logicalWidth/logicalHeight when a custom-sized render target is bound).
        int currentTargetWidth = 0;
        int currentTargetHeight = 0;

        int logicalWidth = 0;
        int logicalHeight = 0;
        CnaPresentationMode presentationMode = CnaPresentationMode::Overscan;

        // Phase X5 (design decision 6): the real, distinct blend mode detected from
        // ApplyBlendState's raw factors (DetectBlendMode) -- gates the SpriteBatch identity fast
        // path (design decision 5, Opaque only) and selects CompositeQuad's per-formula math.
        // Default AlphaBlend matches SpriteBatch::Begin()'s own default blend state.
        Dx3BlendMode currentBlendMode = Dx3BlendMode::AlphaBlend;

        // Resolves to whichever surface Clear()/ReadBackbuffer() should currently target: the
        // bound render target's surface if one is bound, else the shadow backbuffer. Present()
        // deliberately does NOT go through this -- it always Blt()s from the real shadow
        // backbuffer, matching FNA's own backbuffer-vs-render-target separation (a game must
        // SetRenderTarget(null) before presenting, same as every other CNA backend).
        [[nodiscard]] LPDIRECTDRAWSURFACE ActiveSurface() const
        {
            return currentTargetSurface ? currentTargetSurface : backBuffer;
        }

        // Phase X4: the active destination's own bounds -- the bound render target's size if one
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

        // Releases the current primary/shadow-backbuffer surfaces (if any) and recreates both at
        // (width, height): SetDisplayMode(width, height, 32) (design decision 4: 32bpp only) ->
        // primary CreateSurface (DDSCAPS_PRIMARYSURFACE) -> shadow CreateSurface
        // (DDSCAPS_OFFSCREENPLAIN, sized width x height).
        void CreateSurfaces(int width, int height)
        {
            if (backBuffer) { backBuffer->Release(); backBuffer = nullptr; }
            if (primary) { primary->Release(); primary = nullptr; }

            HRESULT hr = dd->SetDisplayMode(static_cast<DWORD>(width), static_cast<DWORD>(height), 32);
            if (FAILED(hr)) ThrowHr("IDirectDraw::SetDisplayMode", hr);

            DDSURFACEDESC primaryDesc{};
            primaryDesc.dwSize = sizeof(DDSURFACEDESC);
            primaryDesc.dwFlags = DDSD_CAPS;
            primaryDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
            hr = dd->CreateSurface(&primaryDesc, &primary, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw::CreateSurface(primary)", hr);

            DDSURFACEDESC backDesc{};
            backDesc.dwSize = sizeof(DDSURFACEDESC);
            backDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            backDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
            backDesc.dwWidth = static_cast<DWORD>(width);
            backDesc.dwHeight = static_cast<DWORD>(height);
            hr = dd->CreateSurface(&backDesc, &backBuffer, nullptr);
            if (FAILED(hr))
            {
                primary->Release();
                primary = nullptr;
                ThrowHr("IDirectDraw::CreateSurface(shadow backbuffer)", hr);
            }

            logicalWidth = width;
            logicalHeight = height;
        }
    };

    Dx3GraphicsBackend::Dx3GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : impl_(std::make_unique<Impl>())
    {
        if (!args.window) throw std::runtime_error("Dx3GraphicsBackend initialized with null window.");
        impl_->window = args.window;
        impl_->presentationMode = args.presentationMode;

        HRESULT hr = DirectDrawCreate(nullptr, &impl_->dd, nullptr);
        if (FAILED(hr)) ThrowHr("DirectDrawCreate", hr);

        // Design decision 2: free-direct's SetCooperativeLevel does
        // sdlWindow_ = reinterpret_cast<SDL_Window*>(hwnd) internally -- HWND is CNA's own already-
        // existing SDL_Window* in disguise, never a real Win32 handle and never a second window.
        hr = impl_->dd->SetCooperativeLevel(reinterpret_cast<HWND>(impl_->window), DDSCL_NORMAL);
        if (FAILED(hr)) ThrowHr("IDirectDraw::SetCooperativeLevel", hr);

        const int width = args.virtualWidth > 0 ? args.virtualWidth : 640;
        const int height = args.virtualHeight > 0 ? args.virtualHeight : 480;
        impl_->CreateSurfaces(width, height);
    }

    Dx3GraphicsBackend::~Dx3GraphicsBackend() = default;

    void Dx3GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        // Not DDBLT_COLORFILL: free-direct's own FillColor() hardcodes the written alpha byte to
        // 255 regardless of dwFillColor's contents, so a ColorFill-based Clear() could never
        // honor a requested alpha other than fully opaque (a real bug, found in review and fixed
        // here). FillSurfaceColor writes all 4 channels directly via Lock()/Unlock() instead.
        int width = 0, height = 0;
        impl_->ActiveSurfaceSize(width, height);
        FillSurfaceColor(impl_->ActiveSurface(), width, height,
                         static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f)),
                         static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f)));
    }

    void Dx3GraphicsBackend::Present()
    {
        // A single identity Blt() (null dest/src rects = full surface, unscaled 1:1 copy) from the
        // shadow backbuffer onto the real primary. This mirrors the exact "once-per-frame back-
        // buffer-to-primary Blt" call shape both of free-direct's real target games already use as
        // their hottest path (docs/directdraw-limitations.md), and relies on free-direct's own
        // auto-present-on-dirty-Blt behavior against the primary surface. Flip() is deliberately
        // never called: it would only set free-direct's internal usesFlip_ flag and permanently
        // *disable* that auto-present path for no benefit -- Flip() itself does not copy pixels
        // from anywhere, it just re-presents the primary's own current buffer.
        const HRESULT hr = impl_->primary->Blt(nullptr, impl_->backBuffer, nullptr, DDBLT_WAIT, nullptr);
        if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Blt(present)", hr);
    }

    void Dx3GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        // Unlike SDL_Renderer-based backends, DX3 has no independent "physical output size" to
        // fall back to -- the primary surface's own size (set via SetDisplayMode) IS the logical
        // size; free-direct's internal SDL_Renderer handles physical window scaling invisibly.
        width = impl_->logicalWidth;
        height = impl_->logicalHeight;
    }

    void Dx3GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // DX3-26: reads from whichever surface is currently active -- the bound render target's
        // surface if one is bound (SetRenderTarget2D), else the shadow backbuffer. Never through
        // free-direct's physical presentation path, so this is exact-pixel regardless of window
        // size/letterboxing (there is no scaling between the shadow buffer and the values read
        // here; scaling only happens later, once, when Present() Blt()s onto the primary).
        ReadSurfacePixels(impl_->ActiveSurface(), x, y, w, h, pixels);
    }

    void Dx3GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        if (width == impl_->logicalWidth && height == impl_->logicalHeight) return;
        // Known limitation (free-direct's own PresentPrimary sets its physical letterbox rect via
        // a one-time logicalPresentationSet_ flag, never re-applied on a later primary resize) --
        // a resolution change after the first Present() will keep presenting at the *old* physical
        // scale until free-direct itself is extended to support that (out of scope here, design
        // decision 8: cannot silently extend free-direct's own surface beyond what it implements).
        impl_->CreateSurfaces(width, height);
    }

    void Dx3GraphicsBackend::SetPresentationMode(int mode)
    {
        impl_->presentationMode = static_cast<CnaPresentationMode>(mode);
        // free-direct's own PresentPrimary hardcodes SDL_LOGICAL_PRESENTATION_LETTERBOX (see
        // src/directdraw/DirectDraw.cpp) -- DX3 cannot honor Stretch/Overscan/NativeBackBuffer's
        // real physical-scaling behavior without modifying free-direct itself (out of scope, design
        // decision 8). The mode is still stored so GetViewportSize()/logical-resolution bookkeeping
        // stays consistent with what the game requested.
    }

    SDL_Window* Dx3GraphicsBackend::GetWindowInternal() const
    {
        return impl_->window;
    }

    // DX3-68: real letterbox scale+offset transform between physical window pixels and logical
    // (virtual) game pixels. free-direct's own PresentPrimary hardcodes
    // SDL_LOGICAL_PRESENTATION_LETTERBOX against its internal SDL_Renderer (never exposed to CNA,
    // GetRendererInternal() always returns nullptr) -- this independently recomputes the exact
    // same letterbox math (uniform scale to fit, centered, black bars on the non-fitting axis)
    // from the real physical SDL_Window size queried directly, so it stays correct without ever
    // needing access to free-direct's own internal renderer state.
    namespace
    {
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
    }

    bool Dx3GraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                       float& logX, float& logY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
            return false;
        logX = (windowX - offsetX) / scale;
        logY = (windowY - offsetY) / scale;
        return true;
    }

    bool Dx3GraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                       float& windowX, float& windowY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->logicalWidth, impl_->logicalHeight, scale, offsetX, offsetY))
            return false;
        windowX = logX * scale + offsetX;
        windowY = logY * scale + offsetY;
        return true;
    }

    // ---- Phase X3: textures and render targets ----
    // Both classes below are never named outside this .cpp (only returned polymorphically), so
    // <ddraw.h> stays fully contained here -- see this file's own Dx3TextureBackend/
    // Dx3RenderTargetBackend definitions for the shared surface-creation helpers they use.

    class Dx3TextureBackend : public ITextureBackend, public Dx3SurfaceOwner
    {
    public:
        Dx3TextureBackend(LPDIRECTDRAW dd, int width, int height)
            : width_(width), height_(height), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        Dx3TextureBackend(LPDIRECTDRAW dd, const ImageData& data)
            : Dx3TextureBackend(dd, data.width, data.height)
        {
            WriteSurfacePixels(surface_, width_, height_, data.pixels.data(), width_ * 4);
        }

        ~Dx3TextureBackend() override { if (surface_) surface_->Release(); }

        Dx3TextureBackend(const Dx3TextureBackend&) = delete;
        Dx3TextureBackend& operator=(const Dx3TextureBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            WriteSurfacePixels(surface_, width_, height_, rgba, stride);
        }

        // DX3-22: no native mip chain on IDirectDrawSurface -- level 0 is unaffected (always
        // routed through UpdatePixels by Texture2D::SetData), level>0 throws honestly rather than
        // silently discarding the upload, matching SDL_RENDERER's Task 681 precedent.
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

    class Dx3RenderTargetBackend final : public IRenderTargetBackend, public Dx3SurfaceOwner
    {
    public:
        Dx3RenderTargetBackend(LPDIRECTDRAWSURFACE* currentTargetSlot, int* currentTargetWidthSlot,
                               int* currentTargetHeightSlot, LPDIRECTDRAW dd,
                               int width, int height, int multiSampleCount)
            : currentTargetSlot_(currentTargetSlot), currentTargetWidthSlot_(currentTargetWidthSlot),
              currentTargetHeightSlot_(currentTargetHeightSlot), width_(width), height_(height),
              multiSampleCount_(multiSampleCount), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        ~Dx3RenderTargetBackend() override
        {
            UnbindAsRenderTarget();
            if (surface_) surface_->Release();
        }

        Dx3RenderTargetBackend(const Dx3RenderTargetBackend&) = delete;
        Dx3RenderTargetBackend& operator=(const Dx3RenderTargetBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

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

        // DX3-24: IDirectDrawSurface has no depth-buffer concept at all, regardless of what
        // DepthFormat was requested -- always false, same reasoning SDL_RENDERER's Task 708 used.
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

    std::unique_ptr<ITextureBackend> Dx3GraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<Dx3TextureBackend>(impl_->dd, data);
    }

    std::unique_ptr<IRenderTargetBackend> Dx3GraphicsBackend::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int multiSampleCount)
    {
        return std::make_unique<Dx3RenderTargetBackend>(&impl_->currentTargetSurface,
                                                         &impl_->currentTargetWidth, &impl_->currentTargetHeight,
                                                         impl_->dd, w, h, multiSampleCount);
    }

    void Dx3GraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
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

    void Dx3GraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        // DX3-27: DirectDraw has no multi-render-target concept -- single active surface only.
        if (count > 1)
            throw std::runtime_error(
                "DX3 (DirectDraw) does not support multiple simultaneous render targets (MRT): "
                "requested " + std::to_string(count) + ", but IDirectDrawSurface supports exactly "
                "one active render target at a time.");
        SetRenderTarget2D(count > 0 ? rts[0] : nullptr);
    }

    // ---- Phase X4: the CPU compositor / SpriteBatch draw path (design decision 5) ----
    // Never named outside this .cpp, same reasoning as Dx3TextureBackend/Dx3RenderTargetBackend.

    class Dx3SpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        Dx3SpriteBatchBackend(std::function<LPDIRECTDRAWSURFACE()> getActiveSurface,
                              std::function<void(int&, int&)> getActiveSurfaceSize,
                              std::function<Dx3BlendMode()> getBlendMode)
            : getActiveSurface_(std::move(getActiveSurface)),
              getActiveSurfaceSize_(std::move(getActiveSurfaceSize)),
              getBlendMode_(std::move(getBlendMode))
        {
        }

        void Begin() override
        {
            if (begun_)
                throw std::runtime_error("Dx3SpriteBatchBackend::Begin: Begin() called without a matching End()");
            begun_ = true;
        }

        void End() override
        {
            if (!begun_)
                throw std::runtime_error("Dx3SpriteBatchBackend::End: End() called without a matching Begin()");
            begun_ = false;
        }

        // DX3-36: applied as a point transform (z=0) directly on the already-screen-space quad
        // corners, same convention SOFTWARE's own SetTransformMatrix uses.
        void SetTransformMatrix(const Matrix& m) override { transformMatrix_ = m; }

        // DX3-38: no programmable shader stage exists on this backend.
        void SetCustomEffect(Effect* effect) override
        {
            if (effect != nullptr)
                throw std::runtime_error(
                    "DX3 (DirectDraw) does not support custom SpriteBatch Effects: no programmable "
                    "shader stage exists on this backend.");
        }

        // DX3-45: 0=Linear (bilinear), else Point/nearest -- matches ISpriteBatchBackend's own
        // documented convention.
        void SetSamplerFilter(int textureFilter) override { filter_ = textureFilter; }

        // DX3-46: raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror).
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            addressU_ = addressU;
            addressV_ = addressV;
        }

        void Draw(const ITextureBackend& texture, float x, float y) override
        {
            Draw(texture,
                 Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                 Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
                 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                 const Rectangle& sourceRectangle, const Color& color) override
        {
            Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
                 SpriteEffects::None, 0.0f);
        }

        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                 const Rectangle& sourceRectangle, const Color& color, float rotation,
                 const Vector2& origin, SpriteEffects effects, float /*layerDepth*/) override
        {
            if (!begun_)
                throw std::runtime_error("Dx3SpriteBatchBackend::Draw: Draw() called before Begin()");

            const auto* owner = dynamic_cast<const Dx3SurfaceOwner*>(&texture);
            if (!owner)
                throw std::runtime_error(
                    "Dx3SpriteBatchBackend::Draw: texture backend is not a DX3 surface (created by "
                    "a different graphics backend?)");
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
            // DX3-34.
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

            const float dw = static_cast<float>(destinationRectangle.Width);
            const float dh = static_cast<float>(destinationRectangle.Height);
            const float sw = static_cast<float>(std::max(1, sourceRectangle.Width));
            const float sh = static_cast<float>(std::max(1, sourceRectangle.Height));
            const float scaleX = dw / sw;   // DX3-35.
            const float scaleY = dh / sh;

            // DX3-31: identity fast path -- a real BltFast straight copy, no CPU compositing at
            // all. Only when every parameter would be a visual no-op relative to a plain copy:
            // 1:1 scale, no rotation, no origin offset, no flip, opaque white tint, no custom
            // transform, and the currently-applied blend state really is Opaque (design decision 5).
            const bool isIdentityGeometry =
                rotation == 0.0f && scaleX == 1.0f && scaleY == 1.0f &&
                origin.X == 0.0f && origin.Y == 0.0f && effects == SpriteEffects::None &&
                transformMatrix_ == Matrix::getIdentityProperty();
            const bool isWhiteTint =
                color.getRProperty() == 255 && color.getGProperty() == 255 &&
                color.getBProperty() == 255 && color.getAProperty() == 255;
            if (isIdentityGeometry && isWhiteTint && getBlendMode_() == Dx3BlendMode::Opaque)
            {
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

            // DX3-32/33/39: general path -- per-pixel CPU compositing. Quad-corner placement
            // (rotation about origin, then scale) reuses the exact formula
            // SoftwareSpriteBatchBackend::Draw() already uses (design decision 5: consume
            // SpriteBatch's already-computed position/rotation/flip, don't re-derive the pivot math).
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

            CompositeQuad(dstSurface, dstW, dstH, srcSurface, texW, texH, corners, uvs,
                         static_cast<float>(color.getRProperty()) / 255.0f,
                         static_cast<float>(color.getGProperty()) / 255.0f,
                         static_cast<float>(color.getBProperty()) / 255.0f,
                         static_cast<float>(color.getAProperty()) / 255.0f,
                         getBlendMode_(), filter_, addressU_, addressV_);
        }

    private:
        std::function<LPDIRECTDRAWSURFACE()> getActiveSurface_;
        std::function<void(int&, int&)> getActiveSurfaceSize_;
        std::function<Dx3BlendMode()> getBlendMode_;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        // Defaults match SpriteBatch::Begin()'s own documented default sampler state
        // ("AlphaBlend, LinearClamp, no depth"): Linear filter (0), Clamp address mode (1) on
        // both axes.
        int filter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
    };

    std::unique_ptr<ISpriteBatchBackend> Dx3GraphicsBackend::CreateSpriteBatch()
    {
        Impl* impl = impl_.get();
        return std::make_unique<Dx3SpriteBatchBackend>(
            [impl]() { return impl->ActiveSurface(); },
            [impl](int& w, int& h) { impl->ActiveSurfaceSize(w, h); },
            [impl]() { return impl->currentBlendMode; });
    }

    void Dx3GraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                             int colorDstBlend, int alphaDstBlend,
                                             int colorBlendFunc, int alphaBlendFunc)
    {
        impl_->currentBlendMode = DetectBlendMode(colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                                                  colorBlendFunc, alphaBlendFunc);
    }

    void Dx3GraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void Dx3GraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void Dx3GraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void Dx3GraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void Dx3GraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil"); }
    void Dx3GraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil"); }
    void Dx3GraphicsBackend::SetDepthTestEnabled(bool)  { ThrowNo3D("SetDepthTestEnabled"); }
    void Dx3GraphicsBackend::SetBlendEnabled(bool)      { ThrowNo3D("SetBlendEnabled"); }
    void Dx3GraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> Dx3GraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> Dx3GraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16");
    }

    void Dx3GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                   const Matrix&, const Matrix&, const Matrix&,
                                                   PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }

    void Dx3GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                                          const IIndexBufferBackend&,
                                                          const Matrix&, const Matrix&, const Matrix&,
                                                          PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_DX3
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Dx3::Dx3GraphicsBackend>(args);
    }
#endif
}
