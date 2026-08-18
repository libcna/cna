// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/LogCategory.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

namespace CNA::Graphics::detail {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    bool ReportShaderCompileFailure(GraphicsDevice& device, const std::string& passName,
                                    const ShaderEffect* effect, bool& alreadyLogged)
    {
        if (effect != nullptr && effect->IsEffectValid())
            return true;

        if (alreadyLogged)
            return false;
        alreadyLogged = true;

        const std::string renderer(device.GetGraphicsRendererName());
        std::string message = passName + ": its shader did not compile on the " + renderer
                            + " renderer, so the pass will copy its input through instead of "
                              "running.";

        // The log is the whole reason this exists. Without it the only evidence is an effect that
        // is not there, which reads as "bloom looks weak" rather than as "bloom never ran".
        const std::string log = effect != nullptr ? effect->GetCompileErrorEXT() : std::string();
        if (!log.empty())
            message += " Compiler log: " + log;
        else if (effect == nullptr)
            message += " The effect was never created -- the renderer accepts no custom effects.";
        else
            message += " This renderer keeps no compiler log.";

        CNA::Logger::Info(message, CNA::LogCategory::RENDER);
        return false;
    }

} // namespace CNA::Graphics::detail

#endif // CNA_CNAEXT
