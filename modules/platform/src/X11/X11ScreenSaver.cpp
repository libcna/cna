// SPDX-License-Identifier: MS-PL

#include "X11ScreenSaver.hpp"

#include "X11Display.hpp"

namespace CNA::Platform::X11 {

    X11ScreenSaverInhibitor::X11ScreenSaverInhibitor(X11Connection& connection) : connection_(connection) {}

    X11ScreenSaverInhibitor::~X11ScreenSaverInhibitor()
    {
        Lift();
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
        if (desktop_.Inhibit())
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
                desktop_.Lift();
                break;
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
