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
