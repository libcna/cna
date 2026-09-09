// SPDX-License-Identifier: MS-PL
#pragma once

#include <d3d11shader.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::D3DCommon
{
    /**
     * @brief Reflected resource and constant-buffer layout shared by runtime HLSL effects.
     */
    class D3DProgramReflection
    {
    public:
        static constexpr int kMaxConstantBuffers = 14;
        static constexpr int kMaxShaderResources = 16;
        static constexpr int kMaxSamplers = 16;

        struct ConstantBuffer
        {
            bool present = false;
            std::vector<std::uint8_t> data;
        };

        /** @brief Clears all reflected layout and parameter data. */
        void Reset();

        /**
         * @brief Merges one compiled shader stage into this program reflection.
         *
         * @param bytecode Compiled DXBC bytes.
         * @param bytecodeSize Number of bytes in @p bytecode.
         * @param error Receives a diagnostic when reflection or layout validation fails.
         * @return True when every reflected core resource can be represented.
         */
        bool AddShader(const void* bytecode, std::size_t bytecodeSize, std::string& error);

        /** @brief Writes one reflected float parameter. */
        void SetFloat(const char* name, float value);
        /** @brief Writes one reflected integer parameter. */
        void SetInt(const char* name, int value);
        /** @brief Writes one reflected two-component vector parameter. */
        void SetVec2(const char* name, float x, float y);
        /** @brief Writes one reflected three-component vector parameter. */
        void SetVec3(const char* name, float x, float y, float z);
        /** @brief Writes one reflected four-component vector parameter. */
        void SetVec4(const char* name, float x, float y, float z, float w);
        /** @brief Writes one reflected column-major 4x4 matrix parameter. */
        void SetMat4(const char* name, const float* matrix);
        /** @brief Writes a reflected scalar-float array. */
        void SetFloatArray(const char* name, const float* values, int count);
        /** @brief Writes a reflected two-component vector array. */
        void SetVec2Array(const char* name, const float* values, int count);
        /** @brief Writes a reflected three-component vector array. */
        void SetVec3Array(const char* name, const float* values, int count);
        /** @brief Writes a reflected column-major 4x4 matrix array. */
        void SetMat4Array(const char* name, const float* matrices, int count);

        /** @brief Returns one reflected constant buffer by native b-register. */
        [[nodiscard]] const ConstantBuffer& GetConstantBuffer(int slot) const;
        /** @brief Returns one mutable reflected constant buffer by native b-register. */
        [[nodiscard]] ConstantBuffer& GetConstantBuffer(int slot);
        /** @brief Returns one plus the highest reflected b-register, or zero. */
        [[nodiscard]] int GetConstantBufferCount() const { return constantBufferCount_; }
        /** @brief Returns one plus the highest reflected t-register, or zero. */
        [[nodiscard]] int GetShaderResourceCount() const { return shaderResourceCount_; }
        /** @brief Returns one plus the highest reflected s-register, or zero. */
        [[nodiscard]] int GetSamplerCount() const { return samplerCount_; }
        /** @brief Reports whether a shader texture is reflected at @p slot. */
        [[nodiscard]] bool HasShaderResource(int slot) const;
        /** @brief Returns the reflected D3D resource dimension at @p slot. */
        [[nodiscard]] D3D_SRV_DIMENSION GetShaderResourceDimension(int slot) const;

    private:
        struct UniformLocation
        {
            int bufferSlot = 0;
            std::size_t offset = 0;
            std::size_t size = 0;
            D3D_SHADER_VARIABLE_CLASS variableClass = D3D_SVC_SCALAR;
            D3D_SHADER_VARIABLE_TYPE variableType = D3D_SVT_FLOAT;
            int rows = 1;
            int columns = 1;
            int elements = 0;
        };

        struct ShaderResource
        {
            bool present = false;
            D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;
        };

        static std::string CanonicalName(const char* name);
        void WriteFloatVectors(const char* name, const float* values, int count,
                               int components);
        void WriteMatrixLocation(const UniformLocation& location, const float* matrices,
                                 int count);

        std::array<ConstantBuffer, kMaxConstantBuffers> constantBuffers_{};
        std::array<ShaderResource, kMaxShaderResources> shaderResources_{};
        std::array<bool, kMaxSamplers> samplers_{};
        std::unordered_map<std::string, std::vector<UniformLocation>> uniforms_;
        int constantBufferCount_ = 0;
        int shaderResourceCount_ = 0;
        int samplerCount_ = 0;
    };
}
