//
// Created by robertvokac on 6/7/25.
//

#pragma once

#include <string>

#include "System/InvalidOperationException.hpp"

namespace System {

    /**
     * @brief The exception that is thrown when accessing an object that was disposed.
     *
     * This is a partial C++ counterpart of the .NET System::ObjectDisposedException type.
     *
     * @note Status: Partial.
     * @note Serialization, HResult, inner exception handling, and Type-based overloads
     * are not implemented here.
     */
    class ObjectDisposedException : public InvalidOperationException {
    private:
        std::string objectName_;

        static std::string BuildMessage(const std::string& objectName, const std::string& message);

    public:
        /**
         * @brief Initializes a new instance of the ObjectDisposedException class.
         *
         * Uses a default generic disposed-object message.
         */
        ObjectDisposedException();

        /**
         * @brief Initializes a new instance of the ObjectDisposedException class
         * with the name of the disposed object.
         *
         * @param objectName Name of the disposed object.
         */
        explicit ObjectDisposedException(const char* objectName);

        /**
         * @brief Initializes a new instance of the ObjectDisposedException class
         * with the name of the disposed object.
         *
         * @param objectName Name of the disposed object.
         */
        explicit ObjectDisposedException(const std::string& objectName);

        /**
         * @brief Initializes a new instance of the ObjectDisposedException class
         * with the specified object name and message.
         *
         * @param objectName Name of the disposed object.
         * @param message Error message.
         */
        ObjectDisposedException(const char* objectName, const char* message);

        /**
         * @brief Initializes a new instance of the ObjectDisposedException class
         * with the specified object name and message.
         *
         * @param objectName Name of the disposed object.
         * @param message Error message.
         */
        ObjectDisposedException(const std::string& objectName, const std::string& message);

        /**
         * @brief Gets the name of the disposed object.
         *
         * @return Object name, or empty string if not specified.
         */
        [[nodiscard]] const std::string& getObjectNameProperty() const;

        /**
         * @brief Throws ObjectDisposedException if the specified condition is true.
         *
         * @param condition Condition to evaluate.
         * @param objectName Name of the disposed object to include in the exception.
         */
        static void ThrowIf(bool condition, const char* objectName);

        /**
         * @brief Throws ObjectDisposedException if the specified condition is true.
         *
         * @param condition Condition to evaluate.
         * @param objectName Name of the disposed object to include in the exception.
         */
        static void ThrowIf(bool condition, const std::string& objectName);
    };

} // namespace System