// SPDX-License-Identifier: MS-PL
//
// input_noxna.md N-013: haptic (force-feedback) tests using the injectable fake backend (no real
// FFB hardware). Covers enumeration, open/close (standalone, from-joystick, from-mouse), capabilities,
// the HapticEffectEXT -> SDL_HapticEffect conversion for every effect family, the effect lifecycle
// (create/update/run/stop/destroy/status), rumble, and gain/autocenter/pause/resume.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <limits>
#include <utility>

#include "CNA/Input/HapticCapabilities.hpp"
#include "CNA/Input/HapticDevice.hpp"
#include "CNA/Input/HapticEffect.hpp"
#include "CNA/Input/HapticInfo.hpp"
#include "CNA/Input/Haptics.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlHapticBackend.hpp"
#include "CNA/Platform/CannedHaptics.hpp"

#include "RecordingHapticBackend.hpp"

using CNA::Input::HapticCapabilitiesEXT;
using CNA::Input::HapticDevice;
using CNA::Input::HapticDirectionEXT;
using CNA::Input::HapticDirectionTypeEXT;
using CNA::Input::HapticEffectEXT;
using CNA::Input::HapticEffectTypeEXT;
using CNA::Input::HapticFeatureEXT;
using CNA::Input::HapticInfoEXT;
using CNA::Input::Haptics;
using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SetSdlHapticBackendForTests;
using CNA::Internal::Input::test_support::FakeHapticConfig;
using CNA::Internal::Input::test_support::RecordingHapticBackend;
using CNA::Platform::JoystickInfo;
using CNA::Platform::JoystickKind;
using CNA::Platform::Testing::CannedHapticsPlatform;
using CNA::Platform::Testing::PlatformTestDecorator;
using CNA::Platform::Testing::ScopedCurrentPlatform;

namespace
{
    struct FakeHapticTest : ::testing::Test
    {
        RecordingHapticBackend fake;
        CannedHapticsPlatform platform;
        ScopedCurrentPlatform installed{platform};

        void SetUp() override
        {
            InputManager::ResetAllForTests();
            SetSdlHapticBackendForTests(&fake);
            fake.onRegister = [this](const SDL_HapticID id, const FakeHapticConfig& config)
            {
                platform.haptics.Connect(
                    {static_cast<CNA::Platform::DeviceId>(id), config.name,
                     config.rumbleSupported});
            };
        }
        void TearDown() override
        {
            SetSdlHapticBackendForTests(nullptr);
            InputManager::ResetAllForTests();
        }
    };

    class NoHapticsPlatform final : public PlatformTestDecorator
    {
    public:
        CNA::Platform::IPlatformHaptics* GetHaptics() override { return nullptr; }

        CNA::Platform::PlatformCapabilities GetCapabilities() const override
        {
            CNA::Platform::PlatformCapabilities capabilities =
                PlatformTestDecorator::GetCapabilities();
            capabilities.haptics = false;
            return capabilities;
        }
    };

    FakeHapticConfig WheelConfig()
    {
        FakeHapticConfig cfg;
        cfg.name = "Test Wheel";
        cfg.features = SDL_HAPTIC_CONSTANT | SDL_HAPTIC_SINE | SDL_HAPTIC_SPRING | SDL_HAPTIC_GAIN;
        cfg.axisCount = 2;
        cfg.maxEffects = 16;
        cfg.maxEffectsPlaying = 4;
        cfg.rumbleSupported = true;
        cfg.effectSupported = true;
        return cfg;
    }
}

// --- enumeration ---

TEST_F(FakeHapticTest, GetHapticsEXTForwardsIdAndName)
{
    fake.Register(5, WheelConfig());
    const auto list = Haptics::GetHapticsEXT();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0], (HapticInfoEXT{5, "Test Wheel"}));
    EXPECT_EQ(platform.hapticSubsystemBalance, 0);
}

TEST_F(FakeHapticTest, GetHapticsEXTIsEmptyWhenNoneRegistered)
{
    EXPECT_TRUE(Haptics::GetHapticsEXT().empty());
}

TEST_F(FakeHapticTest, GetHapticsEXTDropsIdsThatDoNotFitTheFrozenPublicType)
{
    platform.haptics.Connect(
        {static_cast<CNA::Platform::DeviceId>(std::numeric_limits<std::uint32_t>::max()) + 1u,
         "Too wide", true});
    EXPECT_TRUE(Haptics::GetHapticsEXT().empty());
}

TEST(HapticsPlatformTest, MissingPlatformServiceReturnsNoDevicesAndCannotOpen)
{
    NoHapticsPlatform noHaptics;
    ScopedCurrentPlatform installed(noHaptics);

    EXPECT_TRUE(Haptics::GetHapticsEXT().empty());
    EXPECT_FALSE(Haptics::OpenEXT(5).IsOpenEXT());
}

// --- open / close (standalone) ---

TEST_F(FakeHapticTest, OpenEXTOpensRegisteredDevice)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);
    EXPECT_TRUE(device.IsOpenEXT());
    EXPECT_EQ(fake.openCount, 1);
    EXPECT_EQ(device.GetNameEXT(), "Test Wheel");
}

TEST_F(FakeHapticTest, OpenEXTOfUnknownIdFails)
{
    HapticDevice device = Haptics::OpenEXT(999);
    EXPECT_FALSE(device.IsOpenEXT());
    EXPECT_EQ(fake.openCount, 0);
}

TEST_F(FakeHapticTest, StandaloneRumbleRemainsAvailableWhenTheRicherEffectOpenFails)
{
    FakeHapticConfig cfg = WheelConfig();
    cfg.openFails = true;
    fake.Register(5, cfg);
    HapticDevice device = Haptics::OpenEXT(5);
    EXPECT_TRUE(device.IsOpenEXT());
    EXPECT_TRUE(device.GetCapabilitiesEXT().rumbleSupported);
    EXPECT_TRUE(device.InitRumbleEXT());
    EXPECT_TRUE(device.PlayRumbleEXT(0.5f, 10));
    EXPECT_EQ(fake.openCount, 0);
    EXPECT_EQ(fake.playRumbleCalls, 0);
    EXPECT_EQ(platform.haptics.playCalls, 1);
    EXPECT_EQ(device.CreateEffectEXT(HapticEffectEXT{}), -1);
}

TEST_F(FakeHapticTest, DisposeClosesTheDeviceAndIsIdempotent)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);
    device.Dispose();
    EXPECT_EQ(fake.closeCount, 1);
    EXPECT_EQ(platform.hapticSubsystemBalance, 0);
    EXPECT_FALSE(device.IsOpenEXT());
    EXPECT_NO_THROW(device.Dispose()); // idempotent
    EXPECT_EQ(fake.closeCount, 1);
}

TEST_F(FakeHapticTest, MoveConstructionTransfersOwnership)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);
    HapticDevice moved(std::move(device));
    EXPECT_TRUE(moved.IsOpenEXT());
    EXPECT_FALSE(device.IsOpenEXT()); // NOLINT(bugprone-use-after-move) -- intentional post-move check
    // Destroying the moved-from device must not double-close.
}

TEST_F(FakeHapticTest, MoveAssignmentClosesPreviousHandleAndTransfersOwnership)
{
    fake.Register(5, WheelConfig());
    FakeHapticConfig secondCfg = WheelConfig();
    secondCfg.name = "Second";
    fake.Register(6, secondCfg);

    HapticDevice a = Haptics::OpenEXT(5);
    HapticDevice b = Haptics::OpenEXT(6);
    a = std::move(b);

    EXPECT_EQ(fake.closeCount, 1) << "move-assign must close a's original handle";
    EXPECT_EQ(a.GetNameEXT(), "Second");
}

// --- capabilities ---

TEST_F(FakeHapticTest, CapabilitiesReportsFeaturesAxesEffectsAndRumble)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    const HapticCapabilitiesEXT caps = device.GetCapabilitiesEXT();
    EXPECT_TRUE(caps.isOpen);
    EXPECT_EQ(caps.name, "Test Wheel");
    EXPECT_EQ(caps.axisCount, 2);
    EXPECT_EQ(caps.maxEffects, 16);
    EXPECT_EQ(caps.maxEffectsPlaying, 4);
    EXPECT_TRUE(caps.rumbleSupported);
    EXPECT_NE((caps.features & HapticFeatureEXT::Constant), HapticFeatureEXT::None);
    EXPECT_NE((caps.features & HapticFeatureEXT::Sine), HapticFeatureEXT::None);
    EXPECT_NE((caps.features & HapticFeatureEXT::Spring), HapticFeatureEXT::None);
    EXPECT_NE((caps.features & HapticFeatureEXT::Gain), HapticFeatureEXT::None);
    EXPECT_EQ((caps.features & HapticFeatureEXT::Custom), HapticFeatureEXT::None);
}

TEST_F(FakeHapticTest, CapabilitiesIsDefaultWhenClosed)
{
    HapticDevice device;
    EXPECT_EQ(device.GetCapabilitiesEXT(), HapticCapabilitiesEXT{});
}

// --- rumble ---

TEST_F(FakeHapticTest, RumbleRoundTripsThroughBackend)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    EXPECT_TRUE(device.InitRumbleEXT());
    EXPECT_EQ(platform.haptics.initializeCalls, 1);
    EXPECT_EQ(fake.initRumbleCalls, 0);

    EXPECT_TRUE(device.PlayRumbleEXT(0.75f, 250));
    EXPECT_EQ(platform.haptics.playCalls, 1);
    EXPECT_EQ(fake.playRumbleCalls, 0);
    EXPECT_EQ(platform.haptics.lastId, 5u);
    EXPECT_FLOAT_EQ(platform.haptics.lastStrength, 0.75f);
    EXPECT_EQ(platform.haptics.lastDurationMilliseconds, 250u);

    EXPECT_TRUE(device.StopRumbleEXT());
    EXPECT_EQ(platform.haptics.stopCalls, 1);
    EXPECT_EQ(fake.stopRumbleCalls, 0);
}

TEST_F(FakeHapticTest, RumbleIsSafeFalseWhenClosed)
{
    HapticDevice device;
    EXPECT_FALSE(device.InitRumbleEXT());
    EXPECT_FALSE(device.PlayRumbleEXT(1.0f, 100));
    EXPECT_FALSE(device.StopRumbleEXT());
    EXPECT_EQ(fake.initRumbleCalls, 0);
}

TEST_F(FakeHapticTest, StandaloneRumbleRefusesAfterThePlatformIdDisconnects)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);
    ASSERT_TRUE(device.IsOpenEXT());

    platform.haptics.Disconnect(5);
    EXPECT_FALSE(device.InitRumbleEXT());
    EXPECT_FALSE(device.PlayRumbleEXT(0.5f, 20));
    EXPECT_FALSE(device.StopRumbleEXT());
    EXPECT_EQ(fake.initRumbleCalls, 0);
    EXPECT_EQ(fake.playRumbleCalls, 0);
    EXPECT_EQ(fake.stopRumbleCalls, 0);
}

// --- effect lifecycle ---

TEST_F(FakeHapticTest, EffectLifecycleCreateRunStatusStopDestroy)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Constant;
    effect.length = 500;
    effect.level = 12000;

    const int effectId = device.CreateEffectEXT(effect);
    EXPECT_GE(effectId, 0);
    EXPECT_EQ(fake.createEffectCalls, 1);

    EXPECT_FALSE(device.GetEffectStatusEXT(effectId)) << "not playing until Run";
    EXPECT_TRUE(device.RunEffectEXT(effectId));
    EXPECT_TRUE(device.GetEffectStatusEXT(effectId));

    EXPECT_TRUE(device.StopEffectEXT(effectId));
    EXPECT_FALSE(device.GetEffectStatusEXT(effectId));

    device.DestroyEffectEXT(effectId);
    EXPECT_EQ(fake.destroyEffectCalls, 1);
    EXPECT_FALSE(device.UpdateEffectEXT(effectId, effect)) << "destroyed effect id is no longer valid";
}

TEST_F(FakeHapticTest, StopAllEffectsStopsEveryPlayingEffect)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Constant;
    const int a = device.CreateEffectEXT(effect);
    const int b = device.CreateEffectEXT(effect);
    device.RunEffectEXT(a);
    device.RunEffectEXT(b);

    EXPECT_TRUE(device.StopAllEffectsEXT());
    EXPECT_FALSE(device.GetEffectStatusEXT(a));
    EXPECT_FALSE(device.GetEffectStatusEXT(b));
}

TEST_F(FakeHapticTest, IsEffectSupportedEXTForwardsBackendAnswer)
{
    FakeHapticConfig cfg = WheelConfig();
    cfg.effectSupported = false;
    fake.Register(5, cfg);
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Custom;
    EXPECT_FALSE(device.IsEffectSupportedEXT(effect));
}

TEST_F(FakeHapticTest, EffectMethodsAreSafeWhenClosed)
{
    HapticDevice device;
    HapticEffectEXT effect;
    EXPECT_EQ(device.CreateEffectEXT(effect), -1);
    EXPECT_FALSE(device.UpdateEffectEXT(0, effect));
    EXPECT_FALSE(device.RunEffectEXT(0));
    EXPECT_FALSE(device.StopEffectEXT(0));
    EXPECT_NO_THROW(device.DestroyEffectEXT(0));
    EXPECT_FALSE(device.GetEffectStatusEXT(0));
    EXPECT_FALSE(device.StopAllEffectsEXT());
    EXPECT_FALSE(device.IsEffectSupportedEXT(effect));
}

// --- effect-family -> SDL_HapticEffect conversion (the most bug-prone part) ---

TEST_F(FakeHapticTest, ConstantEffectMapsDirectionLevelAndEnvelope)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Constant;
    effect.direction.type = HapticDirectionTypeEXT::Cartesian;
    effect.direction.values = {1, 0, 0};
    effect.length = 1000;
    effect.delay = 5;
    effect.button = 1;
    effect.interval = 10;
    effect.level = 20000;
    effect.attackLength = 100;
    effect.attackLevel = 0;
    effect.fadeLength = 200;
    effect.fadeLevel = 0;

    (void)device.CreateEffectEXT(effect);
    const SDL_HapticEffect& sdl = fake.lastCreatedEffect;

    EXPECT_EQ(sdl.constant.type, static_cast<Uint16>(SDL_HAPTIC_CONSTANT));
    EXPECT_EQ(sdl.constant.direction.type, SDL_HAPTIC_CARTESIAN);
    EXPECT_EQ(sdl.constant.direction.dir[0], 1);
    EXPECT_EQ(sdl.constant.length, 1000u);
    EXPECT_EQ(sdl.constant.delay, 5);
    EXPECT_EQ(sdl.constant.button, 1);
    EXPECT_EQ(sdl.constant.interval, 10);
    EXPECT_EQ(sdl.constant.level, 20000);
    EXPECT_EQ(sdl.constant.attack_length, 100);
    EXPECT_EQ(sdl.constant.fade_length, 200);
}

TEST_F(FakeHapticTest, PeriodicEffectMapsWaveParameters)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Sine;
    effect.period = 100;
    effect.magnitude = 15000;
    effect.offset = 0;
    effect.phase = 9000;

    (void)device.CreateEffectEXT(effect);
    const SDL_HapticEffect& sdl = fake.lastCreatedEffect;

    EXPECT_EQ(sdl.periodic.type, static_cast<Uint16>(SDL_HAPTIC_SINE));
    EXPECT_EQ(sdl.periodic.period, 100);
    EXPECT_EQ(sdl.periodic.magnitude, 15000);
    EXPECT_EQ(sdl.periodic.phase, 9000);
}

TEST_F(FakeHapticTest, AllFivePeriodicWaveformsMapToDistinctSdlTypes)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    struct Case { HapticEffectTypeEXT ext; Uint16 sdl; };
    const Case cases[] = {
        {HapticEffectTypeEXT::Sine, SDL_HAPTIC_SINE},
        {HapticEffectTypeEXT::Square, SDL_HAPTIC_SQUARE},
        {HapticEffectTypeEXT::Triangle, SDL_HAPTIC_TRIANGLE},
        {HapticEffectTypeEXT::SawtoothUp, SDL_HAPTIC_SAWTOOTHUP},
        {HapticEffectTypeEXT::SawtoothDown, SDL_HAPTIC_SAWTOOTHDOWN},
    };
    for (const auto& c : cases)
    {
        HapticEffectEXT effect;
        effect.type = c.ext;
        (void)device.CreateEffectEXT(effect);
        EXPECT_EQ(fake.lastCreatedEffect.periodic.type, c.sdl) << "ext type " << static_cast<int>(c.ext);
    }
}

TEST_F(FakeHapticTest, RampEffectMapsStartAndEnd)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Ramp;
    effect.rampStart = -32768;
    effect.rampEnd = 32767;

    (void)device.CreateEffectEXT(effect);
    const SDL_HapticEffect& sdl = fake.lastCreatedEffect;

    EXPECT_EQ(sdl.ramp.type, static_cast<Uint16>(SDL_HAPTIC_RAMP));
    EXPECT_EQ(sdl.ramp.start, -32768);
    EXPECT_EQ(sdl.ramp.end, 32767);
}

TEST_F(FakeHapticTest, ConditionEffectMapsPerAxisArrays)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Spring;
    effect.rightSaturation = {1, 2, 3};
    effect.leftSaturation = {4, 5, 6};
    effect.rightCoefficient = {7, 8, 9};
    effect.leftCoefficient = {10, 11, 12};
    effect.deadband = {13, 14, 15};
    effect.center = {16, 17, 18};

    (void)device.CreateEffectEXT(effect);
    const SDL_HapticEffect& sdl = fake.lastCreatedEffect;

    EXPECT_EQ(sdl.condition.type, static_cast<Uint16>(SDL_HAPTIC_SPRING));
    EXPECT_EQ(sdl.condition.right_sat[0], 1);
    EXPECT_EQ(sdl.condition.right_sat[2], 3);
    EXPECT_EQ(sdl.condition.left_sat[1], 5);
    EXPECT_EQ(sdl.condition.right_coeff[2], 9);
    EXPECT_EQ(sdl.condition.left_coeff[0], 10);
    EXPECT_EQ(sdl.condition.deadband[1], 14);
    EXPECT_EQ(sdl.condition.center[2], 18);
}

TEST_F(FakeHapticTest, AllFourConditionTypesMapToDistinctSdlTypes)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    struct Case { HapticEffectTypeEXT ext; Uint16 sdl; };
    const Case cases[] = {
        {HapticEffectTypeEXT::Spring, SDL_HAPTIC_SPRING},
        {HapticEffectTypeEXT::Damper, SDL_HAPTIC_DAMPER},
        {HapticEffectTypeEXT::Inertia, SDL_HAPTIC_INERTIA},
        {HapticEffectTypeEXT::Friction, SDL_HAPTIC_FRICTION},
    };
    for (const auto& c : cases)
    {
        HapticEffectEXT effect;
        effect.type = c.ext;
        (void)device.CreateEffectEXT(effect);
        EXPECT_EQ(fake.lastCreatedEffect.condition.type, c.sdl) << "ext type " << static_cast<int>(c.ext);
    }
}

TEST_F(FakeHapticTest, LeftRightEffectMapsMotorMagnitudesOnly)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::LeftRight;
    effect.length = 300;
    effect.largeMagnitude = 40000;
    effect.smallMagnitude = 20000;

    (void)device.CreateEffectEXT(effect);
    const SDL_HapticEffect& sdl = fake.lastCreatedEffect;

    EXPECT_EQ(sdl.leftright.type, static_cast<Uint16>(SDL_HAPTIC_LEFTRIGHT));
    EXPECT_EQ(sdl.leftright.length, 300u);
    EXPECT_EQ(sdl.leftright.large_magnitude, 40000);
    EXPECT_EQ(sdl.leftright.small_magnitude, 20000);
}

TEST_F(FakeHapticTest, CustomEffectMapsChannelsPeriodAndSampleData)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Custom;
    effect.customChannels = 2;
    effect.customPeriod = 10;
    effect.customData = {1, 2, 3, 4, 5, 6}; // 2 channels * 3 samples

    (void)device.CreateEffectEXT(effect);
    const SDL_HapticEffect& sdl = fake.lastCreatedEffect;

    EXPECT_EQ(sdl.custom.type, static_cast<Uint16>(SDL_HAPTIC_CUSTOM));
    EXPECT_EQ(sdl.custom.channels, 2);
    EXPECT_EQ(sdl.custom.period, 10);
    EXPECT_EQ(sdl.custom.samples, 3);
    ASSERT_NE(sdl.custom.data, nullptr);
    EXPECT_EQ(sdl.custom.data[0], 1);
    EXPECT_EQ(sdl.custom.data[5], 6);
}

TEST_F(FakeHapticTest, CustomEffectWithEmptyDataHasNullDataPointer)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Custom;
    effect.customChannels = 1;

    (void)device.CreateEffectEXT(effect);
    EXPECT_EQ(fake.lastCreatedEffect.custom.data, nullptr);
    EXPECT_EQ(fake.lastCreatedEffect.custom.samples, 0);
}

TEST_F(FakeHapticTest, UpdateEffectEXTForwardsNewParameters)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    HapticEffectEXT effect;
    effect.type = HapticEffectTypeEXT::Constant;
    effect.level = 1000;
    const int effectId = device.CreateEffectEXT(effect);

    HapticEffectEXT updated = effect;
    updated.level = 5000;
    EXPECT_TRUE(device.UpdateEffectEXT(effectId, updated));
    EXPECT_EQ(fake.lastUpdatedEffect.constant.level, 5000);
}

// --- gain / autocenter / pause / resume ---

TEST_F(FakeHapticTest, GainAutocenterPauseResumeRoundTripThroughBackend)
{
    fake.Register(5, WheelConfig());
    HapticDevice device = Haptics::OpenEXT(5);

    EXPECT_TRUE(device.SetGainEXT(80));
    EXPECT_EQ(fake.lastGain, 80);

    EXPECT_TRUE(device.SetAutocenterEXT(50));
    EXPECT_EQ(fake.lastAutocenter, 50);

    EXPECT_TRUE(device.PauseEXT());
    EXPECT_EQ(fake.pauseCalls, 1);

    EXPECT_TRUE(device.ResumeEXT());
    EXPECT_EQ(fake.resumeCalls, 1);
}

TEST_F(FakeHapticTest, GainAutocenterPauseResumeAreSafeFalseWhenClosed)
{
    HapticDevice device;
    EXPECT_FALSE(device.SetGainEXT(50));
    EXPECT_FALSE(device.SetAutocenterEXT(50));
    EXPECT_FALSE(device.PauseEXT());
    EXPECT_FALSE(device.ResumeEXT());
}

// --- open from joystick / mouse ---

TEST_F(FakeHapticTest, OpenFromJoystickEXTOpensHapticForAConnectedJoystick)
{
    platform.joystick.Connect(JoystickInfo{10, "Wheel", JoystickKind::Wheel}, {});

    FakeHapticConfig cfg = WheelConfig();
    fake.joystickConfig = cfg;

    HapticDevice device = Haptics::OpenFromJoystickEXT(10);
    EXPECT_TRUE(device.IsOpenEXT());
    EXPECT_EQ(device.GetNameEXT(), "Test Wheel");
    EXPECT_EQ(fake.lastJoystickId, 10u);
}

TEST_F(FakeHapticTest, OpenFromJoystickEXTFailsForUnconnectedJoystick)
{
    HapticDevice device = Haptics::OpenFromJoystickEXT(12345);
    EXPECT_FALSE(device.IsOpenEXT());
    EXPECT_EQ(fake.openCount, 0);
}

TEST_F(FakeHapticTest, IsJoystickHapticEXTForwardsAndIsFalseWhenDisconnected)
{
    EXPECT_FALSE(Haptics::IsJoystickHapticEXT(999)) << "no such joystick connected";

    platform.joystick.Connect(JoystickInfo{10, "Wheel", JoystickKind::Wheel}, {});

    fake.joystickIsHaptic = true;
    EXPECT_TRUE(Haptics::IsJoystickHapticEXT(10));
    EXPECT_EQ(fake.lastJoystickId, 10u);
}

TEST_F(FakeHapticTest, OpenFromMouseEXTAndIsMouseHapticEXTForwardToBackend)
{
    EXPECT_FALSE(Haptics::IsMouseHapticEXT());
    fake.mouseIsHaptic = true;
    EXPECT_TRUE(Haptics::IsMouseHapticEXT());

    fake.mouseConfig = WheelConfig();
    HapticDevice device = Haptics::OpenFromMouseEXT();
    EXPECT_TRUE(device.IsOpenEXT());
    EXPECT_EQ(device.GetNameEXT(), "Test Wheel");
}

TEST_F(FakeHapticTest, MouseBackedRumbleStaysOnTheRichEffectSeam)
{
    fake.mouseConfig = WheelConfig();
    HapticDevice device = Haptics::OpenFromMouseEXT();
    ASSERT_TRUE(device.IsOpenEXT());

    EXPECT_TRUE(device.InitRumbleEXT());
    EXPECT_TRUE(device.PlayRumbleEXT(0.25f, 40));
    EXPECT_TRUE(device.StopRumbleEXT());
    EXPECT_EQ(fake.initRumbleCalls, 1);
    EXPECT_EQ(fake.playRumbleCalls, 1);
    EXPECT_EQ(fake.stopRumbleCalls, 1);
    EXPECT_EQ(platform.haptics.playCalls, 0);
}

// --- descriptor equality (no backend needed) ---

TEST(HapticInfoEXTTest, EqualityComparesIdAndName)
{
    EXPECT_EQ((HapticInfoEXT{1, "a"}), (HapticInfoEXT{1, "a"}));
    EXPECT_NE((HapticInfoEXT{1, "a"}), (HapticInfoEXT{2, "a"}));
    EXPECT_NE((HapticInfoEXT{1, "a"}), (HapticInfoEXT{1, "b"}));
}

TEST(HapticCapabilitiesEXTTest, EqualityComparesEveryField)
{
    HapticCapabilitiesEXT a;
    a.isOpen = true;
    a.name = "Wheel";
    a.features = HapticFeatureEXT::Constant | HapticFeatureEXT::Gain;
    a.axisCount = 2;
    a.maxEffects = 16;
    a.maxEffectsPlaying = 4;
    a.rumbleSupported = true;

    HapticCapabilitiesEXT b = a;
    EXPECT_EQ(a, b);
    b.maxEffectsPlaying = 5;
    EXPECT_NE(a, b);
}

TEST(HapticDirectionEXTTest, EqualityComparesTypeAndValues)
{
    HapticDirectionEXT a;
    a.type = HapticDirectionTypeEXT::Cartesian;
    a.values = {1, 2, 3};
    HapticDirectionEXT b = a;
    EXPECT_EQ(a, b);
    b.values[1] = 9;
    EXPECT_NE(a, b);
}

TEST(HapticEffectEXTTest, EqualityComparesEveryField)
{
    HapticEffectEXT a;
    a.type = HapticEffectTypeEXT::Custom;
    a.customData = {1, 2, 3};
    a.customChannels = 1;
    HapticEffectEXT b = a;
    EXPECT_EQ(a, b);
    b.customData[0] = 99;
    EXPECT_NE(a, b);
}

TEST(HapticFeatureEXTTest, BitOperatorsCombineMaskAndInvert)
{
    const auto combined = HapticFeatureEXT::Constant | HapticFeatureEXT::Gain;
    EXPECT_NE((combined & HapticFeatureEXT::Constant), HapticFeatureEXT::None);
    EXPECT_NE((combined & HapticFeatureEXT::Gain), HapticFeatureEXT::None);
    EXPECT_EQ((combined & HapticFeatureEXT::Sine), HapticFeatureEXT::None);

    auto mutableFlags = HapticFeatureEXT::None;
    mutableFlags |= HapticFeatureEXT::Sine;
    EXPECT_NE((mutableFlags & HapticFeatureEXT::Sine), HapticFeatureEXT::None);
    mutableFlags &= ~HapticFeatureEXT::Sine;
    EXPECT_EQ(mutableFlags, HapticFeatureEXT::None);
}
