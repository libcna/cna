// SPDX-License-Identifier: MS-PL

#include "EvdevLayout.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Platform::Linux {

    namespace {

        bool AnyKeyIn(const EvdevDescription& description, const int first, const int last)
        {
            for (int code = first; code < last && code < KEY_CNT; ++code)
            {
                if (description.keys.test(static_cast<std::size_t>(code)))
                {
                    return true;
                }
            }
            return false;
        }

        bool HasAxis(const EvdevDescription& description, const int code)
        {
            return description.axes.test(static_cast<std::size_t>(code));
        }

        bool HasKey(const EvdevDescription& description, const int code)
        {
            return description.keys.test(static_cast<std::size_t>(code));
        }

        bool IsVerticalStick(const GamepadAxis axis)
        {
            return axis == GamepadAxis::LeftThumbstickY || axis == GamepadAxis::RightThumbstickY;
        }

        bool IsTrigger(const GamepadAxis axis)
        {
            return axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
        }

        constexpr std::uint16_t kMicrosoftVendor = 0x045E;
        constexpr std::uint16_t kSonyVendor = 0x054C;
        constexpr std::uint16_t kNintendoVendor = 0x057E;

        GamepadModel ModelOf(const EvdevDescription& description)
        {
            switch (description.vendor)
            {
                case kMicrosoftVendor:
                    // 0x028E/0x028F/0x0719 are the Xbox 360 wired, play-and-charge and wireless
                    // receiver; everything later Microsoft ships is the Xbox One family.
                    return description.product == 0x028E || description.product == 0x028F ||
                                   description.product == 0x0719
                               ? GamepadModel::Xbox360
                               : GamepadModel::XboxOne;
                case kSonyVendor:
                    if (description.product == 0x0268)
                    {
                        return GamepadModel::PlayStation3;
                    }
                    return description.product == 0x0CE6 || description.product == 0x0DF2
                               ? GamepadModel::PlayStation5
                               : GamepadModel::PlayStation4;
                case kNintendoVendor:
                    return description.product == 0x2009 ? GamepadModel::NintendoSwitchPro
                                                          : GamepadModel::Standard;
                default:
                    return description.driver == "xpad" ? GamepadModel::Xbox360
                                                        : GamepadModel::Standard;
            }
        }

    } // namespace

    EvdevDeviceClass ClassifyEvdevDevice(const EvdevDescription& description)
    {
        // A DualShock or DualSense exposes its motion sensors as a node of their own, with
        // absolute axes but no buttons worth the name; it is not a second controller.
        if (description.properties.test(INPUT_PROP_ACCELEROMETER))
        {
            return EvdevDeviceClass::None;
        }
        // Keyboards, mice, touchpads and tablets carry buttons of their own (KEY_*, BTN_LEFT,
        // BTN_TOUCH, BTN_TOOL_*) and sometimes absolute X/Y, but never the joystick (0x120..0x12f)
        // or gamepad (0x130..0x13f) button ranges.
        const bool gamepadButtons = AnyKeyIn(description, BTN_GAMEPAD, BTN_DIGI);
        const bool joystickButtons = AnyKeyIn(description, BTN_JOYSTICK, BTN_GAMEPAD) ||
                                     AnyKeyIn(description, BTN_TRIGGER_HAPPY, BTN_TRIGGER_HAPPY40 + 1);
        const bool sticks = HasAxis(description, ABS_X) && HasAxis(description, ABS_Y);
        const bool hat = HasAxis(description, ABS_HAT0X) && HasAxis(description, ABS_HAT0Y);
        if (gamepadButtons)
        {
            return EvdevDeviceClass::Gamepad;
        }
        if (joystickButtons && (sticks || hat))
        {
            return EvdevDeviceClass::Joystick;
        }
        return EvdevDeviceClass::None;
    }

    EvdevGamepadLayout BuildEvdevGamepadLayout(const EvdevDescription& description)
    {
        EvdevGamepadLayout layout;
        layout.model = ModelOf(description);
        // xpad names an Xbox pad's left face button BTN_X and its top one BTN_Y -- the codes the
        // kernel's gamepad API calls BTN_NORTH and BTN_WEST. Microsoft pads reached through HID
        // (Bluetooth) report their buttons in the same order. Everyone else follows the gamepad
        // API, where the code names the position.
        layout.xpadFaceButtons = description.driver == "xpad" || description.vendor == kMicrosoftVendor;

        const auto addButton = [&](const int code, const GamepadButton button) {
            if (HasKey(description, code))
            {
                layout.buttons.emplace_back(static_cast<std::uint16_t>(code), button);
                layout.buttonMask |= static_cast<std::uint32_t>(button);
            }
        };
        addButton(BTN_SOUTH, GamepadButton::A);
        addButton(BTN_EAST, GamepadButton::B);
        addButton(BTN_WEST, layout.xpadFaceButtons ? GamepadButton::Y : GamepadButton::X);
        addButton(BTN_NORTH, layout.xpadFaceButtons ? GamepadButton::X : GamepadButton::Y);
        addButton(BTN_TL, GamepadButton::LeftShoulder);
        addButton(BTN_TR, GamepadButton::RightShoulder);
        addButton(BTN_SELECT, GamepadButton::Back);
        addButton(BTN_START, GamepadButton::Start);
        addButton(BTN_MODE, GamepadButton::BigButton);
        addButton(BTN_THUMBL, GamepadButton::LeftStick);
        addButton(BTN_THUMBR, GamepadButton::RightStick);
        addButton(BTN_DPAD_UP, GamepadButton::DPadUp);
        addButton(BTN_DPAD_DOWN, GamepadButton::DPadDown);
        addButton(BTN_DPAD_LEFT, GamepadButton::DPadLeft);
        addButton(BTN_DPAD_RIGHT, GamepadButton::DPadRight);
        if (layout.xpadFaceButtons)
        {
            // xpad reports a wireless pad's D-pad as the first four "trigger happy" buttons, and
            // an Elite's paddles as the next four.
            addButton(BTN_TRIGGER_HAPPY1, GamepadButton::DPadLeft);
            addButton(BTN_TRIGGER_HAPPY2, GamepadButton::DPadRight);
            addButton(BTN_TRIGGER_HAPPY3, GamepadButton::DPadUp);
            addButton(BTN_TRIGGER_HAPPY4, GamepadButton::DPadDown);
            addButton(BTN_TRIGGER_HAPPY5, GamepadButton::Paddle1);
            addButton(BTN_TRIGGER_HAPPY6, GamepadButton::Paddle2);
            addButton(BTN_TRIGGER_HAPPY7, GamepadButton::Paddle3);
            addButton(BTN_TRIGGER_HAPPY8, GamepadButton::Paddle4);
        }

        const auto addAxis = [&](const int code, const GamepadAxis axis) {
            if (HasAxis(description, code))
            {
                layout.axes.push_back({static_cast<std::uint16_t>(code), axis,
                                       description.ranges[static_cast<std::size_t>(code)]});
                layout.axisMask |= GamepadAxisBit(axis);
            }
        };
        addAxis(ABS_X, GamepadAxis::LeftThumbstickX);
        addAxis(ABS_Y, GamepadAxis::LeftThumbstickY);
        // The right stick is RX/RY on every driver that has them, and then Z/RZ are the analogue
        // triggers (xpad, hid-playstation). A pad with no RX/RY puts its right stick on Z/RZ, the
        // DirectInput layout, and its triggers -- if analogue at all -- on BRAKE/GAS.
        bool leftTriggerAnalog = false;
        bool rightTriggerAnalog = false;
        if (HasAxis(description, ABS_RX) && HasAxis(description, ABS_RY))
        {
            addAxis(ABS_RX, GamepadAxis::RightThumbstickX);
            addAxis(ABS_RY, GamepadAxis::RightThumbstickY);
            if (HasAxis(description, ABS_Z))
            {
                addAxis(ABS_Z, GamepadAxis::LeftTrigger);
                leftTriggerAnalog = true;
            }
            if (HasAxis(description, ABS_RZ))
            {
                addAxis(ABS_RZ, GamepadAxis::RightTrigger);
                rightTriggerAnalog = true;
            }
        }
        else if (HasAxis(description, ABS_Z) && HasAxis(description, ABS_RZ))
        {
            addAxis(ABS_Z, GamepadAxis::RightThumbstickX);
            addAxis(ABS_RZ, GamepadAxis::RightThumbstickY);
        }
        if (!leftTriggerAnalog && HasAxis(description, ABS_BRAKE))
        {
            addAxis(ABS_BRAKE, GamepadAxis::LeftTrigger);
            leftTriggerAnalog = true;
        }
        if (!rightTriggerAnalog && HasAxis(description, ABS_GAS))
        {
            addAxis(ABS_GAS, GamepadAxis::RightTrigger);
            rightTriggerAnalog = true;
        }
        // A Switch Pro controller's ZL/ZR, and many a cheap pad's triggers, are buttons. A pad
        // with analogue triggers often reports the same press as BTN_TL2/BTN_TR2 as well; the
        // analogue value is the better one then, so the button is ignored.
        if (!leftTriggerAnalog && HasKey(description, BTN_TL2))
        {
            layout.digitalTriggers.push_back({BTN_TL2, GamepadAxis::LeftTrigger});
            layout.axisMask |= GamepadAxisBit(GamepadAxis::LeftTrigger);
        }
        if (!rightTriggerAnalog && HasKey(description, BTN_TR2))
        {
            layout.digitalTriggers.push_back({BTN_TR2, GamepadAxis::RightTrigger});
            layout.axisMask |= GamepadAxisBit(GamepadAxis::RightTrigger);
        }

        if (HasAxis(description, ABS_HAT0X))
        {
            layout.hats.push_back({ABS_HAT0X, GamepadButton::DPadLeft, GamepadButton::DPadRight});
            layout.buttonMask |= static_cast<std::uint32_t>(GamepadButton::DPadLeft) |
                                 static_cast<std::uint32_t>(GamepadButton::DPadRight);
        }
        if (HasAxis(description, ABS_HAT0Y))
        {
            layout.hats.push_back({ABS_HAT0Y, GamepadButton::DPadUp, GamepadButton::DPadDown});
            layout.buttonMask |= static_cast<std::uint32_t>(GamepadButton::DPadUp) |
                                 static_cast<std::uint32_t>(GamepadButton::DPadDown);
        }
        return layout;
    }

    int ScaleEvdevAxis(const int value, const EvdevAxisRange& range)
    {
        if (range.maximum <= range.minimum)
        {
            return 0;
        }
        const double scale = 65535.0 / (static_cast<double>(range.maximum) - range.minimum);
        const double scaled = std::floor((static_cast<double>(value) - range.minimum) * scale -
                                         32768.0 + 0.5);
        return static_cast<int>(std::clamp(scaled, -32768.0, 32767.0));
    }

    float NormalizeEvdevGamepadAxis(const GamepadAxis axis, const int value,
                                    const EvdevAxisRange& range)
    {
        const int scaled = ScaleEvdevAxis(value, range);
        if (IsTrigger(axis))
        {
            return std::clamp((static_cast<float>(scaled) + 32768.0f) / 65535.0f, 0.0f, 1.0f);
        }
        const float stick = static_cast<float>(scaled) / 32767.0f;
        return std::clamp(IsVerticalStick(axis) ? -stick : stick, -1.0f, 1.0f);
    }

    EvdevGamepadState::EvdevGamepadState(EvdevGamepadLayout layout) : layout_(std::move(layout)) {}

    void EvdevGamepadState::Reset()
    {
        buttons_ = 0;
        axes_.fill(0.0f);
    }

    void EvdevGamepadState::SetButton(const GamepadButton button, const bool pressed,
                                      std::vector<EvdevGamepadChange>& changes)
    {
        const auto bit = static_cast<std::uint32_t>(button);
        const bool held = (buttons_ & bit) != 0;
        if (held == pressed)
        {
            return;
        }
        buttons_ = pressed ? (buttons_ | bit) : (buttons_ & ~bit);
        EvdevGamepadChange change;
        change.isButton = true;
        change.button = button;
        change.pressed = pressed;
        changes.push_back(change);
    }

    void EvdevGamepadState::SetAxis(const GamepadAxis axis, const float value,
                                    std::vector<EvdevGamepadChange>& changes)
    {
        float& current = axes_[static_cast<std::size_t>(axis)];
        if (current == value)
        {
            return;
        }
        current = value;
        EvdevGamepadChange change;
        change.isButton = false;
        change.axis = axis;
        change.value = value;
        changes.push_back(change);
    }

    void EvdevGamepadState::Apply(const std::uint16_t type, const std::uint16_t code,
                                  const std::int32_t value, std::vector<EvdevGamepadChange>& changes)
    {
        if (type == EV_KEY)
        {
            for (const auto& [keyCode, button] : layout_.buttons)
            {
                if (keyCode == code)
                {
                    SetButton(button, value != 0, changes);
                }
            }
            for (const EvdevDigitalTrigger& trigger : layout_.digitalTriggers)
            {
                if (trigger.code == code)
                {
                    SetAxis(trigger.axis, value != 0 ? 1.0f : 0.0f, changes);
                }
            }
            return;
        }
        if (type != EV_ABS)
        {
            return;
        }
        for (const EvdevAxisBinding& binding : layout_.axes)
        {
            if (binding.code == code)
            {
                SetAxis(binding.axis, NormalizeEvdevGamepadAxis(binding.axis, value, binding.range),
                        changes);
            }
        }
        for (const EvdevHatBinding& hat : layout_.hats)
        {
            if (hat.code == code)
            {
                SetButton(hat.negative, value < 0, changes);
                SetButton(hat.positive, value > 0, changes);
            }
        }
    }

} // namespace CNA::Platform::Linux
