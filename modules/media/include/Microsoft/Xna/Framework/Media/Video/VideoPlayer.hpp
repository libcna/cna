// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
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

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Internal::Media
{
    class VideoDecoder;
}

namespace CNA::Internal::Audio
{
    class MixerStream;
}

namespace Microsoft::Xna::Framework::Media
{
    /**
     * @brief Controls video playback.
     *
     * When the optional backend is enabled, decodes video frames via FFmpeg and renders them
     * through the CNA graphics renderer. Audio is fed through CNA's selected audio implementation.
     * The type and its metadata/state API remain available without that backend; Play() then
     * rejects deterministically instead of leaving unresolved linker symbols.
     */
    class VideoPlayer final : public System::Object, public System::IDisposable
    {
    public:
        CNAEXT friend struct VideoPlayerTestAccess;

        /** @brief Constructs a VideoPlayer in the stopped state. */
        VideoPlayer();

        /** @brief Destroys the VideoPlayer and releases decoder and audio resources. */
        CNAEXT ~VideoPlayer() override;

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
         * @throws System::ObjectDisposedException If this VideoPlayer has been disposed.
         * @return Pointer to the frame Texture2D, or nullptr if no video is active.
         */
        Graphics::Texture2D* GetTexture();

        /**
         * @brief Starts playback of the given video from the beginning.
         *
         * @throws System::ObjectDisposedException If this VideoPlayer has been disposed.
         * @throws System::NotSupportedException If CNA was built without a video backend.
         * @throws System::InvalidOperationException If the video's declared width/height/frame
         *         rate does not match what the decoded file actually reports.
         * @param video Video to play.
         */
        void Play(Video* video);

        /**
         * @brief Stops playback and resets the playback position.
         * @throws System::ObjectDisposedException If this VideoPlayer has been disposed.
         */
        void Stop();

        /**
         * @brief Pauses playback at the current position.
         * @throws System::ObjectDisposedException If this VideoPlayer has been disposed.
         */
        void Pause();

        /**
         * @brief Resumes playback from the paused position.
         * @throws System::ObjectDisposedException If this VideoPlayer has been disposed.
         */
        void Resume();

        /**
         * @brief Selects which audio track to use (0-based index among audio streams).
         *
         * An FNA extension beyond the original XNA 4.0 API surface (note the "EXT" suffix),
         * not a CNA invention.
         *
         * @throws System::ObjectDisposedException If this VideoPlayer has been disposed.
         * @param track Zero-based audio stream index.
         */
        CNAEXT void SetAudioTrackEXT(SharpRuntime::intcs track);

        /**
         * @brief Selects which video track to use (0-based index among video streams).
         *
         * An FNA extension beyond the original XNA 4.0 API surface (note the "EXT" suffix),
         * not a CNA invention.
         *
         * @throws System::ObjectDisposedException If this VideoPlayer has been disposed.
         * @param track Zero-based video stream index.
         */
        CNAEXT void SetVideoTrackEXT(SharpRuntime::intcs track);

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
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    public:
        /**
         * @brief Number of frames this player has decoded since it was constructed.
         *
         * Zero before the first decoded frame. Equal across two calls means the same pixels; a
         * higher value means the frame advanced. It is monotonic for the lifetime of the player and
         * is **never** reset -- not by Stop, and not by playing a different video -- so a value
         * captured before any such change can never compare equal to one handed out after it.
         *
         * @return The current frame generation.
         */
        CNAEXT [[nodiscard]] std::uint64_t GetFrameGenerationEXT() const noexcept
        {
            return frameGeneration_;
        }

        /**
         * @brief Presentation timestamp of the frame currently in the texture, in seconds.
         *
         * @return The timestamp, or a negative value when no frame has been decoded.
         */
        CNAEXT [[nodiscard]] double GetFramePresentationTimeEXT() const noexcept
        {
            return lastFramePts_;
        }

    private:
        void OpenDecoder(Video* video);
        void CloseDecoder();
        void ApplyVolume();
        [[nodiscard]] double GetElapsedSeconds() const;

        // Hands whatever decoder_->DrainAudio() produced to the audio stream (if one exists) and
        // always clears audioBuffer_ afterward -- including when audioStream_ is null (no audio
        // device available, or a video-only track). The old inline version at both call sites only
        // cleared the buffer inside the `if (audioStream_)` branch, so a video playing with no
        // audio device accumulated its ENTIRE decoded audio track in memory for the rest of
        // playback (potentially hundreds of MB for a long video), and CloseDecoder() never cleared
        // it either -- stale audio from a failed-device Play() could then be fed to a genuinely
        // opened stream on a later successful Play() (found by external code review, plans/plan_media.md
        // MEDIA-153).
        void DrainAndFlushAudioBuffer();

        // (Re)creates the frame texture to match whatever video track is currently active on
        // `decoder_`. A track switch can change frame dimensions, and an already-created texture
        // sized for the previous track would otherwise silently keep the stale size (plans/plan_media.md
        // MEDIA-90, a real bug found by external code review).
        void ReconfigureVideoOutputForCurrentTrack();

        // (Re)creates the audio stream to match whatever audio track is currently active on
        // `decoder_`. Split from the video-side reconfiguration (plans/plan_media.md MEDIA-148, found by
        // external code review): the two used to be one function always called together, so
        // switching only the video track tore down and reopened the audio stream too,
        // discarding whatever audio was already queued for playback -- and vice versa for an
        // audio-only switch needlessly recreating the video texture.
        void ReconfigureAudioOutputForCurrentTrack();

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
        CNA::Internal::Audio::MixerStream*                   audioStream_ = nullptr;

        // Timing
        using Clock = std::chrono::steady_clock;
        Clock::time_point startTime_;
        double            pauseOffset_ = 0.0; // elapsed seconds at last pause
        double            lastFramePts_= -1.0;
        /**
         * @brief Counts decoded frames, so a caller can tell a new frame from the same one again.
         *
         * plans/plan_cabi.md CABI-9. GetTexture() hands back the same single frameTexture_ every
         * time, so pointer identity answers nothing: two calls against one undecoded frame look
         * exactly like two calls across a frame advance. This increments only where a frame is
         * actually decoded into the texture, which is the question a consumer is really asking.
         */
        std::uint64_t     frameGeneration_ = 0U;

        std::vector<uint8_t> rgbaBuffer_;
        std::vector<float>   audioBuffer_;
    };
}
