// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include <memory>

namespace CNA::Platform {

    /**
     * @brief Gets the platform instance the process is running on.
     *
     * ### Why an ambient accessor exists at all
     *
     * Large parts of the XNA 4.0 API are static: `Keyboard::GetState()`, `Mouse::GetState()`,
     * `StorageDevice`, `TitleContainer`. They take no context argument and cannot be given one
     * without changing the API CNA exists to reproduce, so they need *some* ambient way to reach
     * the platform. This is that way, made explicit and testable rather than each subsystem
     * inventing its own static.
     *
     * It is deliberately **not** how a `Game` should reach its platform: `Game` owns an instance
     * and passes it down, which is why `IPlatform` is an interface rather than a namespace of
     * free functions. This exists for the static API surface that genuinely cannot.
     *
     * On first use with nothing set, the build-time default from `PlatformFactory::Create()` is
     * created and installed. That keeps a bare `StorageDevice` call working with no ceremony,
     * which matters because XNA code does exactly that.
     *
     * @return The current platform; never null.
     * @throws PlatformException If no platform is set and the default cannot be created.
     */
    [[nodiscard]] IPlatform& GetCurrentPlatform();

    /**
     * @brief Installs the platform the static API surface should use.
     *
     * Called by `Game` at startup so the whole process shares one instance, and by tests that
     * need to drive the static API against a controlled implementation — pointing it at
     * `HeadlessPlatform` is what lets storage and title-path logic be tested with no windowing
     * system at all.
     *
     * Ownership stays with the caller: the platform must outlive every use of the static API.
     *
     * @param platform The platform to install, or null to clear it.
     */
    void SetCurrentPlatform(IPlatform* platform);

    /**
     * @brief Reports whether a platform is currently installed.
     *
     * Distinguishes "not yet created" from "created", without triggering the lazy creation
     * `GetCurrentPlatform()` performs. Teardown paths need that difference: constructing a
     * platform while shutting down would be worse than doing nothing.
     *
     * @return True if a platform is installed.
     */
    [[nodiscard]] bool HasCurrentPlatform();

    /**
     * @brief Destroys a lazily-created default platform, if one was created.
     *
     * Has no effect on a platform installed through SetCurrentPlatform() — that one belongs to
     * its caller. Exists so a test can return the process to a clean state.
     */
    void ResetCurrentPlatform();

} // namespace CNA::Platform
