// SPDX-License-Identifier: MS-PL
#pragma once

#if defined(CNA_X11_HAVE_DBUS)

#include <dbus/dbus.h>

#include <memory>
#include <string>

namespace CNA::Platform::X11 {

    /**
     * @brief libdbus, loaded when first asked for (plans/plan_x11.md X11-0169).
     *
     * Only its headers are needed to build: the library is `dlopen`ed, so a machine without it
     * -- a container, a bare X server -- runs everything else and simply has no desktop portal.
     * The entry points are the few the portal client uses, and those a test's own portal needs to
     * answer it.
     */
    struct X11DBusApi
    {
        /** @brief Whether libdbus was found and every entry point resolved. */
        bool loaded = false;

        /** @brief `dbus_error_init`. */
        void (*error_init)(DBusError*) = nullptr;
        /** @brief `dbus_error_is_set`. */
        dbus_bool_t (*error_is_set)(const DBusError*) = nullptr;
        /** @brief `dbus_error_free`. */
        void (*error_free)(DBusError*) = nullptr;

        /** @brief `dbus_connection_open_private`. */
        DBusConnection* (*connection_open_private)(const char*, DBusError*) = nullptr;
        /** @brief `dbus_connection_close`. */
        void (*connection_close)(DBusConnection*) = nullptr;
        /** @brief `dbus_connection_unref`. */
        void (*connection_unref)(DBusConnection*) = nullptr;
        /** @brief `dbus_connection_set_exit_on_disconnect`. */
        void (*connection_set_exit_on_disconnect)(DBusConnection*, dbus_bool_t) = nullptr;
        /** @brief `dbus_connection_send`. */
        dbus_bool_t (*connection_send)(DBusConnection*, DBusMessage*, dbus_uint32_t*) = nullptr;
        /** @brief `dbus_connection_send_with_reply_and_block`. */
        DBusMessage* (*connection_send_with_reply_and_block)(DBusConnection*, DBusMessage*, int, DBusError*) = nullptr;
        /** @brief `dbus_connection_read_write`. */
        dbus_bool_t (*connection_read_write)(DBusConnection*, int) = nullptr;
        /** @brief `dbus_connection_pop_message`. */
        DBusMessage* (*connection_pop_message)(DBusConnection*) = nullptr;
        /** @brief `dbus_connection_flush`. */
        void (*connection_flush)(DBusConnection*) = nullptr;
        /** @brief `dbus_connection_can_send_type`. */
        dbus_bool_t (*connection_can_send_type)(DBusConnection*, int) = nullptr;
        /** @brief `dbus_connection_get_is_connected`. */
        dbus_bool_t (*connection_get_is_connected)(DBusConnection*) = nullptr;

        /** @brief `dbus_bus_register`. */
        dbus_bool_t (*bus_register)(DBusConnection*, DBusError*) = nullptr;
        /** @brief `dbus_bus_get_unique_name`. */
        const char* (*bus_get_unique_name)(DBusConnection*) = nullptr;
        /** @brief `dbus_bus_add_match`. */
        void (*bus_add_match)(DBusConnection*, const char*, DBusError*) = nullptr;
        /** @brief `dbus_bus_request_name`. */
        int (*bus_request_name)(DBusConnection*, const char*, unsigned int, DBusError*) = nullptr;
        /** @brief `dbus_bus_name_has_owner`. */
        dbus_bool_t (*bus_name_has_owner)(DBusConnection*, const char*, DBusError*) = nullptr;

        /** @brief `dbus_message_new_method_call`. */
        DBusMessage* (*message_new_method_call)(const char*, const char*, const char*, const char*) = nullptr;
        /** @brief `dbus_message_new_method_return`. */
        DBusMessage* (*message_new_method_return)(DBusMessage*) = nullptr;
        /** @brief `dbus_message_new_signal`. */
        DBusMessage* (*message_new_signal)(const char*, const char*, const char*) = nullptr;
        /** @brief `dbus_message_new_error`. */
        DBusMessage* (*message_new_error)(DBusMessage*, const char*, const char*) = nullptr;
        /** @brief `dbus_message_unref`. */
        void (*message_unref)(DBusMessage*) = nullptr;
        /** @brief `dbus_message_get_type`. */
        int (*message_get_type)(DBusMessage*) = nullptr;
        /** @brief `dbus_message_get_reply_serial`. */
        dbus_uint32_t (*message_get_reply_serial)(DBusMessage*) = nullptr;
        /** @brief `dbus_message_get_path`. */
        const char* (*message_get_path)(DBusMessage*) = nullptr;
        /** @brief `dbus_message_get_sender`. */
        const char* (*message_get_sender)(DBusMessage*) = nullptr;
        /** @brief `dbus_message_get_error_name`. */
        const char* (*message_get_error_name)(DBusMessage*) = nullptr;
        /** @brief `dbus_message_is_signal`. */
        dbus_bool_t (*message_is_signal)(DBusMessage*, const char*, const char*) = nullptr;
        /** @brief `dbus_message_is_method_call`. */
        dbus_bool_t (*message_is_method_call)(DBusMessage*, const char*, const char*) = nullptr;

        /** @brief `dbus_message_iter_init_append`. */
        void (*message_iter_init_append)(DBusMessage*, DBusMessageIter*) = nullptr;
        /** @brief `dbus_message_iter_append_basic`. */
        dbus_bool_t (*message_iter_append_basic)(DBusMessageIter*, int, const void*) = nullptr;
        /** @brief `dbus_message_iter_append_fixed_array`. */
        dbus_bool_t (*message_iter_append_fixed_array)(DBusMessageIter*, int, const void*, int) = nullptr;
        /** @brief `dbus_message_iter_open_container`. */
        dbus_bool_t (*message_iter_open_container)(DBusMessageIter*, int, const char*, DBusMessageIter*) = nullptr;
        /** @brief `dbus_message_iter_close_container`. */
        dbus_bool_t (*message_iter_close_container)(DBusMessageIter*, DBusMessageIter*) = nullptr;
        /** @brief `dbus_message_iter_init`. */
        dbus_bool_t (*message_iter_init)(DBusMessage*, DBusMessageIter*) = nullptr;
        /** @brief `dbus_message_iter_get_arg_type`. */
        int (*message_iter_get_arg_type)(DBusMessageIter*) = nullptr;
        /** @brief `dbus_message_iter_get_element_type`. */
        int (*message_iter_get_element_type)(DBusMessageIter*) = nullptr;
        /** @brief `dbus_message_iter_get_basic`. */
        void (*message_iter_get_basic)(DBusMessageIter*, void*) = nullptr;
        /** @brief `dbus_message_iter_get_fixed_array`. */
        void (*message_iter_get_fixed_array)(DBusMessageIter*, void*, int*) = nullptr;
        /** @brief `dbus_message_iter_recurse`. */
        void (*message_iter_recurse)(DBusMessageIter*, DBusMessageIter*) = nullptr;
        /** @brief `dbus_message_iter_next`. */
        dbus_bool_t (*message_iter_next)(DBusMessageIter*) = nullptr;
    };

    /**
     * @brief Gets libdbus, loading it on the first call.
     * @return The entry points; `loaded` is false where the library is not installed.
     */
    [[nodiscard]] const X11DBusApi& DBusApi();

    /** @brief Releases a message when it goes out of scope. */
    struct X11DBusMessageDeleter
    {
        /** @brief Unreferences the message. @param message The message. */
        void operator()(DBusMessage* message) const;
    };

    /** @brief An owned reference to a message. */
    using X11DBusMessagePtr = std::unique_ptr<DBusMessage, X11DBusMessageDeleter>;

    /**
     * @brief Connects privately to the session bus, never starting one.
     *
     * The address is `DBUS_SESSION_BUS_ADDRESS`, or the user bus at `$XDG_RUNTIME_DIR/bus` where
     * that is unset; libdbus's own fallback -- starting a bus through `dbus-launch` -- is a
     * program started on a game's behalf, and is never taken.
     *
     * @return The connection, registered on the bus; null when there is none to reach.
     */
    [[nodiscard]] DBusConnection* OpenSessionBus();

    /** @brief Closes and releases a connection OpenSessionBus() made. @param connection It. */
    void CloseSessionBus(DBusConnection* connection);

} // namespace CNA::Platform::X11

#endif // CNA_X11_HAVE_DBUS
