// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include "CNA/Logger.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ShaderEffect::ShaderEffect(GraphicsDevice& device,
                               const std::string& vertSrc,
                               const std::string& fragSrc)
        : Effect(device),
          vertSrc_(vertSrc),
          fragSrc_(fragSrc)
    {
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
        CNA::Logger::Debug("ShaderEffect::OnApply() — GLSL shader effect applied.");
    }

    const std::string& ShaderEffect::GetTypeName() const
    {
        static const std::string name = "CNA.ShaderEffect";
        return name;
    }
}
