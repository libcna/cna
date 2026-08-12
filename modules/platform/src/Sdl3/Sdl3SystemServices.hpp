// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

namespace CNA::Platform::Sdl3 {

    /** @brief SDL3-backed system clipboard access. */
    class Sdl3Clipboard final : public IPlatformClipboard
    {
    public:
        /** @brief Gets whether the clipboard holds text. @return True if there is text. */
        [[nodiscard]] bool HasText() const override;
        /** @brief Reads the clipboard text. @return The text, or empty when there is none. */
        [[nodiscard]] std::string GetText() const override;
        /** @brief Writes text to the clipboard. @param text The text to write. */
        void SetText(const std::string& text) override;
    };

    /** @brief SDL3-backed display enumeration. */
    class Sdl3Displays final : public IPlatformDisplays
    {
    public:
        /** @brief Gets every connected display. @return The displays; empty when none. */
        [[nodiscard]] std::vector<DisplayInfo> GetDisplays() const override;
        /**
         * @brief Gets the display a window is on.
         * @param window The window to locate.
         * @param display Receives the display; untouched on false.
         * @return True if the window is on a known display.
         */
        [[nodiscard]] bool TryGetDisplayForWindow(const IPlatformWindow& window,
                                                  DisplayInfo& display) const override;
        /**
         * @brief Gets the modes a display supports.
         * @param displayId Which display.
         * @return The supported modes; empty when unknown.
         */
        [[nodiscard]] std::vector<DisplayMode> GetDisplayModes(std::uint32_t displayId) const override;
    };

    /** @brief SDL3-backed path resolution and file loading. */
    class Sdl3FileSystem final : public IPlatformFileSystem
    {
    public:
        /** @brief Gets the application's base directory. @return The path, with a trailing separator. */
        [[nodiscard]] std::string GetBasePath() const override;
        /**
         * @brief Gets a writable per-user directory.
         * @param organization The organization name component.
         * @param application The application name component.
         * @return The path, with a trailing separator.
         */
        [[nodiscard]] std::string GetPreferencesPath(const std::string& organization,
                                                     const std::string& application) const override;
        /**
         * @brief Loads a whole file.
         * @param path The file to read.
         * @param data Receives the contents; untouched on false.
         * @return True if the file was read.
         */
        [[nodiscard]] bool TryLoadFile(const std::string& path,
                                       std::vector<std::uint8_t>& data) const override;
        /** @brief Creates a directory and any missing parents. @param path The directory to create. */
        void CreateDirectory(const std::string& path) override;
    };

    /** @brief SDL3-backed host, power and locale information. */
    class Sdl3SystemInfo final : public IPlatformSystemInfo
    {
    public:
        /** @brief Gets the platform name. @return A stable name such as `"Linux"`. */
        [[nodiscard]] std::string GetPlatformName() const override;
        /** @brief Gets system RAM. @return Megabytes, or zero when unknown. */
        [[nodiscard]] int GetSystemMemoryMegabytes() const override;
        /** @brief Gets the logical core count. @return The count, or zero when unknown. */
        [[nodiscard]] int GetLogicalCoreCount() const override;
        /** @brief Gets preferred locales, most preferred first. @return BCP 47 identifiers. */
        [[nodiscard]] std::vector<PlatformLocale> GetPreferredLocales() const override;
        /** @brief Gets power state. @return The current power info. */
        [[nodiscard]] PowerInfo GetPowerInfo() const override;
        /** @brief Opens a URL. @param url The URL to open. @return True if accepted. */
        bool OpenUrl(const std::string& url) override;
    };

    /** @brief SDL3-backed native dialogs. */
    class Sdl3Dialogs final : public IPlatformDialogs
    {
    public:
        /**
         * @brief Shows a modal message box.
         * @param severity How prominently to present it.
         * @param title The dialog title.
         * @param message The message body.
         * @param parent The window to parent the dialog to, or null.
         */
        void ShowMessageBox(MessageBoxSeverity severity, const std::string& title,
                            const std::string& message, IPlatformWindow* parent) override;
        /**
         * @brief Shows a modal message box with buttons.
         * @param severity How prominently to present it.
         * @param title The dialog title.
         * @param message The message body.
         * @param buttons The button labels, in display order.
         * @param parent The window to parent the dialog to, or null.
         * @return The chosen button's index, or -1 when dismissed.
         */
        [[nodiscard]] int ShowMessageBoxWithButtons(MessageBoxSeverity severity,
                                                    const std::string& title,
                                                    const std::string& message,
                                                    const std::vector<std::string>& buttons,
                                                    IPlatformWindow* parent) override;
        /**
         * @brief Refuses: SDL3's file dialogs are asynchronous. See PLAT-104.
         * @param filters The file type filters.
         * @param allowMultiple Whether more than one file may be selected.
         * @param parent The parent window, or null.
         * @return Never returns.
         */
        [[nodiscard]] std::vector<std::string> ShowOpenFileDialog(
            const std::vector<FileDialogFilter>& filters, bool allowMultiple,
            IPlatformWindow* parent) override;
        /**
         * @brief Refuses: SDL3's file dialogs are asynchronous. See PLAT-104.
         * @param filters The file type filters.
         * @param parent The parent window, or null.
         * @return Never returns.
         */
        [[nodiscard]] std::string ShowSaveFileDialog(const std::vector<FileDialogFilter>& filters,
                                                     IPlatformWindow* parent) override;
        /**
         * @brief Refuses: SDL3's file dialogs are asynchronous. See PLAT-104.
         * @param parent The parent window, or null.
         * @return Never returns.
         */
        [[nodiscard]] std::string ShowOpenFolderDialog(IPlatformWindow* parent) override;
    };

} // namespace CNA::Platform::Sdl3
