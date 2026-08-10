// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <memory>
#include <vector>

#include "CNA/Devices/CameraDeviceInfo.hpp"
#include "CNA/Devices/CameraState.hpp"
#include "CNA/Devices/Detail/ICameraBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace CNA::Devices
{
    /**
     * @brief Captures video frames from a camera device into a
     * `Microsoft::Xna::Framework::Graphics::Texture2D`.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Calls through
     * `Detail::ICameraBackend` (default: `Detail::SdlCameraBackend`, backed by SDL3's
     * `SDL_OpenCamera()`/`SDL_AcquireCameraFrame()` family,
     * `third_party/SDL/include/SDL3/SDL_camera.h`) rather than calling SDL3 directly —
     * see the constructor overload taking a backend for why this matters more here
     * than for most other `CNA::Devices` classes.
     *
     * @note Poll-based, not callback-based, unlike `Microsoft::Devices::Sensors`'
     * push-callback model: `SDL_AcquireCameraFrame()` itself is poll-based, so
     * `TryAcquireFrame()` matches that shape directly instead of forcing an
     * artificial event model on top of it. Call it once per `Game::Update()`/`Draw()`.
     *
     * @note First-implementation scope (see `docs/cna-devices-camera-design.md`):
     * a single camera device (the first one the platform reports — no device
     * selection yet), synchronous permission polling (no SDL event-queue
     * integration), RGBA8-only frame delivery. `CameraState::Lost` is never reached
     * by the current backend.
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
         * @return true if at least one SDL3 camera driver is compiled in.
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
         * @brief Opens the first available camera device using the real SDL3
         * backend.
         */
        Camera();

        /**
         * @brief Test-only constructor: opens with a caller-supplied backend instead
         * of the real `Detail::SdlCameraBackend`.
         *
         * Unlike `FileDialog`'s process-wide `SetBackendForTesting()`, the backend is
         * injected here, at construction — mirroring `SystemTray`'s pattern — because
         * the real backend's device-opening call runs immediately when constructed,
         * not deferred to a later call; a post-construction swap would be too late to
         * prevent the real backend from ever running.
         *
         * @param backend Backend to use instead of `Detail::SdlCameraBackend`.
         */
        explicit Camera(std::unique_ptr<Detail::ICameraBackend> backend);

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
        std::unique_ptr<Detail::ICameraBackend> backend_;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
