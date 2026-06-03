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
}
