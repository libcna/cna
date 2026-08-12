// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Platform {

    class IPlatformWindow;

    // --- Clipboard -----------------------------------------------------------------------------

    /** @brief Reads and writes the system clipboard. */
    class IPlatformClipboard
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformClipboard() = default;

        /**
         * @brief Gets whether the clipboard currently holds text.
         *
         * @return True if there is text to read.
         */
        [[nodiscard]] virtual bool HasText() const = 0;

        /**
         * @brief Reads the clipboard's text.
         *
         * @return The text, UTF-8 encoded, or an empty string when there is none. Returning a
         * status rather than throwing: an empty clipboard is ordinary, not exceptional.
         */
        [[nodiscard]] virtual std::string GetText() const = 0;

        /**
         * @brief Writes text to the clipboard.
         *
         * @param text The text to write, UTF-8 encoded.
         * @throws PlatformNotSupportedException If the platform reports no `Clipboard` capability.
         */
        virtual void SetText(const std::string& text) = 0;
    };

    // --- Displays ------------------------------------------------------------------------------

    /** @brief One display mode a display can be in. */
    struct DisplayMode
    {
        /** @brief Width in pixels. */
        int width = 0;
        /** @brief Height in pixels. */
        int height = 0;
        /** @brief Refresh rate in Hz; zero when unknown. */
        float refreshRate = 0.0f;
    };

    /** @brief One connected display. */
    struct DisplayInfo
    {
        /** @brief Identifies the display within this platform instance. */
        std::uint32_t id = 0;
        /** @brief The display's human-readable name. */
        std::string name;
        /** @brief The display's position and size in the virtual desktop, in logical units. */
        int x = 0, y = 0, width = 0, height = 0;
        /** @brief Content scale, where 1.0 means one logical unit per physical pixel. */
        float contentScale = 1.0f;
        /** @brief The mode the desktop is currently in. */
        DisplayMode desktopMode;
    };

    /** @brief Enumerates connected displays. */
    class IPlatformDisplays
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformDisplays() = default;

        /**
         * @brief Gets every connected display.
         *
         * @return The displays; empty on a platform with none.
         */
        [[nodiscard]] virtual std::vector<DisplayInfo> GetDisplays() const = 0;

        /**
         * @brief Gets the display a window is currently on.
         *
         * @param window The window to locate.
         * @param display Receives the display; untouched when this returns false.
         * @return True if the window is on a known display.
         */
        [[nodiscard]] virtual bool TryGetDisplayForWindow(const IPlatformWindow& window,
                                                          DisplayInfo& display) const = 0;

        /**
         * @brief Gets the modes a display supports.
         *
         * @param displayId Which display.
         * @return The supported modes; empty when the display is unknown or exposes none.
         */
        [[nodiscard]] virtual std::vector<DisplayMode> GetDisplayModes(std::uint32_t displayId) const = 0;
    };

    // --- Dialogs -------------------------------------------------------------------------------

    /** @brief How prominently a message box presents itself. */
    enum class MessageBoxSeverity
    {
        /** @brief Informational. */
        Information,
        /** @brief A warning. */
        Warning,
        /** @brief An error. */
        Error
    };

    /** @brief A filter offered in a file dialog. */
    struct FileDialogFilter
    {
        /** @brief The filter's display name, e.g. `"Images"`. */
        std::string name;
        /** @brief Semicolon-separated patterns, e.g. `"png;jpg"`. */
        std::string patterns;
    };

    /** @brief Shows native message boxes and file dialogs. */
    class IPlatformDialogs
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformDialogs() = default;

        /**
         * @brief Shows a modal message box.
         *
         * @param severity How prominently to present it.
         * @param title The dialog title.
         * @param message The message body.
         * @param parent The window to parent the dialog to, or null for none.
         * @throws PlatformNotSupportedException If the platform reports no `MessageBox` capability.
         */
        virtual void ShowMessageBox(MessageBoxSeverity severity, const std::string& title,
                                    const std::string& message, IPlatformWindow* parent) = 0;

        /**
         * @brief Shows a modal file-open dialog.
         *
         * @param filters The file type filters to offer.
         * @param allowMultiple Whether more than one file may be selected.
         * @param parent The window to parent the dialog to, or null for none.
         * @return The selected paths; empty when the user cancelled. Cancellation is an ordinary
         * outcome and does not throw.
         * @throws PlatformNotSupportedException If the platform reports no `NativeFileDialog` capability.
         */
        [[nodiscard]] virtual std::vector<std::string> ShowOpenFileDialog(
            const std::vector<FileDialogFilter>& filters, bool allowMultiple,
            IPlatformWindow* parent) = 0;

        /**
         * @brief Shows a modal file-save dialog.
         *
         * @param filters The file type filters to offer.
         * @param parent The window to parent the dialog to, or null for none.
         * @return The chosen path, or an empty string when the user cancelled.
         * @throws PlatformNotSupportedException If the platform reports no `NativeFileDialog` capability.
         */
        [[nodiscard]] virtual std::string ShowSaveFileDialog(
            const std::vector<FileDialogFilter>& filters, IPlatformWindow* parent) = 0;

        /**
         * @brief Shows a modal folder-selection dialog.
         *
         * @param parent The window to parent the dialog to, or null for none.
         * @return The chosen folder, or an empty string when the user cancelled.
         * @throws PlatformNotSupportedException If the platform reports no `NativeFileDialog` capability.
         */
        [[nodiscard]] virtual std::string ShowOpenFolderDialog(IPlatformWindow* parent) = 0;
    };

    // --- Filesystem ----------------------------------------------------------------------------

    /** @brief Resolves platform paths and loads whole files. */
    class IPlatformFileSystem
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformFileSystem() = default;

        /**
         * @brief Gets the directory the application was launched from.
         *
         * @return The base path, with a trailing separator.
         */
        [[nodiscard]] virtual std::string GetBasePath() const = 0;

        /**
         * @brief Gets a writable per-user directory for this application.
         *
         * Backs `StorageDevice`. Created if it does not exist.
         *
         * @param organization The organization name component.
         * @param application The application name component.
         * @return The preferences path, with a trailing separator.
         * @throws PlatformException If the directory could not be created.
         */
        [[nodiscard]] virtual std::string GetPreferencesPath(const std::string& organization,
                                                             const std::string& application) const = 0;

        /**
         * @brief Loads a whole file into memory.
         *
         * @param path The file to read.
         * @param data Receives the contents; untouched when this returns false.
         * @return True if the file was read. A missing file is an ordinary outcome and returns
         * false rather than throwing.
         */
        [[nodiscard]] virtual bool TryLoadFile(const std::string& path,
                                               std::vector<std::uint8_t>& data) const = 0;

        /**
         * @brief Creates a directory, including missing parents.
         *
         * @param path The directory to create.
         * @throws PlatformException If the directory could not be created.
         */
        virtual void CreateDirectory(const std::string& path) = 0;
    };

    // --- Miscellaneous system information --------------------------------------------------------

    /** @brief How a device is powered. */
    enum class PowerState
    {
        /** @brief Cannot be determined. */
        Unknown,
        /**
         * @brief The query itself failed.
         *
         * Distinct from Unknown: Unknown means "there is no answer on this device", Error means
         * "the answer could not be obtained". A caller showing a battery indicator treats them
         * the same, but one is diagnosable and the other is not.
         */
        Error,
        /** @brief Running on battery. */
        OnBattery,
        /** @brief Plugged in, battery not full. */
        Charging,
        /** @brief Plugged in, battery full. */
        Charged,
        /** @brief Plugged in, no battery present. */
        NoBattery
    };

    /** @brief Battery and power-source state. */
    struct PowerInfo
    {
        /** @brief How the device is powered. */
        PowerState state = PowerState::Unknown;
        /** @brief Remaining charge as a percentage, or -1 when unknown. */
        int percent = -1;
        /** @brief Estimated remaining seconds, or -1 when unknown. */
        int secondsRemaining = -1;
    };

    /**
     * @brief One of the user's preferred locales.
     *
     * Kept structured rather than collapsed into a BCP 47 tag: the consumer
     * (`CNA::Devices::Locale`) needs the parts separately, and re-splitting a formatted tag would
     * be a lossy round-trip through a string for no benefit.
     */
    struct PlatformLocale
    {
        /** @brief ISO 639 language code, e.g. `"cs"`. Never empty in a reported locale. */
        std::string language;
        /** @brief ISO 3166 country code, e.g. `"CZ"`. Empty when the locale names no country. */
        std::string country;
    };

    /** @brief Queries host and power information, and opens URLs. */
    class IPlatformSystemInfo
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformSystemInfo() = default;

        /**
         * @brief Gets the platform's name.
         *
         * @return A stable name such as `"Linux"` or `"Windows"`.
         */
        [[nodiscard]] virtual std::string GetPlatformName() const = 0;

        /**
         * @brief Gets the amount of system RAM.
         *
         * @return RAM in megabytes, or zero when unknown.
         */
        [[nodiscard]] virtual int GetSystemMemoryMegabytes() const = 0;

        /**
         * @brief Gets the number of logical CPU cores.
         *
         * @return The core count, or zero when unknown.
         */
        [[nodiscard]] virtual int GetLogicalCoreCount() const = 0;

        /**
         * @brief Gets the user's preferred locales, most preferred first.
         *
         * @return The locales; empty when unknown.
         */
        [[nodiscard]] virtual std::vector<PlatformLocale> GetPreferredLocales() const = 0;

        /**
         * @brief Gets battery and power-source state.
         *
         * @return The current power info; `Unknown` when the platform reports no `PowerInfo`
         * capability, rather than throwing — callers display this, they do not depend on it.
         */
        [[nodiscard]] virtual PowerInfo GetPowerInfo() const = 0;

        /**
         * @brief Opens a URL in the user's default handler.
         *
         * @param url The URL to open.
         * @return True if the platform accepted it.
         */
        virtual bool OpenUrl(const std::string& url) = 0;
    };

} // namespace CNA::Platform
