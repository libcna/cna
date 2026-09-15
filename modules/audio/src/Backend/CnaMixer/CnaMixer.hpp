// SPDX-License-Identifier: MS-PL
#pragma once

// CNA's own mixer: what MixerEngine.hpp promises, without SDL3_mixer (plans/plan_x11.md X11-0151).
//
// The facade the XNA layer talks to (MixerEngine.hpp) was written against SDL3_mixer, so the
// semantics here are SDL3_mixer's, read from its source rather than guessed: a track's data is
// resampled to the mixer's rate at its frequency ratio and scaled by its gain, THEN handed to the
// track's mix callback (SoundEffectInstance's filter and pan run there), THEN added to the mix at
// the master gain, and the finished mix goes to the post-mix callback. Loops count extra passes
// (-1 forever), each pass ending at the play's max frame and restarting at its loop-start frame.
// A track that runs out stops and fires its stopped callback -- after its last audio has gone
// through the mix callback, not before, which is one ordering SDL3_mixer does not guarantee.
//
// Resampling is linear interpolation, which is what FAudio -- and so FNA -- does.
//
// Nothing in this class locks. The facade serialises every call, the device's callback included;
// tests drive a mixer from one thread.

#include "CNA/Internal/Audio/MixerEngine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace CNA::Internal::Audio
{
    /** @brief Audio data, shared by a MixerAudio handle and every track playing it. */
    struct MixerAudioData
    {
        /** @brief How the samples are held. */
        enum class Encoding
        {
            /** @brief Interleaved signed 16-bit PCM in `pcm16`. */
            Pcm16,
            /** @brief Interleaved 32-bit float PCM in `pcmFloat`. */
            Float32,
            /** @brief A whole Ogg Vorbis file in `encoded`, decoded as it plays. */
            Vorbis
        };

        /** @brief How the samples are held. */
        Encoding encoding = Encoding::Pcm16;
        /** @brief Frames per second. */
        int sampleRate = 0;
        /** @brief Interleaved channels. */
        int channels = 0;
        /** @brief Frames in the data, or -1 when unknown. */
        std::int64_t frames = 0;
        /** @brief Samples, for `Pcm16`. */
        std::vector<std::int16_t> pcm16;
        /** @brief Samples, for `Float32`. */
        std::vector<float> pcmFloat;
        /** @brief The encoded file, for `Vorbis`. */
        std::vector<unsigned char> encoded;
    };

    /** @brief The facade's audio handle: shared data plus what it reports. */
    class MixerAudio final
    {
    public:
        /** @brief The data; tracks share it, so it outlives this handle while they play. */
        std::shared_ptr<const MixerAudioData> data;
        /** @brief The format the facade reports. */
        MixerFormat format{};
        /** @brief Frames, or -1 when unknown. */
        std::int64_t durationFrames = -1;
    };

    /** @brief Queued PCM a track plays as it arrives. */
    class MixerStream final
    {
    public:
        /** @brief The queued data's format. */
        MixerFormat format{};
        /** @brief Queued bytes; everything before `readOffset` has been played. */
        std::vector<std::byte> queue;
        /** @brief Bytes of `queue` already consumed. */
        std::size_t readOffset = 0;
        /** @brief The track CreateMixerPlaybackStream made for it, or null. */
        MixerTrack* playbackTrack = nullptr;

        /** @brief Gets the bytes not yet played. @return The count. */
        [[nodiscard]] std::size_t Queued() const noexcept { return queue.size() - readOffset; }
    };

    /** @brief A per-track Ogg Vorbis decoder (defined with the decoders). */
    class VorbisCursor;

    /** @brief Frees a VorbisCursor where its type is complete. */
    struct VorbisCursorDeleter
    {
        /** @brief Frees the decoder. @param cursor The decoder. */
        void operator()(VorbisCursor* cursor) const noexcept;
    };

    /** @brief An owned per-track Vorbis decoder. */
    using VorbisCursorPtr = std::unique_ptr<VorbisCursor, VorbisCursorDeleter>;

    /** @brief One voice. */
    class MixerTrack final
    {
    public:
        /** @brief Playback state, as SDL3_mixer reports it. */
        enum class State
        {
            Stopped,
            Playing,
            Paused
        };

        /** @brief Where playback stands. */
        State state = State::Stopped;
        /** @brief The bound audio, or null. */
        std::shared_ptr<const MixerAudioData> audio;
        /** @brief The decoder for a bound Vorbis audio. */
        VorbisCursorPtr vorbis;
        /** @brief The bound stream, or null. */
        MixerStream* stream = nullptr;

        /** @brief Passes left after this one: 0 none, -1 forever. */
        int loopsRemaining = 0;
        /** @brief Whether running out of data stops the track. */
        bool haltWhenExhausted = true;
        /** @brief The frame each loop restarts at. */
        std::uint64_t loopStart = 0;
        /** @brief The frame every pass ends at, or -1 for the end of the data. */
        std::int64_t maxFrame = -1;
        /** @brief The next source frame to read. */
        std::uint64_t position = 0;

        /** @brief Linear gain, applied before the mix callback. */
        float gain = 1.0f;
        /** @brief Playback-speed multiplier, [0.01, 100]. */
        float ratio = 1.0f;

        /** @brief The resampler's two frames and its position between them. */
        std::array<float, 2> a{};
        std::array<float, 2> b{};
        double fraction = 0.0;
        bool primed = false;
        bool haveB = false;
        bool lastFrame = false;

        /** @brief Callbacks. */
        MixerTrackMixCallback mixCallback = nullptr;
        void* mixUserdata = nullptr;
        MixerTrackStoppedCallback stoppedCallback = nullptr;
        void* stoppedUserdata = nullptr;

        /** @brief True while one of this track's callbacks runs. */
        bool inCallback = false;
        /** @brief True once destruction was requested where it could not happen at once. */
        bool destroyDeferred = false;

        /** @brief Forgets the resampler's frames, as after a seek. */
        void ResetResampler() noexcept
        {
            primed = false;
            haveB = false;
            lastFrame = false;
            fraction = 0.0;
        }
    };

    /** @brief What reading one source frame produced. */
    enum class MixerFrameResult
    {
        /** @brief A frame. */
        Frame,
        /** @brief Nothing yet: a stream is empty and keeps playing. */
        Starved,
        /** @brief Nothing more: the data (and its loops) are done. */
        Ended
    };

    /** @brief The mixer proper: tracks in, one interleaved stereo float buffer out. */
    class CnaMixer
    {
    public:
        /**
         * @brief Creates a mixer.
         * @param sampleRate The output rate, frames per second.
         */
        explicit CnaMixer(int sampleRate);

        /** @brief Destroys every track. */
        ~CnaMixer();

        CnaMixer(const CnaMixer&) = delete;
        CnaMixer& operator=(const CnaMixer&) = delete;

        /** @brief Gets the output rate. @return Frames per second. */
        [[nodiscard]] int GetSampleRate() const noexcept { return sampleRate_; }

        /**
         * @brief Mixes the next frames.
         * @param stereo Receives `frames` interleaved stereo frames.
         * @param frames How many.
         */
        void Render(float* stereo, int frames) noexcept;

        /** @brief Creates a stopped track with nothing bound. @return The track. */
        [[nodiscard]] MixerTrack* CreateTrack();
        /**
         * @brief Destroys a track, or defers it while one of its callbacks runs.
         *
         * A deferred track leaves the mix at once and is freed by DrainDeferred(), exactly as
         * SDL3_mixer's facade defers a track destroyed from its own stopped callback.
         * @param track The track.
         */
        void DestroyTrack(MixerTrack* track) noexcept;
        /** @brief Frees tracks whose destruction was deferred. */
        void DrainDeferred() noexcept;

        /**
         * @brief Binds audio to a track, replacing whatever it had.
         * @param track The track.
         * @param audio The audio, or null to unbind.
         * @return False when the audio cannot be played (a Vorbis file that fails to open).
         */
        bool BindAudio(MixerTrack* track, const MixerAudio* audio) noexcept;
        /**
         * @brief Binds a stream to a track, replacing whatever it had.
         * @param track The track.
         * @param stream The stream, or null to unbind.
         */
        void BindStream(MixerTrack* track, MixerStream* stream) noexcept;
        /** @brief Unbinds a stream from every track playing it, before it is destroyed. */
        void DetachStream(const MixerStream* stream) noexcept;

        /**
         * @brief Starts a track from the beginning.
         * @param track The track.
         * @param options Loops, loop region and exhaustion behaviour.
         * @return False when nothing is bound.
         */
        bool Play(MixerTrack* track, const MixerPlayOptions& options) noexcept;
        /** @brief Stops a track at once, firing its stopped callback if it was not stopped. */
        void Stop(MixerTrack* track) noexcept;
        /** @brief Pauses a playing track. */
        void Pause(MixerTrack* track) noexcept;
        /** @brief Resumes a paused track. */
        void Resume(MixerTrack* track) noexcept;

        /** @brief Gets the master gain. @return The gain. */
        [[nodiscard]] float GetMasterGain() const noexcept { return masterGain_; }
        /** @brief Sets the master gain, clamped at zero. @param gain The gain. */
        void SetMasterGain(float gain) noexcept { masterGain_ = gain < 0.0f ? 0.0f : gain; }
        /** @brief Installs or removes the post-mix callback. */
        void SetPostMixCallback(MixerPostMixCallback callback, void* userdata) noexcept
        {
            postMix_ = callback;
            postMixUserdata_ = userdata;
        }

        /** @brief Gets the tracks in the mix. @return The tracks. */
        [[nodiscard]] const std::vector<MixerTrack*>& GetTracks() const noexcept { return tracks_; }

    private:
        MixerFrameResult ReadFrame(MixerTrack& track, std::array<float, 2>& frame) noexcept;
        MixerFrameResult Settle(MixerTrack& track) noexcept;
        void FinishTrack(MixerTrack& track) noexcept;
        void RemoveDeferredFromMix() noexcept;

        int sampleRate_;
        float masterGain_ = 1.0f;
        MixerPostMixCallback postMix_ = nullptr;
        void* postMixUserdata_ = nullptr;
        std::vector<MixerTrack*> tracks_;
        std::vector<MixerTrack*> deferred_;
        std::vector<float> scratch_;
        bool rendering_ = false;
    };

    /**
     * @brief Decodes a whole audio file for the mixer.
     *
     * RIFF/WAVE through CNA's own decoder (PCM 8/16/24/32-bit, float, MS-ADPCM, IMA-ADPCM) and
     * Ogg Vorbis through stb_vorbis; nothing else. A Vorbis file is decoded up front when
     * @p predecode is set and as it plays otherwise.
     *
     * @param bytes The file.
     * @param origin A name for diagnostics.
     * @param predecode Whether to decode a Vorbis file up front.
     * @param error Receives why, on failure.
     * @return The data, or null.
     */
    [[nodiscard]] std::shared_ptr<MixerAudioData> DecodeMixerAudio(std::span<const std::byte> bytes,
                                                                   const std::string& origin,
                                                                   bool predecode,
                                                                   std::string& error);

    /**
     * @brief Wraps raw PCM for the mixer.
     * @param pcm Interleaved samples in @p format.
     * @param format Rate, channels and representation.
     * @param error Receives why, on failure.
     * @return The data, or null for an unusable format.
     */
    [[nodiscard]] std::shared_ptr<MixerAudioData> WrapMixerRawAudio(std::span<const std::byte> pcm,
                                                                    const MixerFormat& format,
                                                                    std::string& error);

    /**
     * @brief Opens a per-track decoder for Vorbis data.
     * @param data The data; must be `Vorbis`.
     * @return The decoder, or null when the file does not open.
     */
    [[nodiscard]] VorbisCursorPtr OpenVorbisCursor(
        const std::shared_ptr<const MixerAudioData>& data);

    /**
     * @brief Reads one frame from a Vorbis decoder, as stereo.
     * @param cursor The decoder.
     * @param frame Receives the frame.
     * @return False at the end of the stream.
     */
    bool ReadVorbisFrame(VorbisCursor& cursor, std::array<float, 2>& frame) noexcept;

    /**
     * @brief Moves a Vorbis decoder to a frame.
     * @param cursor The decoder.
     * @param frame The frame.
     * @return False when the stream cannot seek there.
     */
    bool SeekVorbis(VorbisCursor& cursor, std::uint64_t frame) noexcept;
}
