// SPDX-License-Identifier: MS-PL

#include "X11Display.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11ContentScale.hpp"
#include "X11Error.hpp"
#include "X11ModeSwitch.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace CNA::Platform::X11 {

    namespace {

        std::string EnvironmentDisplayName()
        {
            const char* value = std::getenv("DISPLAY");
            return value != nullptr ? std::string(value) : std::string();
        }

    } // namespace

    X11Connection::X11Connection()
    {
        displayName_ = EnvironmentDisplayName();
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr)
        {
            // Naming $DISPLAY is the whole diagnostic value here: "cannot open display" with no
            // value is the single least actionable X error message there is, and the two failures
            // it covers -- no variable set at all, and a variable pointing at a server that is not
            // running -- need different fixes.
            throw PlatformException(
                "X11Connection::Open",
                displayName_.empty()
                    ? std::string("no X server: the DISPLAY environment variable is not set")
                    : "cannot connect to the X server at DISPLAY=" + displayName_);
        }

        // Registered before anything else is asked of the connection, so the very first request
        // is already covered by the non-fatal error policy rather than by Xlib's exit().
        X11ErrorPolicy::Register(display_);

        screen_ = DefaultScreen(display_);
        root_ = RootWindow(display_, screen_);
        if (displayName_.empty())
        {
            const char* actual = DisplayString(display_);
            displayName_ = actual != nullptr ? std::string(actual) : std::string();
        }

        InternAtoms();
        DetectExtensions();

        // The root window's property set is how a window manager announces itself, including one
        // that starts after this application did. Selecting for it here is what makes
        // RefreshWindowManagerState() reachable without polling.
        XSelectInput(display_, root_, PropertyChangeMask | StructureNotifyMask);
        RefreshWindowManagerState();

        modeSwitcher_ = std::make_unique<X11ModeSwitcher>(*this);
        // After the root's input selection: the scale is followed through root property changes
        // and the settings manager's announcements, which arrive through it.
        contentScale_ = std::make_unique<X11ContentScale>(*this);
    }

    X11Connection::~X11Connection()
    {
        // Every display mode exclusive fullscreen still holds goes back while there is still a
        // connection to send it over.
        modeSwitcher_.reset();
        if (display_ != nullptr)
        {
            // XCloseDisplay FIRST, then unregister. Closing flushes the output buffer and reads
            // the replies, so it can deliver a protocol error for a request issued moments
            // earlier -- a window another client destroyed, typically. Restoring Xlib's default
            // handler before that flush means the error arrives at the handler that calls
            // exit(), which turns an ordinary teardown into a process kill. Measured: an
            // X_DestroyWindow BadWindow during X11WithWindowManager teardown did exactly that.
            XCloseDisplay(display_);
            X11ErrorPolicy::Unregister(display_);
            display_ = nullptr;
        }
    }

    void X11Connection::InternAtoms()
    {
        // One round trip for the whole set rather than one per atom: XInternAtoms is the batched
        // form, and this runs at connection time where 37 sequential round trips would be a
        // visible startup cost on a remote display.
        static const char* const names[] = {
            "WM_PROTOCOLS",
            "WM_DELETE_WINDOW",
            "WM_STATE",
            "WM_NAME",
            "WM_CHANGE_STATE",
            "_NET_WM_NAME",
            "_NET_WM_STATE",
            "_NET_WM_STATE_FULLSCREEN",
            "_NET_WM_STATE_MAXIMIZED_VERT",
            "_NET_WM_STATE_MAXIMIZED_HORZ",
            "_NET_WM_STATE_HIDDEN",
            "_NET_ACTIVE_WINDOW",
            "_NET_SUPPORTED",
            "_NET_SUPPORTING_WM_CHECK",
            "_NET_WM_PID",
            "_MOTIF_WM_HINTS",
            "UTF8_STRING",
            "CLIPBOARD",
            "PRIMARY",
            "TARGETS",
            "TEXT",
            "TIMESTAMP",
            "INCR",
            "CNA_SELECTION",
            "XdndAware",
            "XdndEnter",
            "XdndPosition",
            "XdndStatus",
            "XdndLeave",
            "XdndDrop",
            "XdndFinished",
            "XdndSelection",
            "XdndTypeList",
            "XdndActionCopy",
            "text/uri-list",
            "text/plain",
            "text/plain;charset=utf-8",
        };
        constexpr int kCount = static_cast<int>(sizeof(names) / sizeof(names[0]));
        Atom interned[kCount] = {};
        XInternAtoms(display_, const_cast<char**>(names), kCount, False, interned);

        int index = 0;
        atoms_.wmProtocols = interned[index++];
        atoms_.wmDeleteWindow = interned[index++];
        atoms_.wmState = interned[index++];
        atoms_.wmName = interned[index++];
        atoms_.wmChangeState = interned[index++];
        atoms_.netWmName = interned[index++];
        atoms_.netWmState = interned[index++];
        atoms_.netWmStateFullscreen = interned[index++];
        atoms_.netWmStateMaximizedVert = interned[index++];
        atoms_.netWmStateMaximizedHorz = interned[index++];
        atoms_.netWmStateHidden = interned[index++];
        atoms_.netActiveWindow = interned[index++];
        atoms_.netSupported = interned[index++];
        atoms_.netSupportingWmCheck = interned[index++];
        atoms_.netWmPid = interned[index++];
        atoms_.motifWmHints = interned[index++];
        atoms_.utf8String = interned[index++];
        atoms_.clipboard = interned[index++];
        atoms_.primary = interned[index++];
        atoms_.targets = interned[index++];
        atoms_.text = interned[index++];
        atoms_.timestamp = interned[index++];
        atoms_.incr = interned[index++];
        atoms_.cnaSelection = interned[index++];
        atoms_.xdndAware = interned[index++];
        atoms_.xdndEnter = interned[index++];
        atoms_.xdndPosition = interned[index++];
        atoms_.xdndStatus = interned[index++];
        atoms_.xdndLeave = interned[index++];
        atoms_.xdndDrop = interned[index++];
        atoms_.xdndFinished = interned[index++];
        atoms_.xdndSelection = interned[index++];
        atoms_.xdndTypeList = interned[index++];
        atoms_.xdndActionCopy = interned[index++];
        atoms_.textUriList = interned[index++];
        atoms_.textPlain = interned[index++];
        atoms_.textPlainUtf8 = interned[index++];
    }

    void X11Connection::DetectExtensions()
    {
        int opcode = 0;
        int event = 0;
        int error = 0;

        // XKB lives inside libX11 but still has to be negotiated: XkbQueryExtension is what
        // establishes the event base, and a server compiled without XKB answers False.
        int major = XkbMajorVersion;
        int minor = XkbMinorVersion;
        if (XkbQueryExtension(display_, &opcode, &event, &error, &major, &minor) == True)
        {
            hasXkb_ = true;
            xkbEventBase_ = event;

            // Detectable auto-repeat is per client connection, not per server, so asking for it
            // reconfigures nothing outside this process's own connection
            // (plans/plan_x11.md design decision 6). When the server refuses, the keyboard falls
            // back to coalescing the release/press pair it sends instead -- never to a timeout.
            XBool granted = False;
            XkbSetDetectableAutoRepeat(display_, True, &granted);
            detectableAutoRepeat_ = granted == True;

            // State notifications carry layout (group) changes, which is what keeps the logical
            // KeyCode mapping correct when the user switches layouts mid-session; map
            // notifications carry a replaced keymap (setxkbmap, a desktop's input-source
            // settings). Modifier state is deliberately not selected: nothing consumes it as an
            // event, and every Shift press would otherwise wake the keymap refresh.
            XkbSelectEventDetails(display_, XkbUseCoreKbd, XkbStateNotify, XkbGroupStateMask,
                                  XkbGroupStateMask);
            XkbSelectEvents(display_, XkbUseCoreKbd, XkbNewKeyboardNotifyMask | XkbMapNotifyMask,
                            XkbNewKeyboardNotifyMask | XkbMapNotifyMask);
        }

#if defined(CNA_X11_HAVE_XI)
        if (XQueryExtension(display_, "XInputExtension", &opcode, &event, &error) == True)
        {
            // 2.2 is the version with touch events (X11-0155). The server answers with the version
            // it will use -- the lower of the two -- and a client announces one version only, so
            // this is the one call. Raw motion, the 2.0 feature relative mode uses, is unchanged.
            int xiMajor = 2;
            int xiMinor = 2;
            if (XIQueryVersion(display_, &xiMajor, &xiMinor) == Success)
            {
                xi2Opcode_ = opcode;
                xi2Touch_ = xiMajor > 2 || (xiMajor == 2 && xiMinor >= 2);
            }
        }
#endif

#if defined(CNA_X11_HAVE_XRANDR)
        if (XRRQueryExtension(display_, &event, &error) == True)
        {
            int rrMajor = 0;
            int rrMinor = 0;
            // 1.2 introduced CRTCs and outputs; anything older describes a single screen with a
            // mode list, which this backend treats as "no RandR" rather than pretending.
            if (XRRQueryVersion(display_, &rrMajor, &rrMinor) == True &&
                (rrMajor > 1 || (rrMajor == 1 && rrMinor >= 2)))
            {
                randrEventBase_ = event;
                XRRSelectInput(display_, root_, RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask |
                                                    RROutputChangeNotifyMask);
            }
        }
#endif

        // Xwayland 21.1 and later announce themselves with an extension; older ones are known by
        // the names they give their RandR outputs.
        if (XQueryExtension(display_, "XWAYLAND", &opcode, &event, &error) == True)
        {
            isXwayland_ = true;
        }
#if defined(CNA_X11_HAVE_XRANDR)
        else if (randrEventBase_ >= 0)
        {
            X11ErrorTrap trap(display_);
            if (XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display_, root_))
            {
                if (resources->noutput > 0)
                {
                    if (XRROutputInfo* output =
                            XRRGetOutputInfo(display_, resources, resources->outputs[0]))
                    {
                        isXwayland_ = output->name != nullptr &&
                                      std::strncmp(output->name, "XWAYLAND", 8) == 0;
                        XRRFreeOutputInfo(output);
                    }
                }
                XRRFreeScreenResources(resources);
            }
            trap.Sync();
        }
#endif
    }

    void X11Connection::RefreshWindowManagerState()
    {
        supportedHints_.clear();
        hasWindowManager_ = false;

        int format = 0;
        std::vector<unsigned char> data;

        // _NET_SUPPORTING_WM_CHECK is the EWMH handshake: the root property points at a window
        // that must itself carry the same property pointing at itself. A stale root property left
        // by a crashed window manager fails that second half, which is exactly what it is for.
        if (ReadProperty(root_, atoms_.netSupportingWmCheck, XA_WINDOW, format, data) &&
            format == 32 && data.size() >= sizeof(long))
        {
            ::Window check = kNone;
            std::memcpy(&check, data.data(), sizeof(long));
            if (check != kNone)
            {
                X11ErrorTrap trap(display_);
                std::vector<unsigned char> childData;
                int childFormat = 0;
                const bool present = ReadProperty(check, atoms_.netSupportingWmCheck, XA_WINDOW,
                                                  childFormat, childData);
                trap.Sync();
                hasWindowManager_ = present && !trap.HasError();
            }
        }

        if (ReadProperty(root_, atoms_.netSupported, XA_ATOM, format, data) && format == 32)
        {
            const std::size_t count = data.size() / sizeof(long);
            supportedHints_.resize(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                long atom = 0;
                std::memcpy(&atom, data.data() + index * sizeof(long), sizeof(long));
                supportedHints_[index] = static_cast<Atom>(atom);
            }
            // A window manager that publishes _NET_SUPPORTED is EWMH-compliant even if its
            // check window handshake raced us.
            hasWindowManager_ = hasWindowManager_ || !supportedHints_.empty();
        }
    }

    bool X11Connection::SupportsEwmhHint(const Atom hint) const
    {
        if (hint == kNone)
        {
            return false;
        }
        return std::find(supportedHints_.begin(), supportedHints_.end(), hint) !=
               supportedHints_.end();
    }

    bool X11Connection::ReadProperty(const ::Window window, const Atom property, const Atom type,
                                     int& format, std::vector<unsigned char>& data) const
    {
        data.clear();
        if (window == kNone || property == kNone)
        {
            return false;
        }

        // XGetWindowProperty returns at most `long_length` 32-bit words and reports how many
        // bytes remain. Looping on bytes_after is the part a naive single call gets wrong: a
        // long _NET_SUPPORTED or a pasted paragraph silently truncates.
        unsigned long offset = 0;
        unsigned long remaining = 0;
        bool any = false;
        do
        {
            Atom actualType = kNone;
            int actualFormat = 0;
            unsigned long itemCount = 0;
            unsigned char* chunk = nullptr;
            const int status =
                XGetWindowProperty(display_, window, property, static_cast<long>(offset / 4),
                                   4096, False, type, &actualType, &actualFormat, &itemCount,
                                   &remaining, &chunk);
            if (status != Success)
            {
                if (chunk != nullptr) { XFree(chunk); }
                return false;
            }
            if (actualType == kNone || actualFormat == 0)
            {
                if (chunk != nullptr) { XFree(chunk); }
                return any;
            }

            format = actualFormat;
            // 32-bit X properties are delivered as C `long`, which is 64 bits on LP64. Sizing the
            // copy from `long` rather than from the wire format is what keeps the memcpy loops in
            // the callers correct on both 32- and 64-bit hosts.
            const std::size_t unitSize = actualFormat == 32 ? sizeof(long)
                                          : actualFormat == 16 ? sizeof(short)
                                                               : sizeof(char);
            const std::size_t byteCount = static_cast<std::size_t>(itemCount) * unitSize;
            data.insert(data.end(), chunk, chunk + byteCount);
            offset += static_cast<unsigned long>(byteCount);
            any = true;
            XFree(chunk);
        } while (remaining > 0);

        return any;
    }

    bool X11Connection::SendRootClientMessage(const ::Window window, const Atom type,
                                              const long data0, const long data1, const long data2,
                                              const long data3) const
    {
        if (window == kNone || type == kNone)
        {
            return false;
        }
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = type;
        event.xclient.format = 32;
        event.xclient.data.l[0] = data0;
        event.xclient.data.l[1] = data1;
        event.xclient.data.l[2] = data2;
        event.xclient.data.l[3] = data3;
        // EWMH source indication 1 = "a normal application", which is what a window manager uses
        // to decide whether a focus or state request may steal focus from the user.
        event.xclient.data.l[4] = 1;
        return XSendEvent(display_, root_, False,
                          SubstructureNotifyMask | SubstructureRedirectMask, &event) != 0;
    }

} // namespace CNA::Platform::X11
