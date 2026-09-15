// SPDX-License-Identifier: MS-PL

#include "X11Touch.hpp"

#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11Mouse.hpp"
#include "X11Window.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace CNA::Platform::X11 {

    TouchEvent MakeTouchEvent(const WindowId window, const std::uint64_t finger,
                              const TouchEventKind kind, const double x, const double y,
                              const int clientWidth, const int clientHeight,
                              const std::optional<std::pair<float, float>> previous)
    {
        TouchEvent touch;
        touch.window = window;
        touch.fingerId = finger;
        touch.kind = kind;
        touch.clientWidth = std::max(clientWidth, 1);
        touch.clientHeight = std::max(clientHeight, 1);
        touch.x = static_cast<float>(x / touch.clientWidth);
        touch.y = static_cast<float>(y / touch.clientHeight);
        touch.pressure = 1.0f;
        if (kind == TouchEventKind::Motion && previous)
        {
            touch.deltaX = touch.x - previous->first;
            touch.deltaY = touch.y - previous->second;
        }
        return touch;
    }

    float NormalisePressure(const double value, const double minimum, const double maximum) noexcept
    {
        if (!(maximum > minimum))
        {
            return 1.0f;
        }
        return static_cast<float>(std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0));
    }

    X11Touch::X11Touch(X11Connection& connection, X11Mouse* mouse)
        : connection_(connection), mouse_(mouse)
    {
#if defined(CNA_X11_HAVE_XI)
        if (connection_.GetXInput2Opcode() >= 0)
        {
            // Hot-plugged and removed pens: the hierarchy changes on the root window.
            unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
            XISetMask(mask, XI_HierarchyChanged);
            XIEventMask eventMask{};
            eventMask.deviceid = XIAllDevices;
            eventMask.mask_len = sizeof(mask);
            eventMask.mask = mask;
            X11ErrorTrap trap(connection_.GetDisplay());
            XISelectEvents(connection_.GetDisplay(), connection_.GetRoot(), &eventMask, 1);
            trap.Sync();
        }
#endif
        RefreshPens();
    }

    void X11Touch::AttachWindow(const X11Window& window)
    {
#if defined(CNA_X11_HAVE_XI)
        windows_.push_back(window.GetXWindow());
        if (connection_.HasXInput2Touch())
        {
            // All three or none: the protocol refuses a partial touch selection with BadValue.
            unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
            XISetMask(mask, XI_TouchBegin);
            XISetMask(mask, XI_TouchUpdate);
            XISetMask(mask, XI_TouchEnd);
            XIEventMask eventMask{};
            eventMask.deviceid = XIAllMasterDevices;
            eventMask.mask_len = sizeof(mask);
            eventMask.mask = mask;
            X11ErrorTrap trap(connection_.GetDisplay());
            XISelectEvents(connection_.GetDisplay(), window.GetXWindow(), &eventMask, 1);
            trap.Sync();
        }
        SelectPens(window.GetXWindow());
#else
        (void) window;
#endif
    }

    void X11Touch::RefreshPens()
    {
#if defined(CNA_X11_HAVE_XI)
        if (connection_.GetXInput2Opcode() < 0)
        {
            return;
        }
        Display* display = connection_.GetDisplay();
        // Only if some device has ever created the label: a server with no pressure axis has no
        // pens, and interning the name would create an atom for nothing.
        const Atom pressureLabel = XInternAtom(display, "Abs Pressure", kXTrue);
        std::map<int, Pen> found;
        if (pressureLabel != kNone)
        {
            X11ErrorTrap trap(display);
            int count = 0;
            if (XIDeviceInfo* devices = XIQueryDevice(display, XIAllDevices, &count))
            {
                for (int index = 0; index < count; ++index)
                {
                    const XIDeviceInfo& device = devices[index];
                    if ((device.use != XISlavePointer && device.use != XIFloatingSlave) ||
                        device.enabled == 0)
                    {
                        continue;
                    }
                    for (int classIndex = 0; classIndex < device.num_classes; ++classIndex)
                    {
                        const XIAnyClassInfo* info = device.classes[classIndex];
                        if (info->type != XIValuatorClass)
                        {
                            continue;
                        }
                        const auto* valuator = reinterpret_cast<const XIValuatorClassInfo*>(info);
                        if (valuator->label != pressureLabel)
                        {
                            continue;
                        }
                        Pen pen;
                        // A pen that was already known keeps its state -- it may be touching.
                        if (const auto known = pens_.find(device.deviceid); known != pens_.end())
                        {
                            pen = known->second;
                        }
                        pen.pressureValuator = valuator->number;
                        pen.pressureMinimum = valuator->min;
                        pen.pressureMaximum = valuator->max;
                        found[device.deviceid] = pen;
                    }
                }
                XIFreeDeviceInfo(devices);
            }
            trap.Sync();
        }
        pens_ = std::move(found);
        for (const ::Window window : windows_)
        {
            SelectPens(window);
        }
#endif
    }

    void X11Touch::SelectPens(const ::Window window) const
    {
#if defined(CNA_X11_HAVE_XI)
        if (pens_.empty())
        {
            return;
        }
        // Per pen device, never XIAllDevices: a selection for the master pointer's own events
        // would take the core pointer events -- the mouse -- away from this window.
        std::vector<std::array<unsigned char, XIMaskLen(XI_LASTEVENT)>> bits(pens_.size());
        std::vector<XIEventMask> masks;
        masks.reserve(pens_.size());
        std::size_t slot = 0;
        for (const auto& [device, pen] : pens_)
        {
            (void) pen;
            std::array<unsigned char, XIMaskLen(XI_LASTEVENT)>& mask = bits[slot++];
            mask.fill(0);
            XISetMask(mask.data(), XI_ButtonPress);
            XISetMask(mask.data(), XI_ButtonRelease);
            XISetMask(mask.data(), XI_Motion);
            XIEventMask eventMask{};
            eventMask.deviceid = device;
            eventMask.mask_len = static_cast<int>(mask.size());
            eventMask.mask = mask.data();
            masks.push_back(eventMask);
        }
        X11ErrorTrap trap(connection_.GetDisplay());
        XISelectEvents(connection_.GetDisplay(), window, masks.data(),
                       static_cast<int>(masks.size()));
        trap.Sync();
#else
        (void) window;
#endif
    }

    void X11Touch::EmitContact(const std::uint64_t finger, const TouchEventKind kind,
                               const double x, const double y, const float pressure,
                               const bool emulating, X11Window& window,
                               std::vector<PlatformEvent>& destination)
    {
        const auto found = contacts_.find(finger);
        if (kind != TouchEventKind::Down && found == contacts_.end())
        {
            // The rest of a contact whose beginning this platform never saw -- it started before
            // the window selected its events. A motion or a lift of a finger the game was never
            // told about would be worse than nothing.
            return;
        }

        // A contact keeps the size its window had when it began: every event of it is then in
        // one coordinate system, even across a resize mid-gesture.
        Contact contact;
        if (found != contacts_.end())
        {
            contact = found->second;
        }
        else
        {
            const WindowSize size = window.GetCachedSize();
            contact.window = window.GetId();
            contact.clientWidth = std::max(size.width, 1);
            contact.clientHeight = std::max(size.height, 1);
            contact.emulating = emulating;
        }

        std::optional<std::pair<float, float>> previous;
        if (found != contacts_.end())
        {
            previous = std::make_pair(contact.x, contact.y);
        }
        TouchEvent touch = MakeTouchEvent(contact.window, finger, kind, x, y, contact.clientWidth,
                                          contact.clientHeight, previous);
        touch.pressure = pressure;
        if (kind == TouchEventKind::Motion && previous && touch.x == previous->first &&
            touch.y == previous->second && pressure == contact.pressure)
        {
            // An update that changed nothing the contract carries.
            return;
        }
        destination.emplace_back(touch);

        if (contact.emulating && mouse_ != nullptr)
        {
            // What the server would have delivered as pointer events, had this window not taken
            // the touch events instead.
            const auto pixelX = static_cast<float>(x);
            const auto pixelY = static_cast<float>(y);
            MouseMotionEvent motion;
            motion.window = contact.window;
            motion.x = pixelX;
            motion.y = pixelY;
            if (previous)
            {
                motion.deltaX = (touch.x - previous->first) * static_cast<float>(contact.clientWidth);
                motion.deltaY = (touch.y - previous->second) * static_cast<float>(contact.clientHeight);
            }
            destination.emplace_back(motion);
            mouse_->SetLastPosition(contact.window, static_cast<int>(std::lround(x)),
                                    static_cast<int>(std::lround(y)));
            if (kind != TouchEventKind::Motion)
            {
                MouseButtonEvent button;
                button.window = contact.window;
                button.button = 1;
                button.pressed = kind == TouchEventKind::Down;
                button.x = pixelX;
                button.y = pixelY;
                destination.emplace_back(button);
                mouse_->SetButtonState(1, button.pressed);
                mouse_->SetTouchHeld(button.pressed);
            }
        }

        if (kind == TouchEventKind::Up)
        {
            contacts_.erase(finger);
        }
        else
        {
            contact.x = touch.x;
            contact.y = touch.y;
            contact.pressure = pressure;
            contacts_[finger] = contact;
        }
    }

    void X11Touch::HandleEvent(const XIDeviceEvent& event, X11Window& window,
                               std::vector<PlatformEvent>& destination)
    {
#if defined(CNA_X11_HAVE_XI)
        TouchEventKind kind = TouchEventKind::Motion;
        if (event.evtype == XI_TouchBegin) { kind = TouchEventKind::Down; }
        else if (event.evtype == XI_TouchEnd) { kind = TouchEventKind::Up; }
        else if (event.evtype != XI_TouchUpdate) { return; }
        // Pressure 1 throughout, as SDL3's X11 backend reports a touch.
        EmitContact(MakeFingerId(event.sourceid, event.detail), kind, event.event_x, event.event_y,
                    1.0f, (event.flags & XITouchEmulatingPointer) != 0, window, destination);
#else
        (void) event;
        (void) window;
        (void) destination;
#endif
    }

    bool X11Touch::HandlePenEvent(const XIDeviceEvent& event, X11Window& window,
                                  std::vector<PlatformEvent>& destination)
    {
#if defined(CNA_X11_HAVE_XI)
        if (event.evtype != XI_ButtonPress && event.evtype != XI_ButtonRelease &&
            event.evtype != XI_Motion)
        {
            return false;
        }
        const auto found = pens_.find(event.sourceid);
        if (found == pens_.end())
        {
            return false;
        }
        Pen& pen = found->second;

        // The valuator mask names the axes this event carries, packed in axis order.
        const double* values = event.valuators.values;
        for (int axis = 0; axis < event.valuators.mask_len * 8; ++axis)
        {
            if (XIMaskIsSet(event.valuators.mask, axis) == 0)
            {
                continue;
            }
            if (axis == pen.pressureValuator)
            {
                pen.pressure = NormalisePressure(*values, pen.pressureMinimum, pen.pressureMaximum);
            }
            ++values;
        }

        // One contact per pen: the tip. Button 1 is the tip touching the surface.
        const std::uint64_t finger = MakeFingerId(event.sourceid, 0);
        if (event.evtype == XI_ButtonPress && event.detail == 1)
        {
            pen.touching = true;
            EmitContact(finger, TouchEventKind::Down, event.event_x, event.event_y, pen.pressure,
                        false, window, destination);
        }
        else if (event.evtype == XI_ButtonRelease && event.detail == 1 && pen.touching)
        {
            pen.touching = false;
            EmitContact(finger, TouchEventKind::Up, event.event_x, event.event_y, pen.pressure,
                        false, window, destination);
        }
        else if (event.evtype == XI_Motion && pen.touching)
        {
            EmitContact(finger, TouchEventKind::Motion, event.event_x, event.event_y,
                        pen.pressure, false, window, destination);
        }
        // Hovering, or a barrel button: the pen's, and nothing for the contract. The core
        // pointer events it also drives are the mouse's.
        return true;
#else
        (void) event;
        (void) window;
        (void) destination;
        return false;
#endif
    }

    void X11Touch::ForgetWindow(const WindowId window, const ::Window xid)
    {
        if (xid != 0)
        {
            std::erase(windows_, xid);
        }
        for (auto contact = contacts_.begin(); contact != contacts_.end();)
        {
            if (contact->second.window != window)
            {
                ++contact;
                continue;
            }
            TouchEvent cancelled;
            cancelled.window = window;
            cancelled.fingerId = contact->first;
            cancelled.kind = TouchEventKind::Cancelled;
            cancelled.x = contact->second.x;
            cancelled.y = contact->second.y;
            cancelled.clientWidth = contact->second.clientWidth;
            cancelled.clientHeight = contact->second.clientHeight;
            pending_.emplace_back(cancelled);
            if (contact->second.emulating && mouse_ != nullptr)
            {
                // The left button this contact was holding lets go with it.
                MouseButtonEvent release;
                release.window = window;
                release.button = 1;
                release.pressed = false;
                release.x = contact->second.x * static_cast<float>(contact->second.clientWidth);
                release.y = contact->second.y * static_cast<float>(contact->second.clientHeight);
                pending_.emplace_back(release);
                mouse_->SetButtonState(1, false);
                mouse_->SetTouchHeld(false);
            }
            // A pen whose contact this was is not touching any more, as far as the game knows.
            const int device = static_cast<int>(contact->first >> 32);
            if (const auto pen = pens_.find(device);
                pen != pens_.end() && static_cast<std::uint32_t>(contact->first) == 0)
            {
                pen->second.touching = false;
            }
            contact = contacts_.erase(contact);
        }
    }

    void X11Touch::TakePendingEvents(std::vector<PlatformEvent>& destination)
    {
        for (PlatformEvent& event : pending_)
        {
            destination.emplace_back(std::move(event));
        }
        pending_.clear();
    }

} // namespace CNA::Platform::X11
