// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Camera.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatformCamera.hpp"

#include <limits>
#include <utility>

namespace CNA::Devices
{
    namespace
    {
        CameraPosition ToPublicPosition(const CNA::Platform::PlatformCameraPosition position)
        {
            switch (position)
            {
            case CNA::Platform::PlatformCameraPosition::FrontFacing:
                return CameraPosition::FrontFacing;
            case CNA::Platform::PlatformCameraPosition::BackFacing:
                return CameraPosition::BackFacing;
            case CNA::Platform::PlatformCameraPosition::Unknown:
            default:
                return CameraPosition::Unknown;
            }
        }
    } // namespace

    bool Camera::getIsSupportedProperty()
    {
        return CNA::Platform::GetCurrentPlatform().GetCapabilities().camera;
    }

    std::vector<CameraDeviceInfo> Camera::getAvailableCamerasProperty()
    {
        CNA::Platform::IPlatformCameraProvider* provider =
            CNA::Platform::GetCurrentPlatform().GetCamera();
        if (provider == nullptr)
        {
            return {};
        }

        const std::vector<CNA::Platform::PlatformCameraInfo> platformCameras =
            provider->GetCameras();
        std::vector<CameraDeviceInfo> cameras;
        cameras.reserve(platformCameras.size());
        for (const CNA::Platform::PlatformCameraInfo& platformCamera : platformCameras)
        {
            CameraDeviceInfo camera;
            camera.Name = platformCamera.name;
            camera.Position = ToPublicPosition(platformCamera.position);
            cameras.push_back(std::move(camera));
        }
        return cameras;
    }

    Camera::Camera()
    {
        CNA::Platform::IPlatformCameraProvider* provider =
            CNA::Platform::GetCurrentPlatform().GetCamera();
        if (provider == nullptr)
        {
            return;
        }

        const std::vector<CNA::Platform::PlatformCameraInfo> cameras = provider->GetCameras();
        if (!cameras.empty())
        {
            camera_ = provider->OpenCamera(cameras.front().id);
        }
    }

    Camera::~Camera() = default;

    CameraState Camera::getStateProperty() const
    {
        if (camera_ == nullptr)
        {
            return CameraState::NotSupported;
        }

        switch (camera_->GetState())
        {
        case CNA::Platform::PlatformCameraState::Opening:
            return CameraState::Opening;
        case CNA::Platform::PlatformCameraState::Denied:
            return CameraState::Denied;
        case CNA::Platform::PlatformCameraState::Ready:
            return CameraState::Ready;
        case CNA::Platform::PlatformCameraState::Lost:
            return CameraState::Lost;
        }
        return CameraState::Lost;
    }

    int Camera::getFrameWidthProperty() const
    {
        return camera_ != nullptr ? camera_->GetFrameWidth() : 0;
    }

    int Camera::getFrameHeightProperty() const
    {
        return camera_ != nullptr ? camera_->GetFrameHeight() : 0;
    }

    bool Camera::TryAcquireFrame(Microsoft::Xna::Framework::Graphics::Texture2D& outTexture)
    {
        if (camera_ == nullptr)
        {
            return false;
        }

        CNA::Platform::PlatformCameraFrame frame;
        if (!camera_->TryAcquireFrame(frame) || frame.width <= 0 || frame.height <= 0)
        {
            return false;
        }
        if (outTexture.getWidthProperty() != frame.width ||
            outTexture.getHeightProperty() != frame.height)
        {
            return false;
        }

        const std::size_t width = static_cast<std::size_t>(frame.width);
        const std::size_t height = static_cast<std::size_t>(frame.height);
        if (width > std::numeric_limits<std::size_t>::max() / 4u)
        {
            return false;
        }
        const std::size_t rowBytes = width * 4u;
        if (height > std::numeric_limits<std::size_t>::max() / rowBytes ||
            frame.rgbaPixels.size() != rowBytes * height)
        {
            return false;
        }
        if (width > static_cast<std::size_t>(std::numeric_limits<int>::max()) / height)
        {
            return false;
        }

        outTexture.SetDataRGBA(
            frame.rgbaPixels.data(), static_cast<int>(width * height));
        return true;
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
