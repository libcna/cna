// SPDX-License-Identifier: MS-PL

#include "X11DesktopPortal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>

#if defined(CNA_X11_HAVE_DBUS)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace CNA::Platform::X11 {

    namespace {

        int HexValue(const char digit)
        {
            if (digit >= '0' && digit <= '9') { return digit - '0'; }
            if (digit >= 'a' && digit <= 'f') { return digit - 'a' + 10; }
            if (digit >= 'A' && digit <= 'F') { return digit - 'A' + 10; }
            return -1;
        }

    } // namespace

    std::string PortalGlob(const std::string_view pattern)
    {
        if (pattern == "*")
        {
            return "*";
        }
        std::string glob = "*.";
        for (const char character : pattern)
        {
            const auto byte = static_cast<unsigned char>(character);
            if (std::isalpha(byte) != 0 && byte < 0x80)
            {
                glob += '[';
                glob += static_cast<char>(std::tolower(byte));
                glob += static_cast<char>(std::toupper(byte));
                glob += ']';
            }
            else
            {
                glob += character;
            }
        }
        return glob;
    }

    std::vector<X11PortalFilter> ToPortalFilters(const std::vector<FileDialogFilter>& filters)
    {
        std::vector<X11PortalFilter> result;
        for (const FileDialogFilter& filter : filters)
        {
            X11PortalFilter converted;
            converted.name = filter.name;
            std::size_t start = 0;
            while (start <= filter.patterns.size())
            {
                std::size_t end = filter.patterns.find(';', start);
                if (end == std::string::npos) { end = filter.patterns.size(); }
                std::string_view piece(filter.patterns.data() + start, end - start);
                while (!piece.empty() && piece.front() == ' ') { piece.remove_prefix(1); }
                while (!piece.empty() && piece.back() == ' ') { piece.remove_suffix(1); }
                if (piece.rfind("*.", 0) == 0 && piece.size() > 2) { piece.remove_prefix(2); }
                if (!piece.empty())
                {
                    converted.patterns.push_back({0, PortalGlob(piece)});
                }
                start = end + 1;
            }
            if (!converted.patterns.empty())
            {
                result.push_back(std::move(converted));
            }
        }
        return result;
    }

    std::optional<std::string> PathFromFileUri(const std::string_view uri)
    {
        constexpr std::string_view scheme = "file://";
        if (uri.size() < scheme.size() ||
            !std::equal(scheme.begin(), scheme.end(), uri.begin(),
                        [](const char expected, const char actual) {
                            return std::tolower(static_cast<unsigned char>(actual)) == expected;
                        }))
        {
            return std::nullopt;
        }
        std::string_view rest = uri.substr(scheme.size());
        const std::size_t slash = rest.find('/');
        if (slash == std::string_view::npos)
        {
            return std::nullopt;
        }
        const std::string_view host = rest.substr(0, slash);
        if (!host.empty() && host != "localhost")
        {
            return std::nullopt;
        }
        rest.remove_prefix(slash);
        std::string path;
        for (std::size_t at = 0; at < rest.size(); ++at)
        {
            const char character = rest[at];
            if (character == '?' || character == '#')
            {
                break;
            }
            if (character != '%')
            {
                path += character;
                continue;
            }
            if (at + 2 >= rest.size())
            {
                return std::nullopt;
            }
            const int high = HexValue(rest[at + 1]);
            const int low = HexValue(rest[at + 2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0))
            {
                return std::nullopt;
            }
            path += static_cast<char>(high * 16 + low);
            at += 2;
        }
        return path;
    }

    std::string PortalRequestPath(const std::string_view uniqueName, const std::string_view token)
    {
        std::string sender(uniqueName);
        if (!sender.empty() && sender.front() == ':')
        {
            sender.erase(0, 1);
        }
        std::replace(sender.begin(), sender.end(), '.', '_');
        return "/org/freedesktop/portal/desktop/request/" + sender + "/" + std::string(token);
    }

    std::string PortalParentWindow(const unsigned long xid)
    {
        if (xid == 0)
        {
            return {};
        }
        char text[32] = {};
        std::snprintf(text, sizeof(text), "x11:%lx", xid);
        return text;
    }

    X11PortalSaveLocation SplitSaveLocation(const std::string& defaultLocation)
    {
        X11PortalSaveLocation location;
        if (defaultLocation.empty())
        {
            return location;
        }
        namespace fs = std::filesystem;
        std::error_code error;
        const fs::path path(defaultLocation);
        if (fs::is_directory(path, error))
        {
            location.currentFolder = path.string();
            return location;
        }
        const std::string name = path.filename().string();
        if (fs::is_regular_file(path, error))
        {
            location.currentFile = path.string();
            location.currentName = name;
            return location;
        }
        location.currentName = name;
        if (path.has_parent_path() && fs::is_directory(path.parent_path(), error))
        {
            location.currentFolder = path.parent_path().string();
        }
        return location;
    }

#if defined(CNA_X11_HAVE_DBUS)

    namespace {

        constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
        constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
        constexpr const char* kFileChooser = "org.freedesktop.portal.FileChooser";
        constexpr const char* kOpenUri = "org.freedesktop.portal.OpenURI";
        constexpr const char* kRequest = "org.freedesktop.portal.Request";
        /// How long a synchronous question to the bus or the portal may take.
        constexpr int kCallTimeoutMilliseconds = 5000;

        void AppendString(DBusMessageIter* iter, const std::string& value)
        {
            const char* text = value.c_str();
            DBusApi().message_iter_append_basic(iter, DBUS_TYPE_STRING, &text);
        }

        /// One `{sv}` entry of an options dictionary.
        void AppendOption(DBusMessageIter* dictionary, const char* key, const char* signature,
                          const std::function<void(DBusMessageIter*)>& value)
        {
            const X11DBusApi& dbus = DBusApi();
            DBusMessageIter entry;
            DBusMessageIter variant;
            dbus.message_iter_open_container(dictionary, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus.message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus.message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant);
            value(&variant);
            dbus.message_iter_close_container(&entry, &variant);
            dbus.message_iter_close_container(dictionary, &entry);
        }

        void AppendBoolOption(DBusMessageIter* dictionary, const char* key, const bool value)
        {
            AppendOption(dictionary, key, "b", [value](DBusMessageIter* variant) {
                const dbus_bool_t flag = value ? TRUE : FALSE;
                DBusApi().message_iter_append_basic(variant, DBUS_TYPE_BOOLEAN, &flag);
            });
        }

        void AppendStringOption(DBusMessageIter* dictionary, const char* key, const std::string& value)
        {
            AppendOption(dictionary, key, "s", [&value](DBusMessageIter* variant) { AppendString(variant, value); });
        }

        /// A path as the portal takes it: bytes, NUL-terminated (`ay`).
        void AppendPathOption(DBusMessageIter* dictionary, const char* key, const std::string& path)
        {
            AppendOption(dictionary, key, "ay", [&path](DBusMessageIter* variant) {
                const X11DBusApi& dbus = DBusApi();
                DBusMessageIter bytes;
                dbus.message_iter_open_container(variant, DBUS_TYPE_ARRAY, "y", &bytes);
                const char* data = path.c_str();
                dbus.message_iter_append_fixed_array(&bytes, DBUS_TYPE_BYTE, &data, static_cast<int>(path.size() + 1));
                dbus.message_iter_close_container(variant, &bytes);
            });
        }

        void AppendFiltersOption(DBusMessageIter* dictionary, const std::vector<X11PortalFilter>& filters)
        {
            AppendOption(dictionary, "filters", "a(sa(us))", [&filters](DBusMessageIter* variant) {
                const X11DBusApi& dbus = DBusApi();
                DBusMessageIter list;
                dbus.message_iter_open_container(variant, DBUS_TYPE_ARRAY, "(sa(us))", &list);
                for (const X11PortalFilter& filter : filters)
                {
                    DBusMessageIter entry;
                    dbus.message_iter_open_container(&list, DBUS_TYPE_STRUCT, nullptr, &entry);
                    AppendString(&entry, filter.name);
                    DBusMessageIter patterns;
                    dbus.message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "(us)", &patterns);
                    for (const X11PortalPattern& pattern : filter.patterns)
                    {
                        DBusMessageIter pair;
                        dbus.message_iter_open_container(&patterns, DBUS_TYPE_STRUCT, nullptr, &pair);
                        const dbus_uint32_t kind = pattern.kind;
                        dbus.message_iter_append_basic(&pair, DBUS_TYPE_UINT32, &kind);
                        AppendString(&pair, pattern.pattern);
                        dbus.message_iter_close_container(&patterns, &pair);
                    }
                    dbus.message_iter_close_container(&entry, &patterns);
                    dbus.message_iter_close_container(&list, &entry);
                }
                dbus.message_iter_close_container(variant, &list);
            });
        }

        /// The `uris` of a Response's results, as local paths.
        std::vector<std::string> ResponsePaths(DBusMessage* message, bool& success)
        {
            const X11DBusApi& dbus = DBusApi();
            std::vector<std::string> paths;
            success = false;
            DBusMessageIter iter;
            if (!dbus.message_iter_init(message, &iter) || dbus.message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32)
            {
                return paths;
            }
            dbus_uint32_t response = 2;
            dbus.message_iter_get_basic(&iter, &response);
            success = response == 0;
            if (!success || !dbus.message_iter_next(&iter) || dbus.message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
            {
                return paths;
            }
            DBusMessageIter results;
            dbus.message_iter_recurse(&iter, &results);
            while (dbus.message_iter_get_arg_type(&results) == DBUS_TYPE_DICT_ENTRY)
            {
                DBusMessageIter entry;
                dbus.message_iter_recurse(&results, &entry);
                const char* key = nullptr;
                if (dbus.message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
                {
                    dbus.message_iter_get_basic(&entry, &key);
                }
                if (key != nullptr && std::string_view(key) == "uris" && dbus.message_iter_next(&entry) &&
                    dbus.message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT)
                {
                    DBusMessageIter variant;
                    dbus.message_iter_recurse(&entry, &variant);
                    if (dbus.message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY)
                    {
                        DBusMessageIter uris;
                        dbus.message_iter_recurse(&variant, &uris);
                        while (dbus.message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING)
                        {
                            const char* uri = nullptr;
                            dbus.message_iter_get_basic(&uris, &uri);
                            if (uri != nullptr)
                            {
                                if (std::optional<std::string> path = PathFromFileUri(uri))
                                {
                                    paths.push_back(std::move(*path));
                                }
                            }
                            dbus.message_iter_next(&uris);
                        }
                    }
                }
                dbus.message_iter_next(&results);
            }
            return paths;
        }

        /// Whether the bus knows the portal: running, or one it would start when asked.
        bool PortalOnBus(DBusConnection* connection)
        {
            const X11DBusApi& dbus = DBusApi();
            DBusError error;
            dbus.error_init(&error);
            if (dbus.bus_name_has_owner(connection, kPortalService, &error))
            {
                return true;
            }
            dbus.error_free(&error);
            X11DBusMessagePtr call(dbus.message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                                "org.freedesktop.DBus", "ListActivatableNames"));
            if (call == nullptr)
            {
                return false;
            }
            dbus.error_init(&error);
            X11DBusMessagePtr reply(
                dbus.connection_send_with_reply_and_block(connection, call.get(), kCallTimeoutMilliseconds, &error));
            if (reply == nullptr)
            {
                dbus.error_free(&error);
                return false;
            }
            DBusMessageIter iter;
            if (!dbus.message_iter_init(reply.get(), &iter) || dbus.message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
            {
                return false;
            }
            DBusMessageIter names;
            dbus.message_iter_recurse(&iter, &names);
            while (dbus.message_iter_get_arg_type(&names) == DBUS_TYPE_STRING)
            {
                const char* name = nullptr;
                dbus.message_iter_get_basic(&names, &name);
                if (name != nullptr && std::string_view(name) == kPortalService)
                {
                    return true;
                }
                dbus.message_iter_next(&names);
            }
            return false;
        }

    } // namespace

    X11DesktopPortal::X11DesktopPortal(DBusConnection* connection) : connection_(connection)
    {
        const char* name = DBusApi().bus_get_unique_name(connection_);
        uniqueName_ = name != nullptr ? name : "";
    }

    std::unique_ptr<X11DesktopPortal> X11DesktopPortal::Connect()
    {
        DBusConnection* connection = OpenSessionBus();
        if (connection == nullptr)
        {
            return nullptr;
        }
        if (!PortalOnBus(connection))
        {
            CloseSessionBus(connection);
            return nullptr;
        }
        const X11DBusApi& dbus = DBusApi();
        DBusError error;
        dbus.error_init(&error);
        // Every request's answer: a portal may send it to this connection alone or to all.
        dbus.bus_add_match(connection, "type='signal',interface='org.freedesktop.portal.Request',member='Response'",
                           &error);
        if (dbus.error_is_set(&error))
        {
            dbus.error_free(&error);
            CloseSessionBus(connection);
            return nullptr;
        }
        return std::unique_ptr<X11DesktopPortal>(new X11DesktopPortal(connection));
    }

    X11DesktopPortal::~X11DesktopPortal()
    {
        CloseSessionBus(connection_);
    }

    void X11DesktopPortal::ShowFileDialog(const FileRequest kind, FileDialogCallback onResult,
                                          const std::vector<FileDialogFilter>& filters,
                                          const std::string& defaultLocation, const bool multiple,
                                          const unsigned long parentXid)
    {
        const X11DBusApi& dbus = DBusApi();
        std::lock_guard<std::mutex> lock(mutex_);
        X11DBusMessagePtr call(dbus.message_new_method_call(kPortalService, kPortalPath, kFileChooser,
                                                            kind == FileRequest::Save ? "SaveFile" : "OpenFile"));
        if (call == nullptr)
        {
            finished_.emplace_back(std::move(onResult), std::vector<std::string>());
            return;
        }
        DBusMessageIter arguments;
        dbus.message_iter_init_append(call.get(), &arguments);
        AppendString(&arguments, PortalParentWindow(parentXid));
        AppendString(&arguments, kind == FileRequest::Save         ? "Save File"
                                 : kind == FileRequest::OpenFolder ? "Open Folder"
                                                                   : "Open File");
        DBusMessageIter options;
        dbus.message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &options);
        const std::string token = "cna" + std::to_string(++counter_);
        AppendStringOption(&options, "handle_token", token);
        AppendBoolOption(&options, "modal", parentXid != 0);
        if (kind != FileRequest::Save && multiple)
        {
            AppendBoolOption(&options, "multiple", true);
        }
        if (kind == FileRequest::OpenFolder)
        {
            AppendBoolOption(&options, "directory", true);
        }
        if (kind != FileRequest::OpenFolder)
        {
            const std::vector<X11PortalFilter> portalFilters = ToPortalFilters(filters);
            if (!portalFilters.empty())
            {
                AppendFiltersOption(&options, portalFilters);
            }
        }
        if (kind == FileRequest::Save)
        {
            const X11PortalSaveLocation location = SplitSaveLocation(defaultLocation);
            if (!location.currentFile.empty()) { AppendPathOption(&options, "current_file", location.currentFile); }
            if (!location.currentFolder.empty()) { AppendPathOption(&options, "current_folder", location.currentFolder); }
            if (!location.currentName.empty()) { AppendStringOption(&options, "current_name", location.currentName); }
        }
        else if (!defaultLocation.empty())
        {
            AppendPathOption(&options, "current_folder", defaultLocation);
        }
        dbus.message_iter_close_container(&arguments, &options);

        Request request;
        request.callback = std::move(onResult);
        request.path = PortalRequestPath(uniqueName_, token);
        if (!dbus.connection_send(connection_, call.get(), &request.serial))
        {
            finished_.emplace_back(std::move(request.callback), std::vector<std::string>());
            return;
        }
        dbus.connection_flush(connection_);
        pending_.push_back(std::move(request));
    }

    bool X11DesktopPortal::OpenUri(const std::string& uri, const unsigned long parentXid)
    {
        const X11DBusApi& dbus = DBusApi();
        std::lock_guard<std::mutex> lock(mutex_);
        const std::optional<std::string> path = PathFromFileUri(uri);
        int descriptor = -1;
        X11DBusMessagePtr call;
        if (path)
        {
            if (!dbus.connection_can_send_type(connection_, DBUS_TYPE_UNIX_FD))
            {
                return false;
            }
            descriptor = ::open(path->c_str(), O_RDONLY | O_CLOEXEC);
            if (descriptor < 0)
            {
                return false;
            }
            call.reset(dbus.message_new_method_call(kPortalService, kPortalPath, kOpenUri, "OpenFile"));
        }
        else
        {
            // A scheme, then the rest; a string without one is not a URL anything would open.
            const std::size_t colon = uri.find(':');
            if (colon == 0 || colon == std::string::npos ||
                !std::all_of(uri.begin(), uri.begin() + static_cast<std::ptrdiff_t>(colon), [](const char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' || c == '-' || c == '.';
                }))
            {
                return false;
            }
            call.reset(dbus.message_new_method_call(kPortalService, kPortalPath, kOpenUri, "OpenURI"));
        }
        if (call == nullptr)
        {
            if (descriptor >= 0) { ::close(descriptor); }
            return false;
        }
        DBusMessageIter arguments;
        dbus.message_iter_init_append(call.get(), &arguments);
        AppendString(&arguments, PortalParentWindow(parentXid));
        if (descriptor >= 0)
        {
            // libdbus sends its own duplicate; this one is closed below either way.
            dbus.message_iter_append_basic(&arguments, DBUS_TYPE_UNIX_FD, &descriptor);
        }
        else
        {
            AppendString(&arguments, uri);
        }
        DBusMessageIter options;
        dbus.message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &options);
        AppendStringOption(&options, "handle_token", "cna" + std::to_string(++counter_));
        dbus.message_iter_close_container(&arguments, &options);

        DBusError error;
        dbus.error_init(&error);
        X11DBusMessagePtr reply(
            dbus.connection_send_with_reply_and_block(connection_, call.get(), kCallTimeoutMilliseconds, &error));
        if (descriptor >= 0)
        {
            ::close(descriptor);
        }
        if (reply == nullptr)
        {
            dbus.error_free(&error);
            return false;
        }
        return true;
    }

    void X11DesktopPortal::FinishLocked(const std::size_t index, std::vector<std::string> paths)
    {
        finished_.emplace_back(std::move(pending_[index].callback), std::move(paths));
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void X11DesktopPortal::Pump()
    {
        std::vector<std::pair<FileDialogCallback, std::vector<std::string>>> ready;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_.empty() && finished_.empty())
            {
                return;
            }
            const X11DBusApi& dbus = DBusApi();
            if (!pending_.empty())
            {
                dbus.connection_read_write(connection_, 0);
                while (DBusMessage* raw = dbus.connection_pop_message(connection_))
                {
                    X11DBusMessagePtr message(raw);
                    const int type = dbus.message_get_type(raw);
                    if (type == DBUS_MESSAGE_TYPE_METHOD_RETURN || type == DBUS_MESSAGE_TYPE_ERROR)
                    {
                        const dbus_uint32_t serial = dbus.message_get_reply_serial(raw);
                        for (std::size_t index = 0; index < pending_.size(); ++index)
                        {
                            if (pending_[index].serial != serial)
                            {
                                continue;
                            }
                            if (type == DBUS_MESSAGE_TYPE_ERROR)
                            {
                                // No chooser could be shown (no backend for it, a refused
                                // parent...): as the contract has it, an empty answer.
                                FinishLocked(index, {});
                                break;
                            }
                            DBusMessageIter iter;
                            if (dbus.message_iter_init(raw, &iter) &&
                                dbus.message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH)
                            {
                                const char* handle = nullptr;
                                dbus.message_iter_get_basic(&iter, &handle);
                                // An older portal chooses its own path; its answer comes there.
                                if (handle != nullptr) { pending_[index].path = handle; }
                            }
                            break;
                        }
                    }
                    else if (dbus.message_is_signal(raw, kRequest, "Response"))
                    {
                        const char* path = dbus.message_get_path(raw);
                        for (std::size_t index = 0; path != nullptr && index < pending_.size(); ++index)
                        {
                            if (pending_[index].path == path)
                            {
                                bool success = false;
                                std::vector<std::string> paths = ResponsePaths(raw, success);
                                FinishLocked(index, success ? std::move(paths) : std::vector<std::string>());
                                break;
                            }
                        }
                    }
                }
                if (!dbus.connection_get_is_connected(connection_))
                {
                    // The bus went away, and with it every answer still to come.
                    while (!pending_.empty())
                    {
                        FinishLocked(0, {});
                    }
                }
            }
            ready.swap(finished_);
        }
        // Outside the lock: a callback may well ask for another dialog.
        for (auto& [callback, paths] : ready)
        {
            if (callback)
            {
                callback(paths);
            }
        }
    }

#endif // CNA_X11_HAVE_DBUS

} // namespace CNA::Platform::X11
