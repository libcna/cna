#pragma once

// DX9 Phase D9-5 (D9-53): real D3D9 offscreen render-target renderers.
//
// Both classes hold a non-owning DirectX9Renderer* (owner_) -- a render target's Unbind must be
// able to restore the owner's default back-buffer/depth-stencil surfaces, which only the owning
// renderer instance knows how to do.
//
// D3DPOOL_DEFAULT (not MANAGED, unlike D3D9Textures.hpp's plain textures) -- D3DUSAGE_RENDERTARGET
// resources do not survive Reset(), so both classes implement ID3D9DefaultPoolResourceEXT and
// register with the D9-40 device-lost registry. Unlike a dynamic vertex/index buffer (whose "next
// use" is the caller's own SetData()), a render target's natural "next use" is
// BindAsRenderTarget()/BindAsRenderTargetFace() -- both lazily recreate the underlying D3D9
// resources there if a prior device-lost recovery released them (ReleaseDefaultPoolResourceEXT()).
// Real XNA/D3D9 behavior either way: RenderTarget2D content does not survive a device Reset unless
// RenderTargetUsage.PreserveContents (not implemented by this project on any renderer yet).
//
// RGBA8 storage only (D3DFMT_A8B8G8R8) -- same simplification D3D9Textures.hpp already uses. MSAA
// (2D only, D9-53's own note) is clamped to what the device's real
// IDirect3D9::CheckDeviceMultiSampleType() reports (DirectX9Renderer::ClampMultiSampleCountEXT()),
// never assumed -- an unsupported sample count silently falls back to 0 (no MSAA), an
// all-or-nothing clamp (no step-down ladder). An MSAA target is bound as a separate offscreen
// D3DMULTISAMPLE color surface (D3D9 textures cannot themselves be multisampled) and resolved into
// the sampleable D3DPOOL_DEFAULT texture via StretchRect on unbind. D3D9RenderTargetCubeRenderer
// deliberately does NOT support MSAA.
//
// A `mipMap` target allocates its full chain and regenerates it on unbind by StretchRect-ing each
// level into the next (D3D9's own render-target downsample; autogen textures would hide the
// sublevels from GetSurfaceLevel and so from GetData).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "D3D9DefaultPoolResourceEXT.hpp"

#include <d3d9.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    class DirectX9Renderer;

    /// Real D3D9 2D render target (D9-53).
    class D3D9RenderTargetRenderer final : public IRenderTargetRenderer, public ID3D9DefaultPoolResourceEXT
    {
    public:
        D3D9RenderTargetRenderer(DirectX9Renderer& owner, IDirect3DDevice9* device,
                                int w, int h, int depthFormat, bool mipMap, int multiSampleCount);
        ~D3D9RenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * D3D9 has a real, fully synchronous readback for a D3DPOOL_DEFAULT render
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
         * @param level      Mip level; every level of a `mipMap` target is readable once the
         *                   target has been unbound (which regenerates the chain).
         * @param x          Left edge of the requested rectangle, in pixels.
         * @param y          Top edge of the requested rectangle, in pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false if this target's
         *         D3DPOOL_DEFAULT resources are currently released (device lost) or D3D9 could not
         *         complete the copy, leaving @p data untouched.
         * @throws System::NotSupportedException if @p level is beyond the allocated chain.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the level, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Replaces level 0 with caller-supplied RGBA8 texels (a RenderTarget2D is a
         *        Texture2D, so `SetData` on one is legal XNA).
         *
         * Goes through a system-memory surface and `UpdateSurface`, the mirror image of
         * `GetData`'s `GetRenderTargetData` readback. A multisampled target's offscreen surface is
         * seeded too, so a draw that follows composes over the upload rather than resolving stale
         * samples over it.
         *
         * @param data   Tightly packed or @p stride-pitched RGBA8 rows, top row first.
         * @param stride Source row pitch in bytes; a non-positive value means tightly packed.
         * @throws std::invalid_argument if @p stride is smaller than one row.
         * @throws std::runtime_error if D3D9 refuses the copy.
         */
        void UpdatePixels(const uint8_t* data, int stride) override;

        /**
         * @brief Replaces one allocated mip level with tightly packed RGBA8 texels.
         * @param level  Mip level to write.
         * @param data   @p levelW x @p levelH RGBA8 texels, top row first.
         * @param levelW Width of @p level.
         * @param levelH Height of @p level.
         * @throws System::NotSupportedException if @p level is beyond the allocated chain.
         * @throws std::invalid_argument if the dimensions do not match the level.
         */
        void UpdatePixelsLevel(int level, const uint8_t* data, int levelW, int levelH) override;

        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && hasDepth_;
        }

        /// Real sampleable IDirect3DTexture9 -- the color texture itself when single-sample, or the
        /// post-StretchRect-resolve copy when MSAA (CNAEXT, tests/diagnostics/future shader binding).
        [[nodiscard]] IDirect3DTexture9* GetTextureEXT() const { return colorTexture_.Get(); }
        /// Real level-0 IDirect3DSurface9 of GetTextureEXT() (CNAEXT).
        [[nodiscard]] IDirect3DSurface9* GetColorSurfaceEXT() const { return colorSurface_.Get(); }
        /// D9-54: the surface actually bound as the render target (the offscreen MSAA surface when
        /// MSAA, else the same as GetColorSurfaceEXT()) -- what an MRT bind should pass to
        /// SetRenderTarget(), same selection BindAsRenderTarget() itself already makes (CNAEXT).
        [[nodiscard]] IDirect3DSurface9* GetActiveColorSurfaceEXT() const
        {
            return (appliedMultiSampleCount_ > 1 ? msaaSurface_ : colorSurface_).Get();
        }
        /// Real depth-stencil surface, or null if no depth format was requested (CNAEXT).
        [[nodiscard]] IDirect3DSurface9* GetDepthStencilSurfaceEXT() const { return depthStencilSurface_.Get(); }
        /** @brief Returns the XNA depth format used for this target's depth-stencil surface. */
        [[nodiscard]] int GetDepthStencilFormatEXT() const { return depthFormatOrdinal_; }

        /// D9-40/D9-53: releases the D3DPOOL_DEFAULT color texture/MSAA surface/depth-stencil
        /// surface before a device Reset(). Lazily recreated on the next BindAsRenderTarget() call.
        void ReleaseDefaultPoolResourceEXT() override;

        /// Finishes an outgoing target without changing the currently-bound native target pair:
        /// resolves an MSAA target into its texture, then regenerates a `mipMap` target's chain.
        /// The owner uses this before its checked/rollback-capable target transition, so a failed
        /// switch can restore the still-valid prior binding.
        void ResolveForTransitionEXT();

        /// Downsamples level 0 into every further level of a `mipMap` target (StretchRect per
        /// level); a no-op for a single-level target. Part of every transition away from the
        /// target, after the MSAA resolve.
        void RegenerateMipChainEXT();

        /// D9-54: recreates the D3DPOOL_DEFAULT resources if a prior device-lost recovery released
        /// them, without also binding this target -- an MRT bind (DirectX9Renderer::
        /// SetRenderTargets()) needs each target's surfaces ready before it builds its own
        /// SetRenderTarget() call sequence, unlike a single-target bind which can just call
        /// BindAsRenderTarget() directly (CNAEXT).
        void EnsureReadyEXT() { if (!colorTexture_) Recreate(); }

    private:
        void Recreate();
        void UploadLevel(int level, const uint8_t* data, int levelW, int levelH, int stride);

        DirectX9Renderer* owner_ = nullptr;
        ComPtr<IDirect3DDevice9> device_;

        ComPtr<IDirect3DTexture9> colorTexture_;
        ComPtr<IDirect3DSurface9> colorSurface_;
        /// Offscreen MSAA color surface actually bound while rendering; null when not MSAA.
        ComPtr<IDirect3DSurface9> msaaSurface_;
        ComPtr<IDirect3DSurface9> depthStencilSurface_;

        int width_ = 0;
        int height_ = 0;
        int levelCount_ = 1;
        int depthFormatOrdinal_ = 0;
        bool hasDepth_ = false;
        int requestedMultiSampleCount_ = 0;
        int appliedMultiSampleCount_ = 0;
    };

    /// Real D3D9 cube-map render target (D9-53). MSAA is deliberately not supported; a single
    /// depth-stencil surface is shared across all 6 faces, rebound on each BindAsRenderTargetFace()
    /// call (only one face renders at a time).
    class D3D9RenderTargetCubeRenderer final : public IRenderTargetCubeRenderer, public ID3D9DefaultPoolResourceEXT
    {
    public:
        D3D9RenderTargetCubeRenderer(DirectX9Renderer& owner, IDirect3DDevice9* device,
                                    int size, int depthFormat, bool mipMap);
        ~D3D9RenderTargetCubeRenderer() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        /// Finishes the face being rendered without changing the native binding: regenerates a
        /// `mipMap` cube's chain for that face. The owner calls it before every target transition.
        void ResolveForTransitionEXT();
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }
        /** @brief Returns the XNA depth format used for this target's depth-stencil surface. */
        [[nodiscard]] int GetDepthStencilFormatEXT() const { return depthFormatOrdinal_; }

        /// Real IDirect3DCubeTexture9 (CNAEXT).
        [[nodiscard]] IDirect3DCubeTexture9* GetTextureEXT() const { return texture_.Get(); }

        /**
         * @brief Reads a RENDERED cube face back to the CPU.
         *
         * `D3D9RenderTargetRenderer::GetData`'s mechanism, applied to one cube face:
         * a `D3DUSAGE_RENDERTARGET` surface in `D3DPOOL_DEFAULT` cannot be locked, so the face's
         * surface is copied into a `D3DPOOL_SYSTEMMEM` offscreen-plain surface with
         * `GetRenderTargetData` (which also synchronises with the GPU) and that copy is locked.
         * Not `LockRect` on the cube texture itself, which is what the plain
         * `D3D9TextureCubeRenderer` -- a `D3DPOOL_MANAGED` resource -- can do.
         *
         * This target allocates exactly one mip level and no multisampling (see `Recreate` and
         * `GetMultiSampleCount`), so there is no resolve step and level > 0 is refused.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read; only 0 exists on this target.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True once the whole region was written; false for an out-of-range
         *         face/level/region or a failed surface copy/lock.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void ReleaseDefaultPoolResourceEXT() override;

    private:
        void Recreate();

        DirectX9Renderer* owner_ = nullptr;
        ComPtr<IDirect3DDevice9> device_;

        ComPtr<IDirect3DCubeTexture9> texture_;
        ComPtr<IDirect3DSurface9> depthStencilSurface_;

        /// Downsamples one face's level 0 into its further levels; a no-op for a single level.
        void RegenerateFaceMipChain(int face);

        int size_ = 0;
        int levelCount_ = 1;
        int depthFormatOrdinal_ = 0;
        bool hasDepth_ = false;
        int activeFace_ = -1;
    };
}
