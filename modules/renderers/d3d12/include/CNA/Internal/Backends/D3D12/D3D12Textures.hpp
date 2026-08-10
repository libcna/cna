#pragma once

// plan_dx.md Phase DX12 (DX-109): real D3D12 2D texture backend, RGBA8 storage only (matches this
// project's own established simplification -- D3D11TextureBackend.hpp's own header comment applies
// identically here). Same explicit upload-heap-staging discipline as D3D12Buffers.hpp/.cpp:
// CreateCommittedResource on a DEFAULT heap for the GPU-resident texture, a fresh UPLOAD-heap
// staging BUFFER per upload (D3D12 requires texture-copy sources to be laid out as a row-pitch-
// aligned buffer -- D3D12_TEXTURE_DATA_PITCH_ALIGNMENT, 256 bytes -- not a TEXTURE2D resource),
// CopyTextureRegion, and D3D12ResourceStateTracker (DX-106) driving the
// COPY_DEST -> {PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE} transition.
//
// d3dx12.h (Microsoft's optional helper header, which normally provides D3D12CalcSubresource()) is
// NOT present in this project's MinGW-w64 D3D12 headers (DX-100's own spike finding) -- subresource
// indices are computed directly instead of via that helper. For this backend's array-size-1,
// single-plane textures the formula collapses to the mip level itself
// (MipSlice + ArraySlice*MipLevels + PlaneSlice*MipLevels*ArraySize, with ArraySlice=PlaneSlice=0),
// so no general-purpose helper is needed.
//
// Cube/3D texture variants (D3D11's own DX-41/DX-42 equivalents) are deliberately NOT implemented
// in this pass -- DX-109's own plan row explicitly allows triaging 2D textures + buffers first
// since they're DX-111's actual prerequisite; see plan_dx.md's DX-109 row for the honest scope note.

#include "../Common/IGraphicsBackend.hpp"
#include "D3D12DescriptorHeaps.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    class D3D12GraphicsBackend;

    /// Real D3D12 2D texture backend (DX-109). Level 0 is uploaded at construction time from
    /// ImageData::pixels (if non-empty); further mip levels are left undefined until the caller
    /// uploads them via UpdatePixelsLevel(), matching D3D11TextureBackend's own convention. A
    /// texture with no initial pixels still ends construction in a shader-readable resting state
    /// (a real, deliberate transition, not left at CreateCommittedResource's own COPY_DEST initial
    /// state), so any caller can rely on "this texture is always shader-readable after
    /// construction" without checking upload history.
    class D3D12TextureBackend final : public ITextureBackend
    {
    public:
        D3D12TextureBackend(D3D12GraphicsBackend* backend, const ImageData& data);
        /// REMED-GFX-177: returns this texture's SRV slot to the shader-visible allocator, which
        /// reissues it once the GPU has passed the fence value current at destruction. Before this,
        /// the slot was consumed for the lifetime of the process.
        ~D3D12TextureBackend() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /// Real mip level count this texture was allocated with (NOXNA diagnostics).
        [[nodiscard]] int GetMipLevelsEXT() const { return mipLevels_; }
        /// Raw GPU-resident ID3D12Resource* (NOXNA -- Phase DX-111's draw path / readback tests).
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const { return texture_.Get(); }
        /// Staging-heap CPU handle for this texture's SRV (NOXNA -- where the descriptor itself
        /// lives; REMED-GFX-177 keeps the authoritative copy in a non-shader-visible heap).
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceViewCpuHandleEXT() const
        {
            return heaps_ ? heaps_->cbvSrvUav.StagingCpuHandle(srvIndex_) : D3D12_CPU_DESCRIPTOR_HANDLE{};
        }
        /// Shader-visible-heap GPU handle for this texture's SRV (NOXNA -- SetGraphicsRootDescriptorTable).
        /// REMED-GFX-177: resolved on every call against whichever heap is current, never cached.
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const
        {
            return heaps_ ? heaps_->cbvSrvUav.GpuHandle(srvIndex_) : D3D12_GPU_DESCRIPTOR_HANDLE{};
        }
        /// REMED-GFX-177: the stable shader-visible-heap slot index this texture owns (NOXNA).
        [[nodiscard]] std::uint32_t GetShaderResourceViewIndexEXT() const { return srvIndex_; }

    private:
        void UploadRegion(int level, const uint8_t* rgba, int levelW, int levelH, int sourceStrideBytes);
        void TransitionToShaderReadableEXT();

        D3D12GraphicsBackend* backend_ = nullptr;
        ComPtr<ID3D12Resource> texture_;
        /// Kept alive independently of backend_ so the destructor can always free the slot.
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;
        std::uint32_t srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
    };
}
