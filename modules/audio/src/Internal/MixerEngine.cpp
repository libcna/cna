// SPDX-License-Identifier: MS-PL
#include "Internal/MixerEngine.hpp"

#include "CNA/Internal/Audio/AudioMixer.hpp"

#include <atomic>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

namespace CNA::Internal::Audio
{
    enum class MixerAudioSource
    {
        File,
        EncodedMemory,
        RawMemory
    };

    class MixerAudio final
    {
    public:
        ~MixerAudio();

        MixerAudioSource source = MixerAudioSource::RawMemory;
        std::string path;
        std::vector<std::byte> data;
        std::size_t dataLength = 0;
        MixerFormat rawFormat{};
        MixerFormat decodedFormat{};
        std::int64_t durationFrames = -1;
        MIX_Audio* native = nullptr;
        std::uint64_t nativeGeneration = 0;
        std::mutex mutex;
    };

    class MixerStream final
    {
    public:
        SDL_AudioStream* native = nullptr;
        bool audioSubsystemAcquired = false;
    };

    namespace
    {
        constexpr const char* TrackContextProperty = "CNA.internal.mixerTrackContext";

        [[nodiscard]] MIX_Track* Native(MixerTrack* track) noexcept
        {
            return reinterpret_cast<MIX_Track*>(track);
        }

        [[nodiscard]] const MIX_Track* Native(const MixerTrack* track) noexcept
        {
            return reinterpret_cast<const MIX_Track*>(track);
        }

        [[nodiscard]] SDL_AudioStream* Native(MixerStream* stream) noexcept
        {
            return stream ? stream->native : nullptr;
        }

        [[nodiscard]] const SDL_AudioStream* Native(const MixerStream* stream) noexcept
        {
            return stream ? stream->native : nullptr;
        }

        [[nodiscard]] SDL_AudioFormat ToNativeFormat(const MixerSampleFormat format)
        {
            switch (format)
            {
                case MixerSampleFormat::Signed16: return SDL_AUDIO_S16;
                case MixerSampleFormat::Float32: return SDL_AUDIO_F32;
            }
            return SDL_AUDIO_UNKNOWN;
        }

        [[nodiscard]] MixerSampleFormat FromNativeFormat(const SDL_AudioFormat format)
        {
            return format == SDL_AUDIO_F32
                ? MixerSampleFormat::Float32
                : MixerSampleFormat::Signed16;
        }

        [[nodiscard]] SDL_AudioSpec ToNativeSpec(const MixerFormat& format)
        {
            SDL_AudioSpec result{};
            result.freq = format.sampleRate;
            result.channels = format.channels;
            result.format = ToNativeFormat(format.sampleFormat);
            return result;
        }

        [[nodiscard]] MIX_Audio* LoadEncodedNative(MIX_Mixer* mixer,
                                                   const std::span<const std::byte> encodedData)
        {
            if (encodedData.empty()) return nullptr;
            // MixerAudio::data owns stable storage across native mixer generations.
            return MIX_LoadAudioNoCopy(
                mixer, encodedData.data(), encodedData.size(), false);
        }

        [[nodiscard]] MIX_Audio* ReloadNative(MixerAudio& audio, MIX_Mixer* mixer)
        {
            switch (audio.source)
            {
                case MixerAudioSource::File:
                    return MIX_LoadAudio(mixer, audio.path.c_str(), true);
                case MixerAudioSource::EncodedMemory:
                    return LoadEncodedNative(
                        mixer, std::span<const std::byte>(audio.data.data(), audio.dataLength));
                case MixerAudioSource::RawMemory:
                {
                    const SDL_AudioSpec spec = ToNativeSpec(audio.rawFormat);
                    return MIX_LoadRawAudio(
                        mixer, audio.data.data(), audio.dataLength, &spec);
                }
            }
            return nullptr;
        }

        [[nodiscard]] MIX_Audio* GetCurrentNative(MixerAudio* audio)
        {
            if (!audio) return nullptr;
            std::lock_guard<std::mutex> lock(audio->mutex);
            const std::uint64_t generation = GetMixerEngineGeneration();
            if (audio->native && audio->nativeGeneration == generation) return audio->native;

            // MIX_Quit owns and destroys every native MIX_Audio. A SoundEffect can legally
            // outlive DestroyMixer(), so never dereference its old-generation pointer; rebuild
            // the engine resource from the facade-owned source instead.
            audio->native = ReloadNative(*audio, GetMixer());
            audio->nativeGeneration = generation;
            return audio->native;
        }

        [[nodiscard]] MixerAudioPtr FinalizeAudioLoad(MixerAudioPtr audio)
        {
            MIX_Audio* native = GetCurrentNative(audio.get());
            if (!native) return {};

            SDL_AudioSpec spec{};
            if (MIX_GetAudioFormat(native, &spec))
            {
                audio->decodedFormat = {
                    spec.freq, spec.channels, FromNativeFormat(spec.format)};
            }
            audio->durationFrames = MIX_GetAudioDuration(native);
            return audio;
        }

        struct TrackContext
        {
            MixerTrackMixCallback mixCallback = nullptr;
            void* mixUserdata = nullptr;
            MixerTrackStoppedCallback stoppedCallback = nullptr;
            void* stoppedUserdata = nullptr;
            MIX_Track* nativeTrack = nullptr;
            TrackContext* deferredNext = nullptr;
            std::atomic<bool> stoppedCallbackActive{false};
            std::atomic<bool> destructionDeferred{false};
        };

        std::atomic<TrackContext*> g_deferredTrackDestruction{nullptr};
        std::atomic<bool> g_mixerShuttingDown{false};

        void DeferTrackDestruction(TrackContext* context) noexcept
        {
            if (!context || context->destructionDeferred.exchange(true, std::memory_order_acq_rel))
                return;

            context->deferredNext =
                g_deferredTrackDestruction.load(std::memory_order_relaxed);
            while (!g_deferredTrackDestruction.compare_exchange_weak(
                context->deferredNext, context,
                std::memory_order_release, std::memory_order_relaxed))
            {
            }
        }

        void DrainDeferredTrackDestruction() noexcept
        {
            TrackContext* context =
                g_deferredTrackDestruction.exchange(nullptr, std::memory_order_acquire);
            while (context)
            {
                TrackContext* next = context->deferredNext;
                // MIX_DestroyTrack destroys the track properties and therefore `context`.
                // Save every intrusive-list field before crossing that ownership boundary.
                MIX_DestroyTrack(context->nativeTrack);
                context = next;
            }
        }

        void SDLCALL DeleteTrackContext(void*, void* value)
        {
            delete static_cast<TrackContext*>(value);
        }

        [[nodiscard]] TrackContext* GetTrackContext(MIX_Track* track) noexcept
        {
            if (!track) return nullptr;
            const SDL_PropertiesID properties = MIX_GetTrackProperties(track);
            if (properties == 0) return nullptr;
            return static_cast<TrackContext*>(
                SDL_GetPointerProperty(properties, TrackContextProperty, nullptr));
        }

        void SDLCALL MixCallback(void* userdata, MIX_Track* track,
                                 const SDL_AudioSpec* spec, float* pcm, int samples)
        {
            auto* context = static_cast<TrackContext*>(userdata);
            const auto callback = context->mixCallback;
            if (callback)
            {
                callback(context->mixUserdata, reinterpret_cast<MixerTrack*>(track),
                         spec ? spec->channels : 0, pcm, samples);
            }
        }

        void SDLCALL StoppedCallback(void* userdata, MIX_Track* track)
        {
            auto* context = static_cast<TrackContext*>(userdata);
            const auto callback = context->stoppedCallback;
            const auto callbackUserdata = context->stoppedUserdata;
            context->stoppedCallbackActive.store(true, std::memory_order_release);
            if (callback)
            {
                callback(callbackUserdata, reinterpret_cast<MixerTrack*>(track));
            }
            // DestroyMixerTrack turns callback-side destruction into an intrusive lock-free
            // request. The track remains alive through the native callback's final unlock.
            context->stoppedCallbackActive.store(false, std::memory_order_release);
        }

    }

    MixerAudio::~MixerAudio()
    {
        std::lock_guard<std::mutex> lock(mutex);
        // A generation mismatch means MIX_Quit already destroyed this pointer.
        if (native && nativeGeneration == GetMixerEngineGeneration())
            MIX_DestroyAudio(native);
    }

    void EnsureMixer()
    {
        (void)GetMixer();
    }

    bool TryEnsureMixer() noexcept
    {
        try
        {
            EnsureMixer();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::string GetMixerError()
    {
        const char* error = SDL_GetError();
        return error ? error : "unknown mixer error";
    }

    std::uint64_t GetMixerEngineGeneration()
    {
        return GetMixerGeneration();
    }

    void BeginMixerEngineShutdown() noexcept
    {
        g_mixerShuttingDown.store(true, std::memory_order_release);
        DrainDeferredTrackDestruction();
    }

    void EndMixerEngineShutdown() noexcept
    {
        // A stopped callback fired by native mixer shutdown deliberately does not enqueue its
        // track: the native destroy loop owns it immediately afterward. The list is therefore
        // empty here and contains no pointers into the mixer that just disappeared.
        g_deferredTrackDestruction.store(nullptr, std::memory_order_release);
        g_mixerShuttingDown.store(false, std::memory_order_release);
    }

    MixerLock::MixerLock()
        : mixer_(GetMixer())
    {
        MIX_LockMixer(static_cast<MIX_Mixer*>(mixer_));
    }

    MixerLock::~MixerLock()
    {
        if (mixer_) MIX_UnlockMixer(static_cast<MIX_Mixer*>(mixer_));
    }

    MixerAudioPtr LoadMixerAudioFile(const std::string& path)
    {
        auto audio = std::make_shared<MixerAudio>();
        audio->source = MixerAudioSource::File;
        audio->path = path;
        return FinalizeAudioLoad(std::move(audio));
    }

    MixerAudioPtr LoadMixerAudioMemory(const std::span<const std::byte> encodedData)
    {
        if (encodedData.empty()) return {};
        auto audio = std::make_shared<MixerAudio>();
        audio->source = MixerAudioSource::EncodedMemory;
        audio->data.assign(encodedData.begin(), encodedData.end());
        audio->dataLength = encodedData.size();
        return FinalizeAudioLoad(std::move(audio));
    }

    MixerAudioPtr LoadMixerRawAudio(const std::span<const std::byte> pcm,
                                    const MixerFormat& format)
    {
        auto audio = std::make_shared<MixerAudio>();
        audio->source = MixerAudioSource::RawMemory;
        audio->data.assign(pcm.begin(), pcm.end());
        audio->dataLength = pcm.size();
        // SDL_mixer accepts a zero-byte raw resource but still requires a non-null storage
        // address. Keep a one-byte sentinel that is never part of the declared source length.
        if (audio->data.empty()) audio->data.push_back(std::byte{});
        audio->rawFormat = format;
        return FinalizeAudioLoad(std::move(audio));
    }

    MixerFormat GetMixerAudioFormat(const MixerAudio* audio) noexcept
    {
        return audio ? audio->decodedFormat : MixerFormat{};
    }

    std::int64_t GetMixerAudioDuration(const MixerAudio* audio) noexcept
    {
        return audio ? audio->durationFrames : -1;
    }

    float GetMixerMasterGain()
    {
        return MIX_GetMixerGain(GetMixer());
    }

    void SetMixerMasterGain(const float gain)
    {
        (void)MIX_SetMixerGain(GetMixer(), gain);
    }

    int GetMixerSampleRate()
    {
        SDL_AudioSpec spec{};
        return MIX_GetMixerFormat(GetMixer(), &spec) ? spec.freq : 0;
    }

    MixerTrack* CreateMixerTrack()
    {
        DrainDeferredTrackDestruction();
        MIX_Track* track = MIX_CreateTrack(GetMixer());
        if (!track) return nullptr;

        auto* context = new (std::nothrow) TrackContext{};
        if (!context)
        {
            MIX_DestroyTrack(track);
            return nullptr;
        }
        const SDL_PropertiesID properties = MIX_GetTrackProperties(track);
        if (properties == 0)
        {
            delete context;
            MIX_DestroyTrack(track);
            return nullptr;
        }
        if (!SDL_SetPointerPropertyWithCleanup(properties, TrackContextProperty, context,
                                               DeleteTrackContext, nullptr))
        {
            // SDL invokes DeleteTrackContext itself on every failed set.
            MIX_DestroyTrack(track);
            return nullptr;
        }
        context->nativeTrack = track;
        return reinterpret_cast<MixerTrack*>(track);
    }

    void DestroyMixerTrack(MixerTrack* track) noexcept
    {
        if (!track) return;
        TrackContext* context = GetTrackContext(Native(track));
        if (context && context->stoppedCallbackActive.load(std::memory_order_acquire))
        {
            // Destroying from SDL_mixer's stopped callback would free the stream whose lock the
            // native StopTrack frame still owns. During whole-mixer shutdown its own subsequent
            // destroy loop owns this track; otherwise defer to the next safe engine entry.
            if (!g_mixerShuttingDown.load(std::memory_order_acquire))
                DeferTrackDestruction(context);
            return;
        }
        // A callback-side request already owns this track's eventual destruction. A second
        // caller must not free it while its intrusive node is still queued.
        if (context && context->destructionDeferred.load(std::memory_order_acquire)) return;
        MIX_DestroyTrack(Native(track));
    }

    bool SetMixerTrackAudio(MixerTrack* track, MixerAudio* audio) noexcept
    {
        if (!track || !audio) return false;
        try
        {
            MIX_Audio* native = GetCurrentNative(audio);
            return native && MIX_SetTrackAudio(Native(track), native);
        }
        catch (...)
        {
            return false;
        }
    }

    bool SetMixerTrackStream(MixerTrack* track, MixerStream* stream) noexcept
    {
        return track && MIX_SetTrackAudioStream(Native(track), Native(stream));
    }

    void SetMixerTrackGain(MixerTrack* track, const float gain) noexcept
    {
        if (track) (void)MIX_SetTrackGain(Native(track), gain);
    }

    void SetMixerTrackStereoUnity(MixerTrack* track) noexcept
    {
        static const MIX_StereoGains unity{1.0f, 1.0f};
        if (track) (void)MIX_SetTrackStereo(Native(track), &unity);
    }

    void SetMixerTrackFrequencyRatio(MixerTrack* track, const float ratio) noexcept
    {
        if (track) (void)MIX_SetTrackFrequencyRatio(Native(track), ratio);
    }

    void SetMixerTrackLoops(MixerTrack* track, const int loops) noexcept
    {
        if (track) (void)MIX_SetTrackLoops(Native(track), loops);
    }

    void SetMixerTrackMixCallback(MixerTrack* track, const MixerTrackMixCallback callback,
                                  void* userdata) noexcept
    {
        TrackContext* context = GetTrackContext(Native(track));
        if (!context) return;
        context->mixCallback = callback;
        context->mixUserdata = userdata;
        (void)MIX_SetTrackCookedCallback(Native(track), callback ? MixCallback : nullptr,
                                         callback ? context : nullptr);
    }

    void SetMixerTrackStoppedCallback(MixerTrack* track,
                                      const MixerTrackStoppedCallback callback,
                                      void* userdata) noexcept
    {
        TrackContext* context = GetTrackContext(Native(track));
        if (!context) return;
        context->stoppedCallback = callback;
        context->stoppedUserdata = userdata;
        (void)MIX_SetTrackStoppedCallback(Native(track), callback ? StoppedCallback : nullptr,
                                          callback ? context : nullptr);
    }

    bool PlayMixerTrack(MixerTrack* track, const MixerPlayOptions& options) noexcept
    {
        if (!track) return false;
        if (options.loopCount == 0 && options.haltWhenExhausted
            && !options.hasLoopStartFrame && !options.hasMaxFrame)
        {
            return MIX_PlayTrack(Native(track), 0);
        }

        const SDL_PropertiesID properties = SDL_CreateProperties();
        if (properties == 0) return false;
        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, options.loopCount);
        SDL_SetBooleanProperty(properties, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN,
                               options.haltWhenExhausted);
        if (options.hasLoopStartFrame)
        {
            SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER,
                                  static_cast<Sint64>(options.loopStartFrame));
        }
        if (options.hasMaxFrame)
        {
            SDL_SetNumberProperty(properties, MIX_PROP_PLAY_MAX_FRAME_NUMBER,
                                  static_cast<Sint64>(options.maxFrame));
        }
        const bool result = MIX_PlayTrack(Native(track), properties);
        SDL_DestroyProperties(properties);
        return result;
    }

    void StopMixerTrack(MixerTrack* track) noexcept
    {
        if (track) (void)MIX_StopTrack(Native(track), 0);
    }

    void PauseMixerTrack(MixerTrack* track) noexcept
    {
        if (track) (void)MIX_PauseTrack(Native(track));
    }

    void ResumeMixerTrack(MixerTrack* track) noexcept
    {
        if (track) (void)MIX_ResumeTrack(Native(track));
    }

    bool IsMixerTrackPaused(const MixerTrack* track) noexcept
    {
        return track && MIX_TrackPaused(const_cast<MIX_Track*>(Native(track)));
    }

    bool IsMixerTrackPlaying(const MixerTrack* track) noexcept
    {
        return track && MIX_TrackPlaying(const_cast<MIX_Track*>(Native(track)));
    }

    MixerStream* CreateMixerStream(const MixerFormat& sourceFormat) noexcept
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return nullptr;
        const SDL_AudioSpec spec = ToNativeSpec(sourceFormat);
        SDL_AudioStream* native = SDL_CreateAudioStream(&spec, nullptr);
        if (!native)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return nullptr;
        }
        auto* stream = new (std::nothrow) MixerStream{};
        if (!stream)
        {
            SDL_DestroyAudioStream(native);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return nullptr;
        }
        stream->native = native;
        stream->audioSubsystemAcquired = true;
        return stream;
    }

    void DestroyMixerStream(MixerStream* stream) noexcept
    {
        if (!stream) return;
        if (stream->native) SDL_DestroyAudioStream(stream->native);
        if (stream->audioSubsystemAcquired) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        delete stream;
    }

    void ClearMixerStream(MixerStream* stream) noexcept
    {
        if (stream) (void)SDL_ClearAudioStream(Native(stream));
    }

    int GetMixerStreamQueuedBytes(const MixerStream* stream) noexcept
    {
        return stream ? SDL_GetAudioStreamQueued(const_cast<SDL_AudioStream*>(Native(stream))) : -1;
    }

    bool PutMixerStreamData(MixerStream* stream, const std::span<const std::byte> data) noexcept
    {
        if (!stream || data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return false;
        return SDL_PutAudioStreamData(Native(stream), data.data(), static_cast<int>(data.size()));
    }
}
