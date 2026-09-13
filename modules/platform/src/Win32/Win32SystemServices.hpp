// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

#include "Win32ComRuntime.hpp"
#include "Win32InputServices.hpp"

#include "../Common/StandardFileSystem.hpp"

#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Win32 {

    /** @brief Reads and writes the system clipboard as Unicode text. */
    class Win32Clipboard final : public IPlatformClipboard
    {
    public:
        /** @brief Gets whether the clipboard holds text. @return True when it does. */
        [[nodiscard]] bool HasText() const override;

        /** @brief Reads the clipboard's text. @return The text, UTF-8 encoded, or empty. */
        [[nodiscard]] std::string GetText() const override;

        /** @brief Writes text to the clipboard. @param text The text, UTF-8 encoded. */
        void SetText(const std::string& text) override;
    };

    /** @brief Enumerates monitors and their modes. */
    class Win32Displays final : public IPlatformDisplays
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param access The owning platform, used to resolve windows to monitors.
         */
        explicit Win32Displays(Win32PlatformAccess& access);

        /** @brief Gets every connected display. @return The displays, primary first. */
        [[nodiscard]] std::vector<DisplayInfo> GetDisplays() const override;

        /** @brief Gets the display a window is on. @param window The window. @param display Receives it. @return True on success. */
        [[nodiscard]] bool TryGetDisplayForWindow(const IPlatformWindow& window,
                                                  DisplayInfo& display) const override;

        /** @brief Gets a window's unobscured region. @param window The window. @param safeArea Receives it. @return True on success. */
        [[nodiscard]] bool TryGetSafeAreaForWindow(const IPlatformWindow& window,
                                                   WindowBounds& safeArea) const override;

        /** @brief Gets the modes a display supports. @param displayId The display. @return The modes. */
        [[nodiscard]] std::vector<DisplayMode> GetDisplayModes(std::uint32_t displayId) const override;

        /** @brief Gets a display's current mode. @param displayId The display. @param mode Receives it. @return True on success. */
        [[nodiscard]] bool TryGetCurrentDisplayMode(std::uint32_t displayId,
                                                    DisplayMode& mode) const override;

        /** @brief Gets whether the screen saver may activate. @return True when it may. */
        [[nodiscard]] bool IsScreenSaverEnabled() const override;

        /** @brief Allows or prevents screen saving. @param enabled True to allow it. */
        void SetScreenSaverEnabled(bool enabled) override;

    private:
        Win32PlatformAccess* access_;
        mutable bool screenSaverSuppressed_ = false;
    };

    /** @brief Shows native message boxes and shell file dialogs. */
    class Win32Dialogs final : public IPlatformDialogs
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param access The owning platform.
         */
        explicit Win32Dialogs(Win32PlatformAccess& access);

        /** @brief Shows a modal message box. @param severity How to present it. @param title The title. @param message The body. @param parent The parent window. */
        void ShowMessageBox(MessageBoxSeverity severity, const std::string& title,
                            const std::string& message, IPlatformWindow* parent) override;

        /** @brief Shows a message box with buttons. @param severity How to present it. @param title The title. @param message The body. @param buttons The labels. @param parent The parent. @return The chosen index, or -1. */
        [[nodiscard]] int ShowMessageBoxWithButtons(MessageBoxSeverity severity,
                                                    const std::string& title,
                                                    const std::string& message,
                                                    const std::vector<std::string>& buttons,
                                                    IPlatformWindow* parent) override;

        /** @brief Shows a file-open dialog. @param onResult Receives the selection. @param filters The type filters. @param defaultLocation Where to start. @param allowMultiple Whether several files may be picked. @param parent The parent. */
        void ShowOpenFileDialog(FileDialogCallback onResult,
                                const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation, bool allowMultiple,
                                IPlatformWindow* parent) override;

        /** @brief Shows a file-save dialog. @param onResult Receives the selection. @param filters The type filters. @param defaultLocation Where to start. @param parent The parent. */
        void ShowSaveFileDialog(FileDialogCallback onResult,
                                const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation,
                                IPlatformWindow* parent) override;

        /** @brief Shows a folder-selection dialog. @param onResult Receives the selection. @param defaultLocation Where to start. @param allowMultiple Whether several folders may be picked. @param parent The parent. */
        void ShowOpenFolderDialog(FileDialogCallback onResult, const std::string& defaultLocation,
                                  bool allowMultiple, IPlatformWindow* parent) override;

    private:
        Win32PlatformAccess* access_;
        Win32ComRuntime com_;
    };

    /** @brief Answers host and power questions with native Windows calls. */
    class Win32SystemInfo final : public IPlatformSystemInfo
    {
    public:
        /** @brief Gets the platform's name. @return `"Windows"`. */
        [[nodiscard]] std::string GetPlatformName() const override;

        /** @brief Gets installed memory. @return Megabytes, or zero when unknown. */
        [[nodiscard]] int GetSystemMemoryMegabytes() const override;

        /** @brief Gets the logical processor count. @return The count, or zero when unknown. */
        [[nodiscard]] int GetLogicalCoreCount() const override;

        /** @brief Gets the user's preferred locales. @return The locales, most preferred first. */
        [[nodiscard]] std::vector<PlatformLocale> GetPreferredLocales() const override;

        /** @brief Gets battery and power state. @return The current power info. */
        [[nodiscard]] PowerInfo GetPowerInfo() const override;

        /** @brief Opens a URL in the default handler. @param url The URL. @return True when accepted. */
        bool OpenUrl(const std::string& url) override;
    };

    /**
     * @brief Path resolution backed by the standard library plus Windows known folders.
     *
     * Reuses `Common::StandardFileSystem` for everything the C++ standard library can answer --
     * base path, file loading, the case-insensitive lookup -- and overrides only the three
     * questions that genuinely need Windows: where preferences go, and where Music and Pictures
     * are. Reimplementing the shared parts here would be a second copy of the same
     * `std::filesystem` calls with a second set of bugs.
     */
    class Win32FileSystem final : public IPlatformFileSystem
    {
    public:
        /** @brief Creates the service. */
        Win32FileSystem();

        /** @brief Gets the launch directory. @return An absolute path ending in a separator. */
        [[nodiscard]] std::string GetBasePath() const override;

        /** @brief Gets a writable per-application directory. @param organization The organization. @param application The application. @return The path. */
        [[nodiscard]] std::string GetPreferencesPath(const std::string& organization,
                                                     const std::string& application) const override;

        /** @brief Gets a well-known user folder. @param folder Which folder. @return The path, or empty. */
        [[nodiscard]] std::string GetUserFolder(UserFolder folder) const override;

        /** @brief Loads a whole file. @param path The file. @param data Receives the bytes. @return True on success. */
        [[nodiscard]] bool TryLoadFile(const std::string& path,
                                       std::vector<std::uint8_t>& data) const override;

        /** @brief Loads a whole file ignoring ASCII case. @param path The file. @param data Receives the bytes. @return True on success. */
        [[nodiscard]] bool TryLoadFileIgnoringCase(const std::string& path,
                                                   std::vector<std::uint8_t>& data) const override;

        /** @brief Creates a directory and its parents. @param path The directory. */
        void CreateDirectory(const std::string& path) override;

    private:
        Common::StandardFileSystem standard_;
    };

} // namespace CNA::Platform::Win32
