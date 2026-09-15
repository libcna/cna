// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "X11Headers.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;

    /**
     * @brief Where an X input device's id lands in CNA's device ids: far above the Linux
     * controllers' (which count up from 1), so the two never meet.
     */
    inline constexpr DeviceId kX11InputDeviceIdBase = 0x10000;

    /** @brief What XInput2 says about one device, as far as classifying it goes. */
    struct X11InputDeviceFacts
    {
        /** @brief The XInput2 device id. */
        int id = 0;
        /** @brief Its use: master or slave, keyboard or pointer, or floating. */
        int use = 0;
        /** @brief Its name. */
        std::string name;
        /** @brief Whether it is enabled. */
        bool enabled = true;
        /** @brief Whether it reports touch points (an XInput 2.2 touch class). */
        bool touch = false;
    };

    /**
     * @brief Decides what a device is, in the contract's classes.
     *
     * Slave keyboards are keyboards and slave pointers mice; a slave pointer with a touch class is
     * a touch device as well. The master devices -- the core pointer and keyboard every server
     * has, which aggregate the rest -- the server's own `XTEST` devices, floating and disabled
     * devices are not devices a user attached, and are nothing.
     *
     * @param device The device.
     * @return Its classes; empty for none.
     */
    [[nodiscard]] std::vector<InputDeviceKind> ClassifyX11InputDevice(const X11InputDeviceFacts& device);

    /**
     * @brief The connections and disconnections between two enumerations.
     *
     * @param before Each device's classes, by CNA device id, as last seen.
     * @param after The same, now.
     * @return One DeviceEvent per class of each device that appeared or disappeared --
     * disconnections first, then connections, each in id order.
     */
    [[nodiscard]] std::vector<DeviceEvent> DiffX11InputDevices(
        const std::map<DeviceId, std::vector<InputDeviceKind>>& before,
        const std::map<DeviceId, std::vector<InputDeviceKind>>& after);

    /**
     * @brief Input-device enumeration through XInput2 (plans/plan_x11.md X11-0165).
     *
     * Keyboards, mice and touch devices are asked of the server on each call, as the contract
     * wants -- `TouchPanel` asks whether a touchscreen is attached before anyone has touched one.
     * Gamepads, joysticks and haptic devices are Linux's, asked of the callback the platform gives.
     * Hot-plug is followed through `XI_HierarchyChanged`: each change is diffed against the last
     * enumeration and reported as `DeviceEvent`s with the same ids.
     */
    class X11InputDevices final : public IPlatformInputDevices
    {
    public:
        /** @brief Answers the classes Linux serves: gamepads, joysticks and haptic devices. */
        using ControllerSource = std::function<std::vector<InputDeviceInfo>(InputDeviceKind)>;

        /**
         * @brief Enumerates through one connection.
         *
         * @param connection The connection; XInput2 must be present on it.
         * @param controllers The Linux devices, or an empty function where there are none.
         */
        X11InputDevices(X11Connection& connection, ControllerSource controllers);

        /**
         * @brief Lists the attached devices of one class.
         * @param kind The class.
         * @return The devices; sensors never, neither X nor this backend having any.
         */
        [[nodiscard]] std::vector<InputDeviceInfo> GetDevices(InputDeviceKind kind) const override;

        /**
         * @brief Gets whether a device of one class is attached.
         * @param kind The class.
         * @return True when one is.
         */
        [[nodiscard]] bool HasDevice(InputDeviceKind kind) const override;

        /**
         * @brief Follows an `XI_HierarchyChanged`: re-enumerates and reports what changed.
         * @param destination Receives the DeviceEvents.
         */
        void HandleHierarchyChanged(std::vector<PlatformEvent>& destination);

    private:
        struct Device
        {
            InputDeviceInfo info;
            std::vector<InputDeviceKind> kinds;
        };

        [[nodiscard]] std::vector<Device> Query() const;
        [[nodiscard]] static std::map<DeviceId, std::vector<InputDeviceKind>> KindsOf(
            const std::vector<Device>& devices);

        X11Connection& connection_;
        ControllerSource controllers_;
        std::map<DeviceId, std::vector<InputDeviceKind>> known_;
    };

} // namespace CNA::Platform::X11
