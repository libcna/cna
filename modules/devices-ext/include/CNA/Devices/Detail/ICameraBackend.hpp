// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <cstdint>
#include <vector>

#include "CNA/Devices/CameraState.hpp"

namespace CNA::Devices::Detail
{
    /**
     * @brief One captured camera frame: tightly-packed RGBA8 pixels, `Width * Height * 4`
     * bytes, row-major, no padding between rows.
     */
    struct CameraFrame
    {
        /** @brief Frame width, in pixels. */
        int Width = 0;
        /** @brief Frame height, in pixels. */
        int Height = 0;
        /** @brief Tightly-packed RGBA8 pixel data, `Width * Height * 4` bytes. */
        std::vector<std::uint8_t> RgbaPixels;
    };

    /**
     * @brief Native camera backend interface (Task DEVICES-CNA-012).
     *
     * `Camera` calls through this interface instead of calling SDL3's
     * `SDL_OpenCamera()`/`SDL_AcquireCameraFrame()` family directly. This exists for
     * the same reason `Microsoft::Devices::Detail::IVibrateBackend`/`ICompassBackend`
     * and the former dialog/tray test backends did: the real backend (`SdlCameraBackend`) has a
     * genuinely uncontainable side
     * effect an automated test cannot safely trigger — opening a real camera device,
     * which on most platforms either fails loudly (no camera hardware in a CI
     * container) or, worse, actually requests OS camera permission and opens a real
     * device if one happens to be present. Unlike `FileDialog`'s process-wide
     * swappable backend, `Camera` needs constructor-injection (mirroring
     * `SystemTray`'s pattern): the real backend's device-opening call runs
     * immediately when constructed, not deferred to a later call.
     */
    class ICameraBackend
    {
    public:
        virtual ~ICameraBackend() = default;

        /** @brief See `Camera::getStateProperty()` for the full contract. */
        [[nodiscard]] virtual CameraState GetState() = 0;

        /** @brief See `Camera::getFrameWidthProperty()`. 0 if not yet known. */
        [[nodiscard]] virtual int GetFrameWidth() const = 0;

        /** @brief See `Camera::getFrameHeightProperty()`. 0 if not yet known. */
        [[nodiscard]] virtual int GetFrameHeight() const = 0;

        /** @brief See `Camera::TryAcquireFrame()` for the full contract. */
        virtual bool TryAcquireFrame(CameraFrame& outFrame) = 0;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
