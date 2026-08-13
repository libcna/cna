// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace CNA::Internal::Audio
{
    /** @brief Opaque decoded/encoded audio resource owned by the mixer engine. */
    class MixerAudio;

    /** @brief Opaque mixer track. Its lifetime is owned by the shared mixer engine. */
    class MixerTrack;

    /** @brief Opaque queued PCM source consumed by a mixer track. */
    class MixerStream;

    enum class MixerSampleFormat
    {
        Signed16,
        Float32
    };

    struct MixerFormat
    {
        int sampleRate = 0;
        int channels = 0;
        MixerSampleFormat sampleFormat = MixerSampleFormat::Signed16;
    };

    struct MixerPlayOptions
    {
        int loopCount = 0;
        bool haltWhenExhausted = true;
        bool hasLoopStartFrame = false;
        std::uint64_t loopStartFrame = 0;
        bool hasMaxFrame = false;
        std::uint64_t maxFrame = 0;
    };

    using MixerAudioPtr = std::shared_ptr<MixerAudio>;
    using MixerTrackMixCallback = void (*)(void* userdata, MixerTrack* track,
                                            int channels, float* pcm, int samples);
    using MixerTrackStoppedCallback = void (*)(void* userdata, MixerTrack* track);

    /** @brief Ensures the shared memory-backed mixer and selected output device exist. */
    void EnsureMixer();

    /** @brief Attempts EnsureMixer without allowing an exception to escape. */
    [[nodiscard]] bool TryEnsureMixer() noexcept;

    /** @brief Returns the most recent native mixer diagnostic as an owned string. */
    [[nodiscard]] std::string GetMixerError();

    /** @brief Returns the current mixer generation used to invalidate borrowed tracks. */
    [[nodiscard]] std::uint64_t GetMixerEngineGeneration();

    /** @brief Drains deferred callback-side track destruction and enters mixer shutdown. */
    void BeginMixerEngineShutdown() noexcept;

    /** @brief Leaves mixer shutdown after the native mixer and all its tracks are gone. */
    void EndMixerEngineShutdown() noexcept;

    /** @brief Locks the shared mixer's callback/mixing state for a short control update. */
    class MixerLock
    {
    public:
        MixerLock();
        ~MixerLock();

        MixerLock(const MixerLock&) = delete;
        MixerLock& operator=(const MixerLock&) = delete;

    private:
        void* mixer_ = nullptr;
    };

    [[nodiscard]] MixerAudioPtr LoadMixerAudioFile(const std::string& path);
    [[nodiscard]] MixerAudioPtr LoadMixerAudioMemory(std::span<const std::byte> encodedData);
    [[nodiscard]] MixerAudioPtr LoadMixerRawAudio(std::span<const std::byte> pcm,
                                                  const MixerFormat& format);
    [[nodiscard]] MixerFormat GetMixerAudioFormat(const MixerAudio* audio) noexcept;
    [[nodiscard]] std::int64_t GetMixerAudioDuration(const MixerAudio* audio) noexcept;

    [[nodiscard]] float GetMixerMasterGain();
    void SetMixerMasterGain(float gain);
    [[nodiscard]] int GetMixerSampleRate();

    [[nodiscard]] MixerTrack* CreateMixerTrack();
    void DestroyMixerTrack(MixerTrack* track) noexcept;
    [[nodiscard]] bool SetMixerTrackAudio(MixerTrack* track, MixerAudio* audio) noexcept;
    [[nodiscard]] bool SetMixerTrackStream(MixerTrack* track, MixerStream* stream) noexcept;
    void SetMixerTrackGain(MixerTrack* track, float gain) noexcept;
    void SetMixerTrackStereoUnity(MixerTrack* track) noexcept;
    void SetMixerTrackFrequencyRatio(MixerTrack* track, float ratio) noexcept;
    void SetMixerTrackLoops(MixerTrack* track, int loops) noexcept;
    void SetMixerTrackMixCallback(MixerTrack* track, MixerTrackMixCallback callback,
                                  void* userdata) noexcept;
    void SetMixerTrackStoppedCallback(MixerTrack* track, MixerTrackStoppedCallback callback,
                                      void* userdata) noexcept;
    [[nodiscard]] bool PlayMixerTrack(MixerTrack* track,
                                      const MixerPlayOptions& options = {}) noexcept;
    void StopMixerTrack(MixerTrack* track) noexcept;
    void PauseMixerTrack(MixerTrack* track) noexcept;
    void ResumeMixerTrack(MixerTrack* track) noexcept;
    [[nodiscard]] bool IsMixerTrackPaused(const MixerTrack* track) noexcept;
    [[nodiscard]] bool IsMixerTrackPlaying(const MixerTrack* track) noexcept;

    [[nodiscard]] MixerStream* CreateMixerStream(const MixerFormat& sourceFormat) noexcept;
    void DestroyMixerStream(MixerStream* stream) noexcept;
    void ClearMixerStream(MixerStream* stream) noexcept;
    [[nodiscard]] int GetMixerStreamQueuedBytes(const MixerStream* stream) noexcept;
    [[nodiscard]] bool PutMixerStreamData(MixerStream* stream,
                                          std::span<const std::byte> data) noexcept;
}
