#pragma once

// plan_dx.md Phase DX12 (DX-111, closing env_map3d): real D3D12 cube-map texture backend, RGBA8
// storage only (matches this project's own established simplification -- D3D12Textures.hpp's own
// header comment applies identically here). Same explicit upload-heap-staging discipline as
// D3D12Textures.hpp/.cpp's D3D12TextureBackend::UploadRegion(), just parameterized per face: a
// fresh UPLOAD-heap staging BUFFER per SetData() call, CopyTextureRegion into the face's own
// subresource, and D3D12ResourceStateTracker (DX-106) driving the
// COPY_DEST -> {PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE} transition.
//
// d3dx12.h's D3D12CalcSubresource() is not present in this project's MinGW-w64 D3D12 headers
// (DX-100's own spike finding) -- for this backend's single-plane, ArraySize=6 texture, the general
// formula (MipSlice + ArraySlice*MipLevels + PlaneSlice*MipLevels*ArraySize, PlaneSlice=0) collapses
// to `level + face*mipLevels_`, computed directly.
//
// Mirrors D3D11TextureCubeBackend's (DX-41) own XNA-level behavior contract (SetData face/level/
// sub-rect semantics, 6-face D3D11_RESOURCE_MISC_TEXTURECUBE-equivalent layout) -- GetData() is left
// at ITextureCubeBackend's own default no-op (real readback was not needed by DX-111's own env_map3d
// pixel-readback proof, which reads the render-target back, not the cube texture itself; a genuine,
// honest scope gap versus D3D11's real GetData(), not silently claimed equivalent).

#include "../Common/IGraphicsBackend.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    class D3D12GraphicsBackend;

    /// Real D3D12 cube-map texture backend (DX-111/env_map3d). Constructed with no initial pixel
    /// data (matches D3D11TextureCubeBackend's own constructor shape) -- ends construction in a
    /// real, deliberate shader-readable resting state (empty/undefined content), then each SetData()
    /// call uploads one face's sub-rectangle for real via an upload-heap staging buffer.
    class D3D12TextureCubeBackend final : public ITextureCubeBackend
    {
    public:
        D3D12TextureCubeBackend(D3D12GraphicsBackend* backend, int size, bool mipMap, int surfaceFormat);

        void SetData(int face, int level, int x, int y, int w, int h,
                     const void* data, int dataLength) override;

        [[nodiscard]] int GetSizeEXT() const { return size_; }
        [[nodiscard]] int GetMipLevelsEXT() const { return mipLevels_; }
        /// Raw GPU-resident ID3D12Resource* (NOXNA diagnostics).
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const { return texture_.Get(); }
        /// Shader-visible-heap GPU handle for this cube's SRV (NOXNA -- SetGraphicsRootDescriptorTable).
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const { return srvGpuHandle_; }

    private:
        void TransitionToShaderReadableEXT();

        D3D12GraphicsBackend* backend_ = nullptr;
        ComPtr<ID3D12Resource> texture_;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle_{};
        int size_ = 0;
        int mipLevels_ = 1;
    };
}
