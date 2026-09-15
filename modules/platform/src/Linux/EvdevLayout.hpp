// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include <linux/input.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Linux controllers through evdev: the kernel's own input interface, reached with nothing but the
// kernel headers. This file is the part with no I/O in it -- what a device is, how its codes map
// onto CNA's gamepad, and how raw values become the contract's normalised ones -- so every rule
// here is testable from synthetic descriptions, with no device and no permissions.
//
// The reference for the mapping is the kernel's gamepad API (Documentation/input/gamepad.rst):
// face buttons are named by POSITION (BTN_SOUTH is the bottom one), which is exactly what CNA's
// GamepadButton means (A is the bottom face button). One family of drivers predates that
// convention: xpad reports an Xbox pad's left "X" button as BTN_X and its top "Y" button as BTN_Y,
// and BTN_X/BTN_Y are aliases of BTN_NORTH/BTN_WEST -- so for xpad the two are swapped back.

namespace CNA::Platform::Linux {

    /** @brief The range the kernel reports for one absolute axis (`struct input_absinfo`). */
    struct EvdevAxisRange
    {
        /** @brief The smallest value the axis reports. */
        int minimum = 0;
        /** @brief The largest value the axis reports. */
        int maximum = 0;
        /** @brief The kernel's suggested dead band around the centre (informational only). */
        int flat = 0;
    };

    /** @brief What the kernel says about one input device, read once when it is opened. */
    struct EvdevDescription
    {
        /** @brief The device name (`EVIOCGNAME`). */
        std::string name;
        /** @brief The kernel driver bound to the device, e.g. `xpad`, or empty when unknown. */
        std::string driver;
        /** @brief The device's unique identifier (`EVIOCGUNIQ`: a Bluetooth address, a serial). */
        std::string uniq;
        /** @brief Bus type (`struct input_id`). */
        std::uint16_t bus = 0;
        /** @brief Vendor identifier (`struct input_id`). */
        std::uint16_t vendor = 0;
        /** @brief Product identifier (`struct input_id`). */
        std::uint16_t product = 0;
        /** @brief Version (`struct input_id`). */
        std::uint16_t version = 0;
        /** @brief Key and button codes the device can report (`EV_KEY`). */
        std::bitset<KEY_CNT> keys;
        /** @brief Absolute axes the device can report (`EV_ABS`). */
        std::bitset<ABS_CNT> axes;
        /** @brief The range of each absolute axis, indexed by `ABS_*` code. */
        std::array<EvdevAxisRange, ABS_CNT> ranges{};
        /** @brief Force-feedback effects the device supports (`EV_FF`). */
        std::bitset<FF_CNT> forceFeedback;
        /** @brief Device properties (`INPUT_PROP_*`). */
        std::bitset<INPUT_PROP_CNT> properties;
    };

    /** @brief What a device is, as far as CNA's controller services are concerned. */
    enum class EvdevDeviceClass
    {
        /** @brief Not a controller: a keyboard, a mouse, a touchpad, a motion sensor node... */
        None,
        /** @brief A controller without the gamepad button set: a stick, a wheel, a pad of its own. */
        Joystick,
        /** @brief A controller with the kernel's gamepad button set. */
        Gamepad
    };

    /**
     * @brief Decides whether a device is a controller, and which kind.
     *
     * @param description The device.
     * @return The class; motion-sensor nodes (`INPUT_PROP_ACCELEROMETER`) are never controllers.
     */
    [[nodiscard]] EvdevDeviceClass ClassifyEvdevDevice(const EvdevDescription& description);

    /** @brief How one absolute axis feeds a gamepad axis. */
    struct EvdevAxisBinding
    {
        /** @brief The kernel axis (`ABS_*`). */
        std::uint16_t code = 0;
        /** @brief The gamepad axis it drives. */
        GamepadAxis axis = GamepadAxis::LeftThumbstickX;
        /** @brief The axis's range. */
        EvdevAxisRange range{};
    };

    /** @brief How one hat axis (`ABS_HAT0X`/`ABS_HAT0Y`) feeds two D-pad buttons. */
    struct EvdevHatBinding
    {
        /** @brief The kernel axis. */
        std::uint16_t code = 0;
        /** @brief The button a negative value presses (left, or up). */
        GamepadButton negative = GamepadButton::DPadLeft;
        /** @brief The button a positive value presses (right, or down). */
        GamepadButton positive = GamepadButton::DPadRight;
    };

    /** @brief How a digital trigger button (`BTN_TL2`/`BTN_TR2`) drives a trigger axis. */
    struct EvdevDigitalTrigger
    {
        /** @brief The kernel key code. */
        std::uint16_t code = 0;
        /** @brief The trigger axis it drives to 0 or 1. */
        GamepadAxis axis = GamepadAxis::LeftTrigger;
    };

    /** @brief Everything needed to turn one device's events into a gamepad snapshot. */
    struct EvdevGamepadLayout
    {
        /** @brief Key codes and the gamepad buttons they press. */
        std::vector<std::pair<std::uint16_t, GamepadButton>> buttons;
        /** @brief Absolute axes and the gamepad axes they drive. */
        std::vector<EvdevAxisBinding> axes;
        /** @brief Hat axes and the D-pad buttons they press. */
        std::vector<EvdevHatBinding> hats;
        /** @brief Digital triggers, used where the device has no analogue trigger axis. */
        std::vector<EvdevDigitalTrigger> digitalTriggers;
        /** @brief Every gamepad button this layout can report. */
        std::uint32_t buttonMask = 0;
        /** @brief Every gamepad axis this layout can report (`GamepadAxisBit`). */
        std::uint8_t axisMask = 0;
        /** @brief The controller family, as far as the device identifies itself. */
        GamepadModel model = GamepadModel::Standard;
        /** @brief Whether the driver uses xpad's face-button convention (BTN_X is the left one). */
        bool xpadFaceButtons = false;
    };

    /**
     * @brief Builds the gamepad mapping for a device classified as a gamepad.
     *
     * @param description The device.
     * @return The layout; empty masks when the device reports nothing a gamepad has.
     */
    [[nodiscard]] EvdevGamepadLayout BuildEvdevGamepadLayout(const EvdevDescription& description);

    /**
     * @brief Rescales a raw axis value to the signed 16-bit range, as the rest of CNA sees axes.
     *
     * The same arithmetic the SDL3 platform's values go through, so an axis reads the same on
     * either platform: the reported range maps linearly onto [-32768, 32767].
     *
     * @param value The raw value.
     * @param range The axis's range.
     * @return The value in [-32768, 32767]; 0 for a degenerate range.
     */
    [[nodiscard]] int ScaleEvdevAxis(int value, const EvdevAxisRange& range);

    /**
     * @brief Normalises a raw axis value for a gamepad axis.
     *
     * Sticks become [-1, 1] with up positive (the kernel reports Y growing downwards); triggers
     * become [0, 1]. No dead zone is applied: XNA applies its own.
     *
     * @param axis The gamepad axis.
     * @param value The raw value.
     * @param range The axis's range.
     * @return The normalised value.
     */
    [[nodiscard]] float NormalizeEvdevGamepadAxis(GamepadAxis axis, int value,
                                                  const EvdevAxisRange& range);

    /** @brief One change a kernel event made to a gamepad's mapped state. */
    struct EvdevGamepadChange
    {
        /** @brief True for a button change, false for an axis change. */
        bool isButton = false;
        /** @brief The button that changed, when `isButton`. */
        GamepadButton button = GamepadButton::A;
        /** @brief Its new state, when `isButton`. */
        bool pressed = false;
        /** @brief The axis that changed, when not `isButton`. */
        GamepadAxis axis = GamepadAxis::LeftThumbstickX;
        /** @brief Its new normalised value, when not `isButton`. */
        float value = 0.0f;
    };

    /** @brief A gamepad's mapped state, driven one kernel event at a time. */
    class EvdevGamepadState
    {
    public:
        /**
         * @brief Starts from rest.
         *
         * @param layout The device's mapping.
         */
        explicit EvdevGamepadState(EvdevGamepadLayout layout);

        /**
         * @brief Applies one kernel event.
         *
         * @param type The event type (`EV_KEY`, `EV_ABS`; everything else is ignored).
         * @param code The event code.
         * @param value The event value.
         * @param changes Receives each mapped change the event made, in order.
         */
        void Apply(std::uint16_t type, std::uint16_t code, std::int32_t value,
                   std::vector<EvdevGamepadChange>& changes);

        /** @brief Returns to rest, as when a device disappears. */
        void Reset();

        /** @brief Gets the held buttons (`GamepadButton` bits). @return The mask. */
        [[nodiscard]] std::uint32_t GetButtons() const { return buttons_; }

        /** @brief Gets the normalised axes, indexed by `GamepadAxis`. @return The axes. */
        [[nodiscard]] const std::array<float, GamepadAxisCount>& GetAxes() const { return axes_; }

        /** @brief Gets the layout. @return The layout. */
        [[nodiscard]] const EvdevGamepadLayout& GetLayout() const { return layout_; }

    private:
        void SetButton(GamepadButton button, bool pressed, std::vector<EvdevGamepadChange>& changes);
        void SetAxis(GamepadAxis axis, float value, std::vector<EvdevGamepadChange>& changes);

        EvdevGamepadLayout layout_;
        std::uint32_t buttons_ = 0;
        std::array<float, GamepadAxisCount> axes_{};
    };

} // namespace CNA::Platform::Linux
