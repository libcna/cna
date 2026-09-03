// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformWindow.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform {

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
         * @brief Gets the unobscured interactive region of a window.
         *
         * @param window The window whose client area is being described.
         * @param safeArea Receives the region in client coordinates; untouched on false.
         * @return True when the platform can report a safe area for this window.
         */
        [[nodiscard]] virtual bool TryGetSafeAreaForWindow(
            const IPlatformWindow& window, WindowBounds& safeArea) const = 0;

        /**
         * @brief Gets the modes a display supports.
         *
         * @param displayId Which display.
         * @return The supported modes; empty when the display is unknown or exposes none.
         */
        [[nodiscard]] virtual std::vector<DisplayMode> GetDisplayModes(std::uint32_t displayId) const = 0;

        /**
         * @brief Gets the mode a display is currently using.
         *
         * This can differ from @ref DisplayInfo::desktopMode while an application owns an
         * exclusive-fullscreen mode.
         *
         * @param displayId Which display.
         * @param mode Receives the current mode; untouched when this returns false.
         * @return True when the display is known and its current mode is available.
         */
        [[nodiscard]] virtual bool TryGetCurrentDisplayMode(std::uint32_t displayId, DisplayMode& mode) const = 0;

        /** @brief Gets whether the host screen saver may activate. */
        [[nodiscard]] virtual bool IsScreenSaverEnabled() const { return true; }

        /**
         * @brief Allows or prevents the host screen saver from activating.
         * @param enabled True to allow screen saving.
         *
         * The default no-op keeps lightweight display test doubles focused on enumeration. A
         * platform with real displays overrides this pair; a platform without them exposes no
         * display service.
         */
        virtual void SetScreenSaverEnabled(bool enabled) { (void)enabled; }
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

    /**
     * @brief Receives a file dialog's result.
     *
     * Called exactly once per dialog, after the `Show*` call has already returned, with the
     * selected paths — or an empty list when the user cancelled.
     */
    using FileDialogCallback = std::function<void(const std::vector<std::string>& paths)>;

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
         * @brief Shows a modal message box with a choice of buttons and reports which was chosen.
         *
         * Separate from the single-button form rather than an optional parameter on it, because
         * the two answer different questions: one informs, the other asks. A caller that only
         * informs should not have to deal with a return value it must then decide to ignore.
         *
         * @param severity How prominently to present it.
         * @param title The dialog title.
         * @param message The message body.
         * @param buttons The button labels, in display order. Must not be empty.
         * @param parent The window to parent the dialog to, or null for none.
         * @return The index of the chosen button, or -1 when the dialog was dismissed without a
         * choice. Dismissal is an ordinary outcome and does not throw.
         * @throws PlatformNotSupportedException If the platform reports no `MessageBox` capability.
         */
        [[nodiscard]] virtual int ShowMessageBoxWithButtons(
            MessageBoxSeverity severity, const std::string& title, const std::string& message,
            const std::vector<std::string>& buttons, IPlatformWindow* parent) = 0;

        // --- File dialogs ------------------------------------------------------------------
        //
        // Asynchronous, unlike the message box above, and the asymmetry is not a style choice.
        // A message box genuinely blocks; a file dialog on every platform CNA targets does not.
        // A synchronous signature could only be honoured by pumping the event loop from inside a
        // call the game makes during its own frame, which reenters the game loop and deadlocks
        // as readily as it works. CNA's own `CNA::Devices::FileDialog` is callback-shaped for the
        // same reason, so this is also what its one consumer already expects.
        //
        // The callback fires exactly once per call, on the thread that pumps events, and after
        // the Show* call has already returned. It receives an empty list when the user cancelled
        // — cancellation is an ordinary outcome and is not an error.

        /**
         * @brief Shows a file-open dialog and reports the result to a callback.
         *
         * @param onResult Called once with the selected paths, or with an empty list when the
         * user cancelled. Must not be empty.
         * @param filters The file type filters to offer.
         * @param defaultLocation The directory to start in, or empty for the platform default.
         * @param allowMultiple Whether more than one file may be selected.
         * @param parent The window to parent the dialog to, or null for none.
         * @throws PlatformNotSupportedException If the platform reports no `NativeFileDialog` capability.
         */
        virtual void ShowOpenFileDialog(FileDialogCallback onResult,
                                        const std::vector<FileDialogFilter>& filters,
                                        const std::string& defaultLocation, bool allowMultiple,
                                        IPlatformWindow* parent) = 0;

        /**
         * @brief Shows a file-save dialog and reports the result to a callback.
         *
         * @param onResult Called once with the chosen path as a single-element list, or with an
         * empty list when the user cancelled. Must not be empty.
         * @param filters The file type filters to offer.
         * @param defaultLocation The directory or file name to start with, or empty for the
         * platform default.
         * @param parent The window to parent the dialog to, or null for none.
         * @throws PlatformNotSupportedException If the platform reports no `NativeFileDialog` capability.
         */
        virtual void ShowSaveFileDialog(FileDialogCallback onResult,
                                        const std::vector<FileDialogFilter>& filters,
                                        const std::string& defaultLocation,
                                        IPlatformWindow* parent) = 0;

        /**
         * @brief Shows a folder-selection dialog and reports the result to a callback.
         *
         * @param onResult Called once with the chosen folders, or with an empty list when the
         * user cancelled. Must not be empty.
         * @param defaultLocation The directory to start in, or empty for the platform default.
         * @param allowMultiple Whether more than one folder may be selected.
         * @param parent The window to parent the dialog to, or null for none.
         * @throws PlatformNotSupportedException If the platform reports no `NativeFileDialog` capability.
         */
        virtual void ShowOpenFolderDialog(FileDialogCallback onResult,
                                          const std::string& defaultLocation, bool allowMultiple,
                                          IPlatformWindow* parent) = 0;
    };

    // --- System tray ---------------------------------------------------------------------------

    /** @brief Invoked whenever the user activates a tray-menu entry. */
    using TrayEntryClickCallback = std::function<void()>;

    /**
     * @brief One owned system-tray icon and its flat menu.
     *
     * Destroying the interface removes the native icon and all of its entries. Entry indices are
     * stable for that lifetime and are deliberately opaque: implementations may use pointers,
     * integer commands or another native identity underneath them.
     */
    class IPlatformTrayIcon
    {
    public:
        /** @brief Removes the icon and destroys the interface. */
        virtual ~IPlatformTrayIcon() = default;

        /**
         * @brief Changes the icon's tooltip.
         * @param tooltip The new UTF-8 tooltip, or empty to clear it.
         */
        virtual void SetTooltip(const std::string& tooltip) = 0;

        /**
         * @brief Appends an entry to the icon's flat menu.
         *
         * Nested menus are absent deliberately: CNA has no caller for them, and a second
         * implementation should not have to invent hierarchy merely because SDL can express it.
         *
         * @param label The UTF-8 entry label.
         * @param checkable Whether the entry has a checked state.
         * @param initiallyChecked Its initial checked state when checkable.
         * @param initiallyEnabled Whether it initially accepts activation.
         * @param onClick Called every time the user activates the entry; may be empty.
         * @return A stable index for the remaining entry operations.
         * @throws PlatformException If the native entry cannot be created.
         */
        [[nodiscard]] virtual std::size_t AddEntry(
            const std::string& label, bool checkable, bool initiallyChecked,
            bool initiallyEnabled, TrayEntryClickCallback onClick) = 0;

        /**
         * @brief Changes an entry's label.
         * @param index The stable index returned by AddEntry; an unknown index is ignored.
         * @param label The new UTF-8 label.
         */
        virtual void SetEntryLabel(std::size_t index, const std::string& label) = 0;

        /**
         * @brief Changes an entry's checked state.
         * @param index The stable index returned by AddEntry; an unknown index is ignored.
         * @param checked The new state.
         */
        virtual void SetEntryChecked(std::size_t index, bool checked) = 0;

        /**
         * @brief Reads an entry's checked state.
         * @param index The stable index returned by AddEntry.
         * @return The state, or false for an unknown index.
         */
        [[nodiscard]] virtual bool GetEntryChecked(std::size_t index) const = 0;

        /**
         * @brief Changes whether an entry accepts activation.
         * @param index The stable index returned by AddEntry; an unknown index is ignored.
         * @param enabled The new state.
         */
        virtual void SetEntryEnabled(std::size_t index, bool enabled) = 0;

        /**
         * @brief Reads whether an entry accepts activation.
         * @param index The stable index returned by AddEntry.
         * @return The state, or false for an unknown index.
         */
        [[nodiscard]] virtual bool GetEntryEnabled(std::size_t index) const = 0;
    };

    /** @brief Creates independently owned system-tray icons. */
    class IPlatformTray
    {
    public:
        /** @brief Destroys the service after every icon it created has been released. */
        virtual ~IPlatformTray() = default;

        /**
         * @brief Creates a tray icon and its empty flat menu.
         * @param tooltip The initial UTF-8 tooltip, or empty for none.
         * @return The owned icon; destroying it removes the native icon.
         * @throws PlatformException If the host advertises tray support but creation fails.
         * @throws PlatformNotSupportedException If the platform reports no `Tray` capability.
         */
        [[nodiscard]] virtual std::unique_ptr<IPlatformTrayIcon> CreateTray(
            const std::string& tooltip) = 0;
    };

    // --- Filesystem ----------------------------------------------------------------------------

    /** @brief Well-known user folders exposed by the platform filesystem service. */
    enum class UserFolder
    {
        Music,
        Pictures
    };

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
         * @brief Gets a well-known per-user folder.
         * @param folder Which folder to resolve.
         * @return The native path with a trailing separator, or empty when unavailable.
         */
        [[nodiscard]] virtual std::string GetUserFolder(UserFolder folder) const = 0;

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
         * @brief Loads a whole file while matching path components without ASCII case.
         *
         * XNA content names originate on a case-insensitive Windows filesystem, while packaged
         * assets on Android and other targets can be case-sensitive. Ambiguous matches must fail
         * rather than selecting an arbitrary file.
         *
         * @param path The file to read.
         * @param data Receives the contents; untouched when this returns false.
         * @return True if exactly one matching file was read.
         */
        [[nodiscard]] virtual bool TryLoadFileIgnoringCase(
            const std::string& path, std::vector<std::uint8_t>& data) const = 0;

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
