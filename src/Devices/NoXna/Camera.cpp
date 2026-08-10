// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Camera.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_camera.h>

#include "CNA/Devices/Detail/SdlCameraBackend.hpp"

namespace CNA::Devices
{
    bool Camera::getIsSupportedProperty()
    {
        return SDL_GetNumCameraDrivers() > 0;
    }

    std::vector<CameraDeviceInfo> Camera::getAvailableCamerasProperty()
    {
        std::vector<CameraDeviceInfo> cameras;

        int count = 0;
        SDL_CameraID* ids = SDL_GetCameras(&count);
        if (ids == nullptr)
        {
            return cameras;
        }

        cameras.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            CameraDeviceInfo info;
            const char* name = SDL_GetCameraName(ids[i]);
            info.Name = (name != nullptr) ? name : "";

            switch (SDL_GetCameraPosition(ids[i]))
            {
            case SDL_CAMERA_POSITION_FRONT_FACING:
                info.Position = CameraPosition::FrontFacing;
                break;
            case SDL_CAMERA_POSITION_BACK_FACING:
                info.Position = CameraPosition::BackFacing;
                break;
            case SDL_CAMERA_POSITION_UNKNOWN:
            default:
                info.Position = CameraPosition::Unknown;
                break;
            }

            cameras.push_back(std::move(info));
        }

        SDL_free(ids);
        return cameras;
    }

    Camera::Camera() : backend_(std::make_unique<Detail::SdlCameraBackend>())
    {
    }

    Camera::Camera(std::unique_ptr<Detail::ICameraBackend> backend) : backend_(std::move(backend))
    {
    }

    Camera::~Camera() = default;

    CameraState Camera::getStateProperty() const
    {
        return backend_->GetState();
    }

    int Camera::getFrameWidthProperty() const
    {
        return backend_->GetFrameWidth();
    }

    int Camera::getFrameHeightProperty() const
    {
        return backend_->GetFrameHeight();
    }

    bool Camera::TryAcquireFrame(Microsoft::Xna::Framework::Graphics::Texture2D& outTexture)
    {
        Detail::CameraFrame frame;
        if (!backend_->TryAcquireFrame(frame))
        {
            return false;
        }

        if (outTexture.getWidthProperty() != frame.Width || outTexture.getHeightProperty() != frame.Height)
        {
            return false;
        }

        outTexture.SetDataRGBA(frame.RgbaPixels.data(), frame.Width * frame.Height);
        return true;
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
