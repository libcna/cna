#pragma once

// plans/plan_dx9.md Phase D9-5 (D9-50/D9-51): real D3D9 texture renderers.
//
// Texture2D uses the requested classic XNA SurfaceFormat through D3D9FormatMapping. Volume and
// cube textures remain RGBA8-only for now; their `surfaceFormat` parameter is accepted for
// interface-signature compatibility only and their independent capability classifiers keep that
// limitation visible to callers.
//
// D3DPOOL_MANAGED (not DEFAULT) for all three -- design decision 2's own payoff, confirmed for
// real by D9-4's spike: MANAGED textures survive Reset() with no re-upload needed and are
// directly LockRect/LockBox-readable, so none of these register with the
// ID3D9DefaultPoolResourceEXT registry (D3D9Buffers.hpp's dynamic-buffer/D3DPOOL_DEFAULT
// recovery path) the way a render target (D9-53, D3DUSAGE_RENDERTARGET + D3DPOOL_DEFAULT) will.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d9.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    /// Real D3D9 2D texture renderer (D9-50). Level 0 is uploaded at construction time from
    /// ImageData::pixels; further mip levels (when ImageData::mipLevels > 1) are left undefined
    /// until the caller uploads them via UpdatePixelsLevel().
    class D3D9TextureRenderer final : public ITextureRenderer
    {
    public:
        D3D9TextureRenderer(IDirect3DDevice9* device, const ImageData& data);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /// Real mip level count this texture was allocated with (CNAEXT diagnostics).
        [[nodiscard]] int GetMipLevelsEXT() const { return mipLevels_; }
        /// Raw IDirect3DTexture9* for tests/diagnostics -- since this is D3DPOOL_MANAGED, tests
        /// may LockRect it directly for readback (D9-4's own confirmed payoff), no staging
        /// texture dance needed (CNAEXT).
        [[nodiscard]] IDirect3DTexture9* GetTextureEXT() const { return texture_.Get(); }

    private:
        ComPtr<IDirect3DDevice9> device_;
        ComPtr<IDirect3DTexture9> texture_;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        int surfaceFormat_ = 0;
    };

    /// Real D3D9 cube-map texture renderer (D9-51). Face order (0..5) is D3D9's own native
    /// D3DCUBEMAP_FACES enum order (+X,-X,+Y,-Y,+Z,-Z), the same convention
    /// IRenderTargetCubeRenderer::BindAsRenderTargetFace() already documents, so a raw
    /// `static_cast<D3DCUBEMAP_FACES>(face)` is always correct here.
    class D3D9TextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        D3D9TextureCubeRenderer(IDirect3DDevice9* device, int size, bool mipMap, int surfaceFormat);

        /// True only once the whole requested face rectangle has been copied into
        /// the locked surface and unlocked; false for an out-of-range face/level/rectangle, a null
        /// source or a source buffer too small for the region. A failed LockRect still throws --
        /// that is a broken device, not an unsupported request. LockRect/UnlockRect copy out of the
        /// caller's memory inside this call, so the source is never retained past it.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /// True only once the whole requested face rectangle has been copied out of
        /// the locked surface; false for an out-of-range face/level or a failed LockRect, so the
        /// shared layer rejects the read instead of fabricating a transparent-black face.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }
        [[nodiscard]] int GetMipLevelsEXT() const { return mipLevels_; }
        [[nodiscard]] IDirect3DCubeTexture9* GetTextureEXT() const { return texture_.Get(); }

    private:
        ComPtr<IDirect3DDevice9> device_;
        ComPtr<IDirect3DCubeTexture9> texture_;
        int size_ = 0;
        int mipLevels_ = 1;
    };

    /// Real D3D9 volume (3D) texture renderer (D9-51). D9-51's own plan note: volume-texture
    /// support is a genuine D3DCAPS9 capability (queried via D3DCAPS9::MaxVolumeExtent by the
    /// owning DirectX9Renderer::CreateTexture3D() before constructing one of these), not
    /// assumed universal the way 2D/cube textures are.
    class D3D9Texture3DRenderer final : public ITexture3DRenderer
    {
    public:
        D3D9Texture3DRenderer(IDirect3DDevice9* device, int w, int h, int depth, bool mipMap, int surfaceFormat);

        /// Same explicit completion contract as D3D9TextureCubeRenderer::SetData,
        /// applied to LockBox's row pitch and slice pitch.
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// Same explicit completion contract as D3D9TextureCubeRenderer::GetData,
        /// applied to LockBox's row pitch and slice pitch.
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetWidthEXT() const { return width_; }
        [[nodiscard]] int GetHeightEXT() const { return height_; }
        [[nodiscard]] int GetDepthEXT() const { return depth_; }
        [[nodiscard]] IDirect3DVolumeTexture9* GetTextureEXT() const { return texture_.Get(); }

    private:
        ComPtr<IDirect3DDevice9> device_;
        ComPtr<IDirect3DVolumeTexture9> texture_;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };
}
