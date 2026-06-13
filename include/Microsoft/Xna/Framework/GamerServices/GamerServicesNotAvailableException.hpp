// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Thrown when a GamerServices API is called on a platform that does not support it.
     *
     * @note CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
     */
    class GamerServicesNotAvailableException : public std::runtime_error
    {
    public:
        /** @brief Constructs a GamerServicesNotAvailableException with a default message. */
        GamerServicesNotAvailableException();

        /**
         * @brief Constructs a GamerServicesNotAvailableException with the given message.
         *
         * @param message Description of the error.
         */
        explicit GamerServicesNotAvailableException(const std::string& message);
    };
}
