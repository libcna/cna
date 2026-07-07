// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <cstddef>
#include <functional>
#include <string>

namespace CNA::Devices::Detail
{
    /** @brief Invoked when a tray menu entry is clicked. */
    using TrayEntryClickCallback = std::function<void()>;

    /**
     * @brief Native system-tray backend interface (Task DEVICES-CNA-009).
     *
     * `SystemTray` calls through this interface instead of calling SDL3's
     * `SDL_CreateTray()`/`SDL_CreateTrayMenu()`/etc. directly, for the same reason
     * `FileDialog` needed `Detail::IFileDialogBackend` (Task `DEVICES-CNA-008`): the
     * real backend (`SdlTrayBackend`) creates a genuine, visible tray icon on the
     * real desktop the moment `Create()` is called — a side effect an automated test
     * must never trigger (an orphaned tray icon left on the development machine's
     * real desktop is at least as undesirable as `DEVICES-CNA-008`'s orphaned
     * `zenity` windows were). `SystemTray`'s test-only constructor overload takes an
     * explicit `ITrayBackend` so a fake can be substituted *before* `Create()` is
     * ever called on any backend, real or otherwise.
     *
     * Deliberately scoped to a single flat list of menu entries (no submenus) — a
     * game's tray icon realistically needs only a handful of top-level actions (e.g.
     * "Show", "Quit"); `SDL_CreateTraySubmenu()` is not wrapped here. A future task
     * can extend this interface if a real nested-menu need appears.
     */
    class ITrayBackend
    {
    public:
        virtual ~ITrayBackend() = default;

        /** @brief Creates the tray icon with the given tooltip text. */
        virtual void Create(const std::string& tooltip) = 0;

        /** @brief Destroys the tray icon. Safe to call even if Create() was never called. */
        virtual void Destroy() = 0;

        /** @brief Changes the tray icon's tooltip text. */
        virtual void SetTooltip(const std::string& tooltip) = 0;

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
        virtual std::size_t AddEntry(
            const std::string& label,
            bool checkable,
            bool initiallyChecked,
            bool initiallyEnabled,
            TrayEntryClickCallback onClick) = 0;

        /** @brief Changes an entry's label text. */
        virtual void SetEntryLabel(std::size_t index, const std::string& label) = 0;

        /** @brief Changes an entry's checked state. */
        virtual void SetEntryChecked(std::size_t index, bool checked) = 0;

        /** @brief Gets an entry's current checked state. */
        [[nodiscard]] virtual bool GetEntryChecked(std::size_t index) const = 0;

        /** @brief Changes an entry's enabled state. */
        virtual void SetEntryEnabled(std::size_t index, bool enabled) = 0;

        /** @brief Gets an entry's current enabled state. */
        [[nodiscard]] virtual bool GetEntryEnabled(std::size_t index) const = 0;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
