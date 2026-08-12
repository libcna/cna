// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform {

    /**
     * @brief Creates the platform implementation this binary was built with.
     *
     * The single point where an implementation is chosen. Everything else in CNA talks to
     * `IPlatform`, so adding a future implementation touches this file and nothing else — which
     * is the property that makes the whole separation worth doing.
     *
     * Called **once** at startup; the returned pointer outlives every window, renderer and input
     * device and is never replaced. `cnaplatform.md` notes that a stable pointer also lets the
     * CPU's branch predictor learn the indirect target, so the virtual dispatch costs nothing
     * measurable.
     */
    class PlatformFactory
    {
    public:
        /**
         * @brief Creates the platform selected at build time by `CNA_PLATFORM`.
         *
         * @return The platform instance; never null.
         * @throws PlatformException If the implementation could not start.
         */
        [[nodiscard]] static std::unique_ptr<IPlatform> Create();

        /**
         * @brief Creates a specific platform implementation by name.
         *
         * Used by the conformance suite, which runs the same tests against every implementation
         * compiled into the test binary.
         *
         * @param name The implementation to create, e.g. `"SDL3"` or `"Headless"`.
         * @return The platform instance; never null.
         * @throws PlatformException If @p name is not compiled into this binary.
         */
        [[nodiscard]] static std::unique_ptr<IPlatform> Create(const std::string& name);

        /**
         * @brief Gets the names of every implementation compiled into this binary.
         *
         * @return The available names, in a stable order.
         */
        [[nodiscard]] static std::vector<std::string> GetAvailable();

        /**
         * @brief Gets the name of the implementation `Create()` would produce.
         *
         * @return The default implementation's name.
         */
        [[nodiscard]] static const std::string& GetDefaultName();
    };

} // namespace CNA::Platform
