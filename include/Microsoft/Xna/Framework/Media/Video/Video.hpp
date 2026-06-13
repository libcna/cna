// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace Microsoft::Xna::Framework::Media
{
    class VideoPlayer;

    /** @brief Represents a video asset that can be played through VideoPlayer. */
    class Video final : public System::Object
    {
    public:
        /**
         * @brief Creates a Video from a file path and a graphics device (XNB constructor).
         *
         * @param fileName File path to the video file.
         * @param device   GraphicsDevice used for frame rendering.
         */
        NOXNA Video(std::string fileName, Graphics::GraphicsDevice* device);

        /**
         * @brief Creates a Video with explicit metadata (raw-file constructor).
         *
         * @param fileName       File path to the video file.
         * @param device         GraphicsDevice used for frame rendering.
         * @param durationMS     Duration in milliseconds.
         * @param width          Frame width in pixels.
         * @param height         Frame height in pixels.
         * @param framesPerSecond Frame rate.
         * @param soundtrackType Type of audio content in the video.
         */
        NOXNA Video(std::string fileName, Graphics::GraphicsDevice* device,
              SharpRuntime::intcs durationMS, SharpRuntime::intcs width,
              SharpRuntime::intcs height, float framesPerSecond,
              VideoSoundtrackType soundtrackType);

        /**
         * @brief Gets the width of the video in pixels.
         *
         * @return Video width.
         */
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const;

        /**
         * @brief Gets the height of the video in pixels.
         *
         * @return Video height.
         */
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const;

        /**
         * @brief Gets the frame rate of the video.
         *
         * @return Frames per second.
         */
        [[nodiscard]] float getFramesPerSecondProperty() const;

        /**
         * @brief Gets the type of audio content in this video.
         *
         * @return VideoSoundtrackType value.
         */
        [[nodiscard]] VideoSoundtrackType getVideoSoundtrackTypeProperty() const;

        /**
         * @brief Gets the total duration of this video.
         *
         * @return Duration as a TimeSpan.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /**
         * @brief Sets the total duration of this video (internal use).
         *
         * @param value New duration.
         */
        NOXNA void setDurationProperty(System::TimeSpan value);

        /**
         * @brief Creates a Video from a URI and a graphics device.
         *
         * @param uri    File URI or local path.
         * @param device GraphicsDevice used for frame rendering.
         * @return Pointer to the newly created Video.
         */
        static Video* FromUriEXT(const std::string& uri, Graphics::GraphicsDevice* device);

        /**
         * @brief Selects which audio stream to use when the file contains multiple audio tracks.
         *
         * @param track Zero-based audio stream index.
         */
        void SetAudioTrackEXT(SharpRuntime::intcs track);

        /**
         * @brief Selects which video stream to use when the file contains multiple video tracks.
         *
         * @param track Zero-based video stream index.
         */
        void SetVideoTrackEXT(SharpRuntime::intcs track);

        /**
         * @brief Returns the file path this Video was loaded from.
         *
         * @return File path string.
         */
        NOXNA [[nodiscard]] const std::string& getFileNameProperty() const;

        /**
         * @brief Returns the associated GraphicsDevice (may be null).
         *
         * @return Pointer to the GraphicsDevice, or nullptr.
         */
        NOXNA [[nodiscard]] Graphics::GraphicsDevice* getGraphicsDeviceProperty() const;

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        friend class VideoPlayer;

        std::string fileName_;
        Graphics::GraphicsDevice* device_;
        SharpRuntime::intcs width_;
        SharpRuntime::intcs height_;
        float framesPerSecond_;
        VideoSoundtrackType soundtrackType_;
        System::TimeSpan duration_;

        SharpRuntime::intcs audioTrack_ = -1;
        SharpRuntime::intcs videoTrack_ = -1;
        VideoPlayer* parent_ = nullptr;
    };
}
