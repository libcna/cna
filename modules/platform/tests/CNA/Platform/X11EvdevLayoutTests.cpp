// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0150: the Linux evdev controller rules the X11 platform serves its gamepads
// with, tested from synthetic device descriptions -- no device, no permissions, no X server.
//
// The mistakes these catch are silent ones. A swapped face button makes a game's "confirm" the
// wrong button on exactly one family of pads; an inverted stick axis makes a menu scroll the wrong
// way; a keyboard classified as a controller holds someone's keyboard open. None of that fails
// anywhere -- so each rule is pinned here, against descriptions shaped like the kernel's drivers
// actually report them.

#include <gtest/gtest.h>

#ifdef CNA_PLATFORM_HAVE_EVDEV

#include "../../../src/Linux/EvdevControllers.hpp"
#include "../../../src/Linux/EvdevDevice.hpp"
#include "../../../src/Linux/EvdevLayout.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include <sys/inotify.h>
#include <unistd.h>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Linux;

EvdevDescription Describe(std::initializer_list<int> keys,
                          std::initializer_list<std::pair<int, EvdevAxisRange>> axes = {})
{
    EvdevDescription description;
    for (const int key : keys)
    {
        description.keys.set(static_cast<std::size_t>(key));
    }
    for (const auto& [code, range] : axes)
    {
        description.axes.set(static_cast<std::size_t>(code));
        description.ranges[static_cast<std::size_t>(code)] = range;
    }
    return description;
}

constexpr EvdevAxisRange kStick16{-32768, 32767, 128};
constexpr EvdevAxisRange kByte{0, 255, 0};
constexpr EvdevAxisRange kHat{-1, 1, 0};

// An Xbox 360 pad as xpad reports it.
EvdevDescription Xbox360()
{
    EvdevDescription description = Describe(
        {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE,
         BTN_THUMBL, BTN_THUMBR},
        {{ABS_X, kStick16}, {ABS_Y, kStick16}, {ABS_RX, kStick16}, {ABS_RY, kStick16},
         {ABS_Z, kByte}, {ABS_RZ, kByte}, {ABS_HAT0X, kHat}, {ABS_HAT0Y, kHat}});
    description.driver = "xpad";
    description.bus = BUS_USB;
    description.vendor = 0x045E;
    description.product = 0x028E;
    description.version = 0x0114;
    description.forceFeedback.set(FF_RUMBLE);
    return description;
}

// A DualSense as hid-playstation reports it: triggers both analogue (Z/RZ) and digital (TL2/TR2).
EvdevDescription DualSense()
{
    EvdevDescription description = Describe(
        {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_SELECT,
         BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR},
        {{ABS_X, kByte}, {ABS_Y, kByte}, {ABS_Z, kByte}, {ABS_RX, kByte}, {ABS_RY, kByte},
         {ABS_RZ, kByte}, {ABS_HAT0X, kHat}, {ABS_HAT0Y, kHat}});
    description.driver = "playstation";
    description.bus = BUS_USB;
    description.vendor = 0x054C;
    description.product = 0x0CE6;
    return description;
}

// A Switch Pro controller as hid-nintendo reports it: ZL/ZR are buttons only.
EvdevDescription SwitchPro()
{
    EvdevDescription description = Describe(
        {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_SELECT,
         BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR, BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT,
         BTN_DPAD_RIGHT},
        {{ABS_X, kStick16}, {ABS_Y, kStick16}, {ABS_RX, kStick16}, {ABS_RY, kStick16}});
    description.driver = "nintendo";
    description.bus = BUS_BLUETOOTH;
    description.vendor = 0x057E;
    description.product = 0x2009;
    return description;
}

std::uint32_t Bits(std::initializer_list<GamepadButton> buttons)
{
    std::uint32_t mask = 0;
    for (const GamepadButton button : buttons)
    {
        mask |= static_cast<std::uint32_t>(button);
    }
    return mask;
}

GamepadButton ButtonFor(const EvdevGamepadLayout& layout, const int code)
{
    for (const auto& [key, button] : layout.buttons)
    {
        if (key == code)
        {
            return button;
        }
    }
    ADD_FAILURE() << "code " << code << " is not mapped";
    return GamepadButton::A;
}

// --- classification ------------------------------------------------------------------------------

TEST(X11EvdevLayout, PadsWithTheKernelGamepadButtonSetAreGamepads)
{
    EXPECT_EQ(ClassifyEvdevDevice(Xbox360()), EvdevDeviceClass::Gamepad);
    EXPECT_EQ(ClassifyEvdevDevice(DualSense()), EvdevDeviceClass::Gamepad);
    EXPECT_EQ(ClassifyEvdevDevice(SwitchPro()), EvdevDeviceClass::Gamepad);
}

TEST(X11EvdevLayout, KeyboardsMiceTouchpadsAndTabletsAreNotControllers)
{
    // Being wrong here does not merely list a phantom pad: the hub keeps controllers open, and
    // holding someone's keyboard open is not a controller service's business.
    EXPECT_EQ(ClassifyEvdevDevice(Describe({KEY_ESC, KEY_A, KEY_Z, KEY_ENTER, KEY_SPACE})),
              EvdevDeviceClass::None);
    EXPECT_EQ(ClassifyEvdevDevice(Describe({BTN_LEFT, BTN_RIGHT, BTN_MIDDLE})),
              EvdevDeviceClass::None);
    EXPECT_EQ(ClassifyEvdevDevice(Describe({BTN_LEFT, BTN_TOUCH, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP},
                                           {{ABS_X, {0, 1200, 0}}, {ABS_Y, {0, 800, 0}},
                                            {ABS_MT_POSITION_X, {0, 1200, 0}},
                                            {ABS_MT_POSITION_Y, {0, 800, 0}}})),
              EvdevDeviceClass::None);
    EXPECT_EQ(ClassifyEvdevDevice(Describe({BTN_TOOL_PEN, BTN_TOUCH, BTN_STYLUS},
                                           {{ABS_X, {0, 30000, 0}}, {ABS_Y, {0, 20000, 0}},
                                            {ABS_PRESSURE, {0, 4095, 0}}})),
              EvdevDeviceClass::None);
    // A laptop's extra-buttons node: many KEY_* codes, some of them high, none a controller's.
    EXPECT_EQ(ClassifyEvdevDevice(Describe({KEY_MUTE, KEY_VOLUMEUP, KEY_BRIGHTNESSUP, KEY_WLAN,
                                            KEY_MICMUTE, KEY_FAVORITES, KEY_KBDILLUMTOGGLE})),
              EvdevDeviceClass::None);
}

TEST(X11EvdevLayout, AMotionSensorNodeIsNotASecondController)
{
    // hid-playstation's sensor node has three accelerometer and three gyro axes, and must not
    // appear as a controller of its own next to the pad it belongs to.
    EvdevDescription sensors = Describe({}, {{ABS_X, kStick16}, {ABS_Y, kStick16}, {ABS_Z, kStick16},
                                             {ABS_RX, kStick16}, {ABS_RY, kStick16},
                                             {ABS_RZ, kStick16}});
    sensors.properties.set(INPUT_PROP_ACCELEROMETER);
    EXPECT_EQ(ClassifyEvdevDevice(sensors), EvdevDeviceClass::None);

    // Whatever else it claims.
    EvdevDescription odd = Xbox360();
    odd.properties.set(INPUT_PROP_ACCELEROMETER);
    EXPECT_EQ(ClassifyEvdevDevice(odd), EvdevDeviceClass::None);
}

TEST(X11EvdevLayout, JoystickButtonsWithAxesOrAHatMakeAJoystick)
{
    // A flight stick through hid-generic: the joystick button range, not the gamepad one.
    EXPECT_EQ(ClassifyEvdevDevice(Describe({BTN_TRIGGER, BTN_THUMB, BTN_THUMB2, BTN_TOP},
                                           {{ABS_X, {0, 1023, 0}}, {ABS_Y, {0, 1023, 0}},
                                            {ABS_THROTTLE, {0, 255, 0}}})),
              EvdevDeviceClass::Joystick);
    // An arcade panel: trigger-happy buttons and a hat, no stick.
    EXPECT_EQ(ClassifyEvdevDevice(Describe({BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY2},
                                           {{ABS_HAT0X, kHat}, {ABS_HAT0Y, kHat}})),
              EvdevDeviceClass::Joystick);
    // Joystick buttons with nothing to point with are a button box, not a joystick.
    EXPECT_EQ(ClassifyEvdevDevice(Describe({BTN_TRIGGER, BTN_THUMB})), EvdevDeviceClass::None);
}

// --- face buttons --------------------------------------------------------------------------------

TEST(X11EvdevLayout, GamepadApiFaceButtonsAreNamedByPosition)
{
    // The kernel's gamepad API names face buttons by where they are, and so does CNA: A is the
    // bottom one whatever is printed on it. A DualSense's square (left) is therefore X.
    const EvdevGamepadLayout layout = BuildEvdevGamepadLayout(DualSense());
    EXPECT_FALSE(layout.xpadFaceButtons);
    EXPECT_EQ(ButtonFor(layout, BTN_SOUTH), GamepadButton::A);
    EXPECT_EQ(ButtonFor(layout, BTN_EAST), GamepadButton::B);
    EXPECT_EQ(ButtonFor(layout, BTN_WEST), GamepadButton::X);
    EXPECT_EQ(ButtonFor(layout, BTN_NORTH), GamepadButton::Y);
}

TEST(X11EvdevLayout, XpadFaceButtonsAreSwappedBackToTheirPositions)
{
    // xpad predates the gamepad API: it reports an Xbox pad's left X button as BTN_X, which the
    // gamepad API calls BTN_NORTH. Taking the code at its gamepad-API word would put XNA's X on
    // the top button.
    const EvdevGamepadLayout layout = BuildEvdevGamepadLayout(Xbox360());
    EXPECT_TRUE(layout.xpadFaceButtons);
    EXPECT_EQ(ButtonFor(layout, BTN_X), GamepadButton::X);
    EXPECT_EQ(ButtonFor(layout, BTN_Y), GamepadButton::Y);
    EXPECT_EQ(ButtonFor(layout, BTN_NORTH), GamepadButton::X);
    EXPECT_EQ(ButtonFor(layout, BTN_WEST), GamepadButton::Y);

    // A Microsoft pad without xpad -- Bluetooth, through HID -- reports the same order.
    EvdevDescription bluetooth = Xbox360();
    bluetooth.driver = "microsoft";
    bluetooth.product = 0x0B13;
    EXPECT_TRUE(BuildEvdevGamepadLayout(bluetooth).xpadFaceButtons);

    // And a third-party pad on xpad follows xpad's convention, whatever its vendor.
    EvdevDescription thirdParty = Xbox360();
    thirdParty.vendor = 0x0E6F;
    thirdParty.product = 0x0213;
    EXPECT_TRUE(BuildEvdevGamepadLayout(thirdParty).xpadFaceButtons);
}

TEST(X11EvdevLayout, TheRemainingButtonsMapOneToOne)
{
    const EvdevGamepadLayout layout = BuildEvdevGamepadLayout(Xbox360());
    EXPECT_EQ(ButtonFor(layout, BTN_TL), GamepadButton::LeftShoulder);
    EXPECT_EQ(ButtonFor(layout, BTN_TR), GamepadButton::RightShoulder);
    EXPECT_EQ(ButtonFor(layout, BTN_SELECT), GamepadButton::Back);
    EXPECT_EQ(ButtonFor(layout, BTN_START), GamepadButton::Start);
    EXPECT_EQ(ButtonFor(layout, BTN_MODE), GamepadButton::BigButton);
    EXPECT_EQ(ButtonFor(layout, BTN_THUMBL), GamepadButton::LeftStick);
    EXPECT_EQ(ButtonFor(layout, BTN_THUMBR), GamepadButton::RightStick);

    // Everything an Xbox 360 pad has, the D-pad (a hat) included, and nothing it has not.
    EXPECT_EQ(layout.buttonMask,
              Bits({GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y,
                    GamepadButton::LeftShoulder, GamepadButton::RightShoulder, GamepadButton::Back,
                    GamepadButton::Start, GamepadButton::BigButton, GamepadButton::LeftStick,
                    GamepadButton::RightStick, GamepadButton::DPadUp, GamepadButton::DPadDown,
                    GamepadButton::DPadLeft, GamepadButton::DPadRight}));
}

TEST(X11EvdevLayout, XpadTriggerHappyButtonsAreTheDPadAndThePaddles)
{
    EvdevDescription elite = Xbox360();
    elite.axes.reset(ABS_HAT0X);
    elite.axes.reset(ABS_HAT0Y);
    for (int code = BTN_TRIGGER_HAPPY1; code <= BTN_TRIGGER_HAPPY8; ++code)
    {
        elite.keys.set(static_cast<std::size_t>(code));
    }
    const EvdevGamepadLayout layout = BuildEvdevGamepadLayout(elite);
    EXPECT_EQ(ButtonFor(layout, BTN_TRIGGER_HAPPY1), GamepadButton::DPadLeft);
    EXPECT_EQ(ButtonFor(layout, BTN_TRIGGER_HAPPY2), GamepadButton::DPadRight);
    EXPECT_EQ(ButtonFor(layout, BTN_TRIGGER_HAPPY3), GamepadButton::DPadUp);
    EXPECT_EQ(ButtonFor(layout, BTN_TRIGGER_HAPPY4), GamepadButton::DPadDown);
    EXPECT_EQ(ButtonFor(layout, BTN_TRIGGER_HAPPY5), GamepadButton::Paddle1);
    EXPECT_EQ(ButtonFor(layout, BTN_TRIGGER_HAPPY8), GamepadButton::Paddle4);

    // Outside xpad the trigger-happy range means nothing in particular, and is left unmapped.
    EvdevDescription other = elite;
    other.driver = "hid-generic";
    other.vendor = 0x1234;
    for (const auto& [code, button] : BuildEvdevGamepadLayout(other).buttons)
    {
        (void) button;
        EXPECT_FALSE(code >= BTN_TRIGGER_HAPPY1 && code <= BTN_TRIGGER_HAPPY8) << code;
    }
}

// --- sticks and triggers -------------------------------------------------------------------------

TEST(X11EvdevLayout, RxRyAreTheRightStickAndZRzTheTriggersWhenPresent)
{
    const EvdevGamepadLayout layout = BuildEvdevGamepadLayout(Xbox360());
    std::vector<std::pair<int, GamepadAxis>> seen;
    for (const EvdevAxisBinding& binding : layout.axes)
    {
        seen.emplace_back(binding.code, binding.axis);
    }
    const std::vector<std::pair<int, GamepadAxis>> expected{
        {ABS_X, GamepadAxis::LeftThumbstickX},   {ABS_Y, GamepadAxis::LeftThumbstickY},
        {ABS_RX, GamepadAxis::RightThumbstickX}, {ABS_RY, GamepadAxis::RightThumbstickY},
        {ABS_Z, GamepadAxis::LeftTrigger},       {ABS_RZ, GamepadAxis::RightTrigger}};
    EXPECT_EQ(seen, expected);
    EXPECT_EQ(layout.axisMask, 0x3F);
    EXPECT_TRUE(layout.digitalTriggers.empty());
}

TEST(X11EvdevLayout, WithoutRxRyTheRightStickIsOnZRz)
{
    // The DirectInput layout many generic pads keep; their triggers, if analogue, are BRAKE/GAS.
    const EvdevGamepadLayout layout = BuildEvdevGamepadLayout(
        Describe({BTN_SOUTH, BTN_EAST},
                 {{ABS_X, kByte}, {ABS_Y, kByte}, {ABS_Z, kByte}, {ABS_RZ, kByte},
                  {ABS_BRAKE, kByte}, {ABS_GAS, kByte}}));
    ASSERT_EQ(layout.axes.size(), 6u);
    EXPECT_EQ(layout.axes[2].code, ABS_Z);
    EXPECT_EQ(layout.axes[2].axis, GamepadAxis::RightThumbstickX);
    EXPECT_EQ(layout.axes[3].code, ABS_RZ);
    EXPECT_EQ(layout.axes[3].axis, GamepadAxis::RightThumbstickY);
    EXPECT_EQ(layout.axes[4].code, ABS_BRAKE);
    EXPECT_EQ(layout.axes[4].axis, GamepadAxis::LeftTrigger);
    EXPECT_EQ(layout.axes[5].code, ABS_GAS);
    EXPECT_EQ(layout.axes[5].axis, GamepadAxis::RightTrigger);
}

TEST(X11EvdevLayout, DigitalTriggersStandInOnlyForMissingAnalogueOnes)
{
    // Switch Pro: ZL/ZR are buttons, so they drive the trigger axes to 0 or 1.
    const EvdevGamepadLayout pro = BuildEvdevGamepadLayout(SwitchPro());
    ASSERT_EQ(pro.digitalTriggers.size(), 2u);
    EXPECT_NE(pro.axisMask & GamepadAxisBit(GamepadAxis::LeftTrigger), 0);
    EXPECT_NE(pro.axisMask & GamepadAxisBit(GamepadAxis::RightTrigger), 0);

    EvdevGamepadState state(pro);
    std::vector<EvdevGamepadChange> changes;
    state.Apply(EV_KEY, BTN_TL2, 1, changes);
    EXPECT_FLOAT_EQ(state.GetAxes()[static_cast<std::size_t>(GamepadAxis::LeftTrigger)], 1.0f);
    state.Apply(EV_KEY, BTN_TL2, 0, changes);
    EXPECT_FLOAT_EQ(state.GetAxes()[static_cast<std::size_t>(GamepadAxis::LeftTrigger)], 0.0f);

    // DualSense reports each trigger both ways; the analogue value wins and the button is not
    // allowed to snap the axis to 1 half-way through a squeeze.
    const EvdevGamepadLayout dualSense = BuildEvdevGamepadLayout(DualSense());
    EXPECT_TRUE(dualSense.digitalTriggers.empty());
    EvdevGamepadState squeezed(dualSense);
    squeezed.Apply(EV_ABS, ABS_Z, 51, changes);
    squeezed.Apply(EV_KEY, BTN_TL2, 1, changes);
    EXPECT_NEAR(squeezed.GetAxes()[static_cast<std::size_t>(GamepadAxis::LeftTrigger)], 0.2f, 0.001f);
}

TEST(X11EvdevLayout, TheHatIsTheDPad)
{
    EvdevGamepadState state(BuildEvdevGamepadLayout(Xbox360()));
    std::vector<EvdevGamepadChange> changes;

    state.Apply(EV_ABS, ABS_HAT0X, -1, changes);
    EXPECT_EQ(state.GetButtons(), Bits({GamepadButton::DPadLeft}));
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_TRUE(changes[0].isButton);
    EXPECT_EQ(changes[0].button, GamepadButton::DPadLeft);
    EXPECT_TRUE(changes[0].pressed);

    // Straight from left to right: one release and one press, in that order.
    changes.clear();
    state.Apply(EV_ABS, ABS_HAT0X, 1, changes);
    EXPECT_EQ(state.GetButtons(), Bits({GamepadButton::DPadRight}));
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].button, GamepadButton::DPadLeft);
    EXPECT_FALSE(changes[0].pressed);
    EXPECT_EQ(changes[1].button, GamepadButton::DPadRight);
    EXPECT_TRUE(changes[1].pressed);

    // Diagonals are two buttons.
    state.Apply(EV_ABS, ABS_HAT0Y, -1, changes);
    EXPECT_EQ(state.GetButtons(), Bits({GamepadButton::DPadRight, GamepadButton::DPadUp}));
    state.Apply(EV_ABS, ABS_HAT0X, 0, changes);
    state.Apply(EV_ABS, ABS_HAT0Y, 0, changes);
    EXPECT_EQ(state.GetButtons(), 0u);
}

// --- values --------------------------------------------------------------------------------------

TEST(X11EvdevLayout, AxesScaleOntoTheSigned16BitRange)
{
    EXPECT_EQ(ScaleEvdevAxis(-32768, kStick16), -32768);
    EXPECT_EQ(ScaleEvdevAxis(0, kStick16), 0);
    EXPECT_EQ(ScaleEvdevAxis(32767, kStick16), 32767);

    EXPECT_EQ(ScaleEvdevAxis(0, kByte), -32768);
    EXPECT_EQ(ScaleEvdevAxis(255, kByte), 32767);
    EXPECT_EQ(ScaleEvdevAxis(128, kByte), 128);

    // Out-of-range values happen (a worn stick, a driver's calibration) and are clamped.
    EXPECT_EQ(ScaleEvdevAxis(300, kByte), 32767);
    EXPECT_EQ(ScaleEvdevAxis(-5, kByte), -32768);

    // A degenerate range reports centre rather than dividing by zero.
    EXPECT_EQ(ScaleEvdevAxis(7, EvdevAxisRange{5, 5, 0}), 0);
    EXPECT_EQ(ScaleEvdevAxis(7, EvdevAxisRange{9, 1, 0}), 0);
}

TEST(X11EvdevLayout, SticksAreUpPositiveAndTriggersZeroToOne)
{
    // The kernel's Y grows downwards; XNA's grows upwards.
    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::LeftThumbstickY, -32768, kStick16), 1.0f);
    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::LeftThumbstickY, 32767, kStick16), -1.0f);
    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::RightThumbstickY, 32767, kStick16), -1.0f);
    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::LeftThumbstickX, 32767, kStick16), 1.0f);
    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::LeftThumbstickX, -32768, kStick16), -1.0f);
    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::LeftThumbstickX, 0, kStick16), 0.0f);

    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::LeftTrigger, 0, kByte), 0.0f);
    EXPECT_FLOAT_EQ(NormalizeEvdevGamepadAxis(GamepadAxis::RightTrigger, 255, kByte), 1.0f);
    // No dead zone here: XNA applies its own, and a platform one would be applied twice.
    EXPECT_GT(NormalizeEvdevGamepadAxis(GamepadAxis::LeftTrigger, 1, kByte), 0.0f);
    EXPECT_NE(NormalizeEvdevGamepadAxis(GamepadAxis::LeftThumbstickX, 200, kStick16), 0.0f);
}

TEST(X11EvdevLayout, OnlyRealChangesAreReported)
{
    EvdevGamepadState state(BuildEvdevGamepadLayout(Xbox360()));
    std::vector<EvdevGamepadChange> changes;

    state.Apply(EV_KEY, BTN_SOUTH, 1, changes);
    state.Apply(EV_KEY, BTN_SOUTH, 2, changes);  // Autorepeat: still held.
    state.Apply(EV_ABS, ABS_X, 32767, changes);
    state.Apply(EV_ABS, ABS_X, 32767, changes);
    state.Apply(EV_KEY, KEY_A, 1, changes);  // Not part of the layout.
    state.Apply(EV_REL, REL_X, 5, changes);  // Not a type a pad's state cares about.
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_TRUE(changes[0].isButton);
    EXPECT_EQ(changes[0].button, GamepadButton::A);
    EXPECT_FALSE(changes[1].isButton);
    EXPECT_EQ(changes[1].axis, GamepadAxis::LeftThumbstickX);
    EXPECT_FLOAT_EQ(changes[1].value, 1.0f);

    state.Reset();
    EXPECT_EQ(state.GetButtons(), 0u);
    for (const float axis : state.GetAxes())
    {
        EXPECT_EQ(axis, 0.0f);
    }
}

TEST(X11EvdevLayout, TheModelComesFromTheDeviceIdentity)
{
    const auto model = [](const std::uint16_t vendor, const std::uint16_t product,
                          const char* driver = "") {
        EvdevDescription description = Describe({BTN_SOUTH});
        description.vendor = vendor;
        description.product = product;
        description.driver = driver;
        return BuildEvdevGamepadLayout(description).model;
    };
    EXPECT_EQ(model(0x045E, 0x028E), GamepadModel::Xbox360);
    EXPECT_EQ(model(0x045E, 0x0719), GamepadModel::Xbox360);
    EXPECT_EQ(model(0x045E, 0x0B12), GamepadModel::XboxOne);
    EXPECT_EQ(model(0x054C, 0x0268), GamepadModel::PlayStation3);
    EXPECT_EQ(model(0x054C, 0x09CC), GamepadModel::PlayStation4);
    EXPECT_EQ(model(0x054C, 0x0CE6), GamepadModel::PlayStation5);
    EXPECT_EQ(model(0x057E, 0x2009), GamepadModel::NintendoSwitchPro);
    EXPECT_EQ(model(0x0E6F, 0x0213, "xpad"), GamepadModel::Xbox360);
    EXPECT_EQ(model(0x1234, 0x5678), GamepadModel::Standard);
}

// --- identity helpers ----------------------------------------------------------------------------

TEST(X11EvdevLayout, TheGuidIsTheUsbStyleLayoutControllerDatabasesUse)
{
    // Bus, vendor, product and version, each little-endian, at bytes 0, 4, 8 and 12. The same
    // string SDL builds for the same pad, so a mapping keyed on it names the same device.
    EXPECT_EQ(FormatEvdevGuid(Xbox360()), "030000005e0400008e02000014010000");
    EvdevDescription blank;
    EXPECT_EQ(FormatEvdevGuid(blank), std::string(32, '0'));
}

TEST(X11EvdevLayout, HatAxesCombineIntoNinePositions)
{
    EXPECT_EQ(EvdevHatPosition(0, 0), JoystickHat::Centered);
    EXPECT_EQ(EvdevHatPosition(0, -1), JoystickHat::Up);
    EXPECT_EQ(EvdevHatPosition(0, 1), JoystickHat::Down);
    EXPECT_EQ(EvdevHatPosition(-1, 0), JoystickHat::Left);
    EXPECT_EQ(EvdevHatPosition(1, 0), JoystickHat::Right);
    EXPECT_EQ(EvdevHatPosition(-1, -1), JoystickHat::LeftUp);
    EXPECT_EQ(EvdevHatPosition(1, -1), JoystickHat::RightUp);
    EXPECT_EQ(EvdevHatPosition(-1, 1), JoystickHat::LeftDown);
    EXPECT_EQ(EvdevHatPosition(1, 1), JoystickHat::RightDown);
}

// --- sysfs and the hub, over a directory of our own ---------------------------------------------

class ScratchDirectory
{
public:
    ScratchDirectory()
    {
        path_ = std::filesystem::temp_directory_path() /
                ("cna-evdev-test-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~ScratchDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

TEST(X11EvdevHub, TheDriverNameIsReadFromSysfs)
{
    ScratchDirectory sysfs;
    // /sys/class/input/event7/device/device/driver -> .../drivers/xpad
    const std::filesystem::path deviceDirectory = sysfs.Path() / "event7" / "device" / "device";
    std::filesystem::create_directories(deviceDirectory);
    std::filesystem::create_directories(sysfs.Path() / "bus" / "usb" / "drivers" / "xpad");
    std::filesystem::create_directory_symlink(sysfs.Path() / "bus" / "usb" / "drivers" / "xpad",
                                              deviceDirectory / "driver");

    EXPECT_EQ(ReadEvdevDriverName("event7", sysfs.Path().string()), "xpad");
    // A virtual device (uinput) has no bound driver at all; that is an empty answer, not an error.
    EXPECT_EQ(ReadEvdevDriverName("event8", sysfs.Path().string()), "");
}

TEST(X11EvdevHub, SysfsBitmapsParseAsTheKernelPrintsThem)
{
    // A single word, as `capabilities/ev` prints it.
    EXPECT_EQ(ParseEvdevSysfsBitmap("120013\n"), std::vector<unsigned long>{0x120013});
    EXPECT_EQ(ParseEvdevSysfsBitmap("0\n"), std::vector<unsigned long>{0});
    // Not bitmaps at all.
    EXPECT_TRUE(ParseEvdevSysfsBitmap("").empty());
    EXPECT_TRUE(ParseEvdevSysfsBitmap("\n").empty());
    EXPECT_TRUE(ParseEvdevSysfsBitmap("12 zz").empty());
    EXPECT_TRUE(ParseEvdevSysfsBitmap("0x12").empty());

    if constexpr (sizeof(unsigned long) == 8)
    {
        // A laptop keyboard's `capabilities/key`, verbatim: most significant word first.
        const std::vector<unsigned long> keyboard =
            ParseEvdevSysfsBitmap("402000002 3803078f800d001 feffffdfffefffff fffffffffffffffe\n");
        ASSERT_EQ(keyboard.size(), 4u);
        EXPECT_EQ(keyboard[0], 0xfffffffffffffffeUL);
        EXPECT_EQ(keyboard[3], 0x402000002UL);

        // A TrackPoint's: zero words in the middle are printed, and must keep their places, or
        // BTN_LEFT would land 64 bits too low.
        EvdevDescription trackPoint;
        ScratchDirectory sysfs;
        const std::filesystem::path device = sysfs.Path() / "event5" / "device";
        std::filesystem::create_directories(device / "capabilities");
        std::filesystem::create_directories(device / "id");
        std::ofstream(device / "name") << "TPPS/2 Elan TrackPoint\n";
        std::ofstream(device / "capabilities" / "key") << "70000 0 0 0 0\n";
        std::ofstream(device / "capabilities" / "abs") << "0\n";
        std::ofstream(device / "capabilities" / "ff") << "0\n";
        std::ofstream(device / "properties") << "21\n";
        std::ofstream(device / "uniq") << "\n";
        std::ofstream(device / "id" / "bustype") << "0011\n";
        std::ofstream(device / "id" / "vendor") << "0002\n";
        std::ofstream(device / "id" / "product") << "000a\n";
        std::ofstream(device / "id" / "version") << "0063\n";
        ASSERT_TRUE(ReadEvdevSysfsDescription("event5", trackPoint, sysfs.Path().string()));
        EXPECT_TRUE(trackPoint.keys.test(BTN_LEFT));
        EXPECT_TRUE(trackPoint.keys.test(BTN_RIGHT));
        EXPECT_TRUE(trackPoint.keys.test(BTN_MIDDLE));
        EXPECT_EQ(trackPoint.keys.count(), 3u);
        EXPECT_TRUE(trackPoint.properties.test(INPUT_PROP_POINTER));
        EXPECT_TRUE(trackPoint.properties.test(INPUT_PROP_POINTING_STICK));
        EXPECT_EQ(trackPoint.name, "TPPS/2 Elan TrackPoint");
        EXPECT_EQ(trackPoint.bus, 0x11);
        EXPECT_EQ(trackPoint.product, 0x0a);
        EXPECT_EQ(trackPoint.version, 0x63);
        EXPECT_EQ(ClassifyEvdevDevice(trackPoint), EvdevDeviceClass::None);
    }
}

TEST(X11EvdevHub, SysfsDescribesAPadWithoutItsNodeBeingOpened)
{
    ScratchDirectory sysfs;
    const std::filesystem::path device = sysfs.Path() / "event9" / "device";
    std::filesystem::create_directories(device / "capabilities");
    std::filesystem::create_directories(device / "id");
    std::ofstream(device / "name") << "Some Pad\n";
    // BTN_SOUTH (0x130) is bit 48 of word 4; ABS_X, ABS_Y, ABS_RX, ABS_RY are bits 0, 1, 3, 4.
    std::ofstream(device / "capabilities" / "key") << "1000000000000 0 0 0 0\n";
    std::ofstream(device / "capabilities" / "abs") << "1b\n";
    std::ofstream(device / "capabilities" / "ff") << "107030000 0\n";
    std::ofstream(device / "uniq") << "aa:bb:cc:dd:ee:ff\n";
    std::ofstream(device / "id" / "bustype") << "0005\n";
    std::ofstream(device / "id" / "vendor") << "054c\n";
    std::ofstream(device / "id" / "product") << "0ce6\n";
    std::ofstream(device / "id" / "version") << "8100\n";
    // No `properties`: older kernels have none, and that is not a reason to refuse the device.

    EvdevDescription pad;
    ASSERT_TRUE(ReadEvdevSysfsDescription("event9", pad, sysfs.Path().string()));
    if constexpr (sizeof(unsigned long) == 8)
    {
        EXPECT_TRUE(pad.keys.test(BTN_SOUTH));
        EXPECT_EQ(pad.keys.count(), 1u);
        EXPECT_TRUE(pad.forceFeedback.test(FF_RUMBLE));
        EXPECT_EQ(ClassifyEvdevDevice(pad), EvdevDeviceClass::Gamepad);
    }
    EXPECT_TRUE(pad.axes.test(ABS_X));
    EXPECT_TRUE(pad.axes.test(ABS_RY));
    EXPECT_FALSE(pad.axes.test(ABS_Z));
    EXPECT_EQ(pad.uniq, "aa:bb:cc:dd:ee:ff");
    EXPECT_EQ(pad.bus, BUS_BLUETOOTH);
    EXPECT_EQ(pad.vendor, 0x054C);
    EXPECT_EQ(pad.product, 0x0CE6);
    EXPECT_EQ(pad.version, 0x8100);
    EXPECT_EQ(pad.driver, "");

    // A node sysfs knows nothing about is an answer of its own: open it and ask.
    EXPECT_FALSE(ReadEvdevSysfsDescription("event10", pad, sysfs.Path().string()));
}

/// A capability bitmap in the kernel's sysfs format, for this process's word size.
std::string SysfsBitmap(std::initializer_list<int> bits)
{
    constexpr int kWordBits = static_cast<int>(sizeof(unsigned long) * 8);
    std::vector<unsigned long> words;
    for (const int bit : bits)
    {
        const auto word = static_cast<std::size_t>(bit / kWordBits);
        if (words.size() <= word)
        {
            words.resize(word + 1, 0);
        }
        words[word] |= 1UL << (bit % kWordBits);
    }
    if (words.empty())
    {
        return "0\n";
    }
    std::string text;
    for (std::size_t index = words.size(); index-- > 0;)
    {
        char digits[32] = {};
        std::snprintf(digits, sizeof(digits), "%lx", words[index]);
        text += digits;
        text += index > 0 ? " " : "\n";
    }
    return text;
}

void WriteSysfsNode(const std::filesystem::path& sysfs, const std::string& node,
                    std::initializer_list<int> keys, std::initializer_list<int> axes)
{
    const std::filesystem::path device = sysfs / node / "device";
    std::filesystem::create_directories(device / "capabilities");
    std::filesystem::create_directories(device / "id");
    std::ofstream(device / "name") << node << "\n";
    std::ofstream(device / "capabilities" / "key") << SysfsBitmap(keys);
    std::ofstream(device / "capabilities" / "abs") << SysfsBitmap(axes);
    std::ofstream(device / "capabilities" / "ff") << "0\n";
    std::ofstream(device / "properties") << "0\n";
    for (const char* field : {"bustype", "vendor", "product", "version"})
    {
        std::ofstream(device / "id" / field) << "0001\n";
    }
}

TEST(X11EvdevHub, SysfsDecidesWhatIsOpenedAndAKeyboardNeverIs)
{
    // plans/plan_x11.md X11-0150: the hub used to open every node to classify it, and closing an
    // evdev node waits for an RCU grace period -- 0.8 s across a laptop's sixteen nodes, stalling
    // the first GamePad.GetState(). Timing is a flaky thing to assert; which files were opened is
    // not, and inotify reports every open.
    ScratchDirectory root;
    const std::filesystem::path input = root.Path() / "input";
    const std::filesystem::path sysfs = root.Path() / "sysfs";
    std::filesystem::create_directories(input);
    WriteSysfsNode(sysfs, "event0", {KEY_ESC, KEY_A, KEY_Z, KEY_ENTER}, {});
    WriteSysfsNode(sysfs, "event1", {BTN_LEFT, BTN_RIGHT}, {});
    WriteSysfsNode(sysfs, "event2", {BTN_SOUTH, BTN_EAST}, {ABS_X, ABS_Y});
    for (const char* node : {"event0", "event1", "event2", "event3"})
    {
        std::ofstream(input / node) << "a stand-in node";
    }

    const int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    ASSERT_GE(watch, 0);
    ASSERT_GE(inotify_add_watch(watch, input.c_str(), IN_OPEN), 0);

    EvdevControllerHub hub(input.string(), sysfs.string());
    ASSERT_TRUE(hub.Start());
    hub.Pump();

    std::set<std::string> opened;
    alignas(inotify_event) char buffer[4096];
    for (ssize_t bytes; (bytes = ::read(watch, buffer, sizeof(buffer))) > 0;)
    {
        for (ssize_t offset = 0; offset < bytes;)
        {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            if (event->len > 0)
            {
                opened.insert(event->name);
            }
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
        }
    }
    ::close(watch);

    EXPECT_EQ(opened.count("event0"), 0u) << "the keyboard was opened";
    EXPECT_EQ(opened.count("event1"), 0u) << "the mouse was opened";
    EXPECT_EQ(opened.count("event2"), 1u) << "the pad sysfs describes was not opened";
    EXPECT_EQ(opened.count("event3"), 1u) << "a node sysfs does not describe must be opened to ask";
    // None of the stand-ins is a real device, so nothing is kept.
    EXPECT_TRUE(hub.GetControllers().empty());
}

TEST(X11EvdevHub, ADirectoryThatDoesNotExistIsAnAnswerNotAnError)
{
    EvdevControllerHub hub("/nonexistent/cna/input");
    EXPECT_FALSE(hub.Start());
    EXPECT_FALSE(hub.IsStarted());
    hub.Pump();
    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);
    EXPECT_TRUE(events.empty());
    EXPECT_TRUE(hub.GetControllers().empty());
}

TEST(X11EvdevHub, NodesThatAreNotDevicesAreIgnored)
{
    ScratchDirectory input;
    // Files named like event nodes that are no such thing, plus the legacy nodes the hub never
    // looks at. EVIOCGVERSION on a regular file fails, and that is the whole test for "device".
    for (const char* name : {"event0", "event1", "mice", "mouse0", "js0"})
    {
        std::ofstream(input.Path() / name) << "not a device";
    }

    // No sysfs for these, so each is opened and asked -- the path a container without /sys takes.
    EvdevControllerHub hub(input.Path().string(), (input.Path() / "no-sysfs").string());
    ASSERT_TRUE(hub.Start());
    hub.Pump();
    EXPECT_TRUE(hub.GetControllers().empty());

    // A node that appears later is looked at, and refused the same way.
    std::ofstream(input.Path() / "event2") << "not a device either";
    hub.Pump();
    EXPECT_TRUE(hub.GetControllers().empty());

    std::vector<PlatformEvent> events;
    hub.TakeEvents(events);
    EXPECT_TRUE(events.empty());

    hub.Stop();
    EXPECT_FALSE(hub.IsStarted());
}

TEST(X11EvdevHub, ServicesOverAHubWithNoControllersReportEmptySlots)
{
    ScratchDirectory input;
    EvdevControllerHub hub(input.Path().string(), input.Path().string());
    ASSERT_TRUE(hub.Start());
    EvdevGamepad gamepad(hub);
    EvdevJoystick joystick(hub);
    gamepad.Update();
    joystick.Update();

    EXPECT_EQ(gamepad.GetCount(), GamepadSlotCount);
    for (int slot = -1; slot <= GamepadSlotCount; ++slot)
    {
        EXPECT_FALSE(gamepad.GetSnapshot(slot).connected) << slot;
        EXPECT_EQ(gamepad.GetSnapshot(slot).packetNumber, 0u) << slot;
        EXPECT_FALSE(gamepad.GetCapabilities(slot).connected) << slot;
        EXPECT_EQ(gamepad.GetName(slot), "") << slot;
        // Polling an empty slot is ordinary control flow: false, never a throw.
        EXPECT_FALSE(gamepad.SetRumble(slot, 1.0f, 1.0f, 100)) << slot;
        EXPECT_EQ(gamepad.GetPowerInfo(slot).state, GamepadPowerState::Error) << slot;
        EXPECT_EQ(gamepad.GetPowerInfo(slot).percent, -1) << slot;
        EXPECT_EQ(gamepad.GetButtonLabel(slot, GamepadButton::A), GamepadButtonLabel::Unknown);
    }
    GamepadSensorReading reading{1.0f, 2.0f, 3.0f};
    EXPECT_FALSE(gamepad.TryGetSensor(0, GamepadSensor::Gyroscope, reading));
    EXPECT_EQ(reading.x, 0.0f);
    GamepadTouchpadFinger finger{true, 1.0f, 1.0f, 1.0f};
    EXPECT_FALSE(gamepad.TryGetTouchpadFinger(0, 0, 0, finger));
    EXPECT_FALSE(finger.down);

    EXPECT_TRUE(joystick.GetJoysticks().empty());
    EXPECT_FALSE(joystick.IsConnected(0));
    EXPECT_FALSE(joystick.IsConnected(1));
    EXPECT_FALSE(joystick.GetCapabilities(1).connected);
    EXPECT_TRUE(joystick.GetSnapshot(1).axes.empty());
}

} // namespace

#endif // CNA_PLATFORM_HAVE_EVDEV
