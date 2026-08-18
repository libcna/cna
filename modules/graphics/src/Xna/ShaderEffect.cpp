// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
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
        if (device.renderer_)
        {
            effectRenderer_ = device.renderer_->CreateEffectRenderer(vertSrc, fragSrc);
            if (effectRenderer_ && !effectRenderer_->IsValid())
                std::cerr << "[ShaderEffect] Compile error: " << effectRenderer_->GetCompileError() << "\n";
        }
    }

    bool ShaderEffect::IsEffectValid() const
    {
        return effectRenderer_ && effectRenderer_->IsValid();
    }

    void ShaderEffect::DeclareUniformBlockEXT(const int blockSizeBytes, const char* const* names,
                                              const int* offsets, const int count)
    {
        if (effectRenderer_)
            effectRenderer_->DeclareUniformBlockEXT(blockSizeBytes, names, offsets, count);
    }

    void ShaderEffect::SetUniformMat4(const char* name, const float* matrix)
    {
        if (effectRenderer_) effectRenderer_->SetUniformMat4(name, matrix);
    }

    void ShaderEffect::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        if (effectRenderer_) effectRenderer_->SetUniformVec4(name, x, y, z, w);
    }

    void ShaderEffect::SetUniformVec3(const char* name, float x, float y, float z)
    {
        if (effectRenderer_) effectRenderer_->SetUniformVec3(name, x, y, z);
    }

    void ShaderEffect::SetUniformVec2(const char* name, float x, float y)
    {
        if (effectRenderer_) effectRenderer_->SetUniformVec2(name, x, y);
    }

    void ShaderEffect::SetUniformFloat(const char* name, float value)
    {
        if (effectRenderer_) effectRenderer_->SetUniformFloat(name, value);
    }

    void ShaderEffect::SetUniformInt(const char* name, int value)
    {
        if (effectRenderer_) effectRenderer_->SetUniformInt(name, value);
    }

    void ShaderEffect::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        if (effectRenderer_) effectRenderer_->SetUniformFloatArray(name, values, count);
    }

    void ShaderEffect::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        if (effectRenderer_) effectRenderer_->SetUniformVec2Array(name, values, count);
    }

    void ShaderEffect::SetTexture(int unit, Texture2D& texture)
    {
        if (effectRenderer_) effectRenderer_->BindTexture(unit, &texture.GetRenderer());
    }

    void ShaderEffect::SetTexture(int unit, TextureCube& texture)
    {
        if (effectRenderer_) effectRenderer_->BindTextureCube(unit, &texture.GetRenderer());
    }

    void ShaderEffect::SetTexture(int unit, Texture3D& texture)
    {
        if (effectRenderer_) effectRenderer_->BindTexture3D(unit, &texture.GetRenderer());
    }

    ShaderEffect::~ShaderEffect() = default;

    void ShaderEffect::Dispose(bool disposing)
    {
        effectRenderer_.reset();
        Effect::Dispose(disposing);
    }

    const std::string& ShaderEffect::getVertexSourceProperty() const { return vertSrc_; }
    const std::string& ShaderEffect::getFragmentSourceProperty() const { return fragSrc_; }
    const std::string& ShaderEffect::GetVertexSource() const { return vertSrc_; }
    const std::string& ShaderEffect::GetFragmentSource() const { return fragSrc_; }

    void ShaderEffect::OnApply()
    {
        if (effectRenderer_ && effectRenderer_->IsValid())
            effectRenderer_->Bind();
        else
            CNA::Logger::Debug("ShaderEffect::OnApply() — no renderer or compile failed.");
    }

    CNA::Internal::Renderers::IEffectRenderer* ShaderEffect::GetEffectRendererPtr() const
    {
        return effectRenderer_.get();
    }

    void ShaderEffect::FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const
    {
        params.customEffectRequested = true;
        params.customEffectRenderer = effectRenderer_.get();
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
