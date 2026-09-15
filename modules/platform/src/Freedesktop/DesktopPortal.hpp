// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(CNA_PLATFORM_HAVE_DBUS)
#include "DBusLibrary.hpp"
#endif

namespace CNA::Platform::Freedesktop {

    /** @brief One pattern of a portal filter: the kind (0, a glob) and the glob. */
    struct PortalPattern
    {
        /** @brief 0 for a glob, 1 for a MIME type; CNA only writes globs. */
        std::uint32_t kind = 0;
        /** @brief The glob. */
        std::string pattern;
    };

    /** @brief A file-type filter as the portal takes it: `(sa(us))`. */
    struct PortalFilter
    {
        /** @brief The name shown for it. */
        std::string name;
        /** @brief Its patterns. */
        std::vector<PortalPattern> patterns;
    };

    /**
     * @brief Turns a contract pattern -- an extension such as `png`, or `*` -- into the portal's
     * glob: `*.[pP][nN][gG]`, matching either case as the extension would on Windows.
     *
     * @param pattern The extension, without `*.`.
     * @return The glob; `*` stays `*`.
     */
    [[nodiscard]] std::string PortalGlob(std::string_view pattern);

    /**
     * @brief Turns the contract's filters into the portal's.
     * @param filters The filters, their patterns separated by semicolons.
     * @return One portal filter each, empty patterns dropped.
     */
    [[nodiscard]] std::vector<PortalFilter> ToPortalFilters(const std::vector<FileDialogFilter>& filters);

    /**
     * @brief The path a `file:` URI names.
     *
     * Accepts `file:///path` and `file://localhost/path`, percent-decoded. What the portal returns
     * is always local; a URI with another host, another scheme or an encoded NUL names no path
     * here.
     *
     * @param uri The URI.
     * @return The path, or nothing.
     */
    [[nodiscard]] std::optional<std::string> PathFromFileUri(std::string_view uri);

    /**
     * @brief The object path a portal request will have: the portal builds it from the caller's
     * unique name and the token the caller chose, so a caller can listen before it asks.
     *
     * @param uniqueName The caller's unique bus name, e.g. `:1.42`.
     * @param token The `handle_token` the caller sent.
     * @return `/org/freedesktop/portal/desktop/request/1_42/<token>`.
     */
    [[nodiscard]] std::string PortalRequestPath(std::string_view uniqueName, std::string_view token);

    /** @brief Where a save dialog starts, in the portal's terms. */
    struct PortalSaveLocation
    {
        /** @brief `current_folder`: the directory it opens in, or empty. */
        std::string currentFolder;
        /** @brief `current_name`: the name it suggests, or empty. */
        std::string currentName;
        /** @brief `current_file`: an existing file it starts on, or empty. */
        std::string currentFile;
    };

    /**
     * @brief Reads a save dialog's default location the way the contract states it: a directory,
     * or a file name, with or without its directory.
     *
     * An existing directory is where the dialog opens; an existing file is where it starts, under
     * its own name; a name in an existing directory is suggested there; a bare name is suggested
     * wherever the portal opens.
     *
     * @param defaultLocation The location, or empty.
     * @return The portal's options for it.
     */
    [[nodiscard]] PortalSaveLocation SplitSaveLocation(const std::string& defaultLocation);

#if defined(CNA_PLATFORM_HAVE_DBUS)

    /**
     * @brief File dialogs and URL opening through xdg-desktop-portal (plans/plan_x11.md X11-0169).
     *
     * Shared by the X11 and Wayland backends (plans/plan_wayland.md WAYLAND-0011): nothing here
     * knows the window system. A backend names its parent window in the portal's own notation --
     * `x11:<hex xid>`, `wayland:<xdg-foreign handle>` -- or passes an empty string for none.
     *
     * The portal is the desktop's own service on the session bus: GNOME, KDE and the others each
     * put their file chooser behind it, sandboxed applications included. Nothing is started to
     * ask it -- no `zenity`, no `xdg-open`; the bus starts the portal itself if it is not running.
     * A file dialog is a request the portal answers later: the result is read from the bus when
     * the platform pumps events, and the callback runs then, never inside the `Show*` call.
     */
    class DesktopPortal
    {
    public:
        /** @brief Which file chooser. */
        enum class FileRequest
        {
            /** @brief Open one or more files. */
            Open,
            /** @brief Save one file. */
            Save,
            /** @brief Choose one or more folders. */
            OpenFolder
        };

        /**
         * @brief Connects to the session bus and looks for the portal there, without starting it.
         * @return The client, or null where there is no session bus, no libdbus or no portal.
         */
        [[nodiscard]] static std::unique_ptr<DesktopPortal> Connect();

        /** @brief Closes the connection; a dialog still open is never answered. */
        ~DesktopPortal();

        DesktopPortal(const DesktopPortal&) = delete;
        DesktopPortal& operator=(const DesktopPortal&) = delete;

        /**
         * @brief Asks the portal for a file chooser.
         *
         * @param kind Which one.
         * @param onResult Called once from a later Pump(): the chosen paths, or none when the user
         * cancelled or the portal could not show one.
         * @param filters The file types, for Open and Save.
         * @param defaultLocation Where it starts; for Save a directory or a file name.
         * @param multiple Whether several may be chosen (Open and OpenFolder).
         * @param parentWindow The window it belongs to in the portal's notation, or empty; a
         * dialog with a parent is modal to it.
         */
        void ShowFileDialog(FileRequest kind, FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                            const std::string& defaultLocation, bool multiple, const std::string& parentWindow);

        /**
         * @brief Asks the portal to open a URL in the user's default handler.
         *
         * A `file:` URL is handed over as an open file descriptor (`OpenFile`), which is how the
         * portal takes local files; anything else by its URI (`OpenURI`).
         *
         * @param uri The URL.
         * @param parentWindow The window the handler's own dialog belongs to, in the portal's
         * notation, or empty.
         * @return True when the portal accepted the request.
         */
        [[nodiscard]] bool OpenUri(const std::string& uri, const std::string& parentWindow);

        /**
         * @brief Reads the portal's answers and runs the callbacks of the dialogs they finish.
         *
         * Returns at once when no dialog is open, so it can be called every frame.
         */
        void Pump();

        /** @brief Gets the connection's unique name on the bus. @return The name. */
        [[nodiscard]] const std::string& GetUniqueName() const { return uniqueName_; }

    private:
        struct Request
        {
            FileDialogCallback callback;
            std::string path;
            dbus_uint32_t serial = 0;
        };

        explicit DesktopPortal(DBusConnection* connection);

        void FinishLocked(std::size_t index, std::vector<std::string> paths);

        std::mutex mutex_;
        DBusConnection* connection_ = nullptr;
        std::string uniqueName_;
        unsigned counter_ = 0;
        std::vector<Request> pending_;
        std::vector<std::pair<FileDialogCallback, std::vector<std::string>>> finished_;
    };

#endif // CNA_PLATFORM_HAVE_DBUS

} // namespace CNA::Platform::Freedesktop
