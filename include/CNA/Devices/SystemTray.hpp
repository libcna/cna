// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <cstddef>
#include <memory>
#include <string>

#include "CNA/Devices/Detail/ITrayBackend.hpp"

namespace CNA::Devices
{
    /**
     * @brief Shows a system tray (notification area) icon with a simple, flat menu.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Backed by SDL3's
     * `SDL_CreateTray()`/`SDL_CreateTrayMenu()`/`SDL_InsertTrayEntryAt()`/etc.
     * (`third_party/SDL/include/SDL3/SDL_tray.h`) by default, via
     * `Detail::ITrayBackend`/`Detail::SdlTrayBackend`.
     *
     * @note Genuinely desktop-only — confirmed by reading
     * `third_party/SDL/src/tray/{windows,unix,cocoa}/` and
     * `third_party/SDL/CMakeLists.txt`'s own per-platform tray-source selection
     * directly: no Android, iOS, or Web/Emscripten backend exists at all (unlike
     * `FileDialog`, which turned out to have a real Android backend — see
     * `DEVICES-CNA-008`'s resolution note; this one really is desktop-only).
     *
     * @note An instance owns exactly one real tray icon for its lifetime: the icon is
     * created in the constructor and destroyed in the destructor. Deliberately
     * scoped to a single flat list of menu entries (no submenus) — see
     * `Detail::ITrayBackend`'s own doc comment for why.
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
         */
        explicit SystemTray(const std::string& tooltip);

        /**
         * @brief Test-only constructor: creates a SystemTray using an explicit
         * backend instead of the real platform one.
         *
         * See `Detail::ITrayBackend`'s own doc comment for why this exists as a
         * constructor parameter rather than a post-construction
         * `SetBackendForTesting()` call (unlike this project's other swappable-backend
         * classes): the real backend's `Create()` runs as soon as it is asked to, so
         * a fake must already be in place *before* construction, not after.
         *
         * @param tooltip Tooltip text shown when the mouse hovers the icon.
         * @param backend Backend to use instead of the real platform one.
         */
        SystemTray(const std::string& tooltip, std::unique_ptr<Detail::ITrayBackend> backend);

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
            Detail::TrayEntryClickCallback onClick);

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
        std::unique_ptr<Detail::ITrayBackend> backend_;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
