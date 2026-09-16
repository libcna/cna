// SPDX-License-Identifier: MS-PL
//
// org.freedesktop.portal.Desktop, played by the tests themselves on a private bus.
//
// Written for X11-0169 and shared with the Wayland backend (plans/plan_wayland.md WAYLAND-0091),
// which asks the same portal the same questions and differs only in the parent-window handle it
// sends ("wayland:<handle>" from xdg-foreign, where X11 sends "x11:<window>"). One portal played
// one way keeps the two backends' tests comparable, and keeps the answer to "what did the portal
// actually receive" in one place.
//
// **No test may reach the session bus of the desktop it runs on**: a file chooser opened there
// would open on that desktop. Use this only with FreedesktopPrivateSessionBus.hpp's PrivateBus,
// whose dbus-daemon is configured with no service it could start, so no real portal can appear
// on it.
#pragma once

#if defined(CNA_PLATFORM_HAVE_DBUS)

#include "../../../src/Freedesktop/DBusLibrary.hpp"
#include "../../../src/Freedesktop/DesktopPortal.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace CNA::Platform::Freedesktop::Testing {

/// What the test's portal was asked.
struct PortalCall
{
    std::string interface;
    std::string method;
    std::string sender;
    std::string parent;
    std::string title;
    std::string uri;
    std::string fileTarget;  ///< Where an OpenFile descriptor pointed.
    std::map<std::string, std::string> strings;
    std::map<std::string, bool> flags;
    std::map<std::string, std::string> paths;  ///< The `ay` options, NUL dropped.
    std::vector<PortalFilter> filters;
};

/// How the test's portal answers the next file chooser.
struct PortalAnswer
{
    std::uint32_t response = 0;
    std::vector<std::string> uris;
    bool error = false;          ///< Reply with an error instead of a request.
    bool ownPath = false;        ///< Choose its own request path, as an older portal does.
    bool silent = false;         ///< Never send the Response.
};

/// org.freedesktop.portal.Desktop, played on a private bus from a thread of its own.
class FakePortal
{
public:
    explicit FakePortal(const std::string& address)
    {
        const DBusLibrary& dbus = GetDBus();
        DBusError error;
        dbus.error_init(&error);
        connection_ = dbus.connection_open_private(address.c_str(), &error);
        if (connection_ == nullptr)
        {
            dbus.error_free(&error);
            return;
        }
        dbus.connection_set_exit_on_disconnect(connection_, FALSE);
        if (!dbus.bus_register(connection_, &error) ||
            dbus.bus_request_name(connection_, "org.freedesktop.portal.Desktop", DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) !=
                DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
        {
            dbus.error_free(&error);
            return;
        }
        ready_ = true;
        running_ = true;
        thread_ = std::thread([this] { Serve(); });
    }

    ~FakePortal()
    {
        running_ = false;
        if (thread_.joinable()) { thread_.join(); }
        if (connection_ != nullptr)
        {
            GetDBus().connection_close(connection_);
            GetDBus().connection_unref(connection_);
        }
    }

    [[nodiscard]] bool Ready() const { return ready_; }

    void Answer(PortalAnswer answer)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        answer_ = std::move(answer);
    }

    [[nodiscard]] std::vector<PortalCall> Calls()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_;
    }

    /// Sends the Response a silent answer held back, on the path the last request had.
    void RespondLater(const std::uint32_t response, const std::vector<std::string>& uris)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        late_.push_back({lastPath_, response, uris});
    }

private:
    static std::string Text(const DBusLibrary& dbus, DBusMessageIter* iter)
    {
        const char* text = nullptr;
        if (dbus.message_iter_get_arg_type(iter) == DBUS_TYPE_STRING ||
            dbus.message_iter_get_arg_type(iter) == DBUS_TYPE_OBJECT_PATH)
        {
            dbus.message_iter_get_basic(iter, &text);
        }
        return text != nullptr ? text : "";
    }

    static void ReadOptions(const DBusLibrary& dbus, DBusMessageIter* iter, PortalCall& call)
    {
        DBusMessageIter dictionary;
        dbus.message_iter_recurse(iter, &dictionary);
        while (dbus.message_iter_get_arg_type(&dictionary) == DBUS_TYPE_DICT_ENTRY)
        {
            DBusMessageIter entry;
            dbus.message_iter_recurse(&dictionary, &entry);
            const std::string key = Text(dbus, &entry);
            dbus.message_iter_next(&entry);
            DBusMessageIter value;
            dbus.message_iter_recurse(&entry, &value);
            const int type = dbus.message_iter_get_arg_type(&value);
            if (type == DBUS_TYPE_STRING)
            {
                call.strings[key] = Text(dbus, &value);
            }
            else if (type == DBUS_TYPE_BOOLEAN)
            {
                dbus_bool_t flag = FALSE;
                dbus.message_iter_get_basic(&value, &flag);
                call.flags[key] = flag != FALSE;
            }
            else if (type == DBUS_TYPE_ARRAY && dbus.message_iter_get_element_type(&value) == DBUS_TYPE_BYTE)
            {
                DBusMessageIter bytes;
                dbus.message_iter_recurse(&value, &bytes);
                const char* data = nullptr;
                int length = 0;
                dbus.message_iter_get_fixed_array(&bytes, &data, &length);
                std::string text(data != nullptr ? data : "", static_cast<std::size_t>(length));
                if (!text.empty() && text.back() == '\0') { text.pop_back(); }
                call.paths[key] = text;
            }
            else if (type == DBUS_TYPE_ARRAY && key == "filters")
            {
                DBusMessageIter filters;
                dbus.message_iter_recurse(&value, &filters);
                while (dbus.message_iter_get_arg_type(&filters) == DBUS_TYPE_STRUCT)
                {
                    DBusMessageIter filter;
                    dbus.message_iter_recurse(&filters, &filter);
                    PortalFilter read;
                    read.name = Text(dbus, &filter);
                    dbus.message_iter_next(&filter);
                    DBusMessageIter patterns;
                    dbus.message_iter_recurse(&filter, &patterns);
                    while (dbus.message_iter_get_arg_type(&patterns) == DBUS_TYPE_STRUCT)
                    {
                        DBusMessageIter pair;
                        dbus.message_iter_recurse(&patterns, &pair);
                        PortalPattern pattern;
                        dbus_uint32_t kind = 0;
                        dbus.message_iter_get_basic(&pair, &kind);
                        pattern.kind = kind;
                        dbus.message_iter_next(&pair);
                        pattern.pattern = Text(dbus, &pair);
                        read.patterns.push_back(pattern);
                        dbus.message_iter_next(&patterns);
                    }
                    call.filters.push_back(read);
                    dbus.message_iter_next(&filters);
                }
            }
            dbus.message_iter_next(&dictionary);
        }
    }

    void EmitResponse(const std::string& path, const std::uint32_t response, const std::vector<std::string>& uris)
    {
        const DBusLibrary& dbus = GetDBus();
        DBusMessage* signal = dbus.message_new_signal(path.c_str(), "org.freedesktop.portal.Request", "Response");
        DBusMessageIter arguments;
        dbus.message_iter_init_append(signal, &arguments);
        const dbus_uint32_t code = response;
        dbus.message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &code);
        DBusMessageIter results;
        dbus.message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &results);
        if (!uris.empty())
        {
            DBusMessageIter entry;
            DBusMessageIter variant;
            DBusMessageIter list;
            const char* key = "uris";
            dbus.message_iter_open_container(&results, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus.message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus.message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
            dbus.message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &list);
            for (const std::string& uri : uris)
            {
                const char* text = uri.c_str();
                dbus.message_iter_append_basic(&list, DBUS_TYPE_STRING, &text);
            }
            dbus.message_iter_close_container(&variant, &list);
            dbus.message_iter_close_container(&entry, &variant);
            dbus.message_iter_close_container(&results, &entry);
        }
        dbus.message_iter_close_container(&arguments, &results);
        dbus.connection_send(connection_, signal, nullptr);
        dbus.connection_flush(connection_);
        dbus.message_unref(signal);
    }

    void Serve()
    {
        const DBusLibrary& dbus = GetDBus();
        while (running_)
        {
            dbus.connection_read_write(connection_, 10);
            while (DBusMessage* message = dbus.connection_pop_message(connection_))
            {
                Handle(message);
                dbus.message_unref(message);
            }
            std::vector<Late> late;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                late.swap(late_);
            }
            for (const Late& response : late)
            {
                EmitResponse(response.path, response.response, response.uris);
            }
        }
    }

    void Handle(DBusMessage* message)
    {
        const DBusLibrary& dbus = GetDBus();
        const bool openFile = dbus.message_is_method_call(message, "org.freedesktop.portal.FileChooser", "OpenFile");
        const bool saveFile = dbus.message_is_method_call(message, "org.freedesktop.portal.FileChooser", "SaveFile");
        const bool openUri = dbus.message_is_method_call(message, "org.freedesktop.portal.OpenURI", "OpenURI");
        const bool openUriFile = dbus.message_is_method_call(message, "org.freedesktop.portal.OpenURI", "OpenFile");
        if (!openFile && !saveFile && !openUri && !openUriFile)
        {
            return;
        }
        PortalCall call;
        call.interface = openUri || openUriFile ? "org.freedesktop.portal.OpenURI" : "org.freedesktop.portal.FileChooser";
        call.method = openFile || openUriFile ? "OpenFile" : saveFile ? "SaveFile" : "OpenURI";
        const char* sender = dbus.message_get_sender(message);
        call.sender = sender != nullptr ? sender : "";
        DBusMessageIter arguments;
        dbus.message_iter_init(message, &arguments);
        call.parent = Text(dbus, &arguments);
        dbus.message_iter_next(&arguments);
        if (openUriFile)
        {
            int descriptor = -1;
            dbus.message_iter_get_basic(&arguments, &descriptor);
            if (descriptor >= 0)
            {
                std::error_code error;
                call.fileTarget =
                    std::filesystem::read_symlink("/proc/self/fd/" + std::to_string(descriptor), error).string();
                ::close(descriptor);
            }
        }
        else if (openUri)
        {
            call.uri = Text(dbus, &arguments);
        }
        else
        {
            call.title = Text(dbus, &arguments);
        }
        dbus.message_iter_next(&arguments);
        ReadOptions(dbus, &arguments, call);

        PortalAnswer answer;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            answer = answer_;
        }
        if (answer.error && !openUri && !openUriFile)
        {
            DBusMessage* failure =
                dbus.message_new_error(message, "org.freedesktop.portal.Error.Failed", "no chooser for you");
            dbus.connection_send(connection_, failure, nullptr);
            dbus.message_unref(failure);
        }
        else
        {
            std::string path = PortalRequestPath(call.sender, call.strings["handle_token"]);
            if (answer.ownPath)
            {
                path = "/org/freedesktop/portal/desktop/request/elsewhere/r" + std::to_string(calls_.size());
            }
            DBusMessage* reply = dbus.message_new_method_return(message);
            DBusMessageIter replyArguments;
            dbus.message_iter_init_append(reply, &replyArguments);
            const char* handle = path.c_str();
            dbus.message_iter_append_basic(&replyArguments, DBUS_TYPE_OBJECT_PATH, &handle);
            dbus.connection_send(connection_, reply, nullptr);
            dbus.connection_flush(connection_);
            dbus.message_unref(reply);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lastPath_ = path;
            }
            if (!answer.silent && !openUri && !openUriFile)
            {
                EmitResponse(path, answer.response, answer.uris);
            }
        }
        dbus.connection_flush(connection_);
        std::lock_guard<std::mutex> lock(mutex_);
        calls_.push_back(std::move(call));
    }

    struct Late
    {
        std::string path;
        std::uint32_t response = 0;
        std::vector<std::string> uris;
    };

    DBusConnection* connection_ = nullptr;
    bool ready_ = false;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::vector<Late> late_;
    PortalAnswer answer_;
    std::vector<PortalCall> calls_;
    std::string lastPath_;
};
} // namespace CNA::Platform::Freedesktop::Testing

#endif // CNA_PLATFORM_HAVE_DBUS
