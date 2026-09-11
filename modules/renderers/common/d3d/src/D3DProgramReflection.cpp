// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/D3DCommon/D3DProgramReflection.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

namespace CNA::Internal::Renderers::D3DCommon
{
    namespace
    {
#if D3D_COMPILER_VERSION <= 42
        constexpr IID kShaderReflectionIid =
            {0x17f27486, 0xa342, 0x4d10, {0x88, 0x42, 0xab, 0x08, 0x74, 0xe7, 0xf6, 0x70}};
#elif D3D_COMPILER_VERSION == 43
        constexpr IID kShaderReflectionIid =
            {0x0a233719, 0x3960, 0x4578, {0x9d, 0x7c, 0x20, 0x3b, 0x8b, 0x1d, 0x9c, 0xc1}};
#else
        constexpr IID kShaderReflectionIid =
            {0x8d536ca1, 0x0cca, 0x4956, {0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84}};
#endif

        template<typename T>
        bool InRange(T value, int limit)
        {
            return value < static_cast<T>(limit);
        }
    }

    void D3DProgramReflection::Reset()
    {
        constantBuffers_ = {};
        shaderResources_ = {};
        samplers_ = {};
        uniforms_.clear();
        constantBufferCount_ = 0;
        shaderResourceCount_ = 0;
        samplerCount_ = 0;
    }

    bool D3DProgramReflection::AddShader(
        const void* bytecode, const std::size_t bytecodeSize, std::string& error)
    {
        if (bytecode == nullptr || bytecodeSize == 0)
        {
            error = "D3D shader reflection received empty bytecode";
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        const HRESULT reflectHr = D3DReflect(
            bytecode, bytecodeSize, kShaderReflectionIid,
            reinterpret_cast<void**>(reflection.ReleaseAndGetAddressOf()));
        if (FAILED(reflectHr) || !reflection)
        {
            error = "D3DReflect failed for a runtime HLSL stage";
            return false;
        }

        D3D11_SHADER_DESC shaderDesc{};
        if (FAILED(reflection->GetDesc(&shaderDesc)))
        {
            error = "ID3D11ShaderReflection::GetDesc failed";
            return false;
        }

        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC binding{};
            if (FAILED(reflection->GetResourceBindingDesc(i, &binding)))
                continue;

            if (binding.Type == D3D_SIT_CBUFFER)
            {
                if (binding.BindCount != 1 || !InRange(binding.BindPoint, kMaxConstantBuffers))
                {
                    error = "Runtime HLSL cbuffer register is outside the supported D3D core range";
                    return false;
                }
                constantBufferCount_ = std::max(
                    constantBufferCount_, static_cast<int>(binding.BindPoint) + 1);
                continue;
            }

            if (binding.Type == D3D_SIT_TEXTURE)
            {
                if (binding.BindCount == 0 || binding.BindCount > kMaxShaderResources ||
                    binding.BindPoint > kMaxShaderResources - binding.BindCount)
                {
                    error = "Runtime HLSL texture register range exceeds 16 slots";
                    return false;
                }
                for (UINT slot = binding.BindPoint;
                     slot < binding.BindPoint + binding.BindCount; ++slot)
                {
                    auto& resource = shaderResources_[slot];
                    if (resource.present && resource.dimension != binding.Dimension)
                    {
                        error = "Runtime HLSL stages disagree on a texture register dimension";
                        return false;
                    }
                    resource.present = true;
                    resource.dimension = binding.Dimension;
                }
                shaderResourceCount_ = std::max(
                    shaderResourceCount_,
                    static_cast<int>(binding.BindPoint + binding.BindCount));
                continue;
            }

            if (binding.Type == D3D_SIT_SAMPLER)
            {
                if (binding.BindCount == 0 || binding.BindCount > kMaxSamplers ||
                    binding.BindPoint > kMaxSamplers - binding.BindCount)
                {
                    error = "Runtime HLSL sampler register range exceeds 16 slots";
                    return false;
                }
                for (UINT slot = binding.BindPoint;
                     slot < binding.BindPoint + binding.BindCount; ++slot)
                    samplers_[slot] = true;
                samplerCount_ = std::max(
                    samplerCount_, static_cast<int>(binding.BindPoint + binding.BindCount));
                continue;
            }

            error = "Runtime HLSL uses a resource type outside ShaderEffect's core contract";
            return false;
        }

        for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
        {
            ID3D11ShaderReflectionConstantBuffer* reflectedBuffer =
                reflection->GetConstantBufferByIndex(i);
            if (reflectedBuffer == nullptr)
                continue;

            D3D11_SHADER_BUFFER_DESC bufferDesc{};
            if (FAILED(reflectedBuffer->GetDesc(&bufferDesc)) || bufferDesc.Name == nullptr ||
                bufferDesc.Type != D3D_CT_CBUFFER)
                continue;

            D3D11_SHADER_INPUT_BIND_DESC binding{};
            if (FAILED(reflection->GetResourceBindingDescByName(bufferDesc.Name, &binding)) ||
                binding.Type != D3D_SIT_CBUFFER ||
                !InRange(binding.BindPoint, kMaxConstantBuffers))
            {
                error = "Runtime HLSL reflection could not resolve a cbuffer register";
                return false;
            }

            auto& destination = constantBuffers_[binding.BindPoint];
            if (destination.present && destination.data.size() != bufferDesc.Size)
            {
                error = "Runtime HLSL stages disagree on a cbuffer size at the same register";
                return false;
            }
            if (!destination.present)
            {
                destination.present = true;
                destination.data.assign(bufferDesc.Size, 0);
            }

            for (UINT variableIndex = 0; variableIndex < bufferDesc.Variables; ++variableIndex)
            {
                ID3D11ShaderReflectionVariable* variable =
                    reflectedBuffer->GetVariableByIndex(variableIndex);
                if (variable == nullptr)
                    continue;
                D3D11_SHADER_VARIABLE_DESC variableDesc{};
                D3D11_SHADER_TYPE_DESC typeDesc{};
                if (FAILED(variable->GetDesc(&variableDesc)) || variableDesc.Name == nullptr ||
                    variable->GetType() == nullptr ||
                    FAILED(variable->GetType()->GetDesc(&typeDesc)))
                    continue;
                if (variableDesc.StartOffset > destination.data.size() ||
                    variableDesc.Size > destination.data.size() - variableDesc.StartOffset)
                {
                    error = "Runtime HLSL variable lies outside its reflected cbuffer";
                    return false;
                }

                UniformLocation location;
                location.bufferSlot = static_cast<int>(binding.BindPoint);
                location.offset = variableDesc.StartOffset;
                location.size = variableDesc.Size;
                location.variableClass = typeDesc.Class;
                location.variableType = typeDesc.Type;
                location.rows = static_cast<int>(typeDesc.Rows);
                location.columns = static_cast<int>(typeDesc.Columns);
                location.elements = static_cast<int>(typeDesc.Elements);
                auto& locations = uniforms_[CanonicalName(variableDesc.Name)];
                const bool duplicate = std::any_of(
                    locations.begin(), locations.end(), [&](const UniformLocation& existing)
                    {
                        return existing.bufferSlot == location.bufferSlot &&
                               existing.offset == location.offset &&
                               existing.size == location.size;
                    });
                if (!duplicate)
                    locations.push_back(location);
            }
        }

        return true;
    }

    std::string D3DProgramReflection::CanonicalName(const char* name)
    {
        if (name == nullptr)
            return {};
        std::string result(name);
        if (result.size() >= 3 && result.ends_with("[0]"))
            result.resize(result.size() - 3);
        return result;
    }

    void D3DProgramReflection::SetFloat(const char* name, const float value)
    {
        WriteFloatVectors(name, &value, 1, 1);
    }

    void D3DProgramReflection::SetInt(const char* name, const int value)
    {
        const auto found = uniforms_.find(CanonicalName(name));
        if (found == uniforms_.end())
            return;
        const std::int32_t nativeValue = value;
        for (const auto& location : found->second)
        {
            auto& buffer = constantBuffers_[location.bufferSlot].data;
            if (location.offset + sizeof(nativeValue) <= buffer.size())
                std::memcpy(buffer.data() + location.offset, &nativeValue, sizeof(nativeValue));
        }
    }

    void D3DProgramReflection::SetVec2(
        const char* name, const float x, const float y)
    {
        const float values[2] = {x, y};
        WriteFloatVectors(name, values, 1, 2);
    }

    void D3DProgramReflection::SetVec3(
        const char* name, const float x, const float y, const float z)
    {
        const float values[3] = {x, y, z};
        WriteFloatVectors(name, values, 1, 3);
    }

    void D3DProgramReflection::SetVec4(
        const char* name, const float x, const float y, const float z, const float w)
    {
        const float values[4] = {x, y, z, w};
        WriteFloatVectors(name, values, 1, 4);
    }

    void D3DProgramReflection::SetMat4(const char* name, const float* matrix)
    {
        SetMat4Array(name, matrix, 1);
    }

    void D3DProgramReflection::SetFloatArray(
        const char* name, const float* values, const int count)
    {
        WriteFloatVectors(name, values, count, 1);
    }

    void D3DProgramReflection::SetVec2Array(
        const char* name, const float* values, const int count)
    {
        WriteFloatVectors(name, values, count, 2);
    }

    void D3DProgramReflection::SetVec3Array(
        const char* name, const float* values, const int count)
    {
        WriteFloatVectors(name, values, count, 3);
    }

    void D3DProgramReflection::SetMat4Array(
        const char* name, const float* matrices, const int count)
    {
        if (matrices == nullptr || count <= 0)
            return;
        const auto found = uniforms_.find(CanonicalName(name));
        if (found == uniforms_.end())
            return;
        for (const auto& location : found->second)
            WriteMatrixLocation(location, matrices, count);
    }

    void D3DProgramReflection::WriteFloatVectors(
        const char* name, const float* values, const int count, const int components)
    {
        if (values == nullptr || count <= 0 || components <= 0)
            return;
        const auto found = uniforms_.find(CanonicalName(name));
        if (found == uniforms_.end())
            return;

        for (const auto& location : found->second)
        {
            auto& buffer = constantBuffers_[location.bufferSlot].data;
            const int reflectedCount = std::max(1, location.elements);
            const int copyCount = std::min(count, reflectedCount);
            // HLSL cbuffer scalar/vector array elements always start in a fresh
            // 16-byte register. Reflection's variable Size excludes only the
            // final element's trailing padding, so Size / Elements is not a stride.
            const std::size_t stride = location.elements > 0 ? 16u : location.size;
            const std::size_t valueBytes = static_cast<std::size_t>(components) * sizeof(float);
            for (int element = 0; element < copyCount; ++element)
            {
                const std::size_t destination =
                    location.offset + static_cast<std::size_t>(element) * stride;
                const std::size_t bytes = std::min(valueBytes, stride);
                if (destination <= buffer.size() && bytes <= buffer.size() - destination)
                    std::memcpy(buffer.data() + destination,
                                values + static_cast<std::size_t>(element) * components, bytes);
            }
        }
    }

    void D3DProgramReflection::WriteMatrixLocation(
        const UniformLocation& location, const float* matrices, const int count)
    {
        auto& buffer = constantBuffers_[location.bufferSlot].data;
        const int reflectedCount = std::max(1, location.elements);
        const int copyCount = std::min(count, reflectedCount);
        const std::size_t stride = location.elements > 0
            ? location.size / static_cast<std::size_t>(reflectedCount)
            : location.size;
        const int rows = std::clamp(location.rows, 1, 4);
        const int columns = std::clamp(location.columns, 1, 4);
        for (int element = 0; element < copyCount; ++element)
        {
            const std::size_t destination =
                location.offset + static_cast<std::size_t>(element) * stride;
            if (destination > buffer.size() || stride > buffer.size() - destination)
                continue;
            auto* output = reinterpret_cast<float*>(buffer.data() + destination);
            const float* input = matrices + static_cast<std::size_t>(element) * 16;
            for (int row = 0; row < rows; ++row)
            {
                for (int column = 0; column < columns; ++column)
                {
                    const int sourceIndex = column * 4 + row;
                    const int destinationIndex = location.variableClass == D3D_SVC_MATRIX_ROWS
                        ? row * 4 + column : column * 4 + row;
                    if (static_cast<std::size_t>(destinationIndex + 1) * sizeof(float) <= stride)
                        output[destinationIndex] = input[sourceIndex];
                }
            }
        }
    }

    const D3DProgramReflection::ConstantBuffer& D3DProgramReflection::GetConstantBuffer(
        const int slot) const
    {
        return constantBuffers_.at(static_cast<std::size_t>(slot));
    }

    D3DProgramReflection::ConstantBuffer& D3DProgramReflection::GetConstantBuffer(const int slot)
    {
        return constantBuffers_.at(static_cast<std::size_t>(slot));
    }

    bool D3DProgramReflection::HasShaderResource(const int slot) const
    {
        return slot >= 0 && slot < kMaxShaderResources &&
               shaderResources_[static_cast<std::size_t>(slot)].present;
    }

    D3D_SRV_DIMENSION D3DProgramReflection::GetShaderResourceDimension(const int slot) const
    {
        return HasShaderResource(slot)
            ? shaderResources_[static_cast<std::size_t>(slot)].dimension
            : D3D_SRV_DIMENSION_UNKNOWN;
    }
}
