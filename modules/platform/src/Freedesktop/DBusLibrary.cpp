// SPDX-License-Identifier: MS-PL

#include "DBusLibrary.hpp"

#if defined(CNA_PLATFORM_HAVE_DBUS)

#include <cstdlib>
#include <filesystem>
#include <string>

#include <dlfcn.h>

namespace CNA::Platform::Freedesktop {

    namespace {

        template <typename Function>
        bool Resolve(void* library, const char* name, Function& function)
        {
            function = reinterpret_cast<Function>(dlsym(library, name));
            return function != nullptr;
        }

        DBusLibrary Load()
        {
            DBusLibrary api;
            // Deliberately never closed: libdbus keeps process-wide state that outlives any one
            // connection, and unloading it under a live one is undefined.
            void* library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
            if (library == nullptr)
            {
                return api;
            }
            bool all = true;
            const auto get = [&](const char* name, auto& function) { all = Resolve(library, name, function) && all; };
            get("dbus_error_init", api.error_init);
            get("dbus_error_is_set", api.error_is_set);
            get("dbus_error_free", api.error_free);
            get("dbus_connection_open_private", api.connection_open_private);
            get("dbus_connection_close", api.connection_close);
            get("dbus_connection_unref", api.connection_unref);
            get("dbus_connection_set_exit_on_disconnect", api.connection_set_exit_on_disconnect);
            get("dbus_connection_send", api.connection_send);
            get("dbus_connection_send_with_reply_and_block", api.connection_send_with_reply_and_block);
            get("dbus_connection_read_write", api.connection_read_write);
            get("dbus_connection_pop_message", api.connection_pop_message);
            get("dbus_connection_flush", api.connection_flush);
            get("dbus_connection_can_send_type", api.connection_can_send_type);
            get("dbus_connection_get_is_connected", api.connection_get_is_connected);
            get("dbus_bus_register", api.bus_register);
            get("dbus_bus_get_unique_name", api.bus_get_unique_name);
            get("dbus_bus_add_match", api.bus_add_match);
            get("dbus_bus_request_name", api.bus_request_name);
            get("dbus_bus_name_has_owner", api.bus_name_has_owner);
            get("dbus_message_new_method_call", api.message_new_method_call);
            get("dbus_message_new_method_return", api.message_new_method_return);
            get("dbus_message_new_signal", api.message_new_signal);
            get("dbus_message_new_error", api.message_new_error);
            get("dbus_message_unref", api.message_unref);
            get("dbus_message_get_type", api.message_get_type);
            get("dbus_message_get_reply_serial", api.message_get_reply_serial);
            get("dbus_message_get_path", api.message_get_path);
            get("dbus_message_get_sender", api.message_get_sender);
            get("dbus_message_get_error_name", api.message_get_error_name);
            get("dbus_message_is_signal", api.message_is_signal);
            get("dbus_message_is_method_call", api.message_is_method_call);
            get("dbus_message_iter_init_append", api.message_iter_init_append);
            get("dbus_message_iter_append_basic", api.message_iter_append_basic);
            get("dbus_message_iter_append_fixed_array", api.message_iter_append_fixed_array);
            get("dbus_message_iter_open_container", api.message_iter_open_container);
            get("dbus_message_iter_close_container", api.message_iter_close_container);
            get("dbus_message_iter_init", api.message_iter_init);
            get("dbus_message_iter_get_arg_type", api.message_iter_get_arg_type);
            get("dbus_message_iter_get_element_type", api.message_iter_get_element_type);
            get("dbus_message_iter_get_basic", api.message_iter_get_basic);
            get("dbus_message_iter_get_fixed_array", api.message_iter_get_fixed_array);
            get("dbus_message_iter_recurse", api.message_iter_recurse);
            get("dbus_message_iter_next", api.message_iter_next);
            api.loaded = all;
            return api;
        }

    } // namespace

    const DBusLibrary& GetDBus()
    {
        static const DBusLibrary api = Load();
        return api;
    }

    void DBusMessageDeleter::operator()(DBusMessage* message) const
    {
        if (message != nullptr)
        {
            GetDBus().message_unref(message);
        }
    }

    DBusConnection* OpenSessionBus()
    {
        const DBusLibrary& dbus = GetDBus();
        if (!dbus.loaded)
        {
            return nullptr;
        }
        std::string address;
        if (const char* configured = std::getenv("DBUS_SESSION_BUS_ADDRESS"); configured != nullptr && *configured != '\0')
        {
            address = configured;
        }
        else if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime != nullptr && *runtime != '\0')
        {
            const std::filesystem::path bus = std::filesystem::path(runtime) / "bus";
            std::error_code error;
            if (std::filesystem::is_socket(bus, error))
            {
                address = "unix:path=" + bus.string();
            }
        }
        if (address.empty())
        {
            return nullptr;
        }
        DBusError error;
        dbus.error_init(&error);
        DBusConnection* connection = dbus.connection_open_private(address.c_str(), &error);
        if (connection == nullptr)
        {
            dbus.error_free(&error);
            return nullptr;
        }
        // A bus that goes away is a portal that went away, not a reason for libdbus to exit().
        dbus.connection_set_exit_on_disconnect(connection, FALSE);
        if (!dbus.bus_register(connection, &error))
        {
            dbus.error_free(&error);
            dbus.connection_close(connection);
            dbus.connection_unref(connection);
            return nullptr;
        }
        return connection;
    }

    void CloseSessionBus(DBusConnection* connection)
    {
        if (connection != nullptr)
        {
            const DBusLibrary& dbus = GetDBus();
            dbus.connection_close(connection);
            dbus.connection_unref(connection);
        }
    }

} // namespace CNA::Platform::Freedesktop

#endif // CNA_PLATFORM_HAVE_DBUS
