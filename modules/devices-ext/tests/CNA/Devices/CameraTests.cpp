// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <memory>

#include "CNA/Devices/Camera.hpp"
#include "CNA/Devices/Detail/ICameraBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using CNA::Devices::Camera;
using CNA::Devices::CameraState;
using CNA::Devices::Detail::CameraFrame;
using CNA::Devices::Detail::ICameraBackend;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    // Task DEVICES-CNA-012: the real backend (Detail::SdlCameraBackend) opens a
    // genuine camera device the instant it's constructed -- on most machines this
    // either fails loudly (no camera hardware in a CI container) or, worse, actually
    // requests OS camera permission and opens a real device if one happens to be
    // present. Every test below uses this fake instead, injected via Camera's constructor --
    // never the real backend.
    class FakeCameraBackend final : public ICameraBackend
    {
    public:
        CameraState State = CameraState::Ready;
        int FrameWidth = 4;
        int FrameHeight = 2;

        int TryAcquireFrameCallCount = 0;
        bool NextFrameAvailable = true;
        std::vector<std::uint8_t> NextFramePixels;

        [[nodiscard]] CameraState GetState() override
        {
            return State;
        }

        [[nodiscard]] int GetFrameWidth() const override
        {
            return FrameWidth;
        }

        [[nodiscard]] int GetFrameHeight() const override
        {
            return FrameHeight;
        }

        bool TryAcquireFrame(CameraFrame& outFrame) override
        {
            ++TryAcquireFrameCallCount;
            if (State != CameraState::Ready || !NextFrameAvailable)
            {
                return false;
            }

            outFrame.Width = FrameWidth;
            outFrame.Height = FrameHeight;
            if (NextFramePixels.empty())
            {
                outFrame.RgbaPixels.assign(static_cast<std::size_t>(FrameWidth) * FrameHeight * 4, 0x7F);
            }
            else
            {
                outFrame.RgbaPixels = NextFramePixels;
            }
            return true;
        }
    };
} // namespace

TEST(CameraTests, GetIsSupportedPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Camera::getIsSupportedProperty(); });
}

TEST(CameraTests, GetAvailableCamerasPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Camera::getAvailableCamerasProperty(); });
}

TEST(CameraTests, GetStatePropertyForwardsToInjectedFakeBackend)
{
    auto fake = std::make_unique<FakeCameraBackend>();
    fake->State = CameraState::Opening;
    Camera camera(std::move(fake));

    EXPECT_EQ(camera.getStateProperty(), CameraState::Opening);
}

TEST(CameraTests, GetFrameWidthAndHeightPropertiesForwardToInjectedFakeBackend)
{
    auto fake = std::make_unique<FakeCameraBackend>();
    fake->FrameWidth = 640;
    fake->FrameHeight = 480;
    Camera camera(std::move(fake));

    EXPECT_EQ(camera.getFrameWidthProperty(), 640);
    EXPECT_EQ(camera.getFrameHeightProperty(), 480);
}

TEST(CameraTests, TryAcquireFrameReturnsFalseWhenBackendHasNoFrame)
{
    auto fakeOwned = std::make_unique<FakeCameraBackend>();
    fakeOwned->NextFrameAvailable = false;
    Camera camera(std::move(fakeOwned));

    GraphicsDevice device;
    Texture2D texture(device, 4, 2);

    EXPECT_FALSE(camera.TryAcquireFrame(texture));
}

TEST(CameraTests, TryAcquireFrameReturnsFalseWhenNotReady)
{
    auto fakeOwned = std::make_unique<FakeCameraBackend>();
    fakeOwned->State = CameraState::Denied;
    Camera camera(std::move(fakeOwned));

    GraphicsDevice device;
    Texture2D texture(device, 4, 2);

    EXPECT_FALSE(camera.TryAcquireFrame(texture));
}

TEST(CameraTests, TryAcquireFrameReturnsFalseWhenTextureDimensionsDoNotMatch)
{
    auto fakeOwned = std::make_unique<FakeCameraBackend>();
    fakeOwned->FrameWidth = 4;
    fakeOwned->FrameHeight = 2;
    Camera camera(std::move(fakeOwned));

    GraphicsDevice device;
    Texture2D mismatched(device, 8, 8);

    EXPECT_FALSE(camera.TryAcquireFrame(mismatched));
}

TEST(CameraTests, TryAcquireFrameUploadsPixelsWhenReadyAndDimensionsMatch)
{
    auto fakeOwned = std::make_unique<FakeCameraBackend>();
    fakeOwned->FrameWidth = 4;
    fakeOwned->FrameHeight = 2;
    fakeOwned->NextFramePixels.assign(4u * 2u * 4u, 0x42);
    FakeCameraBackend* fake = fakeOwned.get();
    Camera camera(std::move(fakeOwned));

    GraphicsDevice device;
    Texture2D texture(device, 4, 2);

    EXPECT_TRUE(camera.TryAcquireFrame(texture));
    EXPECT_EQ(fake->TryAcquireFrameCallCount, 1);
}

#endif // CNA_DEVICES
