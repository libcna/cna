// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformCamera.hpp"

#include <atomic>
#include <cstdint>

namespace CNA::Platform::Sdl3 {

    /**
     * @brief Records the SDL camera-device events addressed to one open camera.
     *
     * SDL reports an unplugged camera and a revoked permission only as events
     * (SDL_EVENT_CAMERA_DEVICE_REMOVED / _DENIED); after a removal it keeps handing out blank
     * frames. An event watch -- called synchronously on whichever thread pushes the event,
     * including SDL's camera thread -- latches those two facts for the session to read on its next
     * poll, without a per-frame enumeration (CNA-FIX-L18).
     */
    class Sdl3CameraEventLatch final
    {
    public:
        /**
         * @brief Starts watching for events about one camera.
         * @param cameraId The SDL camera instance id the events must name.
         */
        explicit Sdl3CameraEventLatch(std::uint32_t cameraId);

        /** @brief Stops watching; no event is delivered to this latch afterwards. */
        ~Sdl3CameraEventLatch();

        Sdl3CameraEventLatch(const Sdl3CameraEventLatch&) = delete;
        Sdl3CameraEventLatch& operator=(const Sdl3CameraEventLatch&) = delete;

        /** @brief Gets whether SDL reported this camera removed. @return True once removed. */
        [[nodiscard]] bool Removed() const noexcept;

        /** @brief Gets whether SDL reported this camera's permission denied. @return True once denied. */
        [[nodiscard]] bool Denied() const noexcept;

        /** @brief Gets whether the event watch could be installed. @return True when watching. */
        [[nodiscard]] bool Watching() const noexcept;

        /**
         * @brief Records one SDL event, ignoring every event that is not about this camera.
         * @param eventType The SDL event type.
         * @param which The camera instance id the event names.
         */
        void Record(std::uint32_t eventType, std::uint32_t which) noexcept;

    private:
        std::uint32_t cameraId_;
        std::atomic<bool> removed_{false};
        std::atomic<bool> denied_{false};
        bool watching_ = false;
    };

    /** @brief SDL3-backed camera enumeration and RGBA8 capture sessions. */
    class Sdl3CameraProvider final : public IPlatformCameraProvider
    {
    public:
        /**
         * @brief Gets whether this SDL build contains at least one camera driver.
         * @return True when camera operations have a native implementation.
         */
        [[nodiscard]] static bool IsSupported();

        /** @brief Enumerates connected SDL cameras. @return Their stable descriptions. */
        [[nodiscard]] std::vector<PlatformCameraInfo> GetCameras() const override;

        /**
         * @brief Opens an SDL camera and requests RGBA8 conversion.
         * @param id The SDL camera instance id in CNA's width-independent representation.
         * @return The session, or null when it cannot be opened.
         */
        [[nodiscard]] std::unique_ptr<IPlatformCamera> OpenCamera(CameraId id) override;
    };

} // namespace CNA::Platform::Sdl3
