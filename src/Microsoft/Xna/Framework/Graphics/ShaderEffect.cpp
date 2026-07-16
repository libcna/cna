// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Logger.hpp"
#include <iostream>

namespace Microsoft::Xna::Framework::Graphics
{
    ShaderEffect::ShaderEffect(GraphicsDevice& device,
                               const std::string& vertSrc,
                               const std::string& fragSrc)
        : Effect(device),
          vertSrc_(vertSrc),
          fragSrc_(fragSrc)
    {
        if (device.backend_)
        {
            effectBackend_ = device.backend_->CreateEffectBackend(vertSrc, fragSrc);
            if (effectBackend_ && !effectBackend_->IsValid())
                std::cerr << "[ShaderEffect] Compile error: " << effectBackend_->GetCompileError() << "\n";
        }
    }

    bool ShaderEffect::IsEffectValid() const
    {
        return effectBackend_ && effectBackend_->IsValid();
    }

    void ShaderEffect::SetUniformMat4(const char* name, const float* matrix)
    {
        if (effectBackend_) effectBackend_->SetUniformMat4(name, matrix);
    }

    void ShaderEffect::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        if (effectBackend_) effectBackend_->SetUniformVec4(name, x, y, z, w);
    }

    void ShaderEffect::SetUniformVec3(const char* name, float x, float y, float z)
    {
        if (effectBackend_) effectBackend_->SetUniformVec3(name, x, y, z);
    }

    void ShaderEffect::SetUniformVec2(const char* name, float x, float y)
    {
        if (effectBackend_) effectBackend_->SetUniformVec2(name, x, y);
    }

    void ShaderEffect::SetUniformFloat(const char* name, float value)
    {
        if (effectBackend_) effectBackend_->SetUniformFloat(name, value);
    }

    void ShaderEffect::SetUniformInt(const char* name, int value)
    {
        if (effectBackend_) effectBackend_->SetUniformInt(name, value);
    }

    void ShaderEffect::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        if (effectBackend_) effectBackend_->SetUniformFloatArray(name, values, count);
    }

    void ShaderEffect::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        if (effectBackend_) effectBackend_->SetUniformVec2Array(name, values, count);
    }

    void ShaderEffect::SetTexture(int unit, Texture2D& texture)
    {
        if (effectBackend_) effectBackend_->BindTexture(unit, &texture.GetBackend());
    }

    void ShaderEffect::SetTexture(int unit, TextureCube& texture)
    {
        if (effectBackend_) effectBackend_->BindTextureCube(unit, &texture.GetBackend());
    }

    ShaderEffect::~ShaderEffect() = default;

    const std::string& ShaderEffect::getVertexSourceProperty() const { return vertSrc_; }
    const std::string& ShaderEffect::getFragmentSourceProperty() const { return fragSrc_; }
    const std::string& ShaderEffect::GetVertexSource() const { return vertSrc_; }
    const std::string& ShaderEffect::GetFragmentSource() const { return fragSrc_; }

    void ShaderEffect::OnApply()
    {
        if (effectBackend_ && effectBackend_->IsValid())
            effectBackend_->Bind();
        else
            CNA::Logger::Debug("ShaderEffect::OnApply() — no backend or compile failed.");
    }

    CNA::Internal::Backends::IEffectBackend* ShaderEffect::GetEffectBackendPtr() const
    {
        return effectBackend_.get();
    }

    void ShaderEffect::FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& params) const
    {
        params.customEffectBackend = effectBackend_.get();
    }

    const std::string& ShaderEffect::GetTypeName() const
    {
        static const std::string name = "CNA.ShaderEffect";
        return name;
    }

    Effect* ShaderEffect::Clone()
    {
        return new ShaderEffect(*device_, vertSrc_, fragSrc_);
    }
}
