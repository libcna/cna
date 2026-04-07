//
// Created by robertvokac on 5/26/25.
//

#pragma once

#include <exception>
#include <string>

namespace System {

    /**
     * @class Exception
     * @brief Represents the base class for application exceptions.
     *
     * This class is a C++ reimplementation of a .NET-style exception type.
     * It stores an error message and exposes it both through the overridden
     * what() function inherited from std::exception and through the
     * .NET-like getMessageProperty() accessor.
     *
     * @note Status: Partial
     */
    class Exception : public std::exception {
    private:
        std::string message_;

    public:
        /**
         * @brief Initializes a new instance of the Exception class with an empty message.
         */
        Exception();

        ~Exception() override = default;

        /**
         * @brief Initializes a new instance of the Exception class with the specified message.
         *
         * @param msg A null-terminated character string that describes the error.
         *
         * If @p msg is null, an empty message is stored instead.
         */
        explicit Exception(const char* msg);

        /**
         * @brief Initializes a new instance of the Exception class with the specified message.
         *
         * @param msg A string that describes the error.
         */
        explicit Exception(const std::string& msg);

        /**
         * @brief Gets the explanatory message associated with this exception.
         *
         * @return Stored error message.
         */
        [[nodiscard]] virtual const std::string& getMessageProperty() const;

        /**
         * @brief Returns the explanatory message associated with this exception.
         *
         * @return A pointer to a null-terminated character sequence describing the error.
         *
         * The returned pointer remains valid for the lifetime of the exception object
         * and until the stored message is modified.
         */
        [[nodiscard]] const char* what() const noexcept override;
    };

} // namespace System