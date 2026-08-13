// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/Camera.hpp"
#include "CNA/Platform/CannedCamera.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <memory>
#include <vector>

namespace {

    using CNA::Devices::Camera;
    using CNA::Devices::CameraPosition;
    using CNA::Devices::CameraState;
    using CNA::Platform::IPlatform;
    using CNA::Platform::PlatformCameraInfo;
    using CNA::Platform::PlatformCameraPosition;
    using CNA::Platform::PlatformCameraState;
    using CNA::Platform::PlatformFactory;
    using CNA::Platform::Testing::CannedCameraPlatform;
    using CNA::Platform::Testing::ScopedCurrentPlatform;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

} // namespace

TEST(CameraTests, IsSupportedReflectsTheSelectedPlatformCapability)
{
    {
        CannedCameraPlatform platform;
        ScopedCurrentPlatform current(platform);
        EXPECT_TRUE(Camera::getIsSupportedProperty());
    }
    {
        std::unique_ptr<IPlatform> platform = PlatformFactory::Create("Headless");
        ScopedCurrentPlatform current(*platform);
        EXPECT_FALSE(Camera::getIsSupportedProperty());
    }
}

TEST(CameraTests, EnumerationMapsNamesAndPositionsWithoutOpeningADevice)
{
    CannedCameraPlatform platform;
    platform.Canned().cameras = {
        PlatformCameraInfo{11, "Front", PlatformCameraPosition::FrontFacing},
        PlatformCameraInfo{12, "Back", PlatformCameraPosition::BackFacing},
        PlatformCameraInfo{13, "Mystery", PlatformCameraPosition::Unknown}};
    ScopedCurrentPlatform current(platform);

    const std::vector<CNA::Devices::CameraDeviceInfo> cameras =
        Camera::getAvailableCamerasProperty();

    ASSERT_EQ(cameras.size(), 3u);
    EXPECT_EQ(cameras[0].Name, "Front");
    EXPECT_EQ(cameras[0].Position, CameraPosition::FrontFacing);
    EXPECT_EQ(cameras[1].Position, CameraPosition::BackFacing);
    EXPECT_EQ(cameras[2].Position, CameraPosition::Unknown);
    EXPECT_EQ(platform.Canned().state->lastOpenedId, 0u);
}

TEST(CameraTests, UnsupportedPlatformEnumeratesNothingAndConstructsAnInertCamera)
{
    std::unique_ptr<IPlatform> platform = PlatformFactory::Create("Headless");
    ScopedCurrentPlatform current(*platform);

    EXPECT_TRUE(Camera::getAvailableCamerasProperty().empty());
    Camera camera;
    EXPECT_EQ(camera.getStateProperty(), CameraState::NotSupported);
    EXPECT_EQ(camera.getFrameWidthProperty(), 0);
    EXPECT_EQ(camera.getFrameHeightProperty(), 0);
}

TEST(CameraTests, ConstructorOpensTheFirstEnumeratedCamera)
{
    CannedCameraPlatform platform;
    platform.Canned().cameras = {
        PlatformCameraInfo{41, "First", PlatformCameraPosition::Unknown},
        PlatformCameraInfo{42, "Second", PlatformCameraPosition::Unknown}};
    ScopedCurrentPlatform current(platform);

    Camera camera;

    EXPECT_EQ(platform.Canned().state->lastOpenedId, 41u);
}

TEST(CameraTests, NoDeviceOrOpenFailureReportsNotSupported)
{
    CannedCameraPlatform platform;
    platform.Canned().cameras.clear();
    ScopedCurrentPlatform current(platform);

    Camera noDevice;
    EXPECT_EQ(noDevice.getStateProperty(), CameraState::NotSupported);

    platform.Canned().cameras = {
        PlatformCameraInfo{7, "Unavailable", PlatformCameraPosition::Unknown}};
    platform.Canned().openSucceeds = false;
    Camera failedOpen;
    EXPECT_EQ(failedOpen.getStateProperty(), CameraState::NotSupported);
}

TEST(CameraTests, PlatformSessionStatesMapToThePublicVocabulary)
{
    CannedCameraPlatform platform;
    ScopedCurrentPlatform current(platform);
    Camera camera;

    platform.Canned().state->state = PlatformCameraState::Opening;
    EXPECT_EQ(camera.getStateProperty(), CameraState::Opening);
    platform.Canned().state->state = PlatformCameraState::Denied;
    EXPECT_EQ(camera.getStateProperty(), CameraState::Denied);
    platform.Canned().state->state = PlatformCameraState::Ready;
    EXPECT_EQ(camera.getStateProperty(), CameraState::Ready);
    platform.Canned().state->state = PlatformCameraState::Lost;
    EXPECT_EQ(camera.getStateProperty(), CameraState::Lost);
}

TEST(CameraTests, FrameDimensionsComeFromThePlatformSession)
{
    CannedCameraPlatform platform;
    platform.Canned().state->frameWidth = 640;
    platform.Canned().state->frameHeight = 480;
    ScopedCurrentPlatform current(platform);
    Camera camera;

    EXPECT_EQ(camera.getFrameWidthProperty(), 640);
    EXPECT_EQ(camera.getFrameHeightProperty(), 480);
}

TEST(CameraTests, DestructorClosesTheOwnedSession)
{
    CannedCameraPlatform platform;
    ScopedCurrentPlatform current(platform);

    {
        Camera camera;
        EXPECT_FALSE(platform.Canned().state->sessionDestroyed);
    }
    EXPECT_TRUE(platform.Canned().state->sessionDestroyed);
}

TEST(CameraTests, TryAcquireFrameReturnsFalseWhenNoFrameIsReady)
{
    CannedCameraPlatform platform;
    platform.Canned().state->frameAvailable = false;
    ScopedCurrentPlatform current(platform);
    Camera camera;
    GraphicsDevice device;
    Texture2D texture(device, 4, 2);

    EXPECT_FALSE(camera.TryAcquireFrame(texture));
    EXPECT_EQ(platform.Canned().state->acquireCallCount, 1);
}

TEST(CameraTests, TryAcquireFrameReturnsFalseWhenTheSessionIsNotReady)
{
    CannedCameraPlatform platform;
    platform.Canned().state->state = PlatformCameraState::Denied;
    ScopedCurrentPlatform current(platform);
    Camera camera;
    GraphicsDevice device;
    Texture2D texture(device, 4, 2);

    EXPECT_FALSE(camera.TryAcquireFrame(texture));
}

TEST(CameraTests, TryAcquireFrameRejectsTextureDimensionMismatch)
{
    CannedCameraPlatform platform;
    ScopedCurrentPlatform current(platform);
    Camera camera;
    GraphicsDevice device;
    Texture2D texture(device, 8, 8);

    EXPECT_FALSE(camera.TryAcquireFrame(texture));
}

TEST(CameraTests, TryAcquireFrameRejectsMalformedPlatformPixelCount)
{
    CannedCameraPlatform platform;
    platform.Canned().state->nextPixels.assign(7, 0x42);
    ScopedCurrentPlatform current(platform);
    Camera camera;
    GraphicsDevice device;
    Texture2D texture(device, 4, 2);

    EXPECT_FALSE(camera.TryAcquireFrame(texture));
}

TEST(CameraTests, TryAcquireFrameUploadsValidRgbaPixels)
{
    CannedCameraPlatform platform;
    platform.Canned().state->nextPixels.assign(4u * 2u * 4u, 0x42);
    ScopedCurrentPlatform current(platform);
    Camera camera;
    GraphicsDevice device;
    Texture2D texture(device, 4, 2);

    EXPECT_TRUE(camera.TryAcquireFrame(texture));
    EXPECT_EQ(platform.Canned().state->acquireCallCount, 1);
}

#endif // CNA_DEVICES
