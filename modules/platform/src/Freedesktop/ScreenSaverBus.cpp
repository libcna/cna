// SPDX-License-Identifier: MS-PL

#include "ScreenSaverBus.hpp"

#include <fstream>

namespace CNA::Platform::Freedesktop {

    namespace {

#if defined(CNA_PLATFORM_HAVE_DBUS)
        constexpr const char* kScreenSaverService = "org.freedesktop.ScreenSaver";
        constexpr const char* kScreenSaverPath = "/org/freedesktop/ScreenSaver";
        constexpr int kCallTimeoutMilliseconds = 5000;
#endif

    } // namespace

    std::string ScreenSaverApplicationName()
    {
        std::string name;
        std::ifstream comm("/proc/self/comm");
        std::getline(comm, name);
        return name.empty() ? std::string("CNA") : name;
    }

    ScreenSaverBusInhibition::~ScreenSaverBusInhibition()
    {
        Lift();
    }

    bool ScreenSaverBusInhibition::IsHeld() const
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        return bus_ != nullptr;
#else
        return false;
#endif
    }

    bool ScreenSaverBusInhibition::Inhibit()
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (bus_ != nullptr)
        {
            return true;
        }
        DBusConnection* bus = OpenSessionBus();
        if (bus == nullptr)
        {
            return false;
        }
        const DBusLibrary& dbus = GetDBus();
        DBusError error;
        dbus.error_init(&error);
        // Only a service that is there: this is not worth starting one for.
        if (!dbus.bus_name_has_owner(bus, kScreenSaverService, &error))
        {
            dbus.error_free(&error);
            CloseSessionBus(bus);
            return false;
        }
        DBusMessagePtr call(
            dbus.message_new_method_call(kScreenSaverService, kScreenSaverPath, kScreenSaverService, "Inhibit"));
        if (call == nullptr)
        {
            CloseSessionBus(bus);
            return false;
        }
        const std::string application = ScreenSaverApplicationName();
        const char* applicationText = application.c_str();
        const char* reason = "Playing a game";
        DBusMessageIter arguments;
        dbus.message_iter_init_append(call.get(), &arguments);
        dbus.message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &applicationText);
        dbus.message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &reason);
        DBusMessagePtr reply(
            dbus.connection_send_with_reply_and_block(bus, call.get(), kCallTimeoutMilliseconds, &error));
        DBusMessageIter result;
        if (reply == nullptr || !dbus.message_iter_init(reply.get(), &result) ||
            dbus.message_iter_get_arg_type(&result) != DBUS_TYPE_UINT32)
        {
            dbus.error_free(&error);
            CloseSessionBus(bus);
            return false;
        }
        dbus_uint32_t cookie = 0;
        dbus.message_iter_get_basic(&result, &cookie);
        bus_ = bus;
        cookie_ = cookie;
        return true;
#else
        return false;
#endif
    }

    void ScreenSaverBusInhibition::Lift()
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (bus_ == nullptr)
        {
            return;
        }
        const DBusLibrary& dbus = GetDBus();
        DBusMessagePtr call(
            dbus.message_new_method_call(kScreenSaverService, kScreenSaverPath, kScreenSaverService, "UnInhibit"));
        if (call != nullptr)
        {
            DBusMessageIter arguments;
            dbus.message_iter_init_append(call.get(), &arguments);
            const dbus_uint32_t cookie = cookie_;
            dbus.message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &cookie);
            DBusError error;
            dbus.error_init(&error);
            DBusMessagePtr reply(
                dbus.connection_send_with_reply_and_block(bus_, call.get(), kCallTimeoutMilliseconds, &error));
            dbus.error_free(&error);
        }
        // Closing the connection is itself the end of the request for the desktop.
        CloseSessionBus(bus_);
        bus_ = nullptr;
        cookie_ = 0;
#endif
    }

} // namespace CNA::Platform::Freedesktop
