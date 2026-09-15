// SPDX-License-Identifier: MS-PL

#include "WaylandSeat.hpp"

namespace CNA::Platform::Wayland {

    const wl_seat_listener WaylandSeat::kListener = {
        .capabilities = [](void* data, wl_seat*, const std::uint32_t capabilities) {
            static_cast<WaylandSeat*>(data)->OnCapabilities(capabilities);
        },
        .name = [](void* data, wl_seat*, const char* name) {
            static_cast<WaylandSeat*>(data)->name_ = name != nullptr ? name : "";
        },
    };

    WaylandSeat::WaylandSeat(wl_registry* registry, const std::uint32_t name, const std::uint32_t version,
                             Devices devices)
        : globalName_(name), devices_(std::move(devices))
    {
        seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, version));
        wl_seat_add_listener(seat_, &kListener, this);
    }

    WaylandSeat::~WaylandSeat()
    {
        // Devices first: each is the service's, and a device made from a seat must be released
        // before the seat is.
        OnCapabilities(0);
        if (seat_ != nullptr)
        {
            if (wl_seat_get_version(seat_) >= WL_SEAT_RELEASE_SINCE_VERSION)
            {
                wl_seat_release(seat_);
            }
            else
            {
                wl_seat_destroy(seat_);
            }
            seat_ = nullptr;
        }
    }

    void WaylandSeat::OnCapabilities(const std::uint32_t capabilities)
    {
        capabilities_ = capabilities;
        const bool keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
        const bool pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
        const bool touch = (capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;

        if (keyboard && keyboard_ == nullptr && seat_ != nullptr)
        {
            keyboard_ = wl_seat_get_keyboard(seat_);
            if (devices_.keyboardAdded) { devices_.keyboardAdded(keyboard_, seat_); }
        }
        else if (!keyboard && keyboard_ != nullptr)
        {
            wl_keyboard* removed = keyboard_;
            keyboard_ = nullptr;
            if (devices_.keyboardRemoved) { devices_.keyboardRemoved(removed); }
        }

        if (pointer && pointer_ == nullptr && seat_ != nullptr)
        {
            pointer_ = wl_seat_get_pointer(seat_);
            if (devices_.pointerAdded) { devices_.pointerAdded(pointer_, seat_); }
        }
        else if (!pointer && pointer_ != nullptr)
        {
            wl_pointer* removed = pointer_;
            pointer_ = nullptr;
            if (devices_.pointerRemoved) { devices_.pointerRemoved(removed); }
        }

        if (touch && touch_ == nullptr && seat_ != nullptr)
        {
            touch_ = wl_seat_get_touch(seat_);
            if (devices_.touchAdded) { devices_.touchAdded(touch_, seat_); }
        }
        else if (!touch && touch_ != nullptr)
        {
            wl_touch* removed = touch_;
            touch_ = nullptr;
            if (devices_.touchRemoved) { devices_.touchRemoved(removed); }
        }

        if (!announced_ && seat_ != nullptr && devices_.seatReady)
        {
            announced_ = true;
            devices_.seatReady(*this);
        }
    }

} // namespace CNA::Platform::Wayland
