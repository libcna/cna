// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /// Thrown when a GamerServices API is called on a platform that does not support it.
    // CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
    class GamerServicesNotAvailableException : public std::runtime_error
    {
    public:
        GamerServicesNotAvailableException();
        explicit GamerServicesNotAvailableException(const std::string& message);
    };
}
