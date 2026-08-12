// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <memory>
#include <string>
#include <vector>

#include "CNA/Devices/Detail/IMessageBoxBackend.hpp"
#include "CNA/Devices/MessageBoxType.hpp"

namespace CNA::Devices
{
    /**
     * @brief Shows native, modal OS message/alert dialogs.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Calls through
     * `Detail::IMessageBoxBackend` (default: `Detail::PlatformMessageBoxBackend`, backed
     * by the platform's dialog service) rather than calling a windowing system
     * directly, specifically so tests can inject a fake backend — see
     * `SetBackendForTesting()`'s own doc comment for why this matters here.
     *
     * @note Unlike `FileDialog`, every call here is **synchronous**: a message box blocks the
     * calling thread until the user clicks a button or closes the dialog. There is no result
     * callback and no async lifetime to manage.
     *
     * @note This has the broadest cross-platform reach of any `CNA::Devices` capability — every
     * desktop, mobile and web target CNA supports has a native message box. It is nonetheless
     * capability-gated rather than unconditionally available: a headless or terminal platform has
     * nowhere to show one, and `getIsSupportedProperty()` reports that honestly so a caller can
     * fall back rather than silently show nothing.
     */
    class MessageBox
    {
    public:
        /**
         * @brief Gets whether native message boxes are supported on the current
         * platform.
         *
         * @return Always true — a real backend exists on every platform this
         * project targets.
         */
        [[nodiscard]] static bool getIsSupportedProperty();

        /**
         * @brief Shows a simple message box with a single "OK"-style dismissal,
         * using the platform's own default button wording.
         *
         * @param type Icon/severity of the dialog.
         * @param title Dialog title text.
         * @param message Dialog body text.
         */
        static void ShowSimple(MessageBoxType type, const std::string& title, const std::string& message);

        /**
         * @brief Shows a message box with caller-specified buttons and returns which
         * one the user clicked.
         *
         * @param type Icon/severity of the dialog.
         * @param title Dialog title text.
         * @param message Dialog body text.
         * @param buttonLabels Labels for each button, left to right; must not be
         * empty.
         * @return The index into `buttonLabels` of the button the user clicked, or
         * -1 if the dialog could not be shown.
         */
        static int Show(
            MessageBoxType type,
            const std::string& title,
            const std::string& message,
            const std::vector<std::string>& buttonLabels);

        /**
         * @brief Test-only hook: replaces the real, platform-default backend with a
         * caller-supplied one (typically a test fake).
         *
         * The real backend (`Detail::PlatformMessageBoxBackend`) pops a genuine,
         * interactive, modal native OS dialog and blocks the calling thread until a
         * human responds — an automated test must never call it directly. This hook
         * exists specifically so tests never call the real backend at all.
         *
         * @param backend Replacement backend; pass nullptr to restore the
         * platform-default (`Detail::PlatformMessageBoxBackend`) behavior.
         */
        static void SetBackendForTesting(std::unique_ptr<Detail::IMessageBoxBackend> backend);

    private:
        /** @brief Not instantiable — every member is static. */
        MessageBox() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
