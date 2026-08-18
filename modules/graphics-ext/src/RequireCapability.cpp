// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/RequireCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/EngineException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Graphics::detail {

    std::string NameOfCapability(const CNA::GraphicsCapability capability)
    {
        // Listed exhaustively and with no `default:`, so a capability added later fails to compile
        // here rather than reaching a log as "an unnamed capability" -- the one outcome that would
        // make this helper worse than the hand-written message it replaces.
        switch (capability)
        {
        case CNA::GraphicsCapability::ThreeD:                          return "the 3D pipeline";
        case CNA::GraphicsCapability::DepthStencilBuffer:              return "a depth/stencil buffer";
        case CNA::GraphicsCapability::MultiSampleAntiAliasing:         return "multisample anti-aliasing";
        case CNA::GraphicsCapability::MultipleRenderTargets:           return "multiple render targets";
        case CNA::GraphicsCapability::AnisotropicFiltering:            return "anisotropic filtering";
        case CNA::GraphicsCapability::WireFrame:                       return "wireframe fill mode";
        case CNA::GraphicsCapability::OcclusionQuery:                  return "occlusion queries";
        case CNA::GraphicsCapability::CustomEffects:                   return "custom effects";
        case CNA::GraphicsCapability::Texture3D:                       return "3D textures";
        case CNA::GraphicsCapability::MultiStreamVertexInput:          return "multi-stream vertex input";
        case CNA::GraphicsCapability::Instancing:                      return "hardware instancing";
        case CNA::GraphicsCapability::StencilBuffer:                   return "a stencil buffer";
        case CNA::GraphicsCapability::AdditiveBlending:                return "additive blending";
        case CNA::GraphicsCapability::CompiledEffects:                 return "compiled effects";
        case CNA::GraphicsCapability::FloatRenderTargets:              return "float render targets";
        case CNA::GraphicsCapability::HalfFloatRenderTargets:          return "half-float render targets";
        case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering: return "linear filtering of half-float textures";
        case CNA::GraphicsCapability::ComputeShaders:                  return "compute shaders";
        }
        // Only reachable through an integer cast into the enum, which is not a legal input; give it
        // a defined answer rather than an unnamed value in a message.
        return "an unrecognised capability";
    }

    void RequireCapability(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                           const CNA::GraphicsCapability capability, const std::string& subsystem)
    {
        if (device.SupportsCapability(capability))
            return;

        throw EngineException::NotSupported(subsystem, NameOfCapability(capability),
                                            std::string(device.GetGraphicsRendererName()));
    }

} // namespace CNA::Graphics::detail

#endif // CNA_CNAEXT
