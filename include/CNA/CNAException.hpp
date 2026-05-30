#pragma once

#include "System/Exception.hpp"
#include <string>

namespace CNA
{
    /**
     * @brief Exception type used by the CNA namespace.
     *
     * This class extends @c System::Exception and represents
     * errors related to CNA-specific functionality.
     * @note Status: Verified
     */
    class CNAException : public System::Exception
    {
    public:
        /**
         * @brief Constructs an exception from a C-style string message.
         *
         * @param msg Pointer to a null-terminated error message.
         */
        explicit CNAException(const char* msg);

        /**
         * @brief Constructs an exception from a standard string message.
         *
         * @param msg Error message text.
         */
        explicit CNAException(const std::string& msg);
    };
} // CNA
