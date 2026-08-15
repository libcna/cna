// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding: a platform whose camera provider returns scripted RGBA8 frames.

#include "CNA/Platform/IPlatformCamera.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace CNA::Platform::Testing {

    /** @brief Script and observations shared by all canned sessions from one provider. */
    struct CannedCameraState
    {
        /** @brief Current state reported by opened sessions. */
        PlatformCameraState state = PlatformCameraState::Ready;
        /** @brief Negotiated frame width. */
        int frameWidth = 4;
        /** @brief Negotiated frame height. */
        int frameHeight = 2;
        /** @brief Whether the next poll returns a frame. */
        bool frameAvailable = true;
        /** @brief Bytes returned by the next successful poll. Empty scripts a filled valid frame. */
        std::vector<std::uint8_t> nextPixels;
        /** @brief Number of frame polls made through the session. */
        int acquireCallCount = 0;
        /** @brief Last camera id passed to OpenCamera, or zero before one is opened. */
        CameraId lastOpenedId = 0;
        /** @brief Whether an opened session was destroyed. */
        bool sessionDestroyed = false;
    };

    /** @brief One scripted camera session. */
    class CannedCamera final : public IPlatformCamera
    {
    public:
        /** @brief Wraps shared script state. @param state The state to read and mutate. */
        explicit CannedCamera(std::shared_ptr<CannedCameraState> state)
            : state_(std::move(state))
        {
        }

        /** @brief Records session destruction. */
        ~CannedCamera() override { state_->sessionDestroyed = true; }

        /** @brief Gets the scripted state. @return The state. */
        [[nodiscard]] PlatformCameraState GetState() override { return state_->state; }

        /** @brief Gets the scripted width. @return The width. */
        [[nodiscard]] int GetFrameWidth() override { return state_->frameWidth; }

        /** @brief Gets the scripted height. @return The height. */
        [[nodiscard]] int GetFrameHeight() override { return state_->frameHeight; }

        /**
         * @brief Delivers the scripted frame.
         * @param frame Receives it on success.
         * @return Whether a ready frame was available.
         */
        bool TryAcquireFrame(PlatformCameraFrame& frame) override
        {
            ++state_->acquireCallCount;
            if (state_->state != PlatformCameraState::Ready || !state_->frameAvailable)
            {
                return false;
            }

            PlatformCameraFrame next;
            next.width = state_->frameWidth;
            next.height = state_->frameHeight;
            next.timestampNanoseconds = 1234;
            if (state_->nextPixels.empty())
            {
                next.rgbaPixels.assign(
                    static_cast<std::size_t>(state_->frameWidth) * state_->frameHeight * 4u, 0x7f);
            }
            else
            {
                next.rgbaPixels = state_->nextPixels;
            }
            frame = std::move(next);
            return true;
        }

    private:
        std::shared_ptr<CannedCameraState> state_;
    };

    /** @brief A provider with scripted enumeration and sessions. */
    class CannedCameraProvider final : public IPlatformCameraProvider
    {
    public:
        /** @brief Cameras returned by enumeration. */
        std::vector<PlatformCameraInfo> cameras{
            PlatformCameraInfo{7, "Canned camera", PlatformCameraPosition::Unknown}};
        /** @brief Session script and retained observations. */
        std::shared_ptr<CannedCameraState> state = std::make_shared<CannedCameraState>();
        /** @brief Whether opening a known id succeeds. */
        bool openSucceeds = true;

        /** @brief Gets the scripted cameras. @return The cameras. */
        [[nodiscard]] std::vector<PlatformCameraInfo> GetCameras() const override
        {
            return cameras;
        }

        /**
         * @brief Opens a scripted session.
         * @param id The camera id.
         * @return The session, or null for an unknown id/scripted failure.
         */
        [[nodiscard]] std::unique_ptr<IPlatformCamera> OpenCamera(const CameraId id) override
        {
            state->lastOpenedId = id;
            const bool known = std::any_of(
                cameras.begin(), cameras.end(),
                [id](const PlatformCameraInfo& info) { return info.id == id; });
            return openSucceeds && known ? std::make_unique<CannedCamera>(state) : nullptr;
        }
    };

    /** @brief A platform that is real in every respect except its camera provider. */
    class CannedCameraPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Reports camera support in addition to the inner platform's capabilities. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override
        {
            PlatformCapabilities capabilities = PlatformTestDecorator::GetCapabilities();
            capabilities.camera = true;
            return capabilities;
        }

        /** @brief Gets the scripted provider. @return The provider; never null. */
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override { return &camera_; }

        /** @brief Gets the provider for scripting and inspection. @return The provider. */
        [[nodiscard]] CannedCameraProvider& Canned() { return camera_; }

    private:
        CannedCameraProvider camera_;
    };

} // namespace CNA::Platform::Testing
