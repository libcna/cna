// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace CNA::Platform
{
    class IPlatformTrayIcon;
}

namespace CNA::Devices
{
    /** @brief Invoked whenever the user activates a system-tray menu entry. */
    using TrayEntryClickCallback = std::function<void()>;

    /**
     * @brief Shows a system tray (notification area) icon with a simple, flat menu.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Backed by the selected platform's
     * capability-gated tray service; `SystemTray` itself contains no native API dependency.
     *
     * @note Genuinely desktop-only. Mobile, web, headless and terminal implementations report the
     * capability as unavailable, so callers can select another notification surface.
     *
     * @note An instance owns exactly one real tray icon for its lifetime: the icon is
     * created in the constructor and destroyed in the destructor. The menu is deliberately a
     * single flat list: CNA has no nested-menu consumer, so the platform contract does not force
     * every implementation to invent one.
     */
    class SystemTray
    {
    public:
        /**
         * @brief Gets whether a system tray is supported on the current platform.
         *
         * @return true on Desktop (Windows/Linux/macOS); false on Android, iOS, and
         * Web/Emscripten, where no native tray backend exists.
         */
        [[nodiscard]] static bool getIsSupportedProperty();

        /**
         * @brief Creates a tray icon with the given tooltip, using the real
         * platform-default backend.
         *
         * @param tooltip Tooltip text shown when the mouse hovers the icon; not
         * supported on all platforms.
         * @throws CNA::Platform::PlatformNotSupportedException If the selected platform reports
         * no tray capability.
         */
        explicit SystemTray(const std::string& tooltip);

        /** @brief Destroys the tray icon. */
        ~SystemTray();

        SystemTray(const SystemTray&) = delete;
        SystemTray& operator=(const SystemTray&) = delete;

        /** @brief Changes the tray icon's tooltip text. */
        void setTooltipProperty(const std::string& tooltip);

        /**
         * @brief Appends a new entry to the tray's (flat, single-level) menu.
         *
         * @param label Entry label text.
         * @param checkable true if the entry should show a checkbox.
         * @param initiallyChecked Initial checked state, if checkable.
         * @param initiallyEnabled Initial enabled state.
         * @param onClick Invoked when the user clicks this entry.
         * @return An opaque index identifying this entry for the setters below.
         */
        std::size_t AddEntry(
            const std::string& label,
            bool checkable,
            bool initiallyChecked,
            bool initiallyEnabled,
            TrayEntryClickCallback onClick);

        /** @brief Changes an entry's label text. */
        void SetEntryLabel(std::size_t index, const std::string& label);

        /** @brief Changes an entry's checked state. */
        void SetEntryChecked(std::size_t index, bool checked);

        /** @brief Gets an entry's current checked state. */
        [[nodiscard]] bool GetEntryChecked(std::size_t index) const;

        /** @brief Changes an entry's enabled state. */
        void SetEntryEnabled(std::size_t index, bool enabled);

        /** @brief Gets an entry's current enabled state. */
        [[nodiscard]] bool GetEntryEnabled(std::size_t index) const;

    private:
        std::unique_ptr<CNA::Platform::IPlatformTrayIcon> tray_;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
