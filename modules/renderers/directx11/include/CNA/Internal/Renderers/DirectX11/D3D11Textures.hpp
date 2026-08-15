#pragma once

// plan_dx.md Phase DIRECTX6 (DX-40/DX-41/DX-42): real D3D11 texture renderers.
//
// RGBA8 storage only (DXGI_FORMAT_R8G8B8A8_UNORM) -- matches this project's own established
// simplification: EasyGL/Vulkan/Software all treat every ITextureRenderer/ITextureCubeRenderer/
// ITexture3DRenderer as RGBA8 regardless of the XNA SurfaceFormat/`surfaceFormat` ordinal the
// caller passed (CreateTexture3D/CreateTextureCube's own `surfaceFormat` parameter is accepted
// for interface-signature compatibility but not yet honored by any renderer, this one included).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    /// Real D3D11 2D texture renderer (DX-40). Level 0 is uploaded at construction time from
    /// ImageData::pixels; further mip levels (when ImageData::mipLevels > 1) are left undefined
    /// until the caller uploads them via UpdatePixelsLevel(), matching Texture2D's own content-
    /// pipeline usage pattern (mirrors EasyGLTextureRenderer's identical level-0-then-later-levels
    /// convention).
    class D3D11TextureRenderer final : public ITextureRenderer
    {
    public:
        D3D11TextureRenderer(ID3D11Device* device, ID3D11DeviceContext* context, const ImageData& data);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /// Real mip level count this texture was allocated with (CNAEXT diagnostics).
        [[nodiscard]] int GetMipLevelsEXT() const { return mipLevels_; }
        /// Raw ID3D11Texture2D* for draw-call binding / readback tests (CNAEXT).
        [[nodiscard]] ID3D11Texture2D* GetTextureEXT() const { return texture_.Get(); }
        /// Raw SRV for Phase DIRECTX8's shader texture binding (CNAEXT).
        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const { return srv_.Get(); }

    private:
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11Texture2D> texture_;
        ComPtr<ID3D11ShaderResourceView> srv_;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
    };

    /// Real D3D11 cube-map texture renderer (DX-41). A single 6-slice ID3D11Texture2D array with
    /// D3D11_RESOURCE_MISC_TEXTURECUBE. Face order (0..5) is D3D11's own native cube-face array-
    /// slice order (+X,-X,+Y,-Y,+Z,-Z) -- the same convention IRenderTargetCubeRenderer's own
    /// BindAsRenderTargetFace() doc comment already documents, so face indices are consistent
    /// across the texture and render-target-cube variants.
    class D3D11TextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        D3D11TextureCubeRenderer(ID3D11Device* device, ID3D11DeviceContext* context,
                                int size, bool mipMap, int surfaceFormat);

        /// REMED-GFX-135: true only once UpdateSubresource has been issued for the whole
        /// requested face rectangle; false for an out-of-range face/level/rectangle, a null source
        /// or a source buffer too small for the region. UpdateSubresource copies out of the
        /// caller's memory before it returns, so the source is never retained past this call.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: true only once the whole requested face rectangle has been copied out of
        /// the STAGING mirror; false for an out-of-range face/level or a failed staging
        /// creation/Map, so the shared layer rejects the read instead of fabricating a face.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }
        [[nodiscard]] int GetMipLevelsEXT() const { return mipLevels_; }
        [[nodiscard]] ID3D11Texture2D* GetTextureEXT() const { return texture_.Get(); }
        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const { return srv_.Get(); }

    private:
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11Texture2D> texture_;
        ComPtr<ID3D11ShaderResourceView> srv_;
        int size_ = 0;
        int mipLevels_ = 1;
    };

    /// Real D3D11 volume (3D) texture renderer (DX-42).
    class D3D11Texture3DRenderer final : public ITexture3DRenderer
    {
    public:
        D3D11Texture3DRenderer(ID3D11Device* device, ID3D11DeviceContext* context,
                              int w, int h, int depth, bool mipMap, int surfaceFormat);

        /// REMED-GFX-135: same explicit completion contract as D3D11TextureCubeRenderer::SetData,
        /// applied to the box's row pitch and depth pitch.
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: same explicit completion contract as D3D11TextureCubeRenderer::GetData,
        /// applied to the staging texture's RowPitch and DepthPitch.
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetWidthEXT() const { return width_; }
        [[nodiscard]] int GetHeightEXT() const { return height_; }
        [[nodiscard]] int GetDepthEXT() const { return depth_; }
        [[nodiscard]] ID3D11Texture3D* GetTextureEXT() const { return texture_.Get(); }
        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const { return srv_.Get(); }

    private:
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11Texture3D> texture_;
        ComPtr<ID3D11ShaderResourceView> srv_;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };
}
