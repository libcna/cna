#pragma once

// plans/plan_dx.md Phase DX13 (DX-122): real D3D12 3D texture renderer -- mirrors D3D11Texture3DRenderer's
// (D3D11's own DX-42) XNA-level behavior contract, RGBA8 storage only (matches this project's own
// established simplification, D3D11TextureRenderer.hpp's own header comment applies identically
// here). Same explicit upload-heap-staging discipline D3D12TextureRenderer (DX-109) already
// established, generalized to a real sub-volume (x,y,z,w,h,depth) upload/readback instead of
// D3D12TextureRenderer's simpler always-full-level 2D case -- D3D12_TEXTURE_DIMENSION_TEXTURE3D has
// no array dimension, so (unlike D3D12TextureRenderer's own array-size-1 simplification note) the
// subresource-index formula is unconditionally just the mip level itself, no special-casing needed.
//
// Explicitly triaged out of DX-109's original pass ("prioritize buffers + 2D textures first, they're
// DX-111's actual prerequisite") -- this is the real, scoped follow-up.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "D3D12DescriptorHeaps.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    class D3D12Texture3DRenderer final : public ITexture3DRenderer
    {
    public:
        D3D12Texture3DRenderer(DirectX12Renderer* renderer, int w, int h, int depth, bool mipMap, int surfaceFormat);
        /// REMED-GFX-177: returns this volume's SRV slot to the shader-visible allocator.
        ~D3D12Texture3DRenderer() override;

        /// REMED-GFX-135: same explicit completion contract as D3D12TextureCubeRenderer::SetData,
        /// applied to the placed footprint's row pitch and slice pitch.
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// REMED-GFX-130: same explicit completion contract as D3D12TextureCubeRenderer::GetData,
        /// applied to the placed footprint's RowPitch and slice pitch.
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetWidthEXT() const { return width_; }
        [[nodiscard]] int GetHeightEXT() const { return height_; }
        [[nodiscard]] int GetDepthEXT() const { return depth_; }
        /// Raw GPU-resident ID3D12Resource* (CNAEXT -- future draw-path/readback-test consumers).
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const { return texture_.Get(); }
        /// Shader-visible-heap GPU handle for this texture's SRV (CNAEXT -- SetGraphicsRootDescriptorTable).
        /// REMED-GFX-177: resolved on every call against whichever heap is current, never cached.
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const
        {
            return heaps_ ? heaps_->cbvSrvUav.GpuHandle(srvIndex_) : D3D12_GPU_DESCRIPTOR_HANDLE{};
        }
        /// REMED-GFX-177: the stable shader-visible-heap slot index this volume owns (CNAEXT).
        [[nodiscard]] std::uint32_t GetShaderResourceViewIndexEXT() const { return srvIndex_; }

    private:
        void TransitionToShaderReadableEXT();

        DirectX12Renderer* renderer_ = nullptr;
        ComPtr<ID3D12Resource> texture_;
        /// Kept alive independently of renderer_ so the destructor can always free the slot.
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;
        std::uint32_t srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };
}
