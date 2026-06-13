// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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

    const std::string& ShaderEffect::getVertexSourceProperty() const
    {
        return vertSrc_;
    }

    const std::string& ShaderEffect::getFragmentSourceProperty() const
    {
        return fragSrc_;
    }

    void ShaderEffect::OnApply()
    {
        if (effectBackend_ && effectBackend_->IsValid())
            effectBackend_->Bind();
        else
            CNA::Logger::Debug("ShaderEffect::OnApply() — no backend or compile failed.");
    }

    CNA::Internal::Backends::IEffectBackend* ShaderEffect::GetEffectBackend() const
    {
        return effectBackend_.get();
    }

    const std::string& ShaderEffect::GetTypeName() const
    {
        static const std::string name = "CNA.ShaderEffect";
        return name;
    }
}
