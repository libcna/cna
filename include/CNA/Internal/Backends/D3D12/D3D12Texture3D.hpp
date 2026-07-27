#pragma once

// plan_dx.md Phase DX13 (DX-122): real D3D12 3D texture backend -- mirrors D3D11Texture3DBackend's
// (D3D11's own DX-42) XNA-level behavior contract, RGBA8 storage only (matches this project's own
// established simplification, D3D11TextureBackend.hpp's own header comment applies identically
// here). Same explicit upload-heap-staging discipline D3D12TextureBackend (DX-109) already
// established, generalized to a real sub-volume (x,y,z,w,h,depth) upload/readback instead of
// D3D12TextureBackend's simpler always-full-level 2D case -- D3D12_TEXTURE_DIMENSION_TEXTURE3D has
// no array dimension, so (unlike D3D12TextureBackend's own array-size-1 simplification note) the
// subresource-index formula is unconditionally just the mip level itself, no special-casing needed.
//
// Explicitly triaged out of DX-109's original pass ("prioritize buffers + 2D textures first, they're
// DX-111's actual prerequisite") -- this is the real, scoped follow-up.

#include "../Common/IGraphicsBackend.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    class D3D12GraphicsBackend;

    class D3D12Texture3DBackend final : public ITexture3DBackend
    {
    public:
        D3D12Texture3DBackend(D3D12GraphicsBackend* backend, int w, int h, int depth, bool mipMap, int surfaceFormat);

        /// REMED-GFX-135: same explicit completion contract as D3D12TextureCubeBackend::SetData,
        /// applied to the placed footprint's row pitch and slice pitch.
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: same explicit completion contract as D3D12TextureCubeBackend::GetData,
        /// applied to the placed footprint's RowPitch and slice pitch.
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetWidthEXT() const { return width_; }
        [[nodiscard]] int GetHeightEXT() const { return height_; }
        [[nodiscard]] int GetDepthEXT() const { return depth_; }
        /// Raw GPU-resident ID3D12Resource* (NOXNA -- future draw-path/readback-test consumers).
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const { return texture_.Get(); }
        /// Shader-visible-heap GPU handle for this texture's SRV (NOXNA -- SetGraphicsRootDescriptorTable).
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const { return srvGpuHandle_; }

    private:
        void TransitionToShaderReadableEXT();

        D3D12GraphicsBackend* backend_ = nullptr;
        ComPtr<ID3D12Resource> texture_;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };
}
