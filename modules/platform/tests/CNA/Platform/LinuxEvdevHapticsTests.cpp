// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0168: force feedback through the kernel -- the contract's effects in the
// kernel's form, and which devices can play them. No device is needed here; the live tests, on
// uinput devices, are in X11EvdevVirtualDeviceTests.cpp.

#include <gtest/gtest.h>

#if defined(CNA_PLATFORM_HAVE_EVDEV)

#include "../../../src/Linux/EvdevHaptics.hpp"

#include <bitset>
#include <cstdint>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Linux;

std::bitset<FF_CNT> Bits(std::initializer_list<int> codes)
{
    std::bitset<FF_CNT> bits;
    for (const int code : codes)
    {
        bits.set(static_cast<std::size_t>(code));
    }
    return bits;
}

HapticDirection Direction(const HapticDirectionType type, const int x, const int y = 0, const int z = 0)
{
    HapticDirection direction;
    direction.type = type;
    direction.values = {x, y, z};
    return direction;
}

TEST(LinuxEvdevHapticEffect, ADeviceThatCanPlayNothingIsNotAHapticDevice)
{
    EXPECT_FALSE(IsEvdevHapticDevice({}));
    EXPECT_FALSE(IsEvdevHapticDevice(Bits({FF_GAIN, FF_AUTOCENTER}))) << "controls without an effect";
    EXPECT_FALSE(IsEvdevHapticDevice(Bits({FF_PERIODIC}))) << "periodic without a waveform";
    EXPECT_FALSE(IsEvdevHapticDevice(Bits({FF_SINE}))) << "a waveform the kernel will not upload without FF_PERIODIC";
    EXPECT_TRUE(IsEvdevHapticDevice(Bits({FF_RUMBLE})));
    EXPECT_TRUE(IsEvdevHapticDevice(Bits({FF_PERIODIC, FF_SINE})));
    for (const int family : {FF_CONSTANT, FF_RAMP, FF_SPRING, FF_FRICTION, FF_DAMPER, FF_INERTIA})
    {
        EXPECT_TRUE(IsEvdevHapticDevice(Bits({family}))) << family;
    }
}

TEST(LinuxEvdevHapticEffect, FeatureBitsAreTheContractsValues)
{
    // CNA::Input::HapticFeatureEXT: Constant 1<<0 ... LeftRight 1<<11, Gain 1<<16, Autocenter 1<<17.
    const std::bitset<FF_CNT> everything =
        Bits({FF_CONSTANT, FF_PERIODIC, FF_SINE, FF_SQUARE, FF_TRIANGLE, FF_SAW_UP, FF_SAW_DOWN, FF_RAMP, FF_SPRING,
              FF_DAMPER, FF_INERTIA, FF_FRICTION, FF_RUMBLE, FF_GAIN, FF_AUTOCENTER, FF_CUSTOM});
    EXPECT_EQ(EvdevHapticFeatures(everything), 0x0FFFu | (1u << 16) | (1u << 17))
        << "custom (1<<15), status (1<<18) and pause (1<<19) are never claimed";
    EXPECT_EQ(EvdevHapticFeatures(Bits({FF_RUMBLE})), 1u << 11);
    EXPECT_EQ(EvdevHapticFeatures(Bits({FF_SINE})), 0u) << "a waveform without FF_PERIODIC";
    EXPECT_EQ(EvdevHapticFeatures(Bits({FF_PERIODIC, FF_SQUARE})), 1u << 2);
}

TEST(LinuxEvdevHapticEffect, AnEffectIsSupportedByItsFamilysBitAndItsWaveforms)
{
    const std::bitset<FF_CNT> wheel = Bits({FF_CONSTANT, FF_PERIODIC, FF_SINE, FF_SPRING, FF_GAIN});
    HapticEffect effect;
    effect.type = HapticEffectType::Constant;
    EXPECT_TRUE(EvdevSupportsEffect(wheel, effect));
    effect.type = HapticEffectType::Sine;
    EXPECT_TRUE(EvdevSupportsEffect(wheel, effect));
    effect.type = HapticEffectType::Square;
    EXPECT_FALSE(EvdevSupportsEffect(wheel, effect)) << "periodic, but not that waveform";
    effect.type = HapticEffectType::Spring;
    EXPECT_TRUE(EvdevSupportsEffect(wheel, effect));
    effect.type = HapticEffectType::Damper;
    EXPECT_FALSE(EvdevSupportsEffect(wheel, effect));
    effect.type = HapticEffectType::LeftRight;
    EXPECT_FALSE(EvdevSupportsEffect(wheel, effect));
    EXPECT_TRUE(EvdevSupportsEffect(Bits({FF_RUMBLE}), effect));
    effect.type = HapticEffectType::Custom;
    EXPECT_FALSE(EvdevSupportsEffect(Bits({FF_PERIODIC, FF_CUSTOM}), effect)) << "never uploaded";
}

TEST(LinuxEvdevHapticEffect, DirectionsBecomeTheKernelsAngle)
{
    // The kernel: 0 is north, a quarter turn 0x4000, clockwise.
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Polar, 0)), 0x0000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Polar, 9000)), 0x4000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Polar, 18000)), 0x8000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Polar, 27000)), 0xC000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Polar, 36000 + 9000)), 0x4000) << "a whole turn more";
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Polar, -9000)), 0xC000) << "a quarter turn back";
    // Spherical is measured from the east.
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Spherical, 0)), 0x4000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Spherical, 27000)), 0x0000);
    // Cartesian: x east, y south.
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Cartesian, 1, 0)), 0x4000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Cartesian, -5, 0)), 0xC000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Cartesian, 0, 3)), 0x8000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Cartesian, 0, -3)), 0x0000);
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Cartesian, 1, 1)), 0x6000) << "south-east";
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::Cartesian, -1, -1)), 0xE000) << "north-west";
    EXPECT_EQ(ToEvdevDirection(Direction(HapticDirectionType::SteeringAxis, 0)), 0x4000);
}

TEST(LinuxEvdevHapticEffect, AConstantForceKeepsItsLevelTimingTriggerAndEnvelope)
{
    HapticEffect effect;
    effect.type = HapticEffectType::Constant;
    effect.direction = Direction(HapticDirectionType::Polar, 18000);
    effect.length = 1500;
    effect.delay = 20;
    effect.button = 1;
    effect.interval = 300;
    effect.level = -12000;
    effect.attackLength = 100;
    effect.attackLevel = 40000;  // Past the kernel's 32767: clamped.
    effect.fadeLength = 200;
    effect.fadeLevel = 1000;
    ff_effect native{};
    ASSERT_TRUE(ToEvdevEffect(effect, native));
    EXPECT_EQ(native.type, FF_CONSTANT);
    EXPECT_EQ(native.id, -1) << "a new effect";
    EXPECT_EQ(native.direction, 0x8000);
    EXPECT_EQ(native.replay.length, 1500);
    EXPECT_EQ(native.replay.delay, 20);
    EXPECT_EQ(native.trigger.button, BTN_GAMEPAD) << "button 1 is the first gamepad button";
    EXPECT_EQ(native.trigger.interval, 300);
    EXPECT_EQ(native.u.constant.level, -12000);
    EXPECT_EQ(native.u.constant.envelope.attack_length, 100);
    EXPECT_EQ(native.u.constant.envelope.attack_level, 0x7FFF);
    EXPECT_EQ(native.u.constant.envelope.fade_length, 200);
    EXPECT_EQ(native.u.constant.envelope.fade_level, 1000);
}

TEST(LinuxEvdevHapticEffect, LengthsBecomeTheKernelsWithItsLimitsAndItsZero)
{
    HapticEffect effect;
    effect.type = HapticEffectType::Constant;
    ff_effect native{};
    effect.length = UINT32_MAX;
    ASSERT_TRUE(ToEvdevEffect(effect, native));
    EXPECT_EQ(native.replay.length, 0) << "unlimited is the kernel's 0";
    effect.length = 0;
    ASSERT_TRUE(ToEvdevEffect(effect, native));
    EXPECT_EQ(native.replay.length, 1) << "zero long is not the kernel's forever";
    effect.length = 100000;
    effect.delay = 60000;
    ASSERT_TRUE(ToEvdevEffect(effect, native));
    EXPECT_EQ(native.replay.length, 0x7FFF);
    EXPECT_EQ(native.replay.delay, 0x7FFF);
    effect.button = 0;
    ASSERT_TRUE(ToEvdevEffect(effect, native));
    EXPECT_EQ(native.trigger.button, 0) << "no trigger";
}

TEST(LinuxEvdevHapticEffect, PeriodicEffectsCarryTheirWaveformAndPhaseAsAFractionOfATurn)
{
    const std::pair<HapticEffectType, int> waveforms[] = {
        {HapticEffectType::Sine, FF_SINE},        {HapticEffectType::Square, FF_SQUARE},
        {HapticEffectType::Triangle, FF_TRIANGLE}, {HapticEffectType::SawtoothUp, FF_SAW_UP},
        {HapticEffectType::SawtoothDown, FF_SAW_DOWN}};
    for (const auto& [type, waveform] : waveforms)
    {
        HapticEffect effect;
        effect.type = type;
        effect.period = 250;
        effect.magnitude = -20000;
        effect.offset = 300;
        effect.phase = 9000;  // A quarter turn.
        effect.fadeLevel = 500;
        ff_effect native{};
        ASSERT_TRUE(ToEvdevEffect(effect, native));
        EXPECT_EQ(native.type, FF_PERIODIC);
        EXPECT_EQ(native.u.periodic.waveform, waveform);
        EXPECT_EQ(native.u.periodic.period, 250);
        EXPECT_EQ(native.u.periodic.magnitude, -20000);
        EXPECT_EQ(native.u.periodic.offset, 300);
        EXPECT_EQ(native.u.periodic.phase, 0x4000);
        EXPECT_EQ(native.u.periodic.envelope.fade_level, 500);
    }
    HapticEffect wrapped;
    wrapped.type = HapticEffectType::Sine;
    wrapped.phase = 36000 + 18000;
    ff_effect native{};
    ASSERT_TRUE(ToEvdevEffect(wrapped, native));
    EXPECT_EQ(native.u.periodic.phase, 0x8000) << "a turn and a half is half a turn";
}

TEST(LinuxEvdevHapticEffect, ConditionsKeepTwoAxesAndARampItsLevels)
{
    HapticEffect spring;
    spring.type = HapticEffectType::Spring;
    spring.rightSaturation = {60000, 50000, 40000};
    spring.leftSaturation = {1, 2, 3};
    spring.rightCoefficient = {-100, 200, 300};
    spring.leftCoefficient = {400, -500, 600};
    spring.deadband = {10, 20, 30};
    spring.center = {-7, 8, 9};
    ff_effect native{};
    ASSERT_TRUE(ToEvdevEffect(spring, native));
    EXPECT_EQ(native.type, FF_SPRING);
    for (int axis = 0; axis < 2; ++axis)
    {
        EXPECT_EQ(native.u.condition[axis].right_saturation, spring.rightSaturation[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(native.u.condition[axis].left_saturation, spring.leftSaturation[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(native.u.condition[axis].right_coeff, spring.rightCoefficient[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(native.u.condition[axis].left_coeff, spring.leftCoefficient[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(native.u.condition[axis].deadband, spring.deadband[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(native.u.condition[axis].center, spring.center[static_cast<std::size_t>(axis)]);
    }
    for (const auto& [type, kernel] : {std::pair{HapticEffectType::Damper, FF_DAMPER},
                                       std::pair{HapticEffectType::Inertia, FF_INERTIA},
                                       std::pair{HapticEffectType::Friction, FF_FRICTION}})
    {
        HapticEffect condition;
        condition.type = type;
        ASSERT_TRUE(ToEvdevEffect(condition, native));
        EXPECT_EQ(native.type, kernel);
    }

    HapticEffect ramp;
    ramp.type = HapticEffectType::Ramp;
    ramp.rampStart = -30000;
    ramp.rampEnd = 30000;
    ramp.attackLength = 50;
    ASSERT_TRUE(ToEvdevEffect(ramp, native));
    EXPECT_EQ(native.type, FF_RAMP);
    EXPECT_EQ(native.u.ramp.start_level, -30000);
    EXPECT_EQ(native.u.ramp.end_level, 30000);
    EXPECT_EQ(native.u.ramp.envelope.attack_length, 50);
}

TEST(LinuxEvdevHapticEffect, ALeftRightEffectIsTheKernelsRumbleAtFullRange)
{
    HapticEffect effect;
    effect.type = HapticEffectType::LeftRight;
    effect.largeMagnitude = 0xFFFF;
    effect.smallMagnitude = 0x1234;
    effect.length = 400;
    effect.delay = 99;
    effect.button = 3;
    ff_effect native{};
    ASSERT_TRUE(ToEvdevEffect(effect, native));
    EXPECT_EQ(native.type, FF_RUMBLE);
    EXPECT_EQ(native.u.rumble.strong_magnitude, 0xFFFF) << "not clamped to half, as SDL3's Linux backend does";
    EXPECT_EQ(native.u.rumble.weak_magnitude, 0x1234);
    EXPECT_EQ(native.replay.length, 400);
    EXPECT_EQ(native.replay.delay, 0) << "the kernel's rumble has no delay or trigger";
    EXPECT_EQ(native.trigger.button, 0);
}

TEST(LinuxEvdevHapticEffect, ACustomWaveformIsNotUploaded)
{
    HapticEffect effect;
    effect.type = HapticEffectType::Custom;
    effect.customChannels = 1;
    effect.customPeriod = 10;
    effect.customData = {1, 2, 3};
    ff_effect native{};
    EXPECT_FALSE(ToEvdevEffect(effect, native));
}

TEST(LinuxEvdevHapticEffect, SimpleRumbleIsASineWhereThereIsOneElseBothMotors)
{
    const std::optional<HapticEffect> sine = EvdevRumbleEffect(Bits({FF_PERIODIC, FF_SINE, FF_RUMBLE}), 0.5f, 700);
    ASSERT_TRUE(sine.has_value());
    EXPECT_EQ(sine->type, HapticEffectType::Sine);
    EXPECT_EQ(sine->magnitude, 16383);
    EXPECT_EQ(sine->period, 1000);
    EXPECT_EQ(sine->length, 700u);

    const std::optional<HapticEffect> motors = EvdevRumbleEffect(Bits({FF_RUMBLE}), 1.0f, 200);
    ASSERT_TRUE(motors.has_value());
    EXPECT_EQ(motors->type, HapticEffectType::LeftRight);
    EXPECT_EQ(motors->largeMagnitude, 0xFFFF);
    EXPECT_EQ(motors->smallMagnitude, 0xFFFF);

    EXPECT_EQ(EvdevRumbleEffect(Bits({FF_RUMBLE}), 7.0f, 1)->largeMagnitude, 0xFFFF) << "clamped";
    EXPECT_EQ(EvdevRumbleEffect(Bits({FF_RUMBLE}), -1.0f, 1)->largeMagnitude, 0) << "clamped";
    EXPECT_FALSE(EvdevRumbleEffect(Bits({FF_CONSTANT, FF_SPRING}), 1.0f, 1).has_value())
        << "a wheel with neither a sine nor motors cannot rumble";
}

} // namespace

#endif // CNA_PLATFORM_HAVE_EVDEV
