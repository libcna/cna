//
// Created by robertvokac on 6/1/25.
//

#pragma once

namespace System {

    /**
     * @brief Defines a mechanism for explicitly releasing resources.
     *
     * This is the C++ counterpart of the .NET IDisposable interface.
     * In .NET, IDisposable declares only the Dispose() method.
     */
    class IDisposable {
    public:
        /**
         * @brief Releases the resources used by the current object.
         */
        virtual void Dispose() = 0;

        /**
         * @brief Virtual destructor for safe polymorphic destruction.
         */
        virtual ~IDisposable() = default;
    };

} // namespace System