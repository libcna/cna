// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief One connected input device. */
    struct InputDeviceInfo
    {
        /** @brief Identifies the device while it stays connected; matches `DeviceEvent::device`. */
        DeviceId id = 0;
        /** @brief What class of device this is. */
        InputDeviceKind kind = InputDeviceKind::Keyboard;
        /** @brief The device's human-readable name, or empty when the platform reports none. */
        std::string name;
    };

    /**
     * @brief Enumerates the input devices currently attached.
     *
     * ### Why this is not covered by `DeviceEvent`
     *
     * `DeviceEvent` reports *changes*: a device was added, a device was removed. A platform can
     * deliver those perfectly and still be unable to answer "what is attached right now", because
     * nothing fires an add event for a device that was already there when the process started.
     *
     * That gap is not hypothetical. `TouchPanel::GetCapabilities()` asks exactly that question,
     * on every call, to decide whether the machine has a touchscreen — and a game asks it before
     * the user has touched anything. Answering it from accumulated events would report "no
     * touchscreen" on a tablet until the first touch, which is precisely backwards.
     *
     * The two views use the same vocabulary on purpose: the `id` here is the same identifier a
     * `DeviceEvent` carries, so a caller can enumerate once at startup and then track changes,
     * without a second identity scheme to reconcile.
     *
     * ### Enumeration is a query, not a snapshot
     *
     * Unlike keyboard, mouse and gamepad state, this is not read per frame and has no `Update()`.
     * It reads the platform's live device list each time it is called, which is what the FNA
     * behaviour `TouchPanel` reproduces requires.
     */
    class IPlatformInputDevices
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformInputDevices() = default;

        /**
         * @brief Gets the devices of one class that are currently attached.
         *
         * @param kind Which class of device to list.
         * @return The attached devices, in no guaranteed order; empty when none are attached or
         * when the platform cannot enumerate that class.
         */
        [[nodiscard]] virtual std::vector<InputDeviceInfo> GetDevices(InputDeviceKind kind) const = 0;

        /**
         * @brief Gets whether at least one device of a class is attached.
         *
         * A separate call rather than `GetDevices(kind).empty()` because that is the only
         * question most callers have, and building and returning a vector of names to answer it
         * is work an implementation can often skip entirely.
         *
         * @param kind Which class of device to test for.
         * @return True if at least one is attached.
         */
        [[nodiscard]] virtual bool HasDevice(InputDeviceKind kind) const = 0;
    };

} // namespace CNA::Platform
