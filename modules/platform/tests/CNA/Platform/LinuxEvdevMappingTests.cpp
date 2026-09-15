// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0160: controller-database mappings -- the gamecontrollerdb.txt format --
// read, matched to a device and evaluated, from synthetic device descriptions. No device.
//
// A mapping names the device's inputs by index, so the numbering is the whole contract with the
// database: one index off and every button is the next one's. The numbering pinned here is the
// one the database's entries were recorded against.

#include <gtest/gtest.h>

#ifdef CNA_PLATFORM_HAVE_EVDEV

#include "../../../src/Linux/EvdevLayout.hpp"
#include "../../../src/Linux/EvdevMapping.hpp"

#include "System/Environment.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Linux;

constexpr EvdevAxisRange kStick{0, 255, 0, 0, 0};
constexpr EvdevAxisRange kHat{-1, 1, 0, 0, 0};

// A generic USB pad of the kind the HID driver exposes as a joystick: twelve buttons from
// BTN_TRIGGER, four byte-wide axes and a hat. No gamepad-API codes at all.
EvdevDescription GenericPad()
{
    EvdevDescription description;
    description.name = "Generic   USB  Joystick  ";
    description.bus = BUS_USB;
    description.vendor = 0x0079;
    description.product = 0x0006;
    description.version = 0x0110;
    for (int code = BTN_TRIGGER; code <= BTN_BASE6; ++code)
    {
        description.keys.set(static_cast<std::size_t>(code));
    }
    for (const int code : {ABS_X, ABS_Y, ABS_Z, ABS_RZ})
    {
        description.axes.set(static_cast<std::size_t>(code));
        description.ranges[static_cast<std::size_t>(code)] = kStick;
    }
    for (const int code : {ABS_HAT0X, ABS_HAT0Y})
    {
        description.axes.set(static_cast<std::size_t>(code));
        description.ranges[static_cast<std::size_t>(code)] = kHat;
    }
    return description;
}

// 03000000 7900 0000 0600 0000 1001 0000: bus, vendor, product, version, little-endian.
constexpr const char* kGenericGuid = "03000000790000000600000010010000";

std::string GenericMapping(const std::string& guid = kGenericGuid, const std::string& extra = {})
{
    return guid +
           ",Generic Pad,a:b2,b:b1,x:b3,y:b0,back:b8,start:b9,leftshoulder:b4,rightshoulder:b5,"
           "lefttrigger:b6,righttrigger:b7,leftstick:b10,rightstick:b11,leftx:a0,lefty:a1,"
           "rightx:a2,righty:a3,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8," +
           extra + "platform:Linux,";
}

std::vector<EvdevGamepadChange> Apply(EvdevGamepadState& state, const std::uint16_t type,
                                      const std::uint16_t code, const std::int32_t value)
{
    std::vector<EvdevGamepadChange> changes;
    state.Apply(type, code, value, changes);
    return changes;
}

bool Held(const EvdevGamepadState& state, const GamepadButton button)
{
    return (state.GetButtons() & static_cast<std::uint32_t>(button)) != 0;
}

float AxisOf(const EvdevGamepadState& state, const GamepadAxis axis)
{
    return state.GetAxes()[static_cast<std::size_t>(axis)];
}

// --- the format ------------------------------------------------------------------------------------

TEST(LinuxEvdevMapping, AnEntryIsReadElementByElement)
{
    const std::optional<ControllerMapping> mapping = ParseControllerMapping(GenericMapping());
    ASSERT_TRUE(mapping.has_value());
    EXPECT_EQ(mapping->name, "Generic Pad");
    EXPECT_EQ(mapping->guid[0], 0x03);
    EXPECT_EQ(mapping->guid[4], 0x79);
    EXPECT_EQ(mapping->guid[8], 0x06);
    EXPECT_EQ(mapping->guid[12], 0x10);
    EXPECT_EQ(mapping->guid[13], 0x01);
    EXPECT_FALSE(mapping->crc.has_value());
    ASSERT_EQ(mapping->bindings.size(), 20u);

    const ControllerMappingBinding& a = mapping->bindings[0];
    EXPECT_TRUE(a.output.isButton);
    EXPECT_EQ(a.output.button, GamepadButton::A);
    EXPECT_EQ(a.input.kind, ControllerMappingInput::Kind::Button);
    EXPECT_EQ(a.input.index, 2);

    const ControllerMappingBinding& trigger = mapping->bindings[8];
    EXPECT_FALSE(trigger.output.isButton);
    EXPECT_EQ(trigger.output.axis, GamepadAxis::LeftTrigger);
    EXPECT_EQ(trigger.output.minimum, 0);
    EXPECT_EQ(trigger.output.maximum, 32767);

    const ControllerMappingBinding& lefty = mapping->bindings[13];
    EXPECT_EQ(lefty.input.kind, ControllerMappingInput::Kind::Axis);
    EXPECT_EQ(lefty.input.index, 1);
    EXPECT_EQ(lefty.input.minimum, -32768);
    EXPECT_EQ(lefty.input.maximum, 32767);

    const ControllerMappingBinding& dpleft = mapping->bindings[19];
    EXPECT_EQ(dpleft.input.kind, ControllerMappingInput::Kind::Hat);
    EXPECT_EQ(dpleft.input.index, 0);
    EXPECT_EQ(dpleft.input.hatMask, 8);
}

TEST(LinuxEvdevMapping, HalfAxesAndInversionAreRead)
{
    const std::optional<ControllerMapping> mapping = ParseControllerMapping(
        std::string(kGenericGuid) + ",Halves,+leftx:b5,-leftx:b4,lefttrigger:+a2,righttrigger:-a2,"
                                    "lefty:a1~,dpup:-a5,");
    ASSERT_TRUE(mapping.has_value());
    ASSERT_EQ(mapping->bindings.size(), 6u);
    EXPECT_EQ(mapping->bindings[0].output.minimum, 0);
    EXPECT_EQ(mapping->bindings[0].output.maximum, 32767);
    EXPECT_EQ(mapping->bindings[1].output.minimum, 0);
    EXPECT_EQ(mapping->bindings[1].output.maximum, -32768);
    EXPECT_EQ(mapping->bindings[2].input.minimum, 0);
    EXPECT_EQ(mapping->bindings[2].input.maximum, 32767);
    EXPECT_EQ(mapping->bindings[3].input.minimum, 0);
    EXPECT_EQ(mapping->bindings[3].input.maximum, -32768);
    EXPECT_EQ(mapping->bindings[4].input.minimum, 32767) << "inverted";
    EXPECT_EQ(mapping->bindings[4].input.maximum, -32768);
    EXPECT_TRUE(mapping->bindings[5].output.isButton);
    EXPECT_EQ(mapping->bindings[5].input.maximum, -32768);
}

TEST(LinuxEvdevMapping, OnlyLinuxEntriesAreTaken)
{
    EXPECT_FALSE(ParseControllerMapping(std::string(kGenericGuid) + ",W,a:b0,platform:Windows,"));
    EXPECT_FALSE(ParseControllerMapping(std::string(kGenericGuid) + ",M,a:b0,platform:Mac OS X,"));
    EXPECT_TRUE(ParseControllerMapping(std::string(kGenericGuid) + ",Any,a:b0,"))
        << "an entry naming no platform is for every platform";
}

TEST(LinuxEvdevMapping, AConditionTakesTheDefaultTheEntryStates)
{
    const std::string guid = kGenericGuid;
    EXPECT_TRUE(ParseControllerMapping(guid + ",On,a:b0,hint:SOME_SETTING:=1,platform:Linux,"));
    EXPECT_FALSE(ParseControllerMapping(guid + ",Off,a:b0,hint:!SOME_SETTING:=1,platform:Linux,"));
    EXPECT_FALSE(ParseControllerMapping(guid + ",Unset,a:b0,hint:SOME_SETTING,platform:Linux,"));
}

TEST(LinuxEvdevMapping, ALabelledNintendoEntryBecomesPositional)
{
    // The same pad twice, as databases carry it: once by position, once by printed label.
    const std::string guid = kGenericGuid;
    const auto faces = [](const ControllerMapping& mapping) {
        std::vector<std::pair<GamepadButton, int>> result;
        for (const ControllerMappingBinding& binding : mapping.bindings)
        {
            result.emplace_back(binding.output.button, binding.input.index);
        }
        return result;
    };
    const std::optional<ControllerMapping> positional = ParseControllerMapping(
        guid + ",Positional,a:b0,b:b1,x:b2,y:b3,hint:!X_USE_BUTTON_LABELS:=1,");
    const std::optional<ControllerMapping> labelled = ParseControllerMapping(
        guid + ",Labelled,a:b1,b:b0,x:b3,y:b2,hint:X_USE_BUTTON_LABELS:=1,");
    ASSERT_TRUE(positional && labelled);
    EXPECT_EQ(faces(*labelled), (std::vector<std::pair<GamepadButton, int>>{
                                    {GamepadButton::B, 1}, {GamepadButton::A, 0},
                                    {GamepadButton::Y, 3}, {GamepadButton::X, 2}}));
    EXPECT_EQ(faces(*positional), (std::vector<std::pair<GamepadButton, int>>{
                                      {GamepadButton::A, 0}, {GamepadButton::B, 1},
                                      {GamepadButton::X, 2}, {GamepadButton::Y, 3}}));

    // A GameCube-labelled entry: its B is on the left, its X on the right.
    const std::optional<ControllerMapping> cube = ParseControllerMapping(
        guid + ",Cube,a:b0,b:b2,x:b1,y:b3,hint:X_USE_GAMECUBE_LABELS:=1,");
    ASSERT_TRUE(cube);
    EXPECT_EQ(faces(*cube), (std::vector<std::pair<GamepadButton, int>>{
                                {GamepadButton::A, 0}, {GamepadButton::X, 2},
                                {GamepadButton::B, 1}, {GamepadButton::Y, 3}}));
}

TEST(LinuxEvdevMapping, CommentsBlanksAndBrokenLinesAreNotEntries)
{
    EXPECT_FALSE(ParseControllerMapping("# Linux"));
    EXPECT_FALSE(ParseControllerMapping("   "));
    EXPECT_FALSE(ParseControllerMapping("0300000079000000,Short GUID,a:b0,"));
    EXPECT_FALSE(ParseControllerMapping("0300000079000000060000001001000g,Bad hex,a:b0,"));
    EXPECT_FALSE(ParseControllerMapping(std::string(kGenericGuid) + ",No elements,"));
    // Elements this reader does not know are skipped, not fatal.
    const std::optional<ControllerMapping> partial = ParseControllerMapping(
        std::string(kGenericGuid) + ",Partial,misc9:b1,a:b0,x:q3,sdk>=:21,b:b1,");
    ASSERT_TRUE(partial.has_value());
    EXPECT_EQ(partial->bindings.size(), 2u);
}

TEST(LinuxEvdevMapping, AChecksumInTheGuidIsMovedOutOfIt)
{
    const std::optional<ControllerMapping> mapping =
        ParseControllerMapping("0300cdab790000000600000010010000,Checksummed,a:b0,");
    ASSERT_TRUE(mapping.has_value());
    ASSERT_TRUE(mapping->crc.has_value());
    EXPECT_EQ(*mapping->crc, 0xABCD);
    EXPECT_EQ(mapping->guid[2], 0);
    EXPECT_EQ(mapping->guid[3], 0);
    const std::optional<ControllerMapping> field =
        ParseControllerMapping(std::string(kGenericGuid) + ",Field,a:b0,crc:1f2e,");
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(*field->crc, 0x1F2E);
}

TEST(LinuxEvdevMapping, TheNameChecksumIsCrc16Arc)
{
    EXPECT_EQ(ControllerNameCrc16("123456789"), 0xBB3D);  // the algorithm's check value
    EXPECT_EQ(ControllerNameCrc16(""), 0);
}

TEST(LinuxEvdevMapping, EveryEntryOfADatabaseFileIsRead)
{
    // Against a real database, where one is given: CNA_TEST_GAMECONTROLLERDB names a
    // gamecontrollerdb.txt-style file. Every entry must be read, except another platform's and
    // one whose condition does not hold -- anything else refused is a reader defect.
    const std::optional<std::string> path =
        System::Environment::GetEnvironmentVariable("CNA_TEST_GAMECONTROLLERDB");
    if (!path || path->empty())
    {
        GTEST_SKIP() << "set CNA_TEST_GAMECONTROLLERDB to a controller database file";
    }
    std::ifstream file(*path);
    ASSERT_TRUE(file.good()) << *path;
    std::size_t entries = 0;
    std::size_t read = 0;
    std::size_t elements = 0;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == '#') { continue; }
        ++entries;
        const std::optional<ControllerMapping> mapping = ParseControllerMapping(line);
        if (mapping)
        {
            ++read;
            elements += mapping->bindings.size();
            continue;
        }
        const bool otherPlatform =
            line.find("platform:") != std::string::npos && line.find("platform:Linux") == std::string::npos;
        const bool conditional = line.find("hint:") != std::string::npos;
        EXPECT_TRUE(otherPlatform || conditional) << "refused: " << line;
    }
    std::printf("[          ] %zu entries, %zu read, %zu elements\n", entries, read, elements);
    EXPECT_GT(read, 0u);
}

// --- numbering and matching -------------------------------------------------------------------------

TEST(LinuxEvdevMapping, InputsAreNumberedAsTheDatabaseNumbersThem)
{
    EvdevDescription description = GenericPad();
    // A key below the joystick range comes after every button in or above it.
    description.keys.set(KEY_BACK);
    description.keys.set(BTN_TRIGGER_HAPPY1);
    const ControllerInputNumbering numbering = NumberControllerInputs(description);
    ASSERT_EQ(numbering.buttons.size(), 14u);
    EXPECT_EQ(numbering.buttons[0], BTN_TRIGGER);
    EXPECT_EQ(numbering.buttons[11], BTN_BASE6);
    EXPECT_EQ(numbering.buttons[12], BTN_TRIGGER_HAPPY1);
    EXPECT_EQ(numbering.buttons[13], KEY_BACK);
    EXPECT_EQ(numbering.axes, (std::vector<std::uint16_t>{ABS_X, ABS_Y, ABS_Z, ABS_RZ}))
        << "a digital hat is a hat, not two axes";
    EXPECT_EQ(numbering.hats, (std::vector<std::uint16_t>{ABS_HAT0X}));
}

TEST(LinuxEvdevMapping, AHatThatLooksAnalogueIsNumberedAsAxes)
{
    EvdevDescription description = GenericPad();
    description.ranges[ABS_HAT0X] = {-127, 127, 4, 2, 0};
    description.ranges[ABS_HAT0Y] = {-127, 127, 4, 2, 0};
    const ControllerInputNumbering numbering = NumberControllerInputs(description);
    EXPECT_TRUE(numbering.hats.empty());
    EXPECT_EQ(numbering.axes,
              (std::vector<std::uint16_t>{ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}));

    // A wider range with no fuzz, flat or resolution is still read as digital.
    description.ranges[ABS_HAT0X] = {-127, 127, 0, 0, 0};
    description.ranges[ABS_HAT0Y] = {-127, 127, 0, 0, 0};
    EXPECT_EQ(NumberControllerInputs(description).hats.size(), 1u);
}

TEST(LinuxEvdevMapping, TheGuidIsBusVendorProductAndVersion)
{
    const std::array<std::uint8_t, 16> guid = ControllerGuidOf(GenericPad());
    const std::optional<ControllerMapping> mapping = ParseControllerMapping(GenericMapping());
    EXPECT_EQ(guid, mapping->guid);

    EvdevDescription anonymous;
    anonymous.bus = BUS_BLUETOOTH;
    anonymous.name = "Nameless Controller";
    const std::array<std::uint8_t, 16> named = ControllerGuidOf(anonymous);
    EXPECT_EQ(named[0], 0x05);
    EXPECT_EQ(named[4], 'N');
    EXPECT_EQ(named[14], 'o') << "eleven bytes of the name";
    EXPECT_EQ(named[15], 0);
}

TEST(LinuxEvdevMapping, TheExactVersionWinsThenAnyVersion)
{
    ControllerMappingDatabase database;
    database.AddMappings("03000000790000000600000000000000,Any version,a:b0,\n" +
                         GenericMapping(kGenericGuid) + "\n");
    ASSERT_EQ(database.GetCount(), 2u);
    ASSERT_NE(database.Find(GenericPad()), nullptr);
    EXPECT_EQ(database.Find(GenericPad())->name, "Generic Pad");

    EvdevDescription newer = GenericPad();
    newer.version = 0x0200;
    ASSERT_NE(database.Find(newer), nullptr);
    EXPECT_EQ(database.Find(newer)->name, "Any version");

    EvdevDescription other = GenericPad();
    other.product = 0x0007;
    EXPECT_EQ(database.Find(other), nullptr);
}

TEST(LinuxEvdevMapping, AStatedChecksumMustBeTheDevicesNames)
{
    const EvdevDescription pad = GenericPad();
    char crc[8];
    std::snprintf(crc, sizeof(crc), "%04x", ControllerNameCrc16(pad.name));

    ControllerMappingDatabase database;
    database.AddMappings(GenericMapping(kGenericGuid, "crc:0001,"));
    EXPECT_EQ(database.Find(pad), nullptr) << "another device with the same ids";

    database.AddMappings(GenericMapping(kGenericGuid, std::string("crc:") + crc + ","));
    ASSERT_NE(database.Find(pad), nullptr);
    EXPECT_EQ(*database.Find(pad)->crc, ControllerNameCrc16(pad.name));
}

TEST(LinuxEvdevMapping, ALaterEntryForTheSameDeviceReplacesTheEarlier)
{
    ControllerMappingDatabase database;
    database.AddMappings(std::string(kGenericGuid) + ",First,a:b0,\n");
    database.AddMappings(std::string(kGenericGuid) + ",Second,a:b1,\n");
    EXPECT_EQ(database.GetCount(), 1u);
    EXPECT_EQ(database.Find(GenericPad())->name, "Second");
}

TEST(LinuxEvdevMapping, TheEnvironmentNamesAFileAndEntries)
{
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("cna-gamecontrollerdb-" + std::to_string(::getpid()));
    {
        std::ofstream out(file);
        out << "# a database file\n" << std::string(kGenericGuid) << ",From the file,a:b0,\n";
    }
    const auto savedFile = System::Environment::GetEnvironmentVariable("CNA_GAMECONTROLLERCONFIG_FILE");
    const auto savedText = System::Environment::GetEnvironmentVariable("CNA_GAMECONTROLLERCONFIG");
    System::Environment::SetEnvironmentVariable("CNA_GAMECONTROLLERCONFIG_FILE", file.string());
    System::Environment::SetEnvironmentVariable(
        "CNA_GAMECONTROLLERCONFIG", "03000000790000000700000010010000,From the variable,a:b0,");
    const ControllerMappingDatabase database = ControllerMappingDatabase::FromEnvironment();
    System::Environment::SetEnvironmentVariable("CNA_GAMECONTROLLERCONFIG_FILE", savedFile);
    System::Environment::SetEnvironmentVariable("CNA_GAMECONTROLLERCONFIG", savedText);
    std::filesystem::remove(file);

    EXPECT_EQ(database.GetCount(), 2u);
    ASSERT_NE(database.Find(GenericPad()), nullptr);
    EXPECT_EQ(database.Find(GenericPad())->name, "From the file");
}

// --- evaluation -------------------------------------------------------------------------------------

EvdevGamepadState MappedState(const std::string& entry, const EvdevDescription& description = GenericPad())
{
    const std::optional<ControllerMapping> mapping = ParseControllerMapping(entry);
    EXPECT_TRUE(mapping.has_value());
    return EvdevGamepadState(BuildMappedGamepadLayout(*mapping, description));
}

TEST(LinuxEvdevMapping, ButtonsGoWhereTheEntrySays)
{
    EvdevGamepadState state = MappedState(GenericMapping());
    EXPECT_EQ(state.GetLayout().mapped.size(), 20u);
    // b2 -- the third button, BTN_THUMB2 -- is A.
    Apply(state, EV_KEY, BTN_THUMB2, 1);
    EXPECT_TRUE(Held(state, GamepadButton::A));
    Apply(state, EV_KEY, BTN_TRIGGER, 1);
    EXPECT_TRUE(Held(state, GamepadButton::Y));
    Apply(state, EV_KEY, BTN_THUMB2, 0);
    EXPECT_FALSE(Held(state, GamepadButton::A));
    // b6 drives the left trigger fully.
    Apply(state, EV_KEY, BTN_BASE, 1);
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftTrigger), 1.0f);
    Apply(state, EV_KEY, BTN_BASE, 0);
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftTrigger), 0.0f);

    const std::uint32_t expected = static_cast<std::uint32_t>(GamepadButton::A) |
                                   static_cast<std::uint32_t>(GamepadButton::DPadLeft);
    EXPECT_EQ(state.GetLayout().buttonMask & expected, expected);
    EXPECT_EQ(state.GetLayout().axisMask, 0x3F);
}

TEST(LinuxEvdevMapping, SticksKeepCnasUpIsPositive)
{
    EvdevGamepadState state = MappedState(GenericMapping());
    Apply(state, EV_ABS, ABS_X, 255);
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftThumbstickX), 1.0f);
    Apply(state, EV_ABS, ABS_Y, 0);  // pushed up: the kernel's minimum
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftThumbstickY), 1.0f);
    Apply(state, EV_ABS, ABS_RZ, 255);  // a3, pushed down
    EXPECT_EQ(AxisOf(state, GamepadAxis::RightThumbstickY), -1.0f);
}

TEST(LinuxEvdevMapping, AHatPressesAndReleasesItsDirections)
{
    EvdevGamepadState state = MappedState(GenericMapping());
    Apply(state, EV_ABS, ABS_HAT0Y, -1);
    EXPECT_TRUE(Held(state, GamepadButton::DPadUp));
    Apply(state, EV_ABS, ABS_HAT0X, 1);
    EXPECT_TRUE(Held(state, GamepadButton::DPadUp));
    EXPECT_TRUE(Held(state, GamepadButton::DPadRight));
    Apply(state, EV_ABS, ABS_HAT0Y, 0);
    EXPECT_FALSE(Held(state, GamepadButton::DPadUp));
    EXPECT_TRUE(Held(state, GamepadButton::DPadRight));
    Apply(state, EV_ABS, ABS_HAT0X, -1);
    EXPECT_FALSE(Held(state, GamepadButton::DPadRight));
    EXPECT_TRUE(Held(state, GamepadButton::DPadLeft));
}

TEST(LinuxEvdevMapping, OneAxisSplitInTwoDrivesEachHalfAndLetsGoOfTheOther)
{
    // A pad reporting both triggers on one axis, each pulling it one way from the centre.
    EvdevGamepadState state = MappedState(std::string(kGenericGuid) +
                                          ",Shared triggers,lefttrigger:-a2,righttrigger:+a2,");
    Apply(state, EV_ABS, ABS_Z, 0);  // fully one way
    EXPECT_NEAR(AxisOf(state, GamepadAxis::LeftTrigger), 1.0f, 1e-4f);
    EXPECT_EQ(AxisOf(state, GamepadAxis::RightTrigger), 0.0f);
    Apply(state, EV_ABS, ABS_Z, 255);  // fully the other: the first is let go
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftTrigger), 0.0f);
    EXPECT_NEAR(AxisOf(state, GamepadAxis::RightTrigger), 1.0f, 1e-4f);
}

TEST(LinuxEvdevMapping, AFullAxisBecomesAFullTrigger)
{
    EvdevGamepadState state = MappedState(std::string(kGenericGuid) + ",Trigger axis,lefttrigger:a2,");
    Apply(state, EV_ABS, ABS_Z, 0);
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftTrigger), 0.0f) << "released at the axis's minimum";
    Apply(state, EV_ABS, ABS_Z, 255);
    EXPECT_NEAR(AxisOf(state, GamepadAxis::LeftTrigger), 1.0f, 1e-4f);
}

TEST(LinuxEvdevMapping, AnInvertedAxisAndButtonsOnHalfAxes)
{
    EvdevGamepadState state = MappedState(std::string(kGenericGuid) +
                                          ",Odd,leftx:a0~,-lefty:b4,+lefty:b5,dpup:-a1,dpdown:+a1,");
    Apply(state, EV_ABS, ABS_X, 255);
    EXPECT_NEAR(AxisOf(state, GamepadAxis::LeftThumbstickX), -1.0f, 1e-4f) << "inverted";
    Apply(state, EV_KEY, BTN_PINKIE, 1);  // b5: +lefty, i.e. down
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftThumbstickY), -1.0f);
    Apply(state, EV_KEY, BTN_PINKIE, 0);
    EXPECT_EQ(AxisOf(state, GamepadAxis::LeftThumbstickY), 0.0f);
    Apply(state, EV_ABS, ABS_Y, 0);  // a1 pushed to its minimum: dpup
    EXPECT_TRUE(Held(state, GamepadButton::DPadUp));
    EXPECT_FALSE(Held(state, GamepadButton::DPadDown));
    Apply(state, EV_ABS, ABS_Y, 255);
    EXPECT_FALSE(Held(state, GamepadButton::DPadUp)) << "let go as the other half takes over";
    EXPECT_TRUE(Held(state, GamepadButton::DPadDown));
}

TEST(LinuxEvdevMapping, ElementsNamingInputsTheDeviceLacksAreLeftOut)
{
    EvdevGamepadState state = MappedState(std::string(kGenericGuid) +
                                          ",Too many,a:b0,b:b40,leftx:a9,dpup:h3.1,");
    EXPECT_EQ(state.GetLayout().mapped.size(), 1u);
}

TEST(LinuxEvdevMapping, AResetForgetsHatsAndAxes)
{
    EvdevGamepadState state = MappedState(GenericMapping());
    Apply(state, EV_ABS, ABS_HAT0Y, -1);
    state.Reset();
    EXPECT_EQ(state.GetButtons(), 0u);
    // The same hat position after a reset is a press again, not "no change".
    EXPECT_FALSE(Apply(state, EV_ABS, ABS_HAT0Y, -1).empty());
}

TEST(LinuxEvdevMapping, CnasScaleFromTheDatabasesScale)
{
    EXPECT_EQ(NormalizeMappedGamepadAxis(GamepadAxis::LeftThumbstickX, 32767), 1.0f);
    EXPECT_EQ(NormalizeMappedGamepadAxis(GamepadAxis::LeftThumbstickX, -32768), -1.0f);
    EXPECT_EQ(NormalizeMappedGamepadAxis(GamepadAxis::LeftThumbstickY, 32767), -1.0f);
    EXPECT_EQ(NormalizeMappedGamepadAxis(GamepadAxis::RightTrigger, -5), 0.0f);
    EXPECT_EQ(NormalizeMappedGamepadAxis(GamepadAxis::RightTrigger, 32767), 1.0f);
}

} // namespace

#endif // CNA_PLATFORM_HAVE_EVDEV
