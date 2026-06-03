#pragma once

#include <string>

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
    /// Represents a video asset that can be played through VideoPlayer.
    /// @note Status: Stub — video decoding not implemented
    class Video final : public System::Object
    {
    public:
        /// Creates a Video from a file path and a graphics device (XNB constructor).
        Video(std::string fileName, Graphics::GraphicsDevice* device);

        /// Creates a Video with explicit metadata (raw-file constructor).
        Video(std::string fileName, Graphics::GraphicsDevice* device,
              SharpRuntime::intcs durationMS, SharpRuntime::intcs width,
              SharpRuntime::intcs height, float framesPerSecond,
              VideoSoundtrackType soundtrackType);

        /// Gets the width of the video in pixels.
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const;

        /// Gets the height of the video in pixels.
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const;

        /// Gets the frame rate of the video.
        [[nodiscard]] float getFramesPerSecondProperty() const;

        /// Gets the soundtrack type of the video.
        [[nodiscard]] VideoSoundtrackType getVideoSoundtrackTypeProperty() const;

        /// Gets the duration of the video.
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /// Sets the duration of the video (internal use).
        void setDurationProperty(System::TimeSpan value);

        /// Creates a Video from a URI and a graphics device.
        static Video* FromUriEXT(const std::string& uri, Graphics::GraphicsDevice* device);

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string fileName_;
        Graphics::GraphicsDevice* device_;
        SharpRuntime::intcs width_;
        SharpRuntime::intcs height_;
        float framesPerSecond_;
        VideoSoundtrackType soundtrackType_;
        System::TimeSpan duration_;
    };
}
