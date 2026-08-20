// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Platform/CannedHaptics.hpp"
#include "Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp"
#include "Microsoft/Devices/Detail/IVibrateBackend.hpp"
#include "Microsoft/Devices/VibrateController.hpp"

namespace
{
    using CNA::Platform::HapticInfo;
    using CNA::Platform::Testing::CannedHapticsPlatform;
    using CNA::Platform::Testing::ScopedCurrentPlatform;
    using Microsoft::Devices::Detail::DevicesShutdownCoordinator;
    using Microsoft::Devices::VibrateController;
    using System::TimeSpan;

    class InertVibrateBackend final : public Microsoft::Devices::Detail::IVibrateBackend
    {
    public:
        void Start(const TimeSpan&, float) override {}
        void Stop() override {}
        [[nodiscard]] bool IsSupported() override { return false; }
        [[nodiscard]] std::string GetDeviceName() override { return {}; }
        void StartLeftRight(float, float, const TimeSpan&) override {}
    };

    class RestorePlatformBackend
    {
    public:
        ~RestorePlatformBackend()
        {
            // Destroy the platform-bound backend while the scoped platform is
            // still alive, then leave a fresh lazy production backend behind.
            VibrateController::getDefaultProperty()->SetBackendForTesting(
                std::make_unique<InertVibrateBackend>());
            VibrateController::getDefaultProperty()->SetBackendForTesting(nullptr);
        }
    };

    class ResetShutdownState
    {
    public:
        ~ResetShutdownState() { DevicesShutdownCoordinator::ResetForTesting(); }
    };
}

TEST(PlatformVibrateMigrationTests, UsesPreferredPlatformDeviceForEveryPublicOperation)
{
    CannedHapticsPlatform platform;
    platform.haptics.Connect(HapticInfo{17, "Phone vibration motor", true});
    platform.haptics.defaultVibrationId = 17;
    ScopedCurrentPlatform current(platform);
    RestorePlatformBackend restore;

    VibrateController* controller = VibrateController::getDefaultProperty();
    controller->SetBackendForTesting(nullptr);

    EXPECT_TRUE(controller->getIsSupportedProperty());
    EXPECT_EQ(controller->getDeviceNameProperty(), "Phone vibration motor");

    controller->Start(TimeSpan::FromMilliseconds(250), 0.75f);
    EXPECT_EQ(platform.haptics.playCalls, 1);
    EXPECT_EQ(platform.haptics.lastId, 17u);
    EXPECT_FLOAT_EQ(platform.haptics.lastStrength, 0.75f);
    EXPECT_EQ(platform.haptics.lastDurationMilliseconds, 250u);

    controller->StartLeftRight(0.25f, 0.5f, TimeSpan::FromMilliseconds(125));
    EXPECT_EQ(platform.haptics.leftRightCalls, 1);
    EXPECT_FLOAT_EQ(platform.haptics.lastLargeMotor, 0.25f);
    EXPECT_FLOAT_EQ(platform.haptics.lastSmallMotor, 0.5f);
    EXPECT_EQ(platform.haptics.lastDurationMilliseconds, 125u);

    controller->Stop();
    EXPECT_EQ(platform.haptics.stopAllCalls, 1);
    EXPECT_EQ(platform.hapticSubsystemBalance, 1);
}

TEST(PlatformVibrateMigrationTests, MissingPreferredDeviceIsAnInertUnsupportedController)
{
    CannedHapticsPlatform platform;
    ScopedCurrentPlatform current(platform);
    RestorePlatformBackend restore;

    VibrateController* controller = VibrateController::getDefaultProperty();
    controller->SetBackendForTesting(nullptr);

    EXPECT_FALSE(controller->getIsSupportedProperty());
    EXPECT_TRUE(controller->getDeviceNameProperty().empty());
    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(10)));
    EXPECT_NO_THROW(controller->StartLeftRight(1.0f, 1.0f, TimeSpan::FromMilliseconds(10)));
    EXPECT_NO_THROW(controller->Stop());
    EXPECT_EQ(platform.haptics.playCalls, 0);
    EXPECT_EQ(platform.haptics.leftRightCalls, 0);
}

TEST(PlatformVibrateMigrationTests, ShutdownReleasesPlatformBeforeMakingControllerInert)
{
    CannedHapticsPlatform platform;
    platform.haptics.Connect(HapticInfo{17, "Phone vibration motor", true});
    platform.haptics.defaultVibrationId = 17;
    ScopedCurrentPlatform current(platform);
    ResetShutdownState reset;

    VibrateController* controller = VibrateController::getDefaultProperty();
    controller->SetBackendForTesting(nullptr);
    ASSERT_TRUE(controller->getIsSupportedProperty());
    ASSERT_EQ(platform.hapticSubsystemBalance, 1);

    DevicesShutdownCoordinator::Shutdown();

    EXPECT_TRUE(DevicesShutdownCoordinator::IsShutdown());
    EXPECT_EQ(platform.hapticSubsystemBalance, 0);
    EXPECT_FALSE(controller->getIsSupportedProperty());
    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(10)));
    EXPECT_EQ(platform.hapticSubsystemBalance, 0);
    EXPECT_NO_THROW(DevicesShutdownCoordinator::Shutdown());
}

// A backend must not call through a platform that is no longer installed.
//
// Found while measuring the engine layer on a second renderer (plan_modern.md Phase 16):
// `CnaTests --gtest_filter=*Instanc*` segfaulted at process *exit*, after every test had reported,
// on every renderer and with CNA_CNAEXT off. The backtrace was __run_exit_handlers ->
// ~VibrateController -> ~PlatformVibrateBackend -> ReleaseService -> a call through address 0:
// VibrateController's function-local static outlives the platform, and ReleaseService trusted the
// IPlatform* it captured when it acquired the subsystem.
//
// The shutdown coordinator was meant to cover exactly this and does not on its own -- its flag is
// process-global and DevicesShutdownCoordinatorTest::TearDown resets it, so a later exit finds it
// false again.
//
// This pins the guard rather than the crash. A crash test would need ASan to be reliable; what is
// deterministic is the guard's observable consequence -- with the current platform cleared,
// destroying the backend must leave the old platform's subsystem balance untouched, because
// nothing should have called into that platform at all.
TEST(PlatformVibrateMigrationTests, DestroyingABackendDoesNotCallAPlatformThatIsNoLongerInstalled)
{
    CannedHapticsPlatform platform;
    platform.haptics.Connect(HapticInfo{31, "Rumble pack", true});
    platform.haptics.defaultVibrationId = 31;
    ResetShutdownState reset;

    VibrateController* controller = VibrateController::getDefaultProperty();
    {
        ScopedCurrentPlatform current(platform);
        controller->SetBackendForTesting(nullptr);
        ASSERT_TRUE(controller->getIsSupportedProperty());
        // Without this the rest measures nothing: a backend that never took the subsystem would
        // trivially leave the balance alone.
        ASSERT_EQ(platform.hapticSubsystemBalance, 1);
    }

    // The platform object is still alive here -- a test cannot destroy it and then observe it --
    // but it is no longer the installed one, which is the condition the guard actually checks and
    // the condition that holds at process exit.
    CNA::Platform::SetCurrentPlatform(nullptr);
    controller->SetBackendForTesting(std::make_unique<InertVibrateBackend>());

    EXPECT_EQ(platform.hapticSubsystemBalance, 1)
        << "the backend released a subsystem on a platform that is no longer installed; at process "
           "exit that platform is destroyed and the same call is a segfault";

    controller->SetBackendForTesting(nullptr);
}
