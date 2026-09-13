// SPDX-License-Identifier: MS-PL
#pragma once

#include "Win32Common.hpp"

namespace CNA::Platform::Win32 {

    /**
     * @brief A refcounted, process-wide registration of CNA's window class.
     *
     * `RegisterClassExW` is per-`HINSTANCE`, and a second registration of the same name fails with
     * `ERROR_CLASS_ALREADY_EXISTS`. That is not hypothetical here: the conformance suite creates
     * two `Win32Platform` instances in one process on purpose, and a game may outlive one platform
     * and create another. So the class is registered on the first acquisition and unregistered
     * after the last release, rather than being tied to any one platform's lifetime.
     *
     * An RAII token rather than free functions: an unbalanced release would either leak the class
     * for the process lifetime or unregister it while windows still use it, and neither is
     * recoverable.
     */
    class Win32WindowClass
    {
    public:
        /**
         * @brief Registers CNA's window class if this is the first live token.
         *
         * @throws PlatformException If registration failed for any reason other than the class
         *         already existing.
         */
        Win32WindowClass();

        /** @brief Releases this token, unregistering the class when it was the last one. */
        ~Win32WindowClass();

        Win32WindowClass(const Win32WindowClass&) = delete;
        Win32WindowClass& operator=(const Win32WindowClass&) = delete;

        /**
         * @brief Gets the registered class name.
         *
         * @return The wide class name `CreateWindowExW` is called with.
         */
        [[nodiscard]] static const wchar_t* GetClassName();

        /**
         * @brief Gets the module instance the class is registered against.
         *
         * @return The `HINSTANCE` of the module this code is linked into.
         */
        [[nodiscard]] static HINSTANCE GetInstance();
    };

} // namespace CNA::Platform::Win32
