// SPDX-License-Identifier: MS-PL
//
// Diagnostic: exactly what XI_RawMotion events the X server delivers to a client set up the way
// the X11 backend sets itself up for relative mode (XInput 2.0, raw motion on the root for all
// master devices, a confining pointer grab with a hidden cursor) -- with every event's device,
// source device, valuator mask and values printed.
//
// Written because on Xwayland the backend's relative deltas did not match the motion injected,
// and the question "which events, from which device, with which values" has to be answered from
// the server's own output rather than from a theory about it.

#include "Harness.hpp"
#include "UInput.hpp"

#include <cstring>
#include <map>
#include <sstream>
#include <thread>

namespace CnaX11Validation {

    namespace {

        using CNA::Platform::X11::kCurrentTime;
        using CNA::Platform::X11::kNone;
        using CNA::Platform::X11::kXFalse;
        using CNA::Platform::X11::kXTrue;

        struct DeviceTotals
        {
            int events = 0;
            double rawX = 0.0;
            double rawY = 0.0;
            double valueX = 0.0;
            double valueY = 0.0;
        };

        std::string DeviceName(::Display* display, const int device)
        {
            int count = 0;
            XIDeviceInfo* info = XIQueryDevice(display, device, &count);
            std::string name = info != nullptr && count > 0 ? info[0].name : "?";
            if (info != nullptr) { XIFreeDeviceInfo(info); }
            return name;
        }

    } // namespace

    int RunRawProbe(const std::vector<std::string>& arguments)
    {
        const int requestedMinor = static_cast<int>(OptionInt(arguments, "xi-minor", 0));
        const bool grab = !OptionFlag(arguments, "no-grab");
        const bool verbose = OptionFlag(arguments, "verbose");

        ::Display* display = XOpenDisplay(nullptr);
        if (display == nullptr)
        {
            Fail("rawprobe.connect");
            return 1;
        }
        int opcode = 0;
        int eventBase = 0;
        int errorBase = 0;
        if (XQueryExtension(display, "XInputExtension", &opcode, &eventBase, &errorBase) == 0)
        {
            Skip("rawprobe", "no XInputExtension");
            return 0;
        }
        int major = 2;
        int minor = requestedMinor;
        XIQueryVersion(display, &major, &minor);
        Info("probe negotiated XInput " + std::to_string(major) + "." + std::to_string(minor) +
             (grab ? ", with a confining core pointer grab" : ", without a grab"));

        const int screen = DefaultScreen(display);
        const ::Window root = RootWindow(display, screen);
        const ::Window window = XCreateSimpleWindow(display, root, 200, 200, 640, 480, 0,
                                                    BlackPixel(display, screen),
                                                    BlackPixel(display, screen));
        XStoreName(display, window, "CNA raw-motion probe");
        XSelectInput(display, window, ExposureMask | FocusChangeMask | StructureNotifyMask);
        XMapRaised(display, window);
        XSync(display, kXFalse);

        // Wait until viewable, then ask for activation as a pager would.
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            XWindowAttributes attributes{};
            XGetWindowAttributes(display, window, &attributes);
            if (attributes.map_state == IsViewable) { break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        {
            XEvent event{};
            event.type = ClientMessage;
            event.xclient.window = window;
            event.xclient.message_type = XInternAtom(display, "_NET_ACTIVE_WINDOW", kXFalse);
            event.xclient.format = 32;
            event.xclient.data.l[0] = 2;
            XSendEvent(display, root, kXFalse, SubstructureRedirectMask | SubstructureNotifyMask,
                       &event);
            XSync(display, kXFalse);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        // The real cursor has to be over the window before anything a physical-path device does
        // can reach this client at all (see SteerPointerInto).
        std::string error;
        Driver driver;
        std::unique_ptr<VirtualInput> device =
            UinputReachesDisplay(driver, arguments, error) ? VirtualInput::Create(false, true, error)
                                                           : nullptr;
        if (device != nullptr)
        {
            const bool steered = SteerPointerInto(*device, driver, window, 320, 240);
            Info(std::string("real cursor steered into the probe window: ") +
                 (steered ? "yes" : "NO"));
        }
        else
        {
            Info("uinput unavailable: " + error);
        }

        unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
        XISetMask(mask, XI_RawMotion);
        XIEventMask eventMask{};
        eventMask.deviceid = XIAllMasterDevices;
        eventMask.mask_len = sizeof(mask);
        eventMask.mask = mask;
        XISelectEvents(display, root, &eventMask, 1);

        if (grab)
        {
            char zero[8] = {};
            const Pixmap pixmap = XCreateBitmapFromData(display, root, zero, 1, 1);
            XColor black{};
            const ::Cursor hidden = XCreatePixmapCursor(display, pixmap, pixmap, &black, &black, 0, 0);
            XFreePixmap(display, pixmap);
            const int status = XGrabPointer(display, window, kXTrue,
                                            ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                            GrabModeAsync, GrabModeAsync, window, hidden,
                                            kCurrentTime);
            Info("XGrabPointer status " + std::to_string(status) + " (0 = GrabSuccess)");
        }
        XSync(display, kXFalse);

        const auto collect = [&](const std::string& label, const std::function<void()>& inject) {
            // Drain anything pending first.
            while (XPending(display) > 0)
            {
                XEvent discard;
                XNextEvent(display, &discard);
                if (discard.type == GenericEvent && XGetEventData(display, &discard.xcookie))
                {
                    XFreeEventData(display, &discard.xcookie);
                }
            }
            inject();
            std::map<std::pair<int, int>, DeviceTotals> totals;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() < deadline)
            {
                while (XPending(display) > 0)
                {
                    XEvent event;
                    XNextEvent(display, &event);
                    if (event.type != GenericEvent || event.xcookie.extension != opcode ||
                        XGetEventData(display, &event.xcookie) == 0)
                    {
                        continue;
                    }
                    if (event.xcookie.evtype == XI_RawMotion)
                    {
                        const auto* raw = static_cast<const XIRawEvent*>(event.xcookie.data);
                        DeviceTotals& total = totals[{raw->deviceid, raw->sourceid}];
                        ++total.events;
                        const double* rawValues = raw->raw_values;
                        const double* values = raw->valuators.values;
                        std::ostringstream line;
                        line << "  raw dev=" << raw->deviceid << " src=" << raw->sourceid
                             << " axes:";
                        for (int axis = 0; axis < raw->valuators.mask_len * 8; ++axis)
                        {
                            if (XIMaskIsSet(raw->valuators.mask, axis) == 0) { continue; }
                            line << ' ' << axis << "=raw " << *rawValues << "/val " << *values;
                            if (axis == 0) { total.rawX += *rawValues; total.valueX += *values; }
                            if (axis == 1) { total.rawY += *rawValues; total.valueY += *values; }
                            ++rawValues;
                            ++values;
                        }
                        if (verbose) { Info(line.str()); }
                    }
                    XFreeEventData(display, &event.xcookie);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (totals.empty())
            {
                Info(label + ": no raw events at all");
            }
            for (const auto& [key, total] : totals)
            {
                std::ostringstream line;
                line << label << ": dev " << key.first << " (" << DeviceName(display, key.first)
                     << ") src " << key.second << " (" << DeviceName(display, key.second)
                     << "): " << total.events << " events, raw sum (" << total.rawX << ", "
                     << total.rawY << "), processed sum (" << total.valueX << ", " << total.valueY
                     << ")";
                Info(line.str());
            }
        };

        collect("idle 0.5 s", [] {});
#if defined(CNA_X11_VALIDATION_HAVE_XTEST)
        collect("XTest 20 x (+10, 0)", [display] {
            for (int step = 0; step < 20; ++step)
            {
                XTestFakeRelativeMotionEvent(display, 10, 0, 0);
            }
            XSync(display, kXFalse);
        });
#endif
        if (device != nullptr)
        {
            collect("uinput 30 x (+7, -3)", [&device] {
                for (int step = 0; step < 30; ++step)
                {
                    device->Move(7, -3);
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
                }
            });
            collect("uinput 50 x (+1, 0)", [&device] {
                for (int step = 0; step < 50; ++step)
                {
                    device->Move(1, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                }
            });
        }
        collect("idle 0.5 s again", [] {});

        XUngrabPointer(display, kCurrentTime);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return 0;
    }

} // namespace CnaX11Validation
