// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DProgramReflection.hpp"

#include <array>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <memory>
#include <string>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX11
{
    /** @brief Native D3D11 compute program with retained resource bindings. */
    class D3D11ComputeShader final : public IComputeShaderRenderer
    {
    public:
        /**
         * @brief Constructs a compute program for one device.
         * @param device Owning D3D11 device.
         * @param context Immediate context used for dispatches.
         */
        D3D11ComputeShader(ID3D11Device* device, ID3D11DeviceContext* context);

        /**
         * @brief Compiles HLSL entry point main as shader model 5 compute bytecode.
         * @param source HLSL source text.
         * @return True when compilation and native creation succeed.
         */
        bool CompileProgram(const std::string& source) override;
        /** @brief Binds the compiled compute shader to the immediate context. */
        void Bind() override;
        /**
         * @brief Updates a reflected integer uniform.
         * @param name Uniform name.
         * @param value New integer value.
         */
        void SetUniformInt(const char* name, int value) override;
        /**
         * @brief Updates a reflected float uniform.
         * @param name Uniform name.
         * @param value New float value.
         */
        void SetUniformFloat(const char* name, float value) override;
        /**
         * @brief Retains and binds a raw read/write storage buffer.
         * @param binding HLSL UAV register.
         * @param buffer Native storage record, or null to clear it.
         */
        void BindStorageBuffer(int binding, IStorageBufferRenderer* buffer) override;
        /**
         * @brief Retains and binds a typed two-dimensional storage image.
         * @param unit HLSL UAV register.
         * @param texture Native storage image, or null to clear it.
         * @param accessMode GraphicsImageAccess ordinal.
         * @return True when usage, register and device agree.
         */
        [[nodiscard]] bool BindStorageTexture2DEXT(
            int unit, std::shared_ptr<IStorageTexture2DRenderer> texture,
            int accessMode) override;
        /**
         * @brief Retains an ordinary Texture2D as a typed compute image.
         * @param unit HLSL UAV register.
         * @param texture Native texture, or null to clear it.
         * @param accessMode GraphicsImageAccess ordinal.
         */
        void BindImageTexture(int unit, ITextureRenderer* texture,
                              int accessMode) override;
        /**
         * @brief Retains and binds a read-only constant buffer.
         * @param binding HLSL constant-buffer register.
         * @param buffer Native constant record, or null to clear it.
         * @return True when the binding is representable.
         */
        [[nodiscard]] bool BindConstantBufferEXT(
            int binding, IStorageBufferRenderer* buffer) override;
        /**
         * @brief Retains a sampled 2D texture for subsequent dispatches.
         * @param unit HLSL texture and sampler register.
         * @param texture Renderer texture, or null to clear it.
         */
        void BindTexture(int unit, ITextureRenderer* texture) override;
        /**
         * @brief Reports direct HLSL register binding for sampled textures.
         * @return True.
         */
        [[nodiscard]] bool UsesDirectSampledTextureBindingsEXT() const override { return true; }
        /**
         * @brief Reports whether the native shader exists.
         * @return True after successful compilation.
         */
        [[nodiscard]] bool IsValid() const override { return shader_ != nullptr; }
        /**
         * @brief Returns the last compiler or native creation diagnostic.
         * @return Diagnostic text, empty after success.
         */
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }
        /**
         * @brief Submits a compute dispatch and releases its pipeline bindings.
         * @param groupsX Work groups along X.
         * @param groupsY Work groups along Y.
         * @param groupsZ Work groups along Z.
         */
        void Dispatch(int groupsX, int groupsY, int groupsZ);

    private:
        static constexpr std::size_t kUavSlots = D3D11_PS_CS_UAV_REGISTER_COUNT;
        static constexpr std::size_t kTextureSlots =
            D3DCommon::D3DProgramReflection::kMaxShaderResources;
        static constexpr std::size_t kConstantSlots = D3DCommon::D3DProgramReflection::kMaxConstantBuffers;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
        std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, kConstantSlots> uniformBuffers_{};
        std::array<std::shared_ptr<IStorageBufferRenderer>, kUavSlots> storageBuffers_{};
        std::array<std::shared_ptr<IStorageTexture2DRenderer>, kUavSlots> storageTextures_{};
        std::array<std::shared_ptr<ITextureRenderer>, kUavSlots> imageTextures_{};
        std::array<std::shared_ptr<IStorageBufferRenderer>, kConstantSlots> constantBuffers_{};
        std::array<std::shared_ptr<ITextureRenderer>, kTextureSlots> textures_{};
        D3DCommon::D3DProgramReflection reflection_;
        std::string compileError_;
    };
}
