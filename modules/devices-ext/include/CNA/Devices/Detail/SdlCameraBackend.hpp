// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include "CNA/Devices/Detail/ICameraBackend.hpp"

struct SDL_Camera;

namespace CNA::Devices::Detail
{
    /**
     * @brief Real `ICameraBackend` implementation, opening the first camera device
     * `SDL_GetCameras()` reports and requesting RGBA8 frame delivery. The default
     * backend `Camera` uses outside of tests.
     *
     * Scope, per `docs/cna-devices-camera-design.md`'s recommended first-implementation
     * scope: single camera device (the first one `SDL_GetCameras()` returns, no device
     * selection), synchronous permission polling (no SDL event-queue integration —
     * `SDL_GetCameraPermissionState()` is re-checked on every `GetState()`/
     * `TryAcquireFrame()` call instead), RGBA-only (opening always requests
     * `SDL_PIXELFORMAT_RGBA32`; if the device ever reports back a different actual
     * format than requested — contradicting SDL's own "seamlessly converts" contract
     * for `SDL_OpenCamera()` — this backend reports `CameraState::NotSupported` for
     * that device rather than attempting its own pixel-format conversion).
     *
     * Never transitions to `CameraState::Lost` — detecting device loss requires SDL's
     * event queue, explicitly out of scope for this first implementation.
     */
    class SdlCameraBackend final : public ICameraBackend
    {
    public:
        SdlCameraBackend();
        ~SdlCameraBackend() override;

        SdlCameraBackend(const SdlCameraBackend&) = delete;
        SdlCameraBackend& operator=(const SdlCameraBackend&) = delete;

        [[nodiscard]] CameraState GetState() override;
        [[nodiscard]] int GetFrameWidth() const override;
        [[nodiscard]] int GetFrameHeight() const override;
        bool TryAcquireFrame(CameraFrame& outFrame) override;

    private:
        SDL_Camera* camera_ = nullptr;
        CameraState state_ = CameraState::NotSupported;
        int frameWidth_ = 0;
        int frameHeight_ = 0;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
