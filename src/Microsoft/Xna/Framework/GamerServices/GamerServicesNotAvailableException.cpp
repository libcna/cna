#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerServicesNotAvailableException::GamerServicesNotAvailableException()
        : std::runtime_error("GamerServices are not available on this platform.")
    {
    }

    GamerServicesNotAvailableException::GamerServicesNotAvailableException(const std::string& message)
        : std::runtime_error(message)
    {
    }
}
