// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <memory>
#include <vector>

#include "CNA/Devices/CameraDeviceInfo.hpp"
#include "CNA/Devices/CameraState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace CNA::Platform
{
    class IPlatformCamera;
}

namespace CNA::Devices
{
    /**
     * @brief Captures video frames from a camera device into a
     * `Microsoft::Xna::Framework::Graphics::Texture2D`.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Enumeration, permission polling and capture
     * all use the selected platform's capability-gated camera provider.
     *
     * @note Poll-based, not callback-based, unlike `Microsoft::Devices::Sensors`' push-callback
     * model. Call `TryAcquireFrame()` once per `Game::Update()`/`Draw()`.
     *
     * @note First-implementation scope (see `docs/cna-devices-camera-design.md`):
     * a single camera device (the first one the platform reports — no device
     * selection yet), synchronous permission polling and RGBA8-only frame delivery.
     */
    class Camera
    {
    public:
        /**
         * @brief Gets whether camera capture is supported on the current platform
         * (a camera *backend* is available — not whether a physical device is
         * currently present or permission has been granted; see `getStateProperty()`
         * for that).
         *
         * @return true if the selected platform exposes a camera provider.
         */
        [[nodiscard]] static bool getIsSupportedProperty();

        /**
         * @brief Gets the list of camera devices currently available on this
         * platform.
         *
         * @return Zero or more available cameras. Enumeration alone never requests
         * permission or opens a device.
         */
        [[nodiscard]] static std::vector<CameraDeviceInfo> getAvailableCamerasProperty();

        /**
         * @brief Opens the first camera reported by the selected platform, when one is available.
         */
        Camera();

        /** @brief Closes the underlying camera device, if open. */
        ~Camera();

        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;

        /**
         * @brief Gets the current state of this camera's access to its device.
         *
         * Re-checks the platform's permission decision on every call (it may arrive
         * seconds, minutes, or hours after opening) — cheap enough to poll from
         * `Game::Update()` alongside `TryAcquireFrame()`.
         *
         * @return The current `CameraState`.
         */
        [[nodiscard]] CameraState getStateProperty() const;

        /**
         * @brief Gets the width, in pixels, of frames this camera produces.
         *
         * @return The frame width, or 0 if not yet known (e.g. `NotSupported`).
         */
        [[nodiscard]] int getFrameWidthProperty() const;

        /**
         * @brief Gets the height, in pixels, of frames this camera produces.
         *
         * @return The frame height, or 0 if not yet known (e.g. `NotSupported`).
         */
        [[nodiscard]] int getFrameHeightProperty() const;

        /**
         * @brief Polls for a new camera frame and, if one is available, uploads it
         * into `outTexture`.
         *
         * @param outTexture Destination texture; must already have been constructed
         * with dimensions exactly matching `getFrameWidthProperty()`/
         * `getFrameHeightProperty()`, or this call returns false without modifying
         * it.
         * @return true if a new frame was available and `outTexture` was updated;
         * false if no new frame was ready yet, the state is not `Ready`, or
         * `outTexture`'s dimensions do not match the camera's frame size.
         */
        bool TryAcquireFrame(Microsoft::Xna::Framework::Graphics::Texture2D& outTexture);

    private:
        std::unique_ptr<CNA::Platform::IPlatformCamera> camera_;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
