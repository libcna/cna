#pragma once

// plan_dx9.md Phase D9-5 (D9-53): real D3D9 offscreen render-target backends.
//
// Both classes hold a non-owning D3D9GraphicsBackend* (owner_) -- a render target's Unbind must be
// able to restore the owner's default back-buffer/depth-stencil surfaces, which only the owning
// backend instance knows how to do (mirrors D3D11RenderTargetBackend's identical owner_ pattern).
//
// D3DPOOL_DEFAULT (not MANAGED, unlike D3D9Textures.hpp's plain textures) -- D3DUSAGE_RENDERTARGET
// resources do not survive Reset(), so both classes implement ID3D9DefaultPoolResourceEXT and
// register with the D9-40 device-lost registry. Unlike a dynamic vertex/index buffer (whose "next
// use" is the caller's own SetData()), a render target's natural "next use" is
// BindAsRenderTarget()/BindAsRenderTargetFace() -- both lazily recreate the underlying D3D9
// resources there if a prior device-lost recovery released them (ReleaseDefaultPoolResourceEXT()).
// Real XNA/D3D9 behavior either way: RenderTarget2D content does not survive a device Reset unless
// RenderTargetUsage.PreserveContents (not implemented by this project on any backend yet).
//
// RGBA8 storage only (D3DFMT_A8B8G8R8) -- same simplification D3D9Textures.hpp/D3D11 already use.
// MSAA (2D only, D9-53's own note) is clamped to what the device's real
// IDirect3D9::CheckDeviceMultiSampleType() reports (D3D9GraphicsBackend::ClampMultiSampleCountEXT()),
// never assumed -- an unsupported sample count silently falls back to 0 (no MSAA), matching
// D3D11RenderTargetBackend's own all-or-nothing clamp (no step-down ladder). An MSAA target is
// bound as a separate offscreen D3DMULTISAMPLE color surface (D3D9 textures cannot themselves be
// multisampled) and resolved into the sampleable D3DPOOL_DEFAULT texture via StretchRect on unbind
// -- mirrors D3D11's own resolve-on-unbind convention. D3D9RenderTargetCubeBackend deliberately does
// NOT support MSAA, same as D3D11RenderTargetCubeBackend's own precedent.
//
// Mip-chain auto-generation on unbind (D3D11's documented mipMap behavior) is NOT implemented here
// -- `mipMap` is accepted but only a single level is ever allocated. A genuine, named gap
// (plan_dx9.md's own D9-53 row), not a silent divergence.

#include "../Common/IGraphicsBackend.hpp"
#include "D3D9DefaultPoolResourceEXT.hpp"

#include <d3d9.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D9
{
    using Microsoft::WRL::ComPtr;

    class D3D9GraphicsBackend;

    /// Real D3D9 2D render target (D9-53).
    class D3D9RenderTargetBackend final : public IRenderTargetBackend, public ID3D9DefaultPoolResourceEXT
    {
    public:
        D3D9RenderTargetBackend(D3D9GraphicsBackend& owner, IDirect3DDevice9* device,
                                int w, int h, int depthFormat, int multiSampleCount);
        ~D3D9RenderTargetBackend() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127. D3D9 has a real, fully synchronous readback for a D3DPOOL_DEFAULT render
         * target: `IDirect3DDevice9::GetRenderTargetData` copies the colour surface into a
         * D3DPOOL_SYSTEMMEM offscreen plain surface, which is then `LockRect`ed for the requested
         * rectangle. When this target is multisampled the read still comes from the resolved,
         * sampleable texture surface (`UnbindAsRenderTarget` StretchRects into it), never from the
         * multisampled surface, which `GetRenderTargetData` cannot accept. Before this override
         * existed the shared layer converted its own zero-initialized scratch buffer for the
         * caller, i.e. a fabricated transparent-black frame.
         *
         * The system-memory surface is created and released inside this call, so repeated readbacks
         * hold no extra resources. Storage is D3DFMT_A8B8G8R8, whose byte order is already R,G,B,A,
         * so no swizzle applies; rows are top-first, so no flip applies.
         *
         * @param level      Mip level; this backend allocates a single level (see this file's own
         *                   header note on mip-chain generation).
         * @param x          Left edge of the requested rectangle, in pixels.
         * @param y          Top edge of the requested rectangle, in pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false if this target's
         *         D3DPOOL_DEFAULT resources are currently released (device lost) or D3D9 could not
         *         complete the copy, leaving @p data untouched.
         * @throws System::NotSupportedException if @p level is above 0.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the target, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && hasDepth_;
        }

        /// Real sampleable IDirect3DTexture9 -- the color texture itself when single-sample, or the
        /// post-StretchRect-resolve copy when MSAA (NOXNA, tests/diagnostics/future shader binding).
        [[nodiscard]] IDirect3DTexture9* GetTextureEXT() const { return colorTexture_.Get(); }
        /// Real level-0 IDirect3DSurface9 of GetTextureEXT() (NOXNA).
        [[nodiscard]] IDirect3DSurface9* GetColorSurfaceEXT() const { return colorSurface_.Get(); }
        /// D9-54: the surface actually bound as the render target (the offscreen MSAA surface when
        /// MSAA, else the same as GetColorSurfaceEXT()) -- what an MRT bind should pass to
        /// SetRenderTarget(), same selection BindAsRenderTarget() itself already makes (NOXNA).
        [[nodiscard]] IDirect3DSurface9* GetActiveColorSurfaceEXT() const
        {
            return (appliedMultiSampleCount_ > 1 ? msaaSurface_ : colorSurface_).Get();
        }
        /// Real depth-stencil surface, or null if no depth format was requested (NOXNA).
        [[nodiscard]] IDirect3DSurface9* GetDepthStencilSurfaceEXT() const { return depthStencilSurface_.Get(); }

        /// D9-40/D9-53: releases the D3DPOOL_DEFAULT color texture/MSAA surface/depth-stencil
        /// surface before a device Reset(). Lazily recreated on the next BindAsRenderTarget() call.
        void ReleaseDefaultPoolResourceEXT() override;

        /// REMED-GFX-092: resolves an outgoing MSAA target without changing the currently-bound
        /// native target pair.  The owner uses this before its checked/rollback-capable target
        /// transition, so a failed switch can restore the still-valid prior binding.
        void ResolveForTransitionEXT();

        /// D9-54: recreates the D3DPOOL_DEFAULT resources if a prior device-lost recovery released
        /// them, without also binding this target -- an MRT bind (D3D9GraphicsBackend::
        /// SetRenderTargets()) needs each target's surfaces ready before it builds its own
        /// SetRenderTarget() call sequence, unlike a single-target bind which can just call
        /// BindAsRenderTarget() directly (NOXNA).
        void EnsureReadyEXT() { if (!colorTexture_) Recreate(); }

    private:
        void Recreate();

        D3D9GraphicsBackend* owner_ = nullptr;
        ComPtr<IDirect3DDevice9> device_;

        ComPtr<IDirect3DTexture9> colorTexture_;
        ComPtr<IDirect3DSurface9> colorSurface_;
        /// Offscreen MSAA color surface actually bound while rendering; null when not MSAA.
        ComPtr<IDirect3DSurface9> msaaSurface_;
        ComPtr<IDirect3DSurface9> depthStencilSurface_;

        int width_ = 0;
        int height_ = 0;
        int depthFormatOrdinal_ = 0;
        bool hasDepth_ = false;
        int requestedMultiSampleCount_ = 0;
        int appliedMultiSampleCount_ = 0;
    };

    /// Real D3D9 cube-map render target (D9-53). MSAA is deliberately not supported (matches
    /// D3D11RenderTargetCubeBackend's own precedent); a single depth-stencil surface is shared
    /// across all 6 faces, rebound on each BindAsRenderTargetFace() call (only one face renders at
    /// a time).
    class D3D9RenderTargetCubeBackend final : public IRenderTargetCubeBackend, public ID3D9DefaultPoolResourceEXT
    {
    public:
        D3D9RenderTargetCubeBackend(D3D9GraphicsBackend& owner, IDirect3DDevice9* device,
                                    int size, int depthFormat);
        ~D3D9RenderTargetCubeBackend() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }

        /// Real IDirect3DCubeTexture9 (NOXNA).
        [[nodiscard]] IDirect3DCubeTexture9* GetTextureEXT() const { return texture_.Get(); }

        void ReleaseDefaultPoolResourceEXT() override;

    private:
        void Recreate();

        D3D9GraphicsBackend* owner_ = nullptr;
        ComPtr<IDirect3DDevice9> device_;

        ComPtr<IDirect3DCubeTexture9> texture_;
        ComPtr<IDirect3DSurface9> depthStencilSurface_;

        int size_ = 0;
        int depthFormatOrdinal_ = 0;
        bool hasDepth_ = false;
        int activeFace_ = -1;
    };
}
