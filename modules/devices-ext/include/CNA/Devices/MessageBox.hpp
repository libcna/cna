// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>
#include <vector>

#include "CNA/Devices/MessageBoxType.hpp"

namespace CNA::Devices
{
    /**
     * @brief Shows native, modal OS message/alert dialogs.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Calls straight through to the platform's
     * dialog service. A test that must not show a real dialog installs a platform whose dialog
     * service records instead of showing (`CNA::Platform::Testing::CannedDialogPlatform`); there
     * is deliberately no test-only injection hook on this class.
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


    private:
        /** @brief Not instantiable — every member is static. */
        MessageBox() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
