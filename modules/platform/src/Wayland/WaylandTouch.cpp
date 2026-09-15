// SPDX-License-Identifier: MS-PL

#include "WaylandTouch.hpp"

#include <algorithm>

namespace CNA::Platform::Wayland {

    const wl_touch_listener WaylandTouch::kListener = {
        .down = [](void* data, wl_touch* proxy, const std::uint32_t serial, std::uint32_t, wl_surface* surface,
                   const std::int32_t id, const wl_fixed_t x, const wl_fixed_t y) {
            auto* self = static_cast<WaylandTouch*>(data);
            Device* device = self->Find(proxy);
            if (device == nullptr || surface == nullptr)
            {
                return;
            }
            if (self->host_.recordSerial) { self->host_.recordSerial(device->seat, serial); }
            Finger finger;
            finger.id = id;
            finger.window = self->host_.resolveSurface ? self->host_.resolveSurface(surface) : 0;
            if (finger.window == 0)
            {
                return;
            }
            if (self->host_.clientSize) { self->host_.clientSize(finger.window, finger.width, finger.height); }
            finger.width = std::max(1, finger.width);
            finger.height = std::max(1, finger.height);
            finger.x = static_cast<float>(wl_fixed_to_double(x)) / static_cast<float>(finger.width);
            finger.y = static_cast<float>(wl_fixed_to_double(y)) / static_cast<float>(finger.height);
            std::erase_if(device->fingers, [id](const Finger& other) { return other.id == id; });
            device->fingers.push_back(finger);
            self->Emit(*device, finger, TouchEventKind::Down, 0.0f, 0.0f);
        },
        .up = [](void* data, wl_touch* proxy, const std::uint32_t serial, std::uint32_t, const std::int32_t id) {
            auto* self = static_cast<WaylandTouch*>(data);
            Device* device = self->Find(proxy);
            if (device == nullptr)
            {
                return;
            }
            if (self->host_.recordSerial) { self->host_.recordSerial(device->seat, serial); }
            const auto found = std::find_if(device->fingers.begin(), device->fingers.end(),
                                            [id](const Finger& finger) { return finger.id == id; });
            if (found == device->fingers.end())
            {
                return;
            }
            const Finger lifted = *found;
            device->fingers.erase(found);
            self->Emit(*device, lifted, TouchEventKind::Up, 0.0f, 0.0f);
        },
        .motion = [](void* data, wl_touch* proxy, std::uint32_t, const std::int32_t id, const wl_fixed_t x,
                     const wl_fixed_t y) {
            auto* self = static_cast<WaylandTouch*>(data);
            Device* device = self->Find(proxy);
            if (device == nullptr)
            {
                return;
            }
            for (Finger& finger : device->fingers)
            {
                if (finger.id != id)
                {
                    continue;
                }
                const float nx = static_cast<float>(wl_fixed_to_double(x)) / static_cast<float>(finger.width);
                const float ny = static_cast<float>(wl_fixed_to_double(y)) / static_cast<float>(finger.height);
                const float dx = nx - finger.x;
                const float dy = ny - finger.y;
                finger.x = nx;
                finger.y = ny;
                self->Emit(*device, finger, TouchEventKind::Motion, dx, dy);
                return;
            }
        },
        .frame = [](void*, wl_touch*) {
            // Events are delivered as they come; a frame only groups what arrived together.
        },
        .cancel = [](void* data, wl_touch* proxy) {
            // The compositor took the touch sequence (a gesture of its own): every finger is
            // cancelled, and no up follows.
            auto* self = static_cast<WaylandTouch*>(data);
            if (Device* device = self->Find(proxy))
            {
                self->CancelAll(*device);
            }
        },
        .shape = [](void*, wl_touch*, std::int32_t, wl_fixed_t, wl_fixed_t) {},
        .orientation = [](void*, wl_touch*, std::int32_t, wl_fixed_t) {},
    };

    WaylandTouch::~WaylandTouch()
    {
        while (!devices_.empty())
        {
            Detach(devices_.back().proxy);
        }
    }

    WaylandTouch::Device* WaylandTouch::Find(wl_touch* touch)
    {
        for (Device& device : devices_)
        {
            if (device.proxy == touch)
            {
                return &device;
            }
        }
        return nullptr;
    }

    void WaylandTouch::Attach(wl_touch* touch, wl_seat* seat, const std::uint32_t seatName)
    {
        Device device;
        device.proxy = touch;
        device.seat = seat;
        device.seatName = seatName;
        devices_.push_back(std::move(device));
        wl_touch_add_listener(touch, &kListener, this);
    }

    void WaylandTouch::Detach(wl_touch* touch)
    {
        const auto found = std::find_if(devices_.begin(), devices_.end(),
                                        [touch](const Device& device) { return device.proxy == touch; });
        if (found == devices_.end())
        {
            return;
        }
        CancelAll(*found);
        if (wl_touch_get_version(touch) >= WL_TOUCH_RELEASE_SINCE_VERSION)
        {
            wl_touch_release(touch);
        }
        else
        {
            wl_touch_destroy(touch);
        }
        devices_.erase(found);
    }

    void WaylandTouch::ForgetWindow(const WindowId window)
    {
        for (Device& device : devices_)
        {
            for (auto it = device.fingers.begin(); it != device.fingers.end();)
            {
                if (it->window == window)
                {
                    const Finger lost = *it;
                    it = device.fingers.erase(it);
                    Emit(device, lost, TouchEventKind::Cancelled, 0.0f, 0.0f);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void WaylandTouch::CancelAll(Device& device)
    {
        const std::vector<Finger> fingers = std::move(device.fingers);
        device.fingers.clear();
        for (const Finger& finger : fingers)
        {
            Emit(device, finger, TouchEventKind::Cancelled, 0.0f, 0.0f);
        }
    }

    void WaylandTouch::Emit(const Device& device, const Finger& finger, const TouchEventKind kind, const float deltaX,
                            const float deltaY)
    {
        if (!host_.post)
        {
            return;
        }
        TouchEvent event;
        event.window = finger.window;
        event.fingerId = (static_cast<std::uint64_t>(device.seatName) << 32) | static_cast<std::uint32_t>(finger.id);
        event.kind = kind;
        event.x = finger.x;
        event.y = finger.y;
        event.deltaX = deltaX;
        event.deltaY = deltaY;
        event.pressure = kind == TouchEventKind::Up || kind == TouchEventKind::Cancelled ? 0.0f : 1.0f;
        event.clientWidth = finger.width;
        event.clientHeight = finger.height;
        host_.post(event);
    }

} // namespace CNA::Platform::Wayland
