// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

struct SDL_AudioStream;

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Internal::Media
{
    class VideoDecoder;
}

namespace Microsoft::Xna::Framework::Media
{
    /// Controls video playback. Decodes video via FFmpeg and renders frames
    /// through the CNA graphics backend. Audio is fed to an SDL3 AudioStream.
    class VideoPlayer final : public System::Object, public System::IDisposable
    {
    public:
        VideoPlayer();
        ~VideoPlayer() override;

        void Dispose() override;

        [[nodiscard]] bool getIsDisposedProperty() const;

        [[nodiscard]] bool getIsLoopedProperty() const;
        void setIsLoopedProperty(bool value);

        [[nodiscard]] bool getIsMutedProperty() const;
        void setIsMutedProperty(bool value);

        /// Gets the current playback position.
        [[nodiscard]] System::TimeSpan getPlayPositionProperty() const;

        [[nodiscard]] MediaState getStateProperty() const;

        [[nodiscard]] Video* getVideoProperty() const;

        [[nodiscard]] float getVolumeProperty() const;
        void setVolumeProperty(float value);

        /// Returns the current video frame as a Texture2D.
        /// Decodes the next frame if needed based on playback timing.
        Graphics::Texture2D* GetTexture();

        void Play(Video* video);
        void Stop();
        void Pause();
        void Resume();

        /// Selects which audio track to use (0-based index among audio streams).
        void SetAudioTrackEXT(SharpRuntime::intcs track);

        /// Selects which video track to use (0-based index among video streams).
        void SetVideoTrackEXT(SharpRuntime::intcs track);

        /// Metadata about a video file (width, height, fps).
        struct VideoInfo
        {
            int    width  = 0;
            int    height = 0;
            double fps    = 0.0;
        };

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        void OpenDecoder(Video* video);
        void CloseDecoder();
        void ApplyVolume();
        [[nodiscard]] double GetElapsedSeconds() const;

        bool isDisposed_ = false;
        bool isLooped_   = false;
        bool isMuted_    = false;
        MediaState state_ = MediaState::Stopped;
        float volume_     = 1.0f;
        Video* video_     = nullptr;

        SharpRuntime::intcs audioTrack_ = -1;
        SharpRuntime::intcs videoTrack_ = -1;

        std::unique_ptr<CNA::Internal::Media::VideoDecoder> decoder_;
        std::unique_ptr<Graphics::Texture2D>                frameTexture_;
        SDL_AudioStream*                                     audioStream_ = nullptr;

        // Timing
        using Clock = std::chrono::steady_clock;
        Clock::time_point startTime_;
        double            pauseOffset_ = 0.0; // elapsed seconds at last pause
        double            lastFramePts_= -1.0;

        std::vector<uint8_t> rgbaBuffer_;
        std::vector<float>   audioBuffer_;
    };
}
