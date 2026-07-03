// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"

#include <algorithm>

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "CNA/Internal/Audio/AudioMixer.hpp"
#endif

namespace Microsoft::Xna::Framework::Audio
{
#ifdef SOUND_ENABLED
    namespace
    {
        MIX_Track*        AsTrackD(void* p) { return static_cast<MIX_Track*>(p); }
        SDL_AudioStream*  AsStream(void* p) { return static_cast<SDL_AudioStream*>(p); }
    }
#endif

    DynamicSoundEffectInstance::DynamicSoundEffectInstance(
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
        : SoundEffectInstance(),
          sampleRate_(sampleRate),
          channels_(channels)
    {
    }

    DynamicSoundEffectInstance::~DynamicSoundEffectInstance()
    {
        if (!getIsDisposedProperty())
        {
            Dispose();
        }
    }

    // --- properties ---

    SharpRuntime::intcs DynamicSoundEffectInstance::getPendingBufferCountProperty() const
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return static_cast<SharpRuntime::intcs>(queuedBuffers_.size());
    }

    bool DynamicSoundEffectInstance::getIsLoopedProperty() const
    {
        return false;
    }

    void DynamicSoundEffectInstance::setIsLoopedProperty(const bool& /*looped*/)
    {
        // No-op: DynamicSoundEffectInstance cannot be looped.
    }

    void DynamicSoundEffectInstance::setIsLoopedProperty(bool&& /*looped*/)
    {
        // No-op: DynamicSoundEffectInstance cannot be looped.
    }

    SoundState DynamicSoundEffectInstance::getStateProperty() const
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrackD(dynamicTrack_);
        if (!track) return SoundState::Stopped;
        if (MIX_TrackPaused(track)) return SoundState::Paused;
        if (MIX_TrackPlaying(track)) return SoundState::Playing;
#endif
        return State_;
    }

    System::TimeSpan DynamicSoundEffectInstance::GetSampleDuration(
        SharpRuntime::intcs sizeInBytes) const
    {
        // FNA delegates straight to SoundEffect.GetSampleDuration, which always assumes 16-bit
        // PCM regardless of whether SubmitFloatBufferEXT put this instance in float (32-bit)
        // mode -- match that exactly rather than computing from the real bytes-per-sample.
        return SoundEffect::GetSampleDuration(sizeInBytes, sampleRate_, channels_);
    }

    SharpRuntime::intcs DynamicSoundEffectInstance::GetSampleSizeInBytes(
        System::TimeSpan duration) const
    {
        return SoundEffect::GetSampleSizeInBytes(duration, sampleRate_, channels_);
    }

    // --- playback control ---

    void DynamicSoundEffectInstance::Play()
    {
        if (getIsDisposedProperty())
        {
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        }

        SoundState current = getStateProperty();

        if (current == SoundState::Paused)
        {
#ifdef SOUND_ENABLED
            MIX_Track* track = AsTrackD(dynamicTrack_);
            if (track)
            {
                MIX_ResumeTrack(track);
                State_   = SoundState::Playing;
                playing_ = true;
            }
#endif
            return;
        }

        if (current == SoundState::Playing)
        {
            return;
        }

        // Stopped — set up fresh playback.
        Update(); // request initial buffers before starting

#ifdef SOUND_ENABLED
        EnsureStream();

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();

        // Destroy previous track if any.
        MIX_Track* track = AsTrackD(dynamicTrack_);
        if (!track)
        {
            track = MIX_CreateTrack(mixer);
            if (!track) return;
            dynamicTrack_ = track;
        }

        if (!MIX_SetTrackAudioStream(track, AsStream(audioStream_)))
        {
            return;
        }

        MIX_SetTrackGain(track, getVolumeProperty() * SoundEffect::getMasterVolumeProperty());

        SDL_PropertiesID props = SDL_CreateProperties();
        if (props != 0)
        {
            // Don't halt the track when the stream runs dry; we'll keep feeding it.
            SDL_SetBooleanProperty(props, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false);
            SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
            MIX_PlayTrack(track, props);
            SDL_DestroyProperties(props);
        }
        else
        {
            MIX_PlayTrack(track, 0);
        }

        // Submit any already-queued buffers.
        QueueInitialBuffers();
#endif

        State_   = SoundState::Playing;
        playing_ = true;

        std::lock_guard<std::mutex> lock(Microsoft::Xna::Framework::FrameworkDispatcher::StreamsMutex);
        auto& streams = Microsoft::Xna::Framework::FrameworkDispatcher::Streams;
        if (std::find(streams.begin(), streams.end(), this) == streams.end())
        {
            streams.push_back(this);
        }
    }

    void DynamicSoundEffectInstance::Stop(bool immediate)
    {
        // Matches FNA's early return when there is no active voice/track yet (e.g. Stop() called
        // before any Play()) -- only once playback has actually started does a non-immediate
        // Stop become a meaningful (and, for dynamic instances, invalid) request.
#ifdef SOUND_ENABLED
        if (!AsTrackD(dynamicTrack_))
        {
            return;
        }
#endif
        // FNA throws for a non-immediate Stop on a dynamic instance: there is no authored loop
        // for FAudioSourceVoice_ExitLoop to release into, unlike a static SoundEffectInstance.
        if (!immediate)
        {
            throw System::InvalidOperationException();
        }
        Stop();
    }

    void DynamicSoundEffectInstance::Stop()
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrackD(dynamicTrack_);
        if (track)
        {
            MIX_StopTrack(track, 0);
            MIX_DestroyTrack(track);
            dynamicTrack_ = nullptr;
        }
#endif
        State_   = SoundState::Stopped;
        playing_ = false;

        ClearBuffers();

        std::lock_guard<std::mutex> lock(Microsoft::Xna::Framework::FrameworkDispatcher::StreamsMutex);
        auto& streams = Microsoft::Xna::Framework::FrameworkDispatcher::Streams;
        streams.erase(std::remove(streams.begin(), streams.end(), this), streams.end());
    }

    void DynamicSoundEffectInstance::Dispose()
    {
        if (getIsDisposedProperty())
        {
            return;
        }
        Stop();                          // stops/destroys the dynamic track and deregisters from the dispatcher
        DestroyStream();                 // frees the SDL audio stream
        SoundEffectInstance::Dispose();  // releases the base track (none) and marks the instance disposed
    }

    // --- buffer submission ---

    void DynamicSoundEffectInstance::SubmitBuffer(
        const std::vector<SharpRuntime::bytecs>& buffer)
    {
        SubmitBuffer(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    void DynamicSoundEffectInstance::SubmitBuffer(
        const std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count)
    {
        if (offset < 0 || count < 0 ||
            offset + count > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw System::ArgumentOutOfRangeException("count");
        }

        std::vector<SharpRuntime::bytecs> chunk(
            buffer.begin() + offset,
            buffer.begin() + offset + count
        );

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queuedBuffers_.push_back(std::move(chunk));
        }

        // If we are already playing, submit immediately to the stream.
        if (getStateProperty() == SoundState::Playing)
        {
            SubmitQueuedToStream();
        }
    }

    void DynamicSoundEffectInstance::SubmitFloatBufferEXT(
        const std::vector<float>& buffer)
    {
        SubmitFloatBufferEXT(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    void DynamicSoundEffectInstance::SubmitFloatBufferEXT(
        const std::vector<float>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count)
    {
        if (offset < 0 || count < 0 ||
            offset + count > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw System::ArgumentOutOfRangeException("count");
        }

        // The format may only switch from int to float while stopped; otherwise the live
        // S16 stream would be fed F32 bytes. EnsureStream rebuilds the stream on the next Play.
        if (!isFloat_ && getStateProperty() != SoundState::Stopped)
        {
            throw System::InvalidOperationException("Submit a float buffer before Playing!");
        }

        isFloat_ = true;

        const auto* bytes = reinterpret_cast<const SharpRuntime::bytecs*>(buffer.data() + offset);
        const std::size_t byteCount = static_cast<std::size_t>(count) * sizeof(float);

        std::vector<SharpRuntime::bytecs> chunk(bytes, bytes + byteCount);

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queuedBuffers_.push_back(std::move(chunk));
        }

        if (getStateProperty() == SoundState::Playing)
        {
            SubmitQueuedToStream();
        }
    }

    void DynamicSoundEffectInstance::QueueInitialBuffers()
    {
        SubmitQueuedToStream();
    }

    void DynamicSoundEffectInstance::ClearBuffers()
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queuedBuffers_.clear();
        }

#ifdef SOUND_ENABLED
        SDL_AudioStream* stream = AsStream(audioStream_);
        if (stream)
        {
            SDL_ClearAudioStream(stream);
        }
#endif
    }

    void DynamicSoundEffectInstance::Update()
    {
        if (getStateProperty() != SoundState::Playing)
        {
            return;
        }

        // Submit any freshly queued buffers.
        SubmitQueuedToStream();

        // Raise BufferNeeded while we are short of MINIMUM_BUFFER_CHECK worth of data.
        for (
            SharpRuntime::intcs i = MINIMUM_BUFFER_CHECK - getPendingBufferCountProperty();
            (i > 0) && !BufferNeeded.Empty();
            --i
        )
        {
            BufferNeeded.Raise(this, System::EventArgs::Empty);
        }
    }

    // --- private helpers ---

    void DynamicSoundEffectInstance::EnsureStream()
    {
#ifdef SOUND_ENABLED
        if (audioStream_ && streamIsFloat_ == isFloat_) return;
        if (audioStream_)
        {
            // The sample format changed (int -> float) since the stream was created; rebuild it.
            DestroyStream();
        }

        SDL_AudioSpec spec{};
        spec.format   = isFloat_ ? SDL_AUDIO_F32LE : SDL_AUDIO_S16LE;
        spec.channels = static_cast<int>(channels_);
        spec.freq     = sampleRate_;

        // dst_spec nullptr → SDL3 uses the device's native format for output conversion.
        audioStream_ = SDL_CreateAudioStream(&spec, nullptr);
        streamIsFloat_ = isFloat_;
#endif
    }

    void DynamicSoundEffectInstance::DestroyStream()
    {
#ifdef SOUND_ENABLED
        SDL_AudioStream* stream = AsStream(audioStream_);
        if (stream)
        {
            SDL_DestroyAudioStream(stream);
            audioStream_ = nullptr;
        }
#endif
    }

    void DynamicSoundEffectInstance::SubmitQueuedToStream()
    {
#ifdef SOUND_ENABLED
        SDL_AudioStream* stream = AsStream(audioStream_);
        if (!stream) return;

        std::vector<std::vector<SharpRuntime::bytecs>> toSubmit;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            toSubmit.swap(queuedBuffers_);
        }

        for (const auto& chunk : toSubmit)
        {
            SDL_PutAudioStreamData(
                stream,
                chunk.data(),
                static_cast<int>(chunk.size())
            );
        }
#endif
    }

    GetTypeNameCPP(DynamicSoundEffectInstance, "Microsoft.Xna.Framework.Audio.DynamicSoundEffectInstance")
}
