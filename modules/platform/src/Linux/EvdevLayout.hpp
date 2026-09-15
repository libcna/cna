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
        /** @brief The noise the driver filters out; with @ref flat, a hint that an axis is analogue. */
        int fuzz = 0;
        /** @brief Units per millimetre or per radian; zero when the driver does not say. */
        int resolution = 0;
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
        /** @brief Where the device is attached (`EVIOCGPHYS`); every node of one HID device shares it. */
        std::string phys;
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

    /**
     * @brief Gets whether a device is a controller's motion-sensor node (plans/plan_x11.md
     * X11-0166): an accelerometer by its property, with an accelerometer's or a gyroscope's three
     * axes -- the second node hid-playstation and hid-nintendo create beside a pad.
     *
     * @param description The device.
     * @return True for a motion sensor.
     */
    [[nodiscard]] bool IsEvdevMotionSensor(const EvdevDescription& description);

    /**
     * @brief Gets whether a motion-sensor node belongs to a controller.
     *
     * The same non-empty unique id (a Bluetooth address, a serial), else the same non-empty
     * physical path -- the kernel gives both nodes of one HID device the same of each.
     *
     * @param sensor The motion-sensor node.
     * @param controller The controller.
     * @return True when they are one device.
     */
    [[nodiscard]] bool EvdevSensorBelongsTo(const EvdevDescription& sensor, const EvdevDescription& controller);

    /** @brief What a paired motion sensor can report, and how its raw values scale. */
    struct EvdevMotionLayout
    {
        /** @brief Whether ABS_X/Y/Z are an accelerometer with a stated resolution. */
        bool accelerometer = false;
        /** @brief Whether ABS_RX/RY/RZ are a gyroscope with a stated resolution. */
        bool gyroscope = false;
        /** @brief Accelerometer units per g, per axis. */
        std::array<int, 3> accelerometerResolution{};
        /** @brief Gyroscope units per degree per second, per axis. */
        std::array<int, 3> gyroscopeResolution{};
        /** @brief Whether the axes are hid-nintendo's order rather than the gamepad convention. */
        bool nintendoAxes = false;
    };

    /**
     * @brief Describes a motion-sensor node for the controller it belongs to.
     *
     * An axis group counts only when all three axes are there and state their resolution: a
     * reading that cannot be scaled to physical units is not reported at all.
     *
     * @param sensor The motion-sensor node.
     * @param controllerVendor The controller's vendor, for the axis order.
     * @return The layout.
     */
    [[nodiscard]] EvdevMotionLayout DescribeEvdevMotionSensor(const EvdevDescription& sensor,
                                                              std::uint16_t controllerVendor);

    /**
     * @brief Scales a motion sensor's raw values as the SDL3 platform reports them: metres per
     * second squared for the accelerometer, radians per second for the gyroscope, hid-nintendo's
     * axis order turned into the gamepad convention.
     *
     * @param layout The sensor's layout.
     * @param sensor Which of the two.
     * @param raw ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ as last reported.
     * @return The reading; zero when the layout lacks that sensor.
     */
    [[nodiscard]] GamepadSensorReading ScaleEvdevMotion(const EvdevMotionLayout& layout, GamepadSensor sensor,
                                                        const std::array<int, 6>& raw);

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

    /**
     * @brief One element of a controller-database mapping, resolved to the kernel's codes
     * (plans/plan_x11.md X11-0160; EvdevMapping.hpp builds them).
     *
     * Values are on the database's [-32768, 32767] scale, positive down for the vertical sticks.
     */
    struct EvdevMappedBinding
    {
        /** @brief What the element reads. */
        enum class Source : std::uint8_t
        {
            /** @brief A key code, pressed or not. */
            Key,
            /** @brief An absolute axis, over a range of it. */
            Axis,
            /** @brief One direction of a hat (an `ABS_HATnX`/`ABS_HATnY` pair). */
            Hat
        };

        /** @brief What the element reads. */
        Source source = Source::Key;
        /** @brief The key code, the axis's `ABS_*` code, or the hat's `ABS_HATnX` code. */
        std::uint16_t code = 0;
        /** @brief The axis's raw range (for a hat, its X axis's). */
        EvdevAxisRange range{};
        /** @brief A hat's Y axis's raw range. */
        EvdevAxisRange yRange{};
        /** @brief An axis's range read, scaled; above @ref inputMaximum when inverted. */
        int inputMinimum = 0;
        /** @brief The other end of the range read. */
        int inputMaximum = 0;
        /** @brief A hat's direction: 1 up, 2 right, 4 down, 8 left. */
        std::uint8_t hatMask = 0;
        /** @brief True when the element drives a button, false for an axis. */
        bool toButton = true;
        /** @brief The button driven. */
        GamepadButton button = GamepadButton::A;
        /** @brief The axis driven. */
        GamepadAxis axis = GamepadAxis::LeftThumbstickX;
        /** @brief The range the axis is driven over. */
        int outputMinimum = 0;
        /** @brief The other end of it. */
        int outputMaximum = 0;
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
        /**
         * @brief A controller-database mapping's elements; when there are any, they alone drive
         * the state and the gamepad-API bindings above are empty.
         */
        std::vector<EvdevMappedBinding> mapped;
    };

    /**
     * @brief Names a device's controller family from its identity.
     *
     * @param description The device.
     * @return The family; `Standard` when the vendor is not one with a family of its own.
     */
    [[nodiscard]] GamepadModel EvdevGamepadModel(const EvdevDescription& description);

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
        void ApplyMapped(std::uint16_t type, std::uint16_t code, std::int32_t value,
                         std::vector<EvdevGamepadChange>& changes);
        void Drive(const EvdevMappedBinding& binding, int value, std::vector<EvdevGamepadChange>& changes);
        void Release(const EvdevMappedBinding& binding, std::vector<EvdevGamepadChange>& changes);

        EvdevGamepadLayout layout_;
        std::uint32_t buttons_ = 0;
        std::array<float, GamepadAxisCount> axes_{};
        // A mapping's memory: each axis's last matching element (its output is released when the
        // axis moves out of that element's range), and each hat's direction bits and raw values.
        std::vector<int> lastAxisMatch_;
        std::array<std::uint8_t, 4> hatMasks_{};
        std::array<std::array<int, 2>, 4> hatValues_{};
    };

    /**
     * @brief Converts a value on the controller databases' scale to CNA's.
     *
     * @param axis The gamepad axis.
     * @param value The value on [-32768, 32767] (a trigger's on [0, 32767]), positive down for the
     * vertical sticks.
     * @return Sticks on [-1, 1] with up positive; triggers on [0, 1].
     */
    [[nodiscard]] float NormalizeMappedGamepadAxis(GamepadAxis axis, int value);

} // namespace CNA::Platform::Linux
