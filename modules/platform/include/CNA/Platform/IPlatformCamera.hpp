// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief Stable camera identity within one platform instance. Zero is invalid. */
    using CameraId = std::uint64_t;

    /** @brief Where a camera is mounted relative to its device, when known. */
    enum class PlatformCameraPosition
    {
        /** @brief The platform does not report a position. */
        Unknown,
        /** @brief Faces the user on the same side as the screen. */
        FrontFacing,
        /** @brief Faces away from the user. */
        BackFacing
    };

    /** @brief One connected camera returned by IPlatformCameraProvider::GetCameras. */
    struct PlatformCameraInfo
    {
        /** @brief Stable identity used to open the camera. */
        CameraId id = 0;
        /** @brief Human-readable device name. */
        std::string name;
        /** @brief Physical position, when the platform reports it. */
        PlatformCameraPosition position = PlatformCameraPosition::Unknown;
    };

    /** @brief Current state of one opened camera session. */
    enum class PlatformCameraState
    {
        /** @brief The device is open but permission or format negotiation is still pending. */
        Opening,
        /** @brief The user or platform policy denied access. */
        Denied,
        /** @brief Frames may be polled. */
        Ready,
        /** @brief The opened device became unusable. */
        Lost
    };

    /** @brief One tightly packed RGBA8 camera frame. */
    struct PlatformCameraFrame
    {
        /** @brief Width in pixels. */
        int width = 0;
        /** @brief Height in pixels. */
        int height = 0;
        /** @brief Tightly packed row-major RGBA8 bytes, exactly width * height * 4 bytes. */
        std::vector<std::uint8_t> rgbaPixels;
        /** @brief Capture timestamp in nanoseconds, or zero when the platform cannot report one. */
        std::uint64_t timestampNanoseconds = 0;
    };

    /** @brief One independently owned, non-blocking camera capture session. */
    class IPlatformCamera
    {
    public:
        /** @brief Closes the camera and destroys the session. */
        virtual ~IPlatformCamera() = default;

        /**
         * @brief Polls the current permission/device state.
         * @return The current state.
         */
        [[nodiscard]] virtual PlatformCameraState GetState() = 0;

        /**
         * @brief Gets the negotiated frame width.
         * @return The width, or zero while it is not yet known.
         */
        [[nodiscard]] virtual int GetFrameWidth() = 0;

        /**
         * @brief Gets the negotiated frame height.
         * @return The height, or zero while it is not yet known.
         */
        [[nodiscard]] virtual int GetFrameHeight() = 0;

        /**
         * @brief Polls one frame without blocking.
         *
         * The destination is changed only when this returns true. Implementations compact any
         * native row padding before returning, so consumers never need a native pitch value.
         *
         * @param frame Receives a tightly packed RGBA8 frame.
         * @return True when a new frame was available; false while not ready or between frames.
         */
        virtual bool TryAcquireFrame(PlatformCameraFrame& frame) = 0;
    };

    /** @brief Enumerates cameras and creates independent capture sessions. */
    class IPlatformCameraProvider
    {
    public:
        /** @brief Destroys the provider after every session it created has been released. */
        virtual ~IPlatformCameraProvider() = default;

        /**
         * @brief Enumerates currently connected cameras without opening them or requesting access.
         * @return Zero or more cameras in the platform's preferred order.
         */
        [[nodiscard]] virtual std::vector<PlatformCameraInfo> GetCameras() const = 0;

        /**
         * @brief Opens one camera for RGBA8 frame polling.
         * @param id A non-zero id returned by GetCameras.
         * @return The session, or null when the device disappeared or could not be opened.
         */
        [[nodiscard]] virtual std::unique_ptr<IPlatformCamera> OpenCamera(CameraId id) = 0;
    };

} // namespace CNA::Platform
