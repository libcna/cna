// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/IPlatformCamera.hpp"
#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <type_traits>

namespace {

using namespace CNA::Platform;

// Every service is owned and destroyed through its interface, so a missing virtual destructor
// would leak silently. Checked mechanically rather than by review.
TEST(ServiceContractTests, EveryServiceInterfaceHasAVirtualDestructor)
{
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformKeyboard>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformMouse>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformGamepad>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformHaptics>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformJoystick>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformTextInput>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformSensors>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformSensorSession>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformClipboard>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformDisplays>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformDialogs>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformTray>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformTrayIcon>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformCameraProvider>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformCamera>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformFileSystem>);
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformSystemInfo>);
}

TEST(ServiceContractTests, EveryServiceInterfaceIsAbstract)
{
    EXPECT_TRUE(std::is_abstract_v<IPlatformKeyboard>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformMouse>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformGamepad>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformHaptics>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformJoystick>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformTextInput>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformSensors>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformSensorSession>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformClipboard>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformDisplays>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformDialogs>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformTray>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformTrayIcon>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformCameraProvider>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformCamera>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformFileSystem>);
    EXPECT_TRUE(std::is_abstract_v<IPlatformSystemInfo>);
}

TEST(InputSnapshotTests, SnapshotsDefaultToNothingHeld)
{
    // A default snapshot is what a caller sees before the first Update(). Reporting a held key or
    // button there would make a game act on input that never happened.
    const KeyboardSnapshot keyboard;
    EXPECT_TRUE(keyboard.pressedKeys.empty());
    EXPECT_EQ(keyboard.modifiers, 0);

    const MouseSnapshot mouse;
    EXPECT_EQ(mouse.buttons, 0);
    EXPECT_EQ(mouse.x, 0);
    EXPECT_EQ(mouse.y, 0);

    const GamepadSnapshot gamepad;
    EXPECT_FALSE(gamepad.connected);
    EXPECT_EQ(gamepad.buttons, 0u);
    EXPECT_TRUE(std::all_of(gamepad.axes.begin(), gamepad.axes.end(),
                            [](const float value) { return value == 0.0f; }));
    EXPECT_EQ(gamepad.packetNumber, 0u);

    const JoystickSnapshot joystick;
    EXPECT_TRUE(joystick.axes.empty());
    EXPECT_TRUE(joystick.buttons.empty());
    EXPECT_TRUE(joystick.hats.empty());
    EXPECT_TRUE(joystick.balls.empty());

}

TEST(HapticContractTests, DeviceDescriptorDefaultsToDisconnectedIdentity)
{
    const HapticInfo haptic;
    EXPECT_EQ(haptic.id, 0u);
    EXPECT_TRUE(haptic.name.empty());
    EXPECT_FALSE(haptic.rumbleSupported);
}

TEST(InputSnapshotTests, KeyboardSnapshotIsAWholeKeyboardNotAPerKeyQuery)
{
    // cnaplatform.md's input-snapshot rule: thousands of IsKeyDown calls must read a local
    // structure, not become thousands of platform calls. The shape of the contract is what
    // enforces that -- there is deliberately no IsKeyDown(key) on the interface.
    KeyboardSnapshot snapshot;
    snapshot.pressedKeys = {KeyCode::A, KeyCode::B, KeyCode::C};
    EXPECT_EQ(snapshot.pressedKeys.size(), 3u);
}

TEST(SystemServiceTests, PowerInfoDefaultsToUnknownRatherThanAPlausibleLie)
{
    // -1 for "unknown" is distinguishable from 0 for "empty battery"; defaulting to 0 would make
    // a platform with no battery look like one about to die.
    const PowerInfo info;
    EXPECT_EQ(info.state, PowerState::Unknown);
    EXPECT_EQ(info.percent, -1);
    EXPECT_EQ(info.secondsRemaining, -1);
}

TEST(SystemServiceTests, DisplayInfoDefaultsToAUnitContentScale)
{
    // A zero scale would divide to infinity in any layout computation.
    const DisplayInfo display;
    EXPECT_FLOAT_EQ(display.contentScale, 1.0f);
}

TEST(SystemServiceTests, SensorReadingDefaultsToZeroWithNoTimestamp)
{
    const SensorReading reading;
    EXPECT_FLOAT_EQ(reading.x, 0.0f);
    EXPECT_EQ(reading.timestampNanoseconds, 0u);
}

} // namespace
