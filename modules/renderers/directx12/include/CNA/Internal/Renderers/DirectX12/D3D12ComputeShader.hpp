// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DProgramReflection.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"

#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers::DirectX12
{
    /** @brief Native HLSL compute program recorded into the renderer's frame command list. */
    class D3D12ComputeShader final : public IComputeShaderRenderer,
                                     public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        /** @brief Maximum number of reflected HLSL constant-buffer registers. */
        static constexpr int kConstantSlots = D3DCommon::D3DProgramReflection::kMaxConstantBuffers;
        /** @brief Maximum number of sampled HLSL texture registers. */
        static constexpr int kTextureSlots = D3DCommon::D3DProgramReflection::kMaxShaderResources;
        /** @brief Number of native raw storage UAV registers. */
        static constexpr int kStorageSlots = 8;

        /**
         * @brief Creates an uncompiled program for one D3D12 device.
         * @param renderer Owning renderer.
         */
        explicit D3D12ComputeShader(DirectX12Renderer* renderer);
        /** @brief Releases native objects and unregisters device recovery. */
        ~D3D12ComputeShader() override;

        /**
         * @brief Compiles HLSL entry point main and creates a compute PSO.
         * @param source HLSL source text.
         * @return True when the shader and root signature are usable.
         */
        bool CompileProgram(const std::string& source) override;
        /** @brief Validates that the program remains bound to a live renderer. */
        void Bind() override;
        /**
         * @brief Sets a reflected integer uniform.
         * @param name HLSL parameter name.
         * @param value New value.
         */
        void SetUniformInt(const char* name, int value) override;
        /**
         * @brief Sets a reflected float uniform.
         * @param name HLSL parameter name.
         * @param value New value.
         */
        void SetUniformFloat(const char* name, float value) override;
        /**
         * @brief Retains a same-device raw UAV buffer at register u(binding).
         * @param binding UAV register.
         * @param buffer Storage buffer record, or null to clear.
         */
        void BindStorageBuffer(int binding, IStorageBufferRenderer* buffer) override;
        /**
         * @brief Retains a same-device constant buffer at register b(binding).
         * @param binding Constant-buffer register.
         * @param buffer Constant buffer record, or null to clear.
         * @return True when the binding was accepted.
         */
        [[nodiscard]] bool BindConstantBufferEXT(
            int binding, IStorageBufferRenderer* buffer) override;
        /**
         * @brief Retains a same-device sampled Texture2D or RenderTarget2D at register t(unit).
         * @param unit Texture register.
         * @param texture Texture record, or null to clear.
         */
        void BindTexture(int unit, ITextureRenderer* texture) override;
        /**
         * @brief Reports that HLSL texture registers are direct binding numbers.
         * @return True for this native HLSL implementation.
         */
        [[nodiscard]] bool UsesDirectSampledTextureBindingsEXT() const override { return true; }
        /**
         * @brief Reports whether compilation created a live native PSO.
         * @return True when the program has a root signature and PSO.
         */
        [[nodiscard]] bool IsValid() const override { return pso_ && rootSignature_; }
        /**
         * @brief Returns the native compiler or root-signature diagnostic.
         * @return Diagnostic text, or an empty string after successful compilation.
         */
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }
        /**
         * @brief Records one dispatch and its resource transitions without waiting for the GPU.
         * @param groupsX X work-group count.
         * @param groupsY Y work-group count.
         * @param groupsZ Z work-group count.
         */
        void Dispatch(int groupsX, int groupsY, int groupsZ);
        /**
         * @brief Returns the native device used to compile this program.
         * @return Current device, or null while device resources are released.
         */
        [[nodiscard]] ID3D12Device* GetDeviceEXT() const noexcept { return device_.Get(); }
        /**
         * @brief Returns the renderer that owns dispatch recording and resource states.
         * @return Live owning renderer.
         */
        [[nodiscard]] DirectX12Renderer* GetOwnerEXT() const { return renderer_.Get(); }
        /** @brief Releases the old device's PSO and root signature. */
        void ReleaseDeviceResourcesEXT() noexcept override;
        /** @brief Recreates the PSO from retained bytecode while preserving uniforms and bindings. */
        void RecreateDeviceResourcesEXT() override;

    private:
        bool CreateRootSignature();
        bool CreatePipelineState();

        D3D12RendererReference renderer_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
        Microsoft::WRL::ComPtr<ID3DBlob> bytecode_;
        D3DCommon::D3DProgramReflection reflection_;
        std::array<std::shared_ptr<IStorageBufferRenderer>, kStorageSlots> storageBuffers_{};
        std::array<std::shared_ptr<IStorageBufferRenderer>, kConstantSlots> constantBuffers_{};
        std::array<std::shared_ptr<ITextureRenderer>, kTextureSlots> textures_{};
        std::string compileError_;
    };
}
