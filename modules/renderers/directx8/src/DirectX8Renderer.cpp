#include "CNA/Internal/Renderers/DirectX8/DirectX8Renderer.hpp"

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

// plan_dx8.md: real DirectX 8 graphics renderer, delivered via DXVK (Direct3DCreate8 resolved from
// DXVK's own d3d8.dll.a -- mingw-w64 ships no real d3d8 import library for x86_64, design decision
// 2). "DirectDraw+Direct3D merged": a single IDirect3D8::CreateDevice call creates both the device
// and its own swap chain -- no separate DirectDraw object, no manual "shadow backbuffer + Blt to
// primary" trick DIRECTX1..DIRECTX7 all needed. Scope decision (made with the project owner before any code
// was written): fixed-function 3D only, matching DIRECTX1..DIRECTX7's own CPU-transform-and-submit shape --
// no Shader Model 1.x. The 2D SpriteBatch layer is a real GPU-rendered-textured-quad compositor
// (design decision 11), not a CPU Blt-based one -- DirectDraw does not exist at this DirectX era at
// all. <d3d8.h> (and the real <windows.h> it pulls in) is contained to this .cpp only -- see
// DirectX8Renderer.hpp's own comment.
#include <d3d8.h>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
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
#include <vector>

namespace CNA::Internal::Renderers::DirectX8
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

        // No native mip chain on IDirect3DTexture8 as created by this renderer (levels=1, design
        // decision 7) -- level 0 is unaffected (always routed through UpdatePixels by
        // Texture2D::SetData), level>0 throws honestly rather than silently discarding the
        // upload, matching DX2-DIRECTX7's own identical precedent.
        [[noreturn]] void ThrowMipLevelUnsupported(int level)
        {
            throw std::runtime_error(
                "DIRECTX8 does not support mip-level texture uploads (level " + std::to_string(level) +
                "): this renderer's IDirect3DTexture8 objects have no native mip chain. Use "
                "Texture2D::SetData(level=0, ...) only.");
        }

        // ---- design decision 9/7/6: raw XNA int -> real D3D8 render-state/stage-state value
        // mapping. D3D8 renamed D3DRENDERSTATE_* to D3DRS_* (identical underlying
        // D3DRENDERSTATETYPE enum values) and moved texture filter/address/anisotropy from
        // per-device render states to per-stage D3DTSS_* texture-stage states -- both confirmed by
        // inspection, not assumed.
        using Microsoft::Xna::Framework::Graphics::Blend;
        using Microsoft::Xna::Framework::Graphics::CompareFunction;
        using Microsoft::Xna::Framework::Graphics::CullMode;
        using Microsoft::Xna::Framework::Graphics::FillMode;
        using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
        using Microsoft::Xna::Framework::Graphics::TextureFilter;

        DWORD DirectX8BlendToD3D(int blend)
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
                default:                               return D3DBLEND_ONE;
            }
        }

        DWORD DirectX8CompareFunctionToD3D(int compareFunction)
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

        DWORD DirectX8StencilOperationToD3D(int stencilOperation)
        {
            switch (static_cast<StencilOperation>(stencilOperation))
            {
                case StencilOperation::Keep:                return D3DSTENCILOP_KEEP;
                case StencilOperation::Zero:                 return D3DSTENCILOP_ZERO;
                case StencilOperation::Replace:              return D3DSTENCILOP_REPLACE;
                case StencilOperation::Increment:            return D3DSTENCILOP_INCR;
                case StencilOperation::Decrement:             return D3DSTENCILOP_DECR;
                case StencilOperation::IncrementSaturation:  return D3DSTENCILOP_INCRSAT;
                case StencilOperation::DecrementSaturation:  return D3DSTENCILOP_DECRSAT;
                case StencilOperation::Invert:               return D3DSTENCILOP_INVERT;
                default:                                        return D3DSTENCILOP_KEEP;
            }
        }

        DWORD DirectX8CullModeToD3D(int cullMode)
        {
            switch (static_cast<CullMode>(cullMode))
            {
                case CullMode::None:                     return D3DCULL_NONE;
                case CullMode::CullClockwiseFace:        return D3DCULL_CW;
                case CullMode::CullCounterClockwiseFace: return D3DCULL_CCW;
                default:                                   return D3DCULL_NONE;
            }
        }

        DWORD DirectX8FillModeToD3D(int fillMode)
        {
            switch (static_cast<FillMode>(fillMode))
            {
                case FillMode::Solid:      return D3DFILL_SOLID;
                case FillMode::WireFrame:  return D3DFILL_WIREFRAME;
                default:                     return D3DFILL_SOLID;
            }
        }

        DWORD DirectX8TextureAddressModeToD3D(int addressMode)
        {
            switch (static_cast<TextureAddressMode>(addressMode))
            {
                case TextureAddressMode::Wrap:   return D3DTADDRESS_WRAP;
                case TextureAddressMode::Clamp:  return D3DTADDRESS_CLAMP;
                case TextureAddressMode::Mirror: return D3DTADDRESS_MIRROR;
                default:                           return D3DTADDRESS_WRAP;
            }
        }

        struct DirectX8FilterPair { DWORD mag; DWORD min; };

        // No mip-filter concept is mapped -- every texture this renderer creates has exactly one
        // mip level (design decision, matching this family's own established "no native mip
        // chain" scope), so every "MipPoint"/"MipLinear" variant collapses to its own mag/min pair.
        DirectX8FilterPair DirectX8TextureFilterToD3D(int textureFilter)
        {
            switch (static_cast<TextureFilter>(textureFilter))
            {
                case TextureFilter::Linear:                        return {D3DTEXF_LINEAR, D3DTEXF_LINEAR};
                case TextureFilter::Point:                          return {D3DTEXF_POINT, D3DTEXF_POINT};
                case TextureFilter::Anisotropic:                    return {D3DTEXF_ANISOTROPIC, D3DTEXF_ANISOTROPIC};
                case TextureFilter::LinearMipPoint:                 return {D3DTEXF_LINEAR, D3DTEXF_LINEAR};
                case TextureFilter::PointMipLinear:                 return {D3DTEXF_POINT, D3DTEXF_POINT};
                case TextureFilter::MinLinearMagPointMipLinear:     return {D3DTEXF_POINT, D3DTEXF_LINEAR};
                case TextureFilter::MinLinearMagPointMipPoint:      return {D3DTEXF_POINT, D3DTEXF_LINEAR};
                case TextureFilter::MinPointMagLinearMipLinear:     return {D3DTEXF_LINEAR, D3DTEXF_POINT};
                case TextureFilter::MinPointMagLinearMipPoint:      return {D3DTEXF_LINEAR, D3DTEXF_POINT};
                default:                                              return {D3DTEXF_LINEAR, D3DTEXF_LINEAR};
            }
        }

        // Every surface this renderer creates uses an explicit D3DFMT_A8R8G8B8 format -- unlike
        // DirectDraw (which picks whatever the display negotiates), D3D8 lets this renderer request
        // its own known format directly, so the in-memory byte order is fixed and known up front
        // (B,G,R,A for a little-endian D3DCOLOR_ARGB-packed DWORD) -- no per-surface channel-layout
        // detection dance is needed at all, unlike DIRECTX2..DIRECTX7's own DetectChannelLayout.
        void RgbaToNativeRow(const uint8_t* rgba, uint8_t* native, int pixelCount)
        {
            for (int i = 0; i < pixelCount; ++i)
            {
                native[i * 4 + 0] = rgba[i * 4 + 2]; // B
                native[i * 4 + 1] = rgba[i * 4 + 1]; // G
                native[i * 4 + 2] = rgba[i * 4 + 0]; // R
                native[i * 4 + 3] = rgba[i * 4 + 3]; // A
            }
        }
        void NativeToRgbaRow(const uint8_t* native, uint8_t* rgba, int pixelCount)
        {
            for (int i = 0; i < pixelCount; ++i)
            {
                rgba[i * 4 + 0] = native[i * 4 + 2]; // R
                rgba[i * 4 + 1] = native[i * 4 + 1]; // G
                rgba[i * 4 + 2] = native[i * 4 + 0]; // B
                rgba[i * 4 + 3] = native[i * 4 + 3]; // A
            }
        }
    }

    // Common accessor so DirectX8SpriteBatchRenderer/the 3D draw path can reach the underlying
    // IDirect3DTexture8* of either concrete renderer it might be asked to sample from (a plain
    // texture, or a former render target now being sampled as one) without needing to know which.
    // Never named outside this .cpp.
    class DirectX8TextureOwner
    {
    public:
        virtual ~DirectX8TextureOwner() = default;
        [[nodiscard]] virtual IDirect3DTexture8* Texture() const = 0;
    };

    struct DirectX8Renderer::Impl
    {
        SDL_Window* window = nullptr;
        HWND hwnd = nullptr;

        IDirect3D8* d3d8 = nullptr;
        IDirect3DDevice8* device8 = nullptr;
        IDirect3DSurface8* defaultBackBuffer = nullptr;
        IDirect3DSurface8* defaultDepthStencil = nullptr;

        // Design decision: the swap chain is created ONCE, matching the real window's pixel size
        // at construction, and never resized -- CTests always use a fixed-size window, and
        // avoiding Reset() sidesteps D3DPOOL_DEFAULT resource invalidation entirely for this v1
        // scope (a real, deliberate boundary, not an oversight -- see plan_dx8.md's own Boundaries
        // section).
        int width = 0;
        int height = 0;
        int virtualWidth = 0;
        int virtualHeight = 0;
        CnaPresentationMode presentationMode = CnaPresentationMode::Overscan;

        // plan_dx8.md design: D3D8 has no scaled-blit primitive at all (StretchRect is a D3D9-only
        // addition, and CopyRects is a same-size copy only) -- so, unlike D3D9's own simpler
        // "swap chain always matches the real window, no letterboxing at this layer" choice, this
        // renderer renders everything into a logical-resolution offscreen render target (real
        // Direct3D8 texture+surface, same render-target-as-texture pattern as
        // DirectX8RenderTargetRenderer) and Present() draws THAT texture as a single full-screen quad
        // through the same fixed-function pipeline onto the real swap chain, letterbox-scaled --
        // matching DIRECTX1..DIRECTX7's own logical/virtual resolution + letterbox guarantee exactly, just
        // via a GPU quad instead of a DirectDraw Blt.
        IDirect3DTexture8* logicalTexture = nullptr;
        IDirect3DSurface8* logicalSurface = nullptr;
        IDirect3DSurface8* logicalDepthStencil = nullptr;

        // The offscreen surface owned by the currently-bound DirectX8RenderTargetRenderer, or nullptr
        // when no custom render target is bound (the logical render target is active). Set
        // directly by DirectX8RenderTargetRenderer::BindAsRenderTarget/UnbindAsRenderTarget via a
        // pointer to this field.
        IDirect3DSurface8* currentTargetSurface = nullptr;
        int currentTargetWidth = 0;
        int currentTargetHeight = 0;

        [[nodiscard]] IDirect3DSurface8* ActiveSurface() const
        {
            return currentTargetSurface ? currentTargetSurface : logicalSurface;
        }
        void ActiveSurfaceSize(int& w, int& h) const
        {
            if (currentTargetSurface) { w = currentTargetWidth; h = currentTargetHeight; }
            else { w = virtualWidth; h = virtualHeight; }
        }

        ~Impl()
        {
            if (logicalDepthStencil) logicalDepthStencil->Release();
            if (logicalSurface) logicalSurface->Release();
            if (logicalTexture) logicalTexture->Release();
            if (defaultDepthStencil) defaultDepthStencil->Release();
            if (defaultBackBuffer) defaultBackBuffer->Release();
            if (device8) device8->Release();
            if (d3d8) d3d8->Release();
        }
    };

    DirectX8Renderer::DirectX8Renderer(const GraphicsRendererCreateArgs& args)
        : impl_(std::make_unique<Impl>())
    {
        if (!args.window) throw std::runtime_error("DirectX8Renderer initialized with null window.");
        impl_->window = args.window;
        impl_->presentationMode = args.presentationMode;

        impl_->hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(impl_->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!impl_->hwnd)
            throw std::runtime_error("DirectX8Renderer: could not obtain a real HWND from the SDL window.");

        SDL_GetWindowSizeInPixels(impl_->window, &impl_->width, &impl_->height);
        if (impl_->width <= 0) impl_->width = args.virtualWidth > 0 ? args.virtualWidth : 640;
        if (impl_->height <= 0) impl_->height = args.virtualHeight > 0 ? args.virtualHeight : 480;
        impl_->virtualWidth = args.virtualWidth > 0 ? args.virtualWidth : impl_->width;
        impl_->virtualHeight = args.virtualHeight > 0 ? args.virtualHeight : impl_->height;

        // plan_dx8.md design decision 2, spike-confirmed (DX8-0a): Direct3DCreate8 resolved from
        // DXVK's own d3d8.dll.a (mingw-w64 ships no real d3d8 import library for x86_64).
        impl_->d3d8 = Direct3DCreate8(D3D_SDK_VERSION);
        if (!impl_->d3d8) throw std::runtime_error("Direct3DCreate8 failed");

        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = static_cast<UINT>(impl_->width);
        pp.BackBufferHeight = static_cast<UINT>(impl_->height);
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = impl_->hwnd;
        pp.Windowed = TRUE;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = D3DFMT_D24S8;
        // plan_dx8.md design decision 4, the one real bug the DX8-0 spike found and fixed:
        // FullScreen_PresentationInterval is documented as meaningful only when Windowed=FALSE --
        // D3DPRESENT_INTERVAL_IMMEDIATE there caused CreateDevice to fail with D3DERR_INVALIDCALL
        // (real DXVK/D3D8-compat validation, not a Wine bug). D3DPRESENT_INTERVAL_DEFAULT (0) is
        // the correct value for windowed mode.
        pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

        // Design decision 3: D3DCREATE_SOFTWARE_VERTEXPROCESSING, not hardware -- this renderer does
        // no GPU T&L at all (CPU pre-transform, decision 8), so requesting hardware vertex
        // processing would buy nothing and risks failing on a device that only advertises software
        // vertex processing.
        HRESULT hr = impl_->d3d8->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, impl_->hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &impl_->device8);
        if (FAILED(hr)) ThrowHr("IDirect3D8::CreateDevice", hr);

        hr = impl_->device8->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &impl_->defaultBackBuffer);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::GetBackBuffer", hr);
        hr = impl_->device8->GetDepthStencilSurface(&impl_->defaultDepthStencil);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::GetDepthStencilSurface", hr);

        // Logical-resolution render target (see the Impl comment above for why) -- created once,
        // sized to the requested virtual resolution, and made the active render target
        // immediately so every subsequent Clear()/3D draw/SpriteBatch call naturally targets it.
        hr = impl_->device8->CreateTexture(static_cast<UINT>(impl_->virtualWidth), static_cast<UINT>(impl_->virtualHeight),
            1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &impl_->logicalTexture);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::CreateTexture(logical render target)", hr);
        hr = impl_->logicalTexture->GetSurfaceLevel(0, &impl_->logicalSurface);
        if (FAILED(hr)) ThrowHr("IDirect3DTexture8::GetSurfaceLevel(logical)", hr);
        hr = impl_->device8->CreateDepthStencilSurface(static_cast<UINT>(impl_->virtualWidth),
            static_cast<UINT>(impl_->virtualHeight), D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &impl_->logicalDepthStencil);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::CreateDepthStencilSurface(logical)", hr);
        hr = impl_->device8->SetRenderTarget(impl_->logicalSurface, impl_->logicalDepthStencil);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetRenderTarget(logical)", hr);

        impl_->device8->SetRenderState(D3DRS_LIGHTING, FALSE);
        // Phase O4-equivalent safe default: cull-none never discards a triangle due to winding,
        // matching this family's own established interim default until ApplyRasterizerState is
        // called with the real per-draw cull mode.
        impl_->device8->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        // Matches real XNA's own DepthStencilState.Default (DepthBufferEnable=true,
        // DepthBufferFunction=CompareFunction.LessEqual, DepthBufferWriteEnable=true).
        impl_->device8->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        impl_->device8->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        impl_->device8->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    }

    DirectX8Renderer::~DirectX8Renderer() = default;

    void DirectX8Renderer::Clear(float r, float g, float b, float a)
    {
        HRESULT hr = impl_->device8->Clear(0, nullptr, D3DCLEAR_TARGET,
                                            D3DCOLOR_COLORVALUE(r, g, b, a), 1.0f, 0);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(TARGET)", hr);
    }

    void DirectX8Renderer::GetViewportSize(int& width, int& height)
    {
        width = impl_->virtualWidth;
        height = impl_->virtualHeight;
    }

    void DirectX8Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w <= 0 || h <= 0) return;

        int surfW = 0, surfH = 0;
        impl_->ActiveSurfaceSize(surfW, surfH);

        // plan_dx8.md design decision 12: D3D8 has no GetRenderTargetData (a D3D9-only addition) --
        // readback uses CreateImageSurface (a lockable system-memory surface) + CopyRects from the
        // real active surface instead, spike-confirmed (DX8-0 throughout).
        IDirect3DSurface8* sysmem = nullptr;
        HRESULT hr = impl_->device8->CreateImageSurface(static_cast<UINT>(surfW), static_cast<UINT>(surfH),
                                                         D3DFMT_A8R8G8B8, &sysmem);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::CreateImageSurface", hr);

        hr = impl_->device8->CopyRects(impl_->ActiveSurface(), nullptr, 0, sysmem, nullptr);
        if (FAILED(hr)) { sysmem->Release(); ThrowHr("IDirect3DDevice8::CopyRects", hr); }

        D3DLOCKED_RECT locked{};
        RECT srcRect{static_cast<LONG>(x), static_cast<LONG>(y),
                     static_cast<LONG>(x + w), static_cast<LONG>(y + h)};
        hr = sysmem->LockRect(&locked, &srcRect, D3DLOCK_READONLY);
        if (FAILED(hr)) { sysmem->Release(); ThrowHr("IDirect3DSurface8::LockRect", hr); }

        auto* src = static_cast<const uint8_t*>(locked.pBits);
        for (int row = 0; row < h; ++row)
        {
            NativeToRgbaRow(src + static_cast<std::size_t>(row) * locked.Pitch,
                             pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4, w);
        }
        sysmem->UnlockRect();
        sysmem->Release();
    }

    void DirectX8Renderer::SetVirtualResolution(int width, int height)
    {
        // Unlike DirectX9Renderer's own vestigial handling of this field (its swap chain always
        // matches the real window, so nothing needs resizing), DirectX8Renderer owns a REAL
        // D3D8 resource sized to the logical resolution (the Impl comment above) -- the
        // logicalTexture/logicalSurface/logicalDepthStencil created at construction must be
        // recreated here, or a later ReadBackbuffer/Present() would keep using the stale size
        // (a real bug this fix closes: CopyRects requires matching source/dest rects, so a
        // ReadBackbuffer sized to the new virtualWidth/Height against a surface still sized to
        // the OLD one fails with D3DERR_INVALIDCALL).
        if (width <= 0 || height <= 0) return;
        if (width == impl_->virtualWidth && height == impl_->virtualHeight) return;

        const bool logicalWasActive = (impl_->currentTargetSurface == nullptr);

        impl_->virtualWidth = width;
        impl_->virtualHeight = height;

        IDirect3DTexture8* newTexture = nullptr;
        HRESULT hr = impl_->device8->CreateTexture(static_cast<UINT>(width), static_cast<UINT>(height),
            1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &newTexture);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::CreateTexture(logical render target resize)", hr);
        IDirect3DSurface8* newSurface = nullptr;
        hr = newTexture->GetSurfaceLevel(0, &newSurface);
        if (FAILED(hr)) { newTexture->Release(); ThrowHr("IDirect3DTexture8::GetSurfaceLevel(logical resize)", hr); }
        IDirect3DSurface8* newDepthStencil = nullptr;
        hr = impl_->device8->CreateDepthStencilSurface(static_cast<UINT>(width), static_cast<UINT>(height),
            D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &newDepthStencil);
        if (FAILED(hr)) { newSurface->Release(); newTexture->Release(); ThrowHr("IDirect3DDevice8::CreateDepthStencilSurface(logical resize)", hr); }

        if (logicalWasActive)
        {
            hr = impl_->device8->SetRenderTarget(newSurface, newDepthStencil);
            if (FAILED(hr))
            {
                newDepthStencil->Release(); newSurface->Release(); newTexture->Release();
                ThrowHr("IDirect3DDevice8::SetRenderTarget(logical resize)", hr);
            }
        }

        if (impl_->logicalDepthStencil) impl_->logicalDepthStencil->Release();
        if (impl_->logicalSurface) impl_->logicalSurface->Release();
        if (impl_->logicalTexture) impl_->logicalTexture->Release();
        impl_->logicalTexture = newTexture;
        impl_->logicalSurface = newSurface;
        impl_->logicalDepthStencil = newDepthStencil;
    }

    void DirectX8Renderer::SetPresentationMode(int mode)
    {
        impl_->presentationMode = static_cast<CnaPresentationMode>(mode);
    }


    // ---- Textures and render targets: real Direct3D8 resources (design decision 7) ----
    // Never named outside this .cpp (only returned polymorphically as ITextureRenderer/
    // IRenderTargetRenderer).

    class DirectX8TextureRenderer final : public ITextureRenderer, public DirectX8TextureOwner
    {
    public:
        DirectX8TextureRenderer(IDirect3DDevice8* device, const ImageData& data)
            : device_(device), width_(data.width), height_(data.height)
        {
            const HRESULT hr = device_->CreateTexture(static_cast<UINT>(width_), static_cast<UINT>(height_),
                                                       1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture_);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::CreateTexture", hr);
            if (!data.pixels.empty())
                UpdatePixels(data.pixels.data(), width_ * 4);
        }
        ~DirectX8TextureRenderer() override { if (texture_) texture_->Release(); }

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            const std::size_t rowBytes = stride > 0 ? static_cast<std::size_t>(stride)
                                                     : static_cast<std::size_t>(width_) * 4;
            D3DLOCKED_RECT locked{};
            const HRESULT hr = texture_->LockRect(0, &locked, nullptr, 0);
            if (FAILED(hr)) ThrowHr("IDirect3DTexture8::LockRect", hr);
            auto* dst = static_cast<uint8_t*>(locked.pBits);
            for (int row = 0; row < height_; ++row)
            {
                RgbaToNativeRow(rgba + static_cast<std::size_t>(row) * rowBytes,
                                dst + static_cast<std::size_t>(row) * locked.Pitch, width_);
            }
            texture_->UnlockRect(0);
        }

        void UpdatePixelsLevel(int level, const uint8_t*, int, int) override
        {
            ThrowMipLevelUnsupported(level);
        }

        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
        {
            if (w <= 0 || h <= 0) return true;
            D3DLOCKED_RECT locked{};
            RECT rect{static_cast<LONG>(x), static_cast<LONG>(y),
                      static_cast<LONG>(x + w), static_cast<LONG>(y + h)};
            const HRESULT hr = texture_->LockRect(static_cast<UINT>(level), &locked, &rect, D3DLOCK_READONLY);
            if (FAILED(hr)) ThrowHr("IDirect3DTexture8::LockRect(read)", hr);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
            auto* dst = static_cast<uint8_t*>(data);
            auto* src = static_cast<const uint8_t*>(locked.pBits);
            for (int row = 0; row < h; ++row)
            {
                NativeToRgbaRow(src + static_cast<std::size_t>(row) * locked.Pitch,
                                 dst + static_cast<std::size_t>(row) * rowBytes, w);
            }
            texture_->UnlockRect(static_cast<UINT>(level));
            return true;
        }

        [[nodiscard]] IDirect3DTexture8* Texture() const override { return texture_; }

    private:
        IDirect3DDevice8* device_;
        int width_, height_;
        IDirect3DTexture8* texture_ = nullptr;
    };

    class DirectX8RenderTargetRenderer final : public IRenderTargetRenderer, public DirectX8TextureOwner
    {
    public:
        DirectX8RenderTargetRenderer(IDirect3DSurface8** currentTargetSlot, int* currentTargetWidthSlot,
                               int* currentTargetHeightSlot, IDirect3DDevice8* device, int w, int h,
                               int multiSampleCount)
            : currentTargetSlot_(currentTargetSlot), currentTargetWidthSlot_(currentTargetWidthSlot),
              currentTargetHeightSlot_(currentTargetHeightSlot), device_(device), width_(w), height_(h),
              multiSampleCount_(multiSampleCount)
        {
            // Design decision 7: created as a real texture with D3DUSAGE_RENDERTARGET so the SAME
            // underlying resource can both be rendered into (via GetSurfaceLevel's surface) and
            // sampled from later (via the texture itself) -- the standard D3D8/9
            // render-target-as-texture pattern. D3DPOOL_DEFAULT is required for render targets
            // (unlike plain textures' D3DPOOL_MANAGED).
            HRESULT hr = device_->CreateTexture(static_cast<UINT>(w), static_cast<UINT>(h), 1,
                                                 D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                                 &texture_);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::CreateTexture(RENDERTARGET)", hr);
            hr = texture_->GetSurfaceLevel(0, &surface_);
            if (FAILED(hr)) { texture_->Release(); ThrowHr("IDirect3DTexture8::GetSurfaceLevel", hr); }
        }
        ~DirectX8RenderTargetRenderer() override
        {
            if (*currentTargetSlot_ == surface_)
            {
                *currentTargetSlot_ = nullptr;
                *currentTargetWidthSlot_ = 0;
                *currentTargetHeightSlot_ = 0;
            }
            if (surface_) surface_->Release();
            if (texture_) texture_->Release();
        }

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t*, int) override {}
        void UpdatePixelsLevel(int level, const uint8_t*, int, int) override
        {
            ThrowMipLevelUnsupported(level);
        }
        [[nodiscard]] bool GetData(int, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
        {
            if (w <= 0 || h <= 0) return true;
            D3DLOCKED_RECT locked{};
            RECT rect{static_cast<LONG>(x), static_cast<LONG>(y),
                      static_cast<LONG>(x + w), static_cast<LONG>(y + h)};
            const HRESULT hr = surface_->LockRect(&locked, &rect, D3DLOCK_READONLY);
            if (FAILED(hr)) ThrowHr("IDirect3DSurface8::LockRect(RT read)", hr);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
            auto* dst = static_cast<uint8_t*>(data);
            auto* src = static_cast<const uint8_t*>(locked.pBits);
            for (int row = 0; row < h; ++row)
            {
                NativeToRgbaRow(src + static_cast<std::size_t>(row) * locked.Pitch,
                                 dst + static_cast<std::size_t>(row) * rowBytes, w);
            }
            surface_->UnlockRect();
            return true;
        }

        void BindAsRenderTarget() override
        {
            *currentTargetSlot_ = surface_;
            *currentTargetWidthSlot_ = width_;
            *currentTargetHeightSlot_ = height_;
            const HRESULT hr = device_->SetRenderTarget(surface_, nullptr);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetRenderTarget(custom)", hr);
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
        // A real Direct3D8 depth-stencil surface has no counterpart attached to a custom render
        // target here -- design decision (3D drawing is scoped to the default backbuffer only,
        // matching every DirectDraw-based renderer in this family's own identical boundary).
        [[nodiscard]] bool HasRealDepthBuffer(bool) const override { return false; }

        [[nodiscard]] IDirect3DTexture8* Texture() const override { return texture_; }

    private:
        IDirect3DSurface8** currentTargetSlot_;
        int* currentTargetWidthSlot_;
        int* currentTargetHeightSlot_;
        IDirect3DDevice8* device_;
        int width_, height_, multiSampleCount_;
        IDirect3DTexture8* texture_ = nullptr;
        IDirect3DSurface8* surface_ = nullptr;
    };

    std::unique_ptr<ITextureRenderer> DirectX8Renderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<DirectX8TextureRenderer>(impl_->device8, data);
    }

    std::unique_ptr<IRenderTargetRenderer> DirectX8Renderer::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int multiSampleCount)
    {
        return std::make_unique<DirectX8RenderTargetRenderer>(&impl_->currentTargetSurface,
                                                         &impl_->currentTargetWidth, &impl_->currentTargetHeight,
                                                         impl_->device8, w, h, multiSampleCount);
    }

    void DirectX8Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt)
            rt->BindAsRenderTarget();
        else
        {
            impl_->currentTargetSurface = nullptr;
            impl_->currentTargetWidth = 0;
            impl_->currentTargetHeight = 0;
            const HRESULT hr = impl_->device8->SetRenderTarget(impl_->logicalSurface, impl_->logicalDepthStencil);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetRenderTarget(logical)", hr);
        }
    }

    void DirectX8Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count > 1)
            throw std::runtime_error(
                "DIRECTX8 does not support multiple simultaneous render targets (MRT): requested " +
                std::to_string(count) + ", but this renderer's fixed-function scope supports "
                "exactly one active render target at a time.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "DIRECTX8 does not support RenderTargetCube face bindings.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    // ---- 2D compositor: real GPU-rendered textured quads (design decision 11) ----

    // Same byte layout the old D3DTLVERTEX macro implied (now gone from the real headers, design
    // decision 6) -- 4 floats position+rhw, 2 D3DCOLORs, 2 floats UV.
    struct DirectX8TLVertex
    {
        float sx, sy, sz, rhw;
        D3DCOLOR color;
        D3DCOLOR specular;
        float tu, tv;
    };
    constexpr DWORD kDx8Fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1;

    // Recomputed fresh on every Present() call from the real physical SDL_Window size, so a
    // resize is correct on the very next call -- same convention DIRECTX1..DIRECTX7's own ComputeLetterbox
    // uses (each renderer in this family keeps its own private copy rather than sharing one).
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

    // plan_dx8.md design: D3D8 has no scaled-blit primitive at all (no StretchRect -- a D3D9-only
    // addition -- and CopyRects is same-size-only), so Present() draws the logical render target's
    // texture as a single full-screen quad through the SAME fixed-function pipeline 3D geometry and
    // SpriteBatch quads use, onto the real swap chain, letterbox-scaled -- then presents for real.
    void DirectX8Renderer::Present()
    {
        IDirect3DDevice8* device = impl_->device8;

        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->virtualWidth, impl_->virtualHeight, scale, offsetX, offsetY))
        {
            scale = 1.0f; offsetX = 0.0f; offsetY = 0.0f;
        }

        HRESULT hr = device->SetRenderTarget(impl_->defaultBackBuffer, nullptr);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetRenderTarget(swap chain, Present)", hr);
        hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(swap chain, Present)", hr);

        // -0.5f: D3D8's XYZRHW (pre-transformed) vertices place texel/pixel centers at integer
        // coordinates, not integer+0.5 like modern APIs (the same half-texel convention applied
        // to DirectX8SpriteBatchRenderer::Draw's own quad corners).
        const float dx0 = offsetX - 0.5f;
        const float dy0 = offsetY - 0.5f;
        const float dx1 = offsetX + static_cast<float>(impl_->virtualWidth) * scale - 0.5f;
        const float dy1 = offsetY + static_cast<float>(impl_->virtualHeight) * scale - 0.5f;
        const D3DCOLOR white = D3DCOLOR_ARGB(255, 255, 255, 255);
        DirectX8TLVertex quad[4] = {
            {dx0, dy0, 0.0f, 1.0f, white, 0, 0.0f, 0.0f},
            {dx1, dy0, 0.0f, 1.0f, white, 0, 1.0f, 0.0f},
            {dx1, dy1, 0.0f, 1.0f, white, 0, 1.0f, 1.0f},
            {dx0, dy1, 0.0f, 1.0f, white, 0, 0.0f, 1.0f},
        };
        static constexpr WORD kQuadIdx[6] = {0, 1, 2, 0, 2, 3};

        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device->SetTexture(0, impl_->logicalTexture);
        device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
        device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

        hr = device->SetVertexShader(kDx8Fvf);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetVertexShader(Present quad)", hr);
        hr = device->BeginScene();
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::BeginScene(Present quad)", hr);
        hr = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, kQuadIdx, D3DFMT_INDEX16,
                                             quad, sizeof(DirectX8TLVertex));
        if (FAILED(hr)) { device->EndScene(); ThrowHr("IDirect3DDevice8::DrawIndexedPrimitiveUP(Present quad)", hr); }
        hr = device->EndScene();
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::EndScene(Present quad)", hr);

        hr = device->Present(nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Present", hr);

        // Restore whichever render target was active before this Present() call for the next
        // frame's draws (the logical target by default, or a still-bound custom RenderTarget2D).
        if (impl_->currentTargetSurface)
        {
            hr = device->SetRenderTarget(impl_->currentTargetSurface, nullptr);
        }
        else
        {
            hr = device->SetRenderTarget(impl_->logicalSurface, impl_->logicalDepthStencil);
        }
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetRenderTarget(restore after Present)", hr);
    }

    bool DirectX8Renderer::TransformWindowToLogical(float windowX, float windowY,
                                                       float& logX, float& logY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->virtualWidth, impl_->virtualHeight, scale, offsetX, offsetY))
            return false;
        logX = (windowX - offsetX) / scale;
        logY = (windowY - offsetY) / scale;
        return true;
    }

    bool DirectX8Renderer::TransformLogicalToWindow(float logX, float logY,
                                                       float& windowX, float& windowY) const
    {
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        if (!ComputeLetterbox(impl_->window, impl_->virtualWidth, impl_->virtualHeight, scale, offsetX, offsetY))
            return false;
        windowX = logX * scale + offsetX;
        windowY = logY * scale + offsetY;
        return true;
    }

    class DirectX8SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        DirectX8SpriteBatchRenderer(IDirect3DDevice8* device, std::function<void(int&, int&)> getActiveSurfaceSize)
            : device_(device), getActiveSurfaceSize_(std::move(getActiveSurfaceSize))
        {
        }

        void Begin() override
        {
            if (begun_) throw std::runtime_error("DirectX8SpriteBatchRenderer::Begin: Begin() called without a matching End()");
            begun_ = true;
        }
        void End() override
        {
            if (!begun_) throw std::runtime_error("DirectX8SpriteBatchRenderer::End: End() called without a matching Begin()");
            FlushBatch();
            begun_ = false;
        }
        // Applied as a point transform (z=0) directly on the already-screen-space quad corners,
        // same convention SOFTWARE/DIRECTX2..DIRECTX7's own SetTransformMatrix uses.
        void SetTransformMatrix(const Matrix& m) override { transformMatrix_ = m; }
        void SetCustomEffect(Effect* effect) override
        {
            if (effect != nullptr)
                throw std::runtime_error(
                    "DIRECTX8 (fixed-function) does not support custom SpriteBatch Effects: no "
                    "programmable shader stage is used by this renderer (scope decision).");
        }
        void SetSamplerFilter(int textureFilter) override { filter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            addressU_ = addressU;
            addressV_ = addressV;
        }

        void Draw(const ITextureRenderer& texture, float x, float y) override
        {
            Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
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
                 const Vector2& origin, SpriteEffects effects, float layerDepth) override
        {
            if (!begun_) throw std::runtime_error("DirectX8SpriteBatchRenderer::Draw: Draw() called before Begin()");

            const auto* owner = dynamic_cast<const DirectX8TextureOwner*>(&texture);
            if (!owner)
                throw std::runtime_error(
                    "DirectX8SpriteBatchRenderer::Draw: texture renderer is not a DIRECTX8 texture (created by "
                    "a different graphics renderer?)");

            if (currentTexture_ != nullptr && currentTexture_ != &texture)
                FlushBatch();
            currentTexture_ = &texture;

            const int texW = std::max(1, texture.GetWidth());
            const int texH = std::max(1, texture.GetHeight());
            float u1 = static_cast<float>(sourceRectangle.X) / static_cast<float>(texW);
            float v1 = static_cast<float>(sourceRectangle.Y) / static_cast<float>(texH);
            float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / static_cast<float>(texW);
            float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / static_cast<float>(texH);
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

            const float dx = static_cast<float>(destinationRectangle.X);
            const float dy = static_cast<float>(destinationRectangle.Y);
            const float dw = static_cast<float>(destinationRectangle.Width);
            const float dh = static_cast<float>(destinationRectangle.Height);
            const float sw = static_cast<float>(std::max(1, sourceRectangle.Width));
            const float sh = static_cast<float>(std::max(1, sourceRectangle.Height));
            const float scaleX = dw / sw;
            const float scaleY = dh / sh;
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
                // transformMatrix_ applied as a point transform directly on the screen-space quad
                // corner, same convention SOFTWARE/DIRECTX2..DIRECTX7 already established.
                const Vector4 t = Vector4::Transform(Vector3(rx, ry, 0.0f), transformMatrix_);
                // D3D8's XYZRHW (pre-transformed) vertices place texel/pixel centers at INTEGER
                // coordinates, not integer+0.5 like modern APIs -- the classic D3D8/9 "half-texel"
                // convention (D3D9SpriteBatchRenderer bakes the same correction into its own
                // orthographic projection matrix; this renderer's quad is already in screen space
                // with no such matrix, so the shift is applied directly here instead).
                return Vector2(t.X - 0.5f, t.Y - 0.5f);
            };

            const Vector2 c0 = placeCorner(p0x, p0y);
            const Vector2 c1 = placeCorner(p1x, p1y);
            const Vector2 c2 = placeCorner(p2x, p2y);
            const Vector2 c3 = placeCorner(p3x, p3y);

            const auto r = static_cast<uint8_t>(color.getRProperty());
            const auto g = static_cast<uint8_t>(color.getGProperty());
            const auto b = static_cast<uint8_t>(color.getBProperty());
            const auto a = static_cast<uint8_t>(color.getAProperty());
            const D3DCOLOR dc = D3DCOLOR_ARGB(a, r, g, b);

            const auto baseIdx = static_cast<WORD>(pendingVertices_.size());
            pendingVertices_.push_back({c0.X, c0.Y, layerDepth, 1.0f, dc, 0, u1, v1});
            pendingVertices_.push_back({c1.X, c1.Y, layerDepth, 1.0f, dc, 0, u2, v1});
            pendingVertices_.push_back({c2.X, c2.Y, layerDepth, 1.0f, dc, 0, u2, v2});
            pendingVertices_.push_back({c3.X, c3.Y, layerDepth, 1.0f, dc, 0, u1, v2});
            pendingIndices_.push_back(baseIdx + 0);
            pendingIndices_.push_back(baseIdx + 1);
            pendingIndices_.push_back(baseIdx + 2);
            pendingIndices_.push_back(baseIdx + 2);
            pendingIndices_.push_back(baseIdx + 3);
            pendingIndices_.push_back(baseIdx + 0);
        }

    private:
        void FlushBatch()
        {
            if (pendingIndices_.empty() || !currentTexture_) { pendingVertices_.clear(); pendingIndices_.clear(); return; }

            const auto* owner = dynamic_cast<const DirectX8TextureOwner*>(currentTexture_);
            IDirect3DTexture8* tex = owner ? owner->Texture() : nullptr;

            HRESULT hr = device_->SetTexture(0, tex);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetTexture(SpriteBatch)", hr);
            const DirectX8FilterPair filterPair = DirectX8TextureFilterToD3D(filter_);
            device_->SetTextureStageState(0, D3DTSS_MAGFILTER, filterPair.mag);
            device_->SetTextureStageState(0, D3DTSS_MINFILTER, filterPair.min);
            device_->SetTextureStageState(0, D3DTSS_ADDRESSU, DirectX8TextureAddressModeToD3D(addressU_));
            device_->SetTextureStageState(0, D3DTSS_ADDRESSV, DirectX8TextureAddressModeToD3D(addressV_));
            device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

            hr = device_->SetVertexShader(kDx8Fvf);
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetVertexShader(SpriteBatch FVF)", hr);

            hr = device_->BeginScene();
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::BeginScene(SpriteBatch)", hr);
            hr = device_->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0,
                static_cast<UINT>(pendingVertices_.size()), static_cast<UINT>(pendingIndices_.size() / 3),
                pendingIndices_.data(), D3DFMT_INDEX16, pendingVertices_.data(), sizeof(DirectX8TLVertex));
            if (FAILED(hr))
            {
                device_->EndScene();
                ThrowHr("IDirect3DDevice8::DrawIndexedPrimitiveUP(SpriteBatch)", hr);
            }
            hr = device_->EndScene();
            if (FAILED(hr)) ThrowHr("IDirect3DDevice8::EndScene(SpriteBatch)", hr);

            pendingVertices_.clear();
            pendingIndices_.clear();
            currentTexture_ = nullptr;
        }

        IDirect3DDevice8* device_;
        std::function<void(int&, int&)> getActiveSurfaceSize_;
        std::vector<DirectX8TLVertex> pendingVertices_;
        std::vector<WORD> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        int filter_ = 0;      // 0=Linear
        int addressU_ = 1;    // 1=Clamp
        int addressV_ = 1;
    };

    std::unique_ptr<ISpriteBatchRenderer> DirectX8Renderer::CreateSpriteBatch()
    {
        return std::make_unique<DirectX8SpriteBatchRenderer>(impl_->device8,
            [this](int& w, int& h) { impl_->ActiveSurfaceSize(w, h); });
    }

    // Real D3DRS_ALPHABLENDENABLE/SRCBLEND/DESTBLEND for both 3D draws and 2D SpriteBatch quads --
    // both paths share the same device blend state (design decision 11). D3D8 has no separate
    // alpha blend-factor/op pair -- alphaSrcBlend/alphaDstBlend/colorBlendFunc/alphaBlendFunc are
    // accepted and ignored, matching every prior renderer in this family.
    void DirectX8Renderer::ApplyBlendState(int colorSrcBlend, int /*alphaSrcBlend*/,
                                             int colorDstBlend, int /*alphaDstBlend*/,
                                             int /*colorBlendFunc*/, int /*alphaBlendFunc*/,
                                             const BlendWriteState& /*writeState*/)
    {
        // REMED-GFX-077: D3D8 does expose D3DRS_COLORWRITEENABLE/D3DRS_MULTISAMPLEMASK, but
        // wiring them is deferred with the rest of this renderer's accepted-and-ignored raster
        // states (scissor/depth-bias, see ApplyRasterizerState); the write state is accepted and
        // ignored -- a documented deferral of this Historical renderer, not a silent drop the
        // caller was told succeeded in full.
        impl_->device8->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        impl_->device8->SetRenderState(D3DRS_SRCBLEND, DirectX8BlendToD3D(colorSrcBlend));
        impl_->device8->SetRenderState(D3DRS_DESTBLEND, DirectX8BlendToD3D(colorDstBlend));
    }

    void DirectX8Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                    bool stencilEnable, int stencilFunc,
                                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                                    bool /*twoSidedStencilMode*/,
                                                    int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                    int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        impl_->device8->SetRenderState(D3DRS_ZENABLE, depthEnable ? D3DZB_TRUE : D3DZB_FALSE);
        impl_->device8->SetRenderState(D3DRS_ZWRITEENABLE, depthWriteEnable ? TRUE : FALSE);
        impl_->device8->SetRenderState(D3DRS_ZFUNC, DirectX8CompareFunctionToD3D(depthFunc));

        impl_->device8->SetRenderState(D3DRS_STENCILENABLE, stencilEnable ? TRUE : FALSE);
        impl_->device8->SetRenderState(D3DRS_STENCILFUNC, DirectX8CompareFunctionToD3D(stencilFunc));
        impl_->device8->SetRenderState(D3DRS_STENCILFAIL, DirectX8StencilOperationToD3D(stencilFail));
        impl_->device8->SetRenderState(D3DRS_STENCILZFAIL, DirectX8StencilOperationToD3D(stencilDepthFail));
        impl_->device8->SetRenderState(D3DRS_STENCILPASS, DirectX8StencilOperationToD3D(stencilPass));
        impl_->device8->SetRenderState(D3DRS_STENCILREF, static_cast<DWORD>(referenceStencil));
        impl_->device8->SetRenderState(D3DRS_STENCILMASK, static_cast<DWORD>(stencilMask));
        impl_->device8->SetRenderState(D3DRS_STENCILWRITEMASK, static_cast<DWORD>(stencilWriteMask));
    }

    void DirectX8Renderer::ApplyRasterizerState(int cullMode, int fillMode, bool /*scissorTestEnable*/,
                                                  float /*depthBias*/, float /*slopeScaleDepthBias*/)
    {
        impl_->device8->SetRenderState(D3DRS_CULLMODE, DirectX8CullModeToD3D(cullMode));
        impl_->device8->SetRenderState(D3DRS_FILLMODE, DirectX8FillModeToD3D(fillMode));
    }

    void DirectX8Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                               int maxAnisotropy)
    {
        if (slot != 0) return; // fixed-function scope: exactly one texture stage
        const DirectX8FilterPair filterPair = DirectX8TextureFilterToD3D(filter);
        impl_->device8->SetTextureStageState(0, D3DTSS_MAGFILTER, filterPair.mag);
        impl_->device8->SetTextureStageState(0, D3DTSS_MINFILTER, filterPair.min);
        impl_->device8->SetTextureStageState(0, D3DTSS_ADDRESSU, DirectX8TextureAddressModeToD3D(addressU));
        impl_->device8->SetTextureStageState(0, D3DTSS_ADDRESSV, DirectX8TextureAddressModeToD3D(addressV));
        impl_->device8->SetTextureStageState(0, D3DTSS_MAXANISOTROPY, static_cast<DWORD>(maxAnisotropy));
    }

    void DirectX8Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        const HRESULT hr = impl_->device8->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                                  D3DCOLOR_COLORVALUE(r, g, b, a), depth, 0);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(TARGET|ZBUFFER)", hr);
    }
    void DirectX8Renderer::ClearDepth(float depth)
    {
        const HRESULT hr = impl_->device8->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, depth, 0);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(ZBUFFER)", hr);
    }
    void DirectX8Renderer::ClearStencil(int stencil)
    {
        const HRESULT hr = impl_->device8->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 0.0f,
                                                  static_cast<DWORD>(stencil));
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(STENCIL)", hr);
    }
    void DirectX8Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        const HRESULT hr = impl_->device8->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                                                  0, depth, static_cast<DWORD>(stencil));
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(ZBUFFER|STENCIL)", hr);
    }
    void DirectX8Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        const HRESULT hr = impl_->device8->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_STENCIL,
                                                  D3DCOLOR_COLORVALUE(r, g, b, a), 1.0f,
                                                  static_cast<DWORD>(stencil));
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(TARGET|STENCIL)", hr);
    }
    void DirectX8Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        const HRESULT hr = impl_->device8->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                                                  D3DCOLOR_COLORVALUE(r, g, b, a), depth,
                                                  static_cast<DWORD>(stencil));
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::Clear(TARGET|ZBUFFER|STENCIL)", hr);
    }
    void DirectX8Renderer::SetDepthTestEnabled(bool enabled)
    {
        impl_->device8->SetRenderState(D3DRS_ZENABLE, enabled ? D3DZB_TRUE : D3DZB_FALSE);
    }
    // Deliberate no-op, matching D3D9's/D3D11's/every prior renderer in this family's own identical
    // choice: real blend configuration always arrives via ApplyBlendState().
    void DirectX8Renderer::SetBlendEnabled(bool) {}
    void DirectX8Renderer::SetDepthWriteEnabled(bool enabled)
    {
        impl_->device8->SetRenderState(D3DRS_ZWRITEENABLE, enabled ? TRUE : FALSE);
    }

    // ---- VertexBuffer/IndexBuffer renderers: plain CPU-side storage (design decision 10) ----

    class DirectX8VertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit DirectX8VertexBufferRenderer(int vertexCapacity) : capacity_(vertexCapacity) {}
        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override
        {
            if (vertex_count < 0 || vertex_count > capacity_)
                throw std::runtime_error("DirectX8VertexBufferRenderer::SetData: vertex_count exceeds capacity");
            if (stride_in_bytes == 0)
                throw std::runtime_error("DirectX8VertexBufferRenderer::SetData: stride_in_bytes must be > 0");
            vertexCount_ = vertex_count;
            stride_ = stride_in_bytes;
            const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
            data_.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + byteCount);
        }
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes, SetDataOptions) override
        { SetData(data, vertex_count, stride_in_bytes); }
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

    class DirectX8IndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        DirectX8IndexBufferRenderer(int indexCapacity, bool thirtyTwoBit)
            : capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit) {}
        void SetData16(const void* data, int index_count) override { Upload(data, index_count, false); }
        void SetData32(const void* data, int index_count) override { Upload(data, index_count, true); }
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions) override { Upload(data, index_count, false); }
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions) override { Upload(data, index_count, true); }
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }
        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] const std::vector<uint8_t>& Data() const { return data_; }
    private:
        void Upload(const void* data, int index_count, bool dataIsThirtyTwoBit)
        {
            if (index_count < 0 || index_count > capacity_)
                throw std::runtime_error("DirectX8IndexBufferRenderer: index_count exceeds capacity");
            if (dataIsThirtyTwoBit != thirtyTwoBit_)
                throw std::runtime_error("DirectX8IndexBufferRenderer: SetData bit-width does not match the buffer's declared width");
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

    std::unique_ptr<IVertexBufferRenderer> DirectX8Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<DirectX8VertexBufferRenderer>(vertex_capacity);
    }
    std::unique_ptr<IIndexBufferRenderer> DirectX8Renderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<DirectX8IndexBufferRenderer>(index_capacity, false);
    }
    std::unique_ptr<IIndexBufferRenderer> DirectX8Renderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<DirectX8IndexBufferRenderer>(index_capacity, true);
    }

    // ---- CPU transform + near-plane clip pipeline, fixed-function submission (decisions 6/8) ----
    // Ported conceptually from DIRECTX7's own DirectX7ClipVertex/DirectX7ClipTriangleNearPlane/
    // DirectX7BuildPositionColorClipVertex/DirectX7BuildGenericClipVertex/DirectX7ClipVertexToD3DTLVERTEX -- same
    // math, re-expressed against DirectX8TLVertex/DrawIndexedPrimitiveUP instead of
    // D3DTLVERTEX/DrawIndexedPrimitive.

    struct DirectX8ClipVertex
    {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        float u = 0.0f, v = 0.0f;
        float sr = 0.0f, sg = 0.0f, sb = 0.0f;
    };

    DirectX8ClipVertex DirectX8LerpClipVertex(const DirectX8ClipVertex& a, const DirectX8ClipVertex& b, float t)
    {
        DirectX8ClipVertex out;
        out.x = a.x + t * (b.x - a.x); out.y = a.y + t * (b.y - a.y);
        out.z = a.z + t * (b.z - a.z); out.w = a.w + t * (b.w - a.w);
        out.r = a.r + t * (b.r - a.r); out.g = a.g + t * (b.g - a.g);
        out.b = a.b + t * (b.b - a.b); out.a = a.a + t * (b.a - a.a);
        out.u = a.u + t * (b.u - a.u); out.v = a.v + t * (b.v - a.v);
        out.sr = a.sr + t * (b.sr - a.sr); out.sg = a.sg + t * (b.sg - a.sg); out.sb = a.sb + t * (b.sb - a.sb);
        return out;
    }

    /// Sutherland-Hodgman clip against the single near-plane half-space `w > kNearEpsilon`.
    /// Returns 0 (fully behind, discarded), 3, or 4 (fanned into 2 triangles by the caller).
    int DirectX8ClipTriangleNearPlane(const DirectX8ClipVertex verts[3], DirectX8ClipVertex out[4])
    {
        constexpr float kNearEpsilon = 1e-5f;
        int count = 0;
        for (int i = 0; i < 3; ++i)
        {
            const DirectX8ClipVertex& cur = verts[i];
            const DirectX8ClipVertex& prev = verts[(i + 2) % 3];
            const bool curIn = cur.w > kNearEpsilon;
            const bool prevIn = prev.w > kNearEpsilon;
            if (curIn != prevIn)
            {
                const float t = (kNearEpsilon - prev.w) / (cur.w - prev.w);
                out[count++] = DirectX8LerpClipVertex(prev, cur, t);
            }
            if (curIn) out[count++] = cur;
        }
        return count;
    }

    DirectX8ClipVertex DirectX8BuildPositionColorClipVertex(const uint8_t* raw, const Matrix& combined)
    {
        Vector3 position;
        std::memcpy(&position, raw, sizeof(Vector3));
        const Vector4 clip = Vector4::Transform(position, combined);
        DirectX8ClipVertex out;
        out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;
        const uint8_t* colorBytes = raw + sizeof(Vector3);
        out.r = colorBytes[0] / 255.0f; out.g = colorBytes[1] / 255.0f;
        out.b = colorBytes[2] / 255.0f; out.a = colorBytes[3] / 255.0f;
        return out;
    }

    struct DirectX8LitColor
    {
        float diffuseR = 0.0f, diffuseG = 0.0f, diffuseB = 0.0f;
        float specR = 0.0f, specG = 0.0f, specB = 0.0f;
    };

    /// CPU-side BasicEffect-style per-vertex lighting: ambient + up to 3 directional lights,
    /// Lambertian diffuse + Blinn-Phong specular -- ported conceptually from every prior renderer
    /// in this family's own identical Phase O9 math (EasyGLRenderer's GLSL, not re-derived).
    DirectX8LitColor DirectX8ComputeVertexLighting(const Vector3& localPosition, const Vector3& localNormal,
                                         const Matrix& world, const GpuDrawParams& params)
    {
        const Vector3 worldPos = Vector3::Transform(localPosition, world);

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

        DirectX8LitColor out;
        out.diffuseR = lightSum[0] * params.diffuseColor[0] + params.emissiveColor[0];
        out.diffuseG = lightSum[1] * params.diffuseColor[1] + params.emissiveColor[1];
        out.diffuseB = lightSum[2] * params.diffuseColor[2] + params.emissiveColor[2];
        out.specR = specSum[0] * params.specularColor[0];
        out.specG = specSum[1] * params.specularColor[1];
        out.specB = specSum[2] * params.specularColor[2];
        return out;
    }

    DirectX8ClipVertex DirectX8BuildGenericClipVertex(const uint8_t* raw, std::size_t stride, const Matrix& combined,
                                            const Matrix& world, const GpuDrawParams& params)
    {
        Vector3 position;
        std::memcpy(&position, raw, sizeof(Vector3));
        const Vector4 clip = Vector4::Transform(position, combined);
        DirectX8ClipVertex out;
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
            std::memcpy(&out.u, raw + 24, sizeof(float));
            std::memcpy(&out.v, raw + 28, sizeof(float));
            if (params.lightingEnabled)
            {
                Vector3 normal;
                std::memcpy(&normal, raw + 12, sizeof(Vector3));
                const DirectX8LitColor litColor = DirectX8ComputeVertexLighting(position, normal, world, params);
                out.r = litColor.diffuseR; out.g = litColor.diffuseG; out.b = litColor.diffuseB;
                out.a = params.diffuseColor[3];
                out.sr = litColor.specR; out.sg = litColor.specG; out.sb = litColor.specB;
                lit = true;
            }
        }
        if (!lit && !params.vertexColorEnabled)
            out.r = out.g = out.b = out.a = 1.0f;
        return out;
    }

    DirectX8TLVertex DirectX8ClipVertexToDx8TLVertex(const DirectX8ClipVertex& cv, int viewportWidth, int viewportHeight)
    {
        const float invW = 1.0f / cv.w;
        const float ndcX = cv.x * invW;
        const float ndcY = cv.y * invW;
        const float ndcZ = cv.z * invW;

        DirectX8TLVertex out{};
        out.sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth);
        out.sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(viewportHeight);
        out.sz = ndcZ;
        out.rhw = invW;
        const auto channel = [](float c) -> uint8_t { return static_cast<uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f); };
        out.color = D3DCOLOR_ARGB(channel(cv.a), channel(cv.r), channel(cv.g), channel(cv.b));
        out.specular = D3DCOLOR_ARGB(0xFFu, channel(cv.sr), channel(cv.sg), channel(cv.sb));
        out.tu = cv.u;
        out.tv = cv.v;
        return out;
    }

    /// Resolves a GpuDrawParams::texture0-style ITextureRenderer* into the real IDirect3DTexture8*
    /// to bind, or nullptr (no texture) when `texture` is null.
    IDirect3DTexture8* DirectX8ResolveTexture(const ITextureRenderer* texture)
    {
        if (!texture) return nullptr;
        const auto* owner = dynamic_cast<const DirectX8TextureOwner*>(texture);
        if (!owner)
            throw std::runtime_error(
                "DirectX8Renderer: texture0 is not a DIRECTX8 texture (created by a different graphics renderer?)");
        return owner->Texture();
    }

    /// Shared core for all 4 draw entry points: clips each triangle, packs into DirectX8TLVertex, and
    /// submits via a single DrawIndexedPrimitiveUP call. `texture` is bound via SetTexture(0, ...)
    /// every call (nullptr = no texture -- COLOROP=MODULATE against no texture defaults to a real
    /// device-side opaque-white texture color, matching every prior renderer's own established
    /// behavior). `specularEnabled`: true only for a lit DrawPrimitivesEx/DrawIndexedPrimitivesEx
    /// call.
    void SubmitDx8Primitives(IDirect3DDevice8* device, int viewportWidth, int viewportHeight,
                             const std::function<DirectX8ClipVertex(int)>& fetchVertex,
                             int primitiveCount, IDirect3DTexture8* texture, bool specularEnabled)
    {
        std::vector<DirectX8TLVertex> verts;
        std::vector<WORD> indices;
        verts.reserve(static_cast<std::size_t>(primitiveCount) * 3);
        indices.reserve(static_cast<std::size_t>(primitiveCount) * 3);

        for (int i = 0; i < primitiveCount; ++i)
        {
            DirectX8ClipVertex cv[3];
            for (int k = 0; k < 3; ++k) cv[k] = fetchVertex(i * 3 + k);

            DirectX8ClipVertex clipped[4];
            const int clippedCount = DirectX8ClipTriangleNearPlane(cv, clipped);
            if (clippedCount == 0) continue;

            const auto baseIdx = static_cast<WORD>(verts.size());
            for (int k = 0; k < clippedCount; ++k)
                verts.push_back(DirectX8ClipVertexToDx8TLVertex(clipped[k], viewportWidth, viewportHeight));

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
        if (indices.empty()) return;

        HRESULT hr = device->SetTexture(0, texture);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetTexture", hr);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        hr = device->SetRenderState(D3DRS_SPECULARENABLE, specularEnabled ? TRUE : FALSE);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetRenderState(SPECULARENABLE)", hr);

        hr = device->SetVertexShader(kDx8Fvf);
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::SetVertexShader", hr);

        hr = device->BeginScene();
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::BeginScene", hr);
        hr = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(verts.size()),
            static_cast<UINT>(indices.size() / 3), indices.data(), D3DFMT_INDEX16, verts.data(), sizeof(DirectX8TLVertex));
        if (FAILED(hr))
        {
            device->EndScene();
            ThrowHr("IDirect3DDevice8::DrawIndexedPrimitiveUP", hr);
        }
        hr = device->EndScene();
        if (FAILED(hr)) ThrowHr("IDirect3DDevice8::EndScene", hr);
    }

    void DirectX8CheckDrawPreconditions(const char* who, bool customRenderTargetBound,
                                   PrimitiveType primitive, int primitiveCount)
    {
        if (customRenderTargetBound)
            throw std::runtime_error(std::string("DirectX8Renderer::") + who +
                ": 3D drawing is not supported while a custom RenderTarget2D is bound "
                "(design decision -- 3D is scoped to the default backbuffer only)");
        if (primitiveCount <= 0)
            throw std::runtime_error(std::string("DirectX8Renderer::") + who + ": primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error(std::string("DirectX8Renderer::") + who + ": only TriangleList is supported");
    }

    void DirectX8Renderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                                   PrimitiveType primitive, int primitiveCount)
    {
        DirectX8CheckDrawPreconditions("DrawColoredPrimitives", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (primitiveCount * 3 > vb.GetVertexCount())
            throw std::runtime_error(
                "DirectX8Renderer::DrawColoredPrimitives: primitiveCount needs more vertices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX8VertexBufferRenderer&>(vb);
        const uint8_t* base = dxVb.Data().data();
        const std::size_t stride = dxVb.Stride();
        const Matrix combined = world * view * projection;
        int vw = 0, vh = 0;
        impl_->ActiveSurfaceSize(vw, vh);

        SubmitDx8Primitives(impl_->device8, vw, vh,
            [&](int i) { return DirectX8BuildPositionColorClipVertex(base + static_cast<std::size_t>(i) * stride, combined); },
            primitiveCount, nullptr, false);
    }

    void DirectX8Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                          const IIndexBufferRenderer& ib,
                                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                                          PrimitiveType primitive, int primitiveCount)
    {
        DirectX8CheckDrawPreconditions("DrawIndexedColoredPrimitives", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (primitiveCount * 3 > ib.GetIndexCount())
            throw std::runtime_error(
                "DirectX8Renderer::DrawIndexedColoredPrimitives: primitiveCount needs more indices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX8VertexBufferRenderer&>(vb);
        const auto& dxIb = static_cast<const DirectX8IndexBufferRenderer&>(ib);
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

        SubmitDx8Primitives(impl_->device8, vw, vh,
            [&](int i) {
                const uint32_t idx = readIndex(i);
                return DirectX8BuildPositionColorClipVertex(vbBase + static_cast<std::size_t>(idx) * stride, combined);
            },
            primitiveCount, nullptr, false);
    }

    // REMED-GFX-DECL-GUARD: the declaration-fidelity boundary. The Draw*Ex routes below read
    // their attributes at byte offsets chosen by the stride alone (REMED-GFX-217), so a
    // declaration those offsets cannot represent is refused before any vertex is fetched. A
    // stride outside this renderer's own supported set is left to its established out-of-table
    // rejection, which is already loud and deterministic.
    static void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& dxVb = static_cast<const DirectX8VertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            dxVb.Declaration(), static_cast<int>(dxVb.Stride()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "DIRECTX8", route);
    }

    void DirectX8Renderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                             const Matrix& world, const Matrix& view, const Matrix& projection,
                                             PrimitiveType primitive, int primitiveCount,
                                             const GpuDrawParams& params)
    {
        RequireFaithfulDeclarationEXT(vb, "ordinary-nonindexed");
        DirectX8CheckDrawPreconditions("DrawPrimitivesEx", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (params.textureEnabled && params.texture0 == nullptr)
            throw std::runtime_error("DirectX8Renderer::DrawPrimitivesEx: textureEnabled=true but texture0 is null");
        if (params.vertexStart + primitiveCount * 3 > vb.GetVertexCount())
            throw std::runtime_error(
                "DirectX8Renderer::DrawPrimitivesEx: vertexStart + primitiveCount needs more vertices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX8VertexBufferRenderer&>(vb);
        const std::size_t stride = dxVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 && stride != 52)
            throw std::runtime_error(
                "DirectX8Renderer::DrawPrimitivesEx: unsupported vertex stride (only 16/20/24/32/52 supported)");

        const uint8_t* base = dxVb.Data().data();
        const Matrix combined = world * view * projection;
        int vw = 0, vh = 0;
        impl_->ActiveSurfaceSize(vw, vh);
        IDirect3DTexture8* texture = params.textureEnabled ? DirectX8ResolveTexture(params.texture0) : nullptr;
        const int vertexStart = params.vertexStart;

        SubmitDx8Primitives(impl_->device8, vw, vh,
            [&](int i) { return DirectX8BuildGenericClipVertex(base + static_cast<std::size_t>(vertexStart + i) * stride,
                                                          stride, combined, world, params); },
            primitiveCount, texture, params.lightingEnabled);
    }

    void DirectX8Renderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount,
                                                     const GpuDrawParams& params)
    {
        RequireFaithfulDeclarationEXT(vb, "ordinary-indexed");
        DirectX8CheckDrawPreconditions("DrawIndexedPrimitivesEx", impl_->currentTargetSurface != nullptr,
                                  primitive, primitiveCount);
        if (params.textureEnabled && params.texture0 == nullptr)
            throw std::runtime_error("DirectX8Renderer::DrawIndexedPrimitivesEx: textureEnabled=true but texture0 is null");
        if (params.startIndex + primitiveCount * 3 > ib.GetIndexCount())
            throw std::runtime_error(
                "DirectX8Renderer::DrawIndexedPrimitivesEx: startIndex + primitiveCount needs more indices than the bound buffer has");

        const auto& dxVb = static_cast<const DirectX8VertexBufferRenderer&>(vb);
        const auto& dxIb = static_cast<const DirectX8IndexBufferRenderer&>(ib);
        const std::size_t stride = dxVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 && stride != 52)
            throw std::runtime_error(
                "DirectX8Renderer::DrawIndexedPrimitivesEx: unsupported vertex stride (only 16/20/24/32/52 supported)");

        const uint8_t* vbBase = dxVb.Data().data();
        const uint8_t* ibBase = dxIb.Data().data();
        const bool thirtyTwoBit = dxIb.IsThirtyTwoBit();
        const Matrix combined = world * view * projection;
        int vw = 0, vh = 0;
        impl_->ActiveSurfaceSize(vw, vh);
        IDirect3DTexture8* texture = params.textureEnabled ? DirectX8ResolveTexture(params.texture0) : nullptr;
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

        SubmitDx8Primitives(impl_->device8, vw, vh,
            [&](int i) {
                const uint32_t idx = readIndex(startIndex + i) + static_cast<uint32_t>(baseVertex);
                return DirectX8BuildGenericClipVertex(vbBase + static_cast<std::size_t>(idx) * stride, stride,
                                                 combined, world, params);
            },
            primitiveCount, texture, params.lightingEnabled);
    }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<DirectX8::DirectX8Renderer>(args);
    }
}
