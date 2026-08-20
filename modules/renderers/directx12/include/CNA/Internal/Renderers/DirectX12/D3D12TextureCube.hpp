#pragma once

// plans/plan_dx.md Phase DX12 (DX-111, closing env_map3d): real D3D12 cube-map texture renderer, RGBA8
// storage only (matches this project's own established simplification -- D3D12Textures.hpp's own
// header comment applies identically here). Same explicit upload-heap-staging discipline as
// D3D12Textures.hpp/.cpp's D3D12TextureRenderer::UploadRegion(), just parameterized per face: a
// fresh UPLOAD-heap staging BUFFER per SetData() call, CopyTextureRegion into the face's own
// subresource, and D3D12ResourceStateTracker (DX-106) driving the
// COPY_DEST -> {PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE} transition.
//
// d3dx12.h's D3D12CalcSubresource() is not present in this project's MinGW-w64 D3D12 headers
// (DX-100's own spike finding) -- for this renderer's single-plane, ArraySize=6 texture, the general
// formula (MipSlice + ArraySlice*MipLevels + PlaneSlice*MipLevels*ArraySize, PlaneSlice=0) collapses
// to `level + face*mipLevels_`, computed directly.
//
// Mirrors D3D11TextureCubeRenderer's (DX-41) own XNA-level behavior contract (SetData face/level/
// sub-rect semantics, 6-face D3D11_RESOURCE_MISC_TEXTURECUBE-equivalent layout). GetData() (DX-123)
// mirrors D3D11TextureCubeRenderer's own real readback -- a D3D12_HEAP_TYPE_READBACK buffer +
// CopyTextureRegion (with a real D3D12_BOX for the requested sub-rectangle) + Map, the same
// discipline D3D12Texture3DRenderer::GetData already established, generalized with this file's own
// face-aware subresource-index formula.

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

    /// Real D3D12 cube-map texture renderer (DX-111/env_map3d). Constructed with no initial pixel
    /// data (matches D3D11TextureCubeRenderer's own constructor shape) -- ends construction in a
    /// real, deliberate shader-readable resting state (empty/undefined content), then each SetData()
    /// call uploads one face's sub-rectangle for real via an upload-heap staging buffer.
    class D3D12TextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        D3D12TextureCubeRenderer(DirectX12Renderer* renderer, int size, bool mipMap, int surfaceFormat);
        /// REMED-GFX-177: returns this cube's SRV slot to the shader-visible allocator.
        ~D3D12TextureCubeRenderer() override;

        /// REMED-GFX-135: true only once the staged copy has been submitted and waited on for the
        /// whole requested face rectangle; false for an out-of-range face/level/rectangle, a null
        /// source or a source buffer too small for the region. A failed D3D12 resource
        /// creation/Map/Close still throws -- that is a broken device, not an unsupported request.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /// REMED-GFX-130: true only once the READBACK-heap copy has completed and the whole
        /// requested face rectangle has been unpacked from it; false for an out-of-range
        /// face/level or a failed resource creation/command-list close/Map, so the shared layer
        /// rejects the read instead of fabricating a transparent-black face.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }
        [[nodiscard]] int GetMipLevelsEXT() const { return mipLevels_; }
        /// Raw GPU-resident ID3D12Resource* (CNAEXT diagnostics).
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const { return texture_.Get(); }
        /// Shader-visible-heap GPU handle for this cube's SRV (CNAEXT -- SetGraphicsRootDescriptorTable).
        /// REMED-GFX-177: resolved on every call against whichever heap is current, never cached.
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const
        {
            return heaps_ ? heaps_->cbvSrvUav.GpuHandle(srvIndex_) : D3D12_GPU_DESCRIPTOR_HANDLE{};
        }
        /// REMED-GFX-177: the stable shader-visible-heap slot index this cube owns (CNAEXT).
        [[nodiscard]] std::uint32_t GetShaderResourceViewIndexEXT() const { return srvIndex_; }

    private:
        void TransitionToShaderReadableEXT();

        DirectX12Renderer* renderer_ = nullptr;
        ComPtr<ID3D12Resource> texture_;
        /// Kept alive independently of renderer_ so the destructor can always free the slot.
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;
        std::uint32_t srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        int size_ = 0;
        int mipLevels_ = 1;
    };
}
