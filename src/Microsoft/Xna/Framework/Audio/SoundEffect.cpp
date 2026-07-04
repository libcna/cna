// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"

#include <algorithm>
#include <istream>
#include <vector>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "CNA/Internal/Audio/AudioMixer.hpp"
#endif

namespace Microsoft::Xna::Framework::Audio
{
    class SoundEffect::Impl
    {
    public:
#ifdef SOUND_ENABLED
        std::shared_ptr<MIX_Audio> audio;
        SharpRuntime::intcs sampleRate = 44100;
        SharpRuntime::uintcs channels  = 2;
#endif
        // Live SoundEffectInstance objects created via CreateInstance(), for Dispose()'s
        // cascade (T-3G). Raw, non-owning pointers: the instances' lifetime belongs to their
        // caller; SoundEffectInstance registers/unregisters itself (see SoundEffect::Register/
        // UnregisterInstance). Not gated by SOUND_ENABLED -- this is pure object-lifecycle
        // bookkeeping, independent of the audio backend.
        std::vector<SoundEffectInstance*> instances;
    };

    void SoundEffect::RegisterInstance(const std::shared_ptr<void>& keepAlive, SoundEffectInstance* instance)
    {
        if (!keepAlive || !instance) return;
        static_cast<Impl*>(keepAlive.get())->instances.push_back(instance);
    }

    void SoundEffect::UnregisterInstance(const std::shared_ptr<void>& keepAlive, SoundEffectInstance* instance)
    {
        if (!keepAlive || !instance) return;
        auto& v = static_cast<Impl*>(keepAlive.get())->instances;
        v.erase(std::remove(v.begin(), v.end(), instance), v.end());
    }

    // --- static members ---

    float SoundEffect::MasterVolume_   = 1.0f;
    float SoundEffect::DistanceScale_  = 1.0f;
    float SoundEffect::DopplerScale_   = 1.0f;
    float SoundEffect::SpeedOfSound_   = 343.5f;

    // --- internal helpers ---

#ifdef SOUND_ENABLED
    namespace
    {
        void SDLCALL OnFireAndForgetStopped(void* /*userdata*/, MIX_Track* track)
        {
            MIX_DestroyTrack(track);
        }
    }
#endif

    // --- private constructor ---

    SoundEffect::SoundEffect(std::shared_ptr<Impl> impl, std::string name)
        : impl_(std::move(impl)), name_(std::move(name))
    {
    }

    // --- public constructors ---

    SoundEffect::SoundEffect(const std::string& assetName)
        : impl_(std::make_shared<Impl>())
    {
        if (assetName.empty())
        {
            return;
        }

#ifdef SOUND_ENABLED
        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();

        MIX_Audio* raw = MIX_LoadAudio(mixer, assetName.c_str(), true);
        if (!raw)
        {
            throw System::NotSupportedException(
                "Failed to load sound: " + assetName + " — " + SDL_GetError()
            );
        }

        impl_->audio = {raw, [](MIX_Audio* p) { if (p) MIX_DestroyAudio(p); }};

        SDL_AudioSpec spec{};
        if (MIX_GetAudioFormat(raw, &spec))
        {
            impl_->sampleRate = spec.freq;
            impl_->channels   = static_cast<SharpRuntime::uintcs>(spec.channels);
        }
#else
        (void)assetName;
#endif
    }

    SoundEffect::SoundEffect(
        const std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
        : SoundEffect(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()),
                      sampleRate, channels, 0, 0)
    {
    }

    SoundEffect::SoundEffect(
        const std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels,
        SharpRuntime::intcs loopStart,
        SharpRuntime::intcs loopLength)
        : impl_(std::make_shared<Impl>())
    {
        loopStart_  = static_cast<SharpRuntime::uintcs>(loopStart);
        loopLength_ = static_cast<SharpRuntime::uintcs>(loopLength);

        if (count < 0 || offset < 0 ||
            offset + count > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw System::ArgumentOutOfRangeException("count");
        }

#ifdef SOUND_ENABLED
        SDL_AudioSpec spec{};
        spec.format   = SDL_AUDIO_S16LE;
        spec.channels = static_cast<int>(channels);
        spec.freq     = sampleRate;

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();

        MIX_Audio* raw = MIX_LoadRawAudio(
            mixer,
            buffer.data() + offset,
            static_cast<std::size_t>(count),
            &spec
        );
        if (!raw)
        {
            throw System::NotSupportedException(
                std::string("Failed to create sound from buffer: ") + SDL_GetError()
            );
        }

        impl_->audio      = {raw, [](MIX_Audio* p) { if (p) MIX_DestroyAudio(p); }};
        impl_->sampleRate = sampleRate;
        impl_->channels   = static_cast<SharpRuntime::uintcs>(channels);
#else
        (void)offset;
        (void)count;
        (void)sampleRate;
        (void)channels;
#endif
    }

    SoundEffect::~SoundEffect() = default;

    // --- properties ---

    System::TimeSpan SoundEffect::getDurationProperty() const
    {
#ifdef SOUND_ENABLED
        if (impl_ && impl_->audio && impl_->sampleRate > 0)
        {
            Sint64 frames = MIX_GetAudioDuration(impl_->audio.get());
            if (frames > 0)
            {
                return System::TimeSpan::FromSeconds(
                    static_cast<double>(frames) / impl_->sampleRate
                );
            }
        }
#endif
        return System::TimeSpan::Zero;
    }

    bool SoundEffect::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    const std::string& SoundEffect::getNameProperty() const
    {
        return name_;
    }

    void SoundEffect::setNameProperty(const std::string& value)
    {
        name_ = value;
    }

    void SoundEffect::setNameProperty(std::string&& value)
    {
        name_ = std::move(value);
    }

    float SoundEffect::getMasterVolumeProperty()
    {
#ifdef SOUND_ENABLED
        // CP-16: query the real SDL3_mixer master gain (matches FNA, which likewise always
        // queries the live FAudio master voice rather than a cached value) so this reflects
        // MIX_SetMixerGain's actual current value, not a value that could drift from it.
        return MIX_GetMixerGain(CNA::Internal::Audio::GetMixer());
#else
        return MasterVolume_;
#endif
    }

    void SoundEffect::setMasterVolumeProperty(const float& v)
    {
#ifdef SOUND_ENABLED
        // CP-16: SDL3_mixer's mixer gain is a real global, applied to every track (including
        // already-playing ones) at mix time -- unlike the old per-track-baked-in approach, this
        // needs no per-instance re-application. FNA passes the value straight through, without
        // clamping; MIX_SetMixerGain does the same (only rejects negative values as an error).
        MIX_SetMixerGain(CNA::Internal::Audio::GetMixer(), v);
#else
        MasterVolume_ = v;
#endif
    }

    void SoundEffect::setMasterVolumeProperty(float&& v)
    {
        setMasterVolumeProperty(v);
    }

    float SoundEffect::getDistanceScaleProperty()
    {
        return DistanceScale_;
    }

    void SoundEffect::setDistanceScaleProperty(float value)
    {
        if (value <= 0.0f)
        {
            throw System::ArgumentOutOfRangeException("value <= 0.0f");
        }
        DistanceScale_ = value;
    }

    float SoundEffect::getDopplerScaleProperty()
    {
        return DopplerScale_;
    }

    void SoundEffect::setDopplerScaleProperty(float value)
    {
        if (value < 0.0f)
        {
            throw System::ArgumentOutOfRangeException("value < 0.0f");
        }
        DopplerScale_ = value;
    }

    float SoundEffect::getSpeedOfSoundProperty()
    {
        return SpeedOfSound_;
    }

    void SoundEffect::setSpeedOfSoundProperty(float value)
    {
        SpeedOfSound_ = value;
    }

    // --- methods ---

    SoundEffectInstance SoundEffect::CreateInstance() const
    {
        return SoundEffectInstance(*this);
    }

    bool SoundEffect::Play()
    {
        return Play(1.0f, 0.0f, 0.0f);
    }

    bool SoundEffect::Play(float volume, float pitch, float pan)
    {
        if (isDisposed_)
        {
            return false;
        }

        // FNA constructs a real SoundEffectInstance and assigns Volume/Pitch/Pan through its
        // property setters before Play(); mirror their validation here: Pan is range-checked
        // (throws), Pitch is clamped rather than validated.
        if (pan > 1.0f || pan < -1.0f)
        {
            throw System::ArgumentOutOfRangeException("pan");
        }
        pitch = (pitch < -1.0f) ? -1.0f : ((pitch > 1.0f) ? 1.0f : pitch);

#ifdef SOUND_ENABLED
        auto* audio = static_cast<MIX_Audio*>(getNativeAudioHandle());
        if (!audio)
        {
            return false;
        }

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_Track* track = MIX_CreateTrack(mixer);
        if (!track)
        {
            return false;
        }

        if (!MIX_SetTrackAudio(track, audio))
        {
            MIX_DestroyTrack(track);
            return false;
        }

        // CP-16: master volume is applied once, globally, via MIX_SetMixerGain (the mixer's own
        // master gain stage) -- not baked into each track's own gain, which would double-apply it.
        MIX_SetTrackGain(track, volume);

        MIX_StereoGains stereo{};
        stereo.left  = (pan < 0.0f) ? 1.0f : (1.0f - pan);
        stereo.right = (pan > 0.0f) ? 1.0f : (1.0f + pan);
        MIX_SetTrackStereo(track, &stereo);

        if (pitch != 0.0f)
        {
            const float ratio = (pitch < 0.0f)
                ? (1.0f + pitch * 0.5f)
                : (1.0f + pitch);
            MIX_SetTrackFrequencyRatio(track, ratio < 0.01f ? 0.01f : ratio);
        }

        // Auto-destroy track when playback finishes.
        MIX_SetTrackStoppedCallback(track, OnFireAndForgetStopped, nullptr);

        if (!MIX_PlayTrack(track, 0))
        {
            MIX_DestroyTrack(track);
            return false;
        }

        return true;
#else
        (void)volume; (void)pitch; (void)pan;
        return false;
#endif
    }

    void SoundEffect::Dispose()
    {
        if (!isDisposed_)
        {
            if (impl_)
            {
                // instance->Dispose() unregisters itself from impl_->instances, mutating the
                // live vector -- iterate a snapshot so that's safe (matches FNA's
                // Instances.ToArray() before the foreach in SoundEffect.Dispose()).
                auto instancesSnapshot = impl_->instances;
                for (auto* instance : instancesSnapshot)
                {
                    if (instance) instance->Dispose();
                }
            }
            impl_.reset();
            isDisposed_ = true;
        }
    }

    void* SoundEffect::getNativeAudioHandle() const
    {
#ifdef SOUND_ENABLED
        if (impl_ && impl_->audio)
        {
            return impl_->audio.get();
        }
#endif
        return nullptr;
    }

    // --- static methods ---

    System::TimeSpan SoundEffect::GetSampleDuration(
        SharpRuntime::intcs sizeInBytes,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
    {
        const int ch = static_cast<int>(channels);
        if (ch <= 0 || sampleRate <= 0)
        {
            return System::TimeSpan::Zero;
        }
        // Matches FNA: truncate to whole milliseconds. 16-bit PCM => 2 bytes per sample.
        const int samples = sizeInBytes / 2;
        const int ms = static_cast<int>(
            (samples / ch) / (sampleRate / 1000.0f)
        );
        return System::TimeSpan::FromMilliseconds(ms);
    }

    SharpRuntime::intcs SoundEffect::GetSampleSizeInBytes(
        System::TimeSpan duration,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
    {
        return static_cast<SharpRuntime::intcs>(
            duration.getTotalSecondsProperty() *
            sampleRate *
            static_cast<int>(channels) *
            2 // 16-bit PCM
        );
    }

    SoundEffect* SoundEffect::FromStream(std::istream& stream)
    {
        // Read all bytes from the stream.
        std::vector<char> bytes(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>()
        );

        if (bytes.empty())
        {
            throw System::NotSupportedException("SoundEffect::FromStream: empty stream");
        }

#ifdef SOUND_ENABLED
        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
        if (!io)
        {
            throw System::NotSupportedException(
                std::string("SoundEffect::FromStream: SDL_IOFromConstMem failed: ") + SDL_GetError()
            );
        }

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_Audio* raw   = MIX_LoadAudio_IO(mixer, io, true, true); // predecode=true, closeio=true
        if (!raw)
        {
            throw System::NotSupportedException(
                std::string("SoundEffect::FromStream: MIX_LoadAudio_IO failed: ") + SDL_GetError()
            );
        }

        auto implPtr = std::make_shared<Impl>();
        implPtr->audio = {raw, [](MIX_Audio* p) { if (p) MIX_DestroyAudio(p); }};

        SDL_AudioSpec spec{};
        if (MIX_GetAudioFormat(raw, &spec))
        {
            implPtr->sampleRate = spec.freq;
            implPtr->channels   = static_cast<SharpRuntime::uintcs>(spec.channels);
        }

        return new SoundEffect(std::move(implPtr));
#else
        (void)bytes;
        return new SoundEffect(std::make_shared<Impl>());
#endif
    }

    GetTypeNameCPP(SoundEffect, "Microsoft.Xna.Framework.Audio.SoundEffect")
}
