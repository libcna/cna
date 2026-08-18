// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/EngineException.hpp"

#ifdef CNA_CNAEXT

#include <utility>

namespace CNA::Graphics {

    EngineException::EngineException(const std::string& message) : System::Exception(message) {}

    EngineException::EngineException(const std::string& message, std::string subsystem,
                                     std::string requirement, std::string rendererName)
        : System::Exception(message)
        , subsystem_(std::move(subsystem))
        , requirement_(std::move(requirement))
        , rendererName_(std::move(rendererName))
    {
    }

    EngineException EngineException::notSupported(const std::string& subsystem,
                                                  const std::string& what,
                                                  const std::string& rendererName)
    {
        return EngineException(subsystem + ": " + what + " is not supported by the " + rendererName
                                   + " renderer",
                               subsystem, what, rendererName);
    }

    const std::string& EngineException::getSubsystemProperty() const { return subsystem_; }

    const std::string& EngineException::getRequirementProperty() const { return requirement_; }

    const std::string& EngineException::getRendererNameProperty() const { return rendererName_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
