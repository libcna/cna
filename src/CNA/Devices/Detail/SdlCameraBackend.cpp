// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Detail/SdlCameraBackend.hpp"

#ifdef CNA_DEVICES

#include <cstring>

#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_stdinc.h>

namespace
{
    using CNA::Devices::CameraState;

    // Builds the SDL_CameraSpec this backend always requests: RGBA8, and whichever
    // width/height/framerate the device itself already advertises as supported (safer
    // than guessing arbitrary values) when that information is available.
    SDL_CameraSpec MakeRequestedSpec(const SDL_CameraSpec* deviceSupportedSpec)
    {
        SDL_CameraSpec spec{};
        if (deviceSupportedSpec != nullptr)
        {
            spec = *deviceSupportedSpec;
        }
        spec.format = SDL_PIXELFORMAT_RGBA32;
        return spec;
    }
} // namespace

namespace CNA::Devices::Detail
{
    SdlCameraBackend::SdlCameraBackend()
    {
        int cameraCount = 0;
        SDL_CameraID* cameraIds = SDL_GetCameras(&cameraCount);
        if (cameraIds == nullptr || cameraCount <= 0)
        {
            SDL_free(cameraIds);
            state_ = CameraState::NotSupported;
            return;
        }
        const SDL_CameraID cameraId = cameraIds[0];
        SDL_free(cameraIds);

        // SDL3's own doc comment for SDL_GetCameraSupportedFormats() states it is
        // legal for a camera to report an empty list -- documented to actually happen
        // on Emscripten, which won't describe a camera's capabilities at all until
        // it's been opened. An empty list here does not mean "no camera" (already
        // ruled out above); it means "ask SDL to pick, then request RGBA anyway".
        int formatCount = 0;
        SDL_CameraSpec** supportedFormats = SDL_GetCameraSupportedFormats(cameraId, &formatCount);
        const SDL_CameraSpec* firstSupported =
            (supportedFormats != nullptr && formatCount > 0) ? supportedFormats[0] : nullptr;
        const SDL_CameraSpec requestedSpec = MakeRequestedSpec(firstSupported);
        SDL_free(supportedFormats);

        camera_ = SDL_OpenCamera(cameraId, &requestedSpec);
        if (camera_ == nullptr)
        {
            state_ = CameraState::NotSupported;
            return;
        }

        SDL_CameraSpec actualSpec{};
        if (!SDL_GetCameraFormat(camera_, &actualSpec) || actualSpec.format != SDL_PIXELFORMAT_RGBA32)
        {
            // Contradicts SDL_OpenCamera()'s own documented "seamlessly converts to
            // the requested format" contract -- rather than attempting our own pixel
            // conversion (explicitly out of scope for this first implementation, per
            // docs/cna-devices-camera-design.md), treat this device as unusable.
            SDL_CloseCamera(camera_);
            camera_ = nullptr;
            state_ = CameraState::NotSupported;
            return;
        }

        frameWidth_ = actualSpec.width;
        frameHeight_ = actualSpec.height;
        state_ = CameraState::Opening;
    }

    SdlCameraBackend::~SdlCameraBackend()
    {
        if (camera_ != nullptr)
        {
            SDL_CloseCamera(camera_);
        }
    }

    CameraState SdlCameraBackend::GetState()
    {
        if (state_ != CameraState::Opening || camera_ == nullptr)
        {
            return state_;
        }

        // Permission may be granted seconds, minutes, or hours after opening (SDL's
        // own doc comment) -- re-checked every call rather than cached, since this
        // backend deliberately does not integrate with SDL's event queue (see this
        // class's own doc comment).
        switch (SDL_GetCameraPermissionState(camera_))
        {
        case SDL_CAMERA_PERMISSION_STATE_APPROVED:
            state_ = CameraState::Ready;
            break;
        case SDL_CAMERA_PERMISSION_STATE_DENIED:
            state_ = CameraState::Denied;
            break;
        case SDL_CAMERA_PERMISSION_STATE_PENDING:
        default:
            break;
        }
        return state_;
    }

    int SdlCameraBackend::GetFrameWidth() const
    {
        return frameWidth_;
    }

    int SdlCameraBackend::GetFrameHeight() const
    {
        return frameHeight_;
    }

    bool SdlCameraBackend::TryAcquireFrame(CameraFrame& outFrame)
    {
        if (GetState() != CameraState::Ready)
        {
            return false;
        }

        Uint64 timestampNs = 0;
        SDL_Surface* surface = SDL_AcquireCameraFrame(camera_, &timestampNs);
        if (surface == nullptr)
        {
            // SDL3's own doc comment is explicit that this means either "no new
            // frame yet" (normal) or a real failure -- both return NULL, and the
            // caller is expected to rely on GetState()/permission events to
            // distinguish a real problem, not this return value alone.
            return false;
        }

        outFrame.Width = surface->w;
        outFrame.Height = surface->h;
        const std::size_t tightRowBytes = static_cast<std::size_t>(surface->w) * 4;
        outFrame.RgbaPixels.resize(tightRowBytes * static_cast<std::size_t>(surface->h));

        if (static_cast<std::size_t>(surface->pitch) == tightRowBytes)
        {
            std::memcpy(outFrame.RgbaPixels.data(), surface->pixels, outFrame.RgbaPixels.size());
        }
        else
        {
            // Surface rows are padded beyond the tight width*4 stride
            // Texture2D::SetDataRGBA() assumes -- compact them.
            const auto* srcBytes = static_cast<const std::uint8_t*>(surface->pixels);
            for (int row = 0; row < surface->h; ++row)
            {
                std::memcpy(
                    outFrame.RgbaPixels.data() + static_cast<std::size_t>(row) * tightRowBytes,
                    srcBytes + static_cast<std::size_t>(row) * surface->pitch,
                    tightRowBytes);
            }
        }

        SDL_ReleaseCameraFrame(camera_, surface);
        return true;
    }
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
