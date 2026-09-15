// SPDX-License-Identifier: MS-PL

#include "X11ScreenSaver.hpp"

#include "X11Display.hpp"

#include <fstream>

namespace CNA::Platform::X11 {

    namespace {

#if defined(CNA_X11_HAVE_DBUS)
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

    X11ScreenSaverInhibitor::X11ScreenSaverInhibitor(X11Connection& connection) : connection_(connection) {}

    X11ScreenSaverInhibitor::~X11ScreenSaverInhibitor()
    {
        Lift();
    }

    bool X11ScreenSaverInhibitor::InhibitOnSessionBus()
    {
#if defined(CNA_X11_HAVE_DBUS)
        DBusConnection* bus = OpenSessionBus();
        if (bus == nullptr)
        {
            return false;
        }
        const X11DBusApi& dbus = DBusApi();
        DBusError error;
        dbus.error_init(&error);
        // Only a service that is there: this is not worth starting one for.
        if (!dbus.bus_name_has_owner(bus, kScreenSaverService, &error))
        {
            dbus.error_free(&error);
            CloseSessionBus(bus);
            return false;
        }
        X11DBusMessagePtr call(
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
        X11DBusMessagePtr reply(
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

    bool X11ScreenSaverInhibitor::SuspendServerSaver(const bool suspend)
    {
#if defined(CNA_X11_HAVE_XSS)
        Display* display = connection_.GetDisplay();
        int event = 0;
        int error = 0;
        int major = 0;
        int minor = 0;
        if (XScreenSaverQueryExtension(display, &event, &error) == 0 ||
            XScreenSaverQueryVersion(display, &major, &minor) == 0 || major < 1 || (major == 1 && minor < 1))
        {
            return false;
        }
        XScreenSaverSuspend(display, suspend ? kXTrue : kXFalse);
        if (suspend)
        {
            XResetScreenSaver(display);
        }
        XFlush(display);
        return true;
#else
        (void) suspend;
        return false;
#endif
    }

    void X11ScreenSaverInhibitor::Inhibit()
    {
        if (method_ != Method::None)
        {
            return;
        }
        if (InhibitOnSessionBus())
        {
            method_ = Method::DesktopService;
            return;
        }
        if (SuspendServerSaver(true))
        {
            method_ = Method::ServerSuspend;
            return;
        }
        // A server without MIT-SCREEN-SAVER 1.1: its timeout, remembered so the user's own comes
        // back rather than a default that would quietly change their settings.
        Display* display = connection_.GetDisplay();
        int timeout = 0;
        int interval = 0;
        int preferBlanking = 0;
        int allowExposures = 0;
        XGetScreenSaver(display, &timeout, &interval, &preferBlanking, &allowExposures);
        savedTimeout_ = timeout;
        XSetScreenSaver(display, 0, interval, preferBlanking, allowExposures);
        XFlush(display);
        method_ = Method::ServerTimeout;
    }

    void X11ScreenSaverInhibitor::Lift()
    {
        switch (method_)
        {
            case Method::None:
                return;
            case Method::DesktopService:
            {
#if defined(CNA_X11_HAVE_DBUS)
                const X11DBusApi& dbus = DBusApi();
                X11DBusMessagePtr call(
                    dbus.message_new_method_call(kScreenSaverService, kScreenSaverPath, kScreenSaverService, "UnInhibit"));
                if (call != nullptr)
                {
                    DBusMessageIter arguments;
                    dbus.message_iter_init_append(call.get(), &arguments);
                    const dbus_uint32_t cookie = cookie_;
                    dbus.message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &cookie);
                    DBusError error;
                    dbus.error_init(&error);
                    X11DBusMessagePtr reply(
                        dbus.connection_send_with_reply_and_block(bus_, call.get(), kCallTimeoutMilliseconds, &error));
                    dbus.error_free(&error);
                }
                // Closing the connection is itself the end of the request for the desktop.
                CloseSessionBus(bus_);
                bus_ = nullptr;
                cookie_ = 0;
#endif
                break;
            }
            case Method::ServerSuspend:
                (void) SuspendServerSaver(false);
                break;
            case Method::ServerTimeout:
            {
                Display* display = connection_.GetDisplay();
                int timeout = 0;
                int interval = 0;
                int preferBlanking = 0;
                int allowExposures = 0;
                XGetScreenSaver(display, &timeout, &interval, &preferBlanking, &allowExposures);
                XSetScreenSaver(display, savedTimeout_ >= 0 ? savedTimeout_ : timeout, interval, preferBlanking,
                                allowExposures);
                XFlush(display);
                savedTimeout_ = -1;
                break;
            }
        }
        method_ = Method::None;
    }

} // namespace CNA::Platform::X11
