// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/LogCategory.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <vector>

namespace CNA::Graphics::detail {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace
    {
        [[nodiscard]] const char* SeverityName(
            const CNA::ShaderDiagnosticSeverityEXT severity) noexcept
        {
            switch (severity)
            {
                case CNA::ShaderDiagnosticSeverityEXT::Information: return "Information";
                case CNA::ShaderDiagnosticSeverityEXT::Warning: return "Warning";
                case CNA::ShaderDiagnosticSeverityEXT::Error: return "Error";
                case CNA::ShaderDiagnosticSeverityEXT::Count: return "Invalid";
            }
            return "Invalid";
        }

        [[nodiscard]] const char* StageName(const CNA::ShaderStageEXT stage) noexcept
        {
            switch (stage)
            {
                case CNA::ShaderStageEXT::Unknown: return "Unknown";
                case CNA::ShaderStageEXT::Vertex: return "Vertex";
                case CNA::ShaderStageEXT::Fragment: return "Fragment";
                case CNA::ShaderStageEXT::Compute: return "Compute";
                case CNA::ShaderStageEXT::Count: return "Invalid";
            }
            return "Invalid";
        }
    }

    bool reportShaderCompileFailure(GraphicsDevice& device, const std::string& passName,
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

        // Structured records keep source and location machine-readable while this logger still
        // renders every record for a human inspecting a single failure line.
        const auto diagnostics = effect != nullptr
            ? effect->GetShaderDiagnosticsEXT() : std::vector<CNA::ShaderDiagnosticEXT>{};
        if (!diagnostics.empty())
        {
            message += " Compiler diagnostics:";
            for (const auto& diagnostic : diagnostics)
            {
                message += " [" + std::string(SeverityName(diagnostic.getSeverity())) + " "
                    + StageName(diagnostic.getStage());
                if (!diagnostic.getSourceLabel().empty())
                    message += " '" + diagnostic.getSourceLabel() + "'";
                if (diagnostic.getLine() > 0)
                {
                    message += ":" + std::to_string(diagnostic.getLine());
                    if (diagnostic.getColumn() > 0)
                        message += ":" + std::to_string(diagnostic.getColumn());
                }
                message += "] " + diagnostic.getMessage();
            }
        }
        else if (effect == nullptr)
            message += " The effect was never created -- the renderer accepts no custom effects.";
        else
            message += " This renderer keeps no compiler log.";

        CNA::Logger::Info(message, CNA::LogCategory::RENDER);
        return false;
    }

} // namespace CNA::Graphics::detail

#endif // CNA_CNAEXT
