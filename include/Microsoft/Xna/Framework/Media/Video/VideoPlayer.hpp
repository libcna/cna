// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
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
    /**
     * @brief Controls video playback.
     *
     * Decodes video frames via FFmpeg and renders them through the CNA graphics
     * backend. Audio is fed to an SDL3 AudioStream.
     */
    class VideoPlayer final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Constructs a VideoPlayer in the stopped state. */
        VideoPlayer();

        /** @brief Destroys the VideoPlayer and releases decoder and audio resources. */
        NOXNA ~VideoPlayer() override;

        /** @brief Releases all resources used by this VideoPlayer. */
        void Dispose() override;

        /**
         * @brief Gets whether this VideoPlayer has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets whether the video loops when it reaches the end.
         *
         * @return true if looping; otherwise false.
         */
        [[nodiscard]] bool getIsLoopedProperty() const;

        /**
         * @brief Sets whether the video loops when it reaches the end.
         *
         * @param value New loop state.
         */
        void setIsLoopedProperty(bool value);

        /**
         * @brief Gets whether audio playback is muted.
         *
         * @return true if muted; otherwise false.
         */
        [[nodiscard]] bool getIsMutedProperty() const;

        /**
         * @brief Sets whether audio playback is muted.
         *
         * @param value New muted state.
         */
        void setIsMutedProperty(bool value);

        /**
         * @brief Gets the current playback position within the video.
         *
         * @return Current position as a TimeSpan.
         */
        [[nodiscard]] System::TimeSpan getPlayPositionProperty() const;

        /**
         * @brief Gets the current playback state.
         *
         * @return Current MediaState.
         */
        [[nodiscard]] MediaState getStateProperty() const;

        /**
         * @brief Gets the video currently associated with this player.
         *
         * @return Pointer to the active Video, or nullptr.
         */
        [[nodiscard]] Video* getVideoProperty() const;

        /**
         * @brief Gets the playback volume. Range [0, 1].
         *
         * @return Current volume.
         */
        [[nodiscard]] float getVolumeProperty() const;

        /**
         * @brief Sets the playback volume. Range [0, 1].
         *
         * @param value New volume.
         */
        void setVolumeProperty(float value);

        /**
         * @brief Returns the current video frame as a Texture2D.
         *
         * Decodes the next frame if the playback clock has advanced past the
         * last decoded presentation timestamp.
         *
         * @return Pointer to the frame Texture2D, or nullptr if no video is active.
         */
        Graphics::Texture2D* GetTexture();

        /**
         * @brief Starts playback of the given video from the beginning.
         *
         * @param video Video to play.
         */
        void Play(Video* video);

        /** @brief Stops playback and resets the playback position. */
        void Stop();

        /** @brief Pauses playback at the current position. */
        void Pause();

        /** @brief Resumes playback from the paused position. */
        void Resume();

        /**
         * @brief Selects which audio track to use (0-based index among audio streams).
         *
         * @param track Zero-based audio stream index.
         */
        void SetAudioTrackEXT(SharpRuntime::intcs track);

        /**
         * @brief Selects which video track to use (0-based index among video streams).
         *
         * @param track Zero-based video stream index.
         */
        void SetVideoTrackEXT(SharpRuntime::intcs track);

        /** @brief Stores basic video file metadata (width, height, fps). */
        struct VideoInfo
        {
            /** @brief Frame width in pixels. */
            int    width  = 0;
            /** @brief Frame height in pixels. */
            int    height = 0;
            /** @brief Frames per second. */
            double fps    = 0.0;
        };

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

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
