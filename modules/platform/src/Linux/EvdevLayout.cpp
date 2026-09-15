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

    GamepadModel EvdevGamepadModel(const EvdevDescription& description)
    {
        return ModelOf(description);
    }

    bool IsEvdevMotionSensor(const EvdevDescription& description)
    {
        const bool accelerometer =
            HasAxis(description, ABS_X) && HasAxis(description, ABS_Y) && HasAxis(description, ABS_Z);
        const bool gyroscope =
            HasAxis(description, ABS_RX) && HasAxis(description, ABS_RY) && HasAxis(description, ABS_RZ);
        return description.properties.test(INPUT_PROP_ACCELEROMETER) && (accelerometer || gyroscope);
    }

    bool EvdevSensorBelongsTo(const EvdevDescription& sensor, const EvdevDescription& controller)
    {
        if (!sensor.uniq.empty() && !controller.uniq.empty())
        {
            return sensor.uniq == controller.uniq;
        }
        return !sensor.phys.empty() && sensor.phys == controller.phys;
    }

    EvdevMotionLayout DescribeEvdevMotionSensor(const EvdevDescription& sensor, const std::uint16_t controllerVendor)
    {
        EvdevMotionLayout layout;
        const auto group = [&sensor](const int first, std::array<int, 3>& resolution) {
            for (int index = 0; index < 3; ++index)
            {
                const int code = first + index;
                if (!HasAxis(sensor, code) || sensor.ranges[static_cast<std::size_t>(code)].resolution <= 0)
                {
                    return false;
                }
                resolution[static_cast<std::size_t>(index)] = sensor.ranges[static_cast<std::size_t>(code)].resolution;
            }
            return true;
        };
        layout.accelerometer = group(ABS_X, layout.accelerometerResolution);
        layout.gyroscope = group(ABS_RX, layout.gyroscopeResolution);
        layout.nintendoAxes = controllerVendor == kNintendoVendor;
        return layout;
    }

    GamepadSensorReading ScaleEvdevMotion(const EvdevMotionLayout& layout, const GamepadSensor sensor,
                                          const std::array<int, 6>& raw)
    {
        constexpr double kStandardGravity = 9.80665;
        constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
        const bool gyroscope = sensor == GamepadSensor::Gyroscope;
        if ((gyroscope && !layout.gyroscope) || (!gyroscope && !layout.accelerometer))
        {
            return GamepadSensorReading{};
        }
        std::array<double, 3> value{};
        for (std::size_t index = 0; index < 3; ++index)
        {
            value[index] = gyroscope
                               ? raw[3 + index] * kRadiansPerDegree / layout.gyroscopeResolution[index]
                               : raw[index] * kStandardGravity / layout.accelerometerResolution[index];
        }
        if (layout.nintendoAxes)
        {
            // hid-nintendo reports its axes in its own order.
            return GamepadSensorReading{static_cast<float>(-value[1]), static_cast<float>(value[2]),
                                        static_cast<float>(-value[0])};
        }
        return GamepadSensorReading{static_cast<float>(value[0]), static_cast<float>(value[1]),
                                    static_cast<float>(value[2])};
    }

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

    float NormalizeMappedGamepadAxis(const GamepadAxis axis, const int value)
    {
        if (IsTrigger(axis))
        {
            return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
        }
        const float stick = std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
        // Databases write the vertical sticks positive down, as the kernel reports them; CNA's
        // are positive up.
        return IsVerticalStick(axis) ? -stick : stick;
    }

    EvdevGamepadState::EvdevGamepadState(EvdevGamepadLayout layout)
        : layout_(std::move(layout)), lastAxisMatch_(ABS_CNT, -1)
    {
    }

    void EvdevGamepadState::Reset()
    {
        buttons_ = 0;
        axes_.fill(0.0f);
        std::fill(lastAxisMatch_.begin(), lastAxisMatch_.end(), -1);
        hatMasks_.fill(0);
        for (std::array<int, 2>& hat : hatValues_)
        {
            hat.fill(0);
        }
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

    void EvdevGamepadState::Drive(const EvdevMappedBinding& binding, const int value,
                                  std::vector<EvdevGamepadChange>& changes)
    {
        if (binding.toButton)
        {
            SetButton(binding.button, value != 0, changes);
        }
        else
        {
            SetAxis(binding.axis, NormalizeMappedGamepadAxis(binding.axis, value), changes);
        }
    }

    void EvdevGamepadState::Release(const EvdevMappedBinding& binding,
                                    std::vector<EvdevGamepadChange>& changes)
    {
        if (binding.toButton)
        {
            SetButton(binding.button, false, changes);
        }
        else
        {
            SetAxis(binding.axis, 0.0f, changes);
        }
    }

    void EvdevGamepadState::ApplyMapped(const std::uint16_t type, const std::uint16_t code,
                                        const std::int32_t value,
                                        std::vector<EvdevGamepadChange>& changes)
    {
        const std::vector<EvdevMappedBinding>& bindings = layout_.mapped;
        if (type == EV_KEY)
        {
            // The first element reading the key decides, as databases are written.
            for (const EvdevMappedBinding& binding : bindings)
            {
                if (binding.source == EvdevMappedBinding::Source::Key && binding.code == code)
                {
                    Drive(binding, value != 0 ? (binding.toButton ? 1 : binding.outputMaximum)
                                              : (binding.toButton ? 0 : binding.outputMinimum),
                          changes);
                    return;
                }
            }
            return;
        }
        if (type != EV_ABS)
        {
            return;
        }

        if (code >= ABS_HAT0X && code <= ABS_HAT3Y)
        {
            const std::size_t hat = static_cast<std::size_t>(code - ABS_HAT0X) / 2;
            const std::uint16_t xCode = static_cast<std::uint16_t>(ABS_HAT0X + 2 * hat);
            bool isHat = false;
            for (const EvdevMappedBinding& binding : bindings)
            {
                isHat = isHat || (binding.source == EvdevMappedBinding::Source::Hat &&
                                  binding.code == xCode);
            }
            if (isHat)
            {
                hatValues_[hat][code == xCode ? 0 : 1] = value;
                // Each axis as -1, 0 or 1 about the centre of its range: a digital hat reports
                // exactly those; any other range is read by which third it is in.
                const auto direction = [](const int raw, const EvdevAxisRange& range) {
                    const double centre = (static_cast<double>(range.minimum) + range.maximum) / 2.0;
                    const double third = (static_cast<double>(range.maximum) - range.minimum) / 3.0;
                    if (range.maximum <= range.minimum)
                    {
                        return raw < 0 ? -1 : (raw > 0 ? 1 : 0);
                    }
                    return raw < centre - third / 2.0 ? -1 : (raw > centre + third / 2.0 ? 1 : 0);
                };
                const EvdevMappedBinding* any = nullptr;
                for (const EvdevMappedBinding& binding : bindings)
                {
                    if (binding.source == EvdevMappedBinding::Source::Hat && binding.code == xCode)
                    {
                        any = &binding;
                        break;
                    }
                }
                const int x = direction(hatValues_[hat][0], any->range);
                const int y = direction(hatValues_[hat][1], any->yRange);
                const auto mask = static_cast<std::uint8_t>((y < 0 ? 1 : 0) | (x > 0 ? 2 : 0) |
                                                            (y > 0 ? 4 : 0) | (x < 0 ? 8 : 0));
                const auto changed = static_cast<std::uint8_t>(mask ^ hatMasks_[hat]);
                for (const EvdevMappedBinding& binding : bindings)
                {
                    if (binding.source != EvdevMappedBinding::Source::Hat || binding.code != xCode ||
                        (changed & binding.hatMask) == 0)
                    {
                        continue;
                    }
                    if ((mask & binding.hatMask) != 0)
                    {
                        Drive(binding, binding.toButton ? 1 : binding.outputMaximum, changes);
                    }
                    else
                    {
                        Release(binding, changes);
                    }
                }
                hatMasks_[hat] = mask;
                return;
            }
        }

        // An axis: the first element whose range holds the value decides. When the axis leaves
        // the range of the element that last decided, that element's control is released first --
        // a half axis on each side of a stick, say, lets go of one as the other takes over.
        int match = -1;
        int scaled = 0;
        for (std::size_t index = 0; index < bindings.size(); ++index)
        {
            const EvdevMappedBinding& binding = bindings[index];
            if (binding.source != EvdevMappedBinding::Source::Axis || binding.code != code)
            {
                continue;
            }
            scaled = ScaleEvdevAxis(value, binding.range);
            const int low = std::min(binding.inputMinimum, binding.inputMaximum);
            const int high = std::max(binding.inputMinimum, binding.inputMaximum);
            if (scaled >= low && scaled <= high)
            {
                match = static_cast<int>(index);
                break;
            }
        }
        int& last = lastAxisMatch_[code];
        if (last >= 0 && last != match)
        {
            const EvdevMappedBinding& previous = bindings[static_cast<std::size_t>(last)];
            const bool sameOutput =
                match >= 0 && previous.toButton == bindings[static_cast<std::size_t>(match)].toButton &&
                (previous.toButton ? previous.button == bindings[static_cast<std::size_t>(match)].button
                                   : previous.axis == bindings[static_cast<std::size_t>(match)].axis);
            if (!sameOutput)
            {
                Release(previous, changes);
            }
        }
        last = match;
        if (match < 0)
        {
            return;
        }
        const EvdevMappedBinding& binding = bindings[static_cast<std::size_t>(match)];
        if (binding.toButton)
        {
            // Pressed past the middle of the range read, in its direction.
            const int threshold =
                binding.inputMinimum + (binding.inputMaximum - binding.inputMinimum) / 2;
            const bool down = binding.inputMaximum < binding.inputMinimum ? scaled <= threshold
                                                                          : scaled >= threshold;
            SetButton(binding.button, down, changes);
            return;
        }
        int output = scaled;
        if (binding.inputMinimum != binding.outputMinimum || binding.inputMaximum != binding.outputMaximum)
        {
            const double position = static_cast<double>(scaled - binding.inputMinimum) /
                                    (binding.inputMaximum - binding.inputMinimum);
            output = binding.outputMinimum +
                     static_cast<int>(position * (binding.outputMaximum - binding.outputMinimum));
        }
        SetAxis(binding.axis, NormalizeMappedGamepadAxis(binding.axis, output), changes);
    }

    void EvdevGamepadState::Apply(const std::uint16_t type, const std::uint16_t code,
                                  const std::int32_t value, std::vector<EvdevGamepadChange>& changes)
    {
        if (!layout_.mapped.empty())
        {
            ApplyMapped(type, code, value, changes);
            return;
        }
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
