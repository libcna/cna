// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

namespace CNA::Internal::Net
{
    /**
     * @brief Owns the process-wide ENet library lifecycle.
     *
     * `enet_initialize()`/`enet_deinitialize()` must be called once per process before any other
     * ENet function is used. `EnsureInitialized()` lazily calls `enet_initialize()` exactly once
     * (via a function-local static, so it is also thread-safe) and is never torn down —
     * matching the same "no C++ equivalent to .NET's AppDomain.ProcessExit, so no reset hook"
     * precedent already used for `GamerServicesDispatcher::IsInitialized`.
     */
    class ENetLibrary
    {
    public:
        /**
         * @brief Initializes the ENet library if it has not been initialized yet.
         *
         * Throws std::runtime_error if ENet fails to initialize.
         */
        static void EnsureInitialized();
    };
}
