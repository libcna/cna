// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dStockEffects.hpp"

#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::Fna3d
{
    void Fna3dStockEffect::Load(FNA3D_Device* device, const unsigned char* blob,
                                unsigned int blobBytes, const char* debugName)
    {
        FNA3D_CreateEffect(
            device,
            const_cast<uint8_t*>(static_cast<const uint8_t*>(blob)),
            blobBytes,
            &effect_,
            &effectData_);

        if (effect_ == nullptr || effectData_ == nullptr)
        {
            throw std::runtime_error(
                std::string("FNA3D renderer: MojoShader could not parse the stock ") + debugName +
                " effect binary. FNA3D has no other shader entry point, so no draw is possible.");
        }
        if (effectData_->error_count > 0)
        {
            const char* first = effectData_->errors != nullptr &&
                                        effectData_->errors[0].error != nullptr
                                    ? effectData_->errors[0].error
                                    : "(no message)";
            throw std::runtime_error(
                std::string("FNA3D renderer: the stock ") + debugName +
                " effect binary parsed with " + std::to_string(effectData_->error_count) +
                " MojoShader error(s); first: " + first);
        }
        if (effectData_->technique_count < 1)
        {
            throw std::runtime_error(
                std::string("FNA3D renderer: the stock ") + debugName +
                " effect binary declares no technique.");
        }

        std::memset(&stateChanges_, 0, sizeof(stateChanges_));
    }

    void Fna3dStockEffect::Destroy(FNA3D_Device* device)
    {
        if (effect_ != nullptr && device != nullptr)
        {
            FNA3D_AddDisposeEffect(device, effect_);
        }
        effect_ = nullptr;
        effectData_ = nullptr;
    }

    MOJOSHADER_effectParam* Fna3dStockEffect::Find(const char* name) const
    {
        if (effectData_ == nullptr || name == nullptr)
        {
            return nullptr;
        }
        for (int i = 0; i < effectData_->param_count; ++i)
        {
            const char* candidate = effectData_->params[i].value.name;
            if (candidate != nullptr && std::strcmp(candidate, name) == 0)
            {
                return &effectData_->params[i];
            }
        }
        return nullptr;
    }

    void Fna3dStockEffect::SetFloats(const char* name, const float* values, int count) const
    {
        MOJOSHADER_effectParam* param = Find(name);
        if (param == nullptr || param->value.values == nullptr || values == nullptr)
        {
            return;
        }
        const int capacity = static_cast<int>(param->value.value_count);
        const int writable = count < capacity ? count : capacity;
        if (writable > 0)
        {
            std::memcpy(param->value.valuesF, values,
                        static_cast<std::size_t>(writable) * sizeof(float));
        }
    }

    void Fna3dStockEffect::SetInt(const char* name, int value) const
    {
        MOJOSHADER_effectParam* param = Find(name);
        if (param == nullptr || param->value.values == nullptr ||
            param->value.value_count == 0)
        {
            return;
        }
        param->value.valuesI[0] = value;
    }

    void Fna3dStockEffect::SetVector4(const char* name, const float* rgb, float alpha) const
    {
        if (rgb == nullptr)
        {
            return;
        }
        const float packed[4] = { rgb[0], rgb[1], rgb[2], alpha };
        SetFloats(name, packed, 4);
    }

    void Fna3dStockEffect::SetMatrix(const char* name, const Matrix& matrix) const
    {
        MOJOSHADER_effectParam* param = Find(name);
        if (param == nullptr || param->value.values == nullptr)
        {
            return;
        }

        // FNA's EffectParameter.SetValue(Matrix) writes column-first with a four-float row
        // stride: the register count MojoShader reports is what distinguishes a float4x4 (16)
        // from a float4x3/float3x3 (12). Anything narrower than three registers is not a
        // transform the stock effects use, and is left untouched rather than half-written.
        float* dst = param->value.valuesF;
        const unsigned int capacity = param->value.value_count;
        if (capacity >= 16)
        {
            dst[0]  = matrix.M11; dst[1]  = matrix.M21; dst[2]  = matrix.M31; dst[3]  = matrix.M41;
            dst[4]  = matrix.M12; dst[5]  = matrix.M22; dst[6]  = matrix.M32; dst[7]  = matrix.M42;
            dst[8]  = matrix.M13; dst[9]  = matrix.M23; dst[10] = matrix.M33; dst[11] = matrix.M43;
            dst[12] = matrix.M14; dst[13] = matrix.M24; dst[14] = matrix.M34; dst[15] = matrix.M44;
        }
        else if (capacity >= 12)
        {
            dst[0]  = matrix.M11; dst[1]  = matrix.M21; dst[2]  = matrix.M31; dst[3]  = matrix.M41;
            dst[4]  = matrix.M12; dst[5]  = matrix.M22; dst[6]  = matrix.M32; dst[7]  = matrix.M42;
            dst[8]  = matrix.M13; dst[9]  = matrix.M23; dst[10] = matrix.M33; dst[11] = matrix.M43;
        }
    }

    void Fna3dStockEffect::SetMatrix4x3Array(const char* name, const float* source,
                                             int count) const
    {
        MOJOSHADER_effectParam* param = Find(name);
        if (param == nullptr || param->value.values == nullptr || source == nullptr)
        {
            return;
        }
        const int capacityMatrices = static_cast<int>(param->value.value_count) / 12;
        const int writable = count < capacityMatrices ? count : capacityMatrices;
        float* dst = param->value.valuesF;
        for (int i = 0; i < writable; ++i)
        {
            PackMatrix4x3(source + static_cast<std::size_t>(i) * 16,
                          dst + static_cast<std::size_t>(i) * 12);
        }
    }

    void Fna3dStockEffect::Apply(FNA3D_Device* device)
    {
        if (effect_ == nullptr || effectData_ == nullptr)
        {
            throw std::runtime_error(
                "FNA3D renderer: Apply() called on a stock effect that was never loaded.");
        }
        FNA3D_SetEffectTechnique(device, effect_, &effectData_->techniques[0]);
        FNA3D_ApplyEffect(device, effect_, 0, &stateChanges_);
    }
}
