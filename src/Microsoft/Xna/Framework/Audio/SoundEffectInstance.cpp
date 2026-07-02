// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
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
        MIX_Track* AsTrack(void* p)
        {
            return static_cast<MIX_Track*>(p);
        }

        void ApplyTrackProperties(MIX_Track* track, float volume, float masterVolume,
                                  float pan, float pitch)
        {
            if (!track) return;

            MIX_SetTrackGain(track, volume * masterVolume);

            MIX_StereoGains stereo{};
            stereo.left  = (pan < 0.0f) ? 1.0f : (1.0f - pan);
            stereo.right = (pan > 0.0f) ? 1.0f : (1.0f + pan);
            MIX_SetTrackStereo(track, &stereo);

            const float ratio = (pitch < 0.0f)
                ? (1.0f + pitch * 0.5f)
                : (1.0f + pitch);
            MIX_SetTrackFrequencyRatio(track, ratio < 0.01f ? 0.01f : ratio);
        }

        void DestroyTrackSafe(void*& trackPtr)
        {
            MIX_Track* track = AsTrack(trackPtr);
            if (track)
            {
                MIX_StopTrack(track, 0);
                MIX_DestroyTrack(track);
                trackPtr = nullptr;
            }
        }
    }
#endif

    SoundEffectInstance::SoundEffectInstance()
        : soundEffect_(nullptr)
    {
    }

    SoundEffectInstance::SoundEffectInstance(const SoundEffect& soundEffect)
        : soundEffect_(&soundEffect)
    {
    }

    SoundEffectInstance::~SoundEffectInstance()
    {
        if (!isDisposed_)
        {
            Dispose();
        }
    }

    SoundEffectInstance::SoundEffectInstance(SoundEffectInstance&& other) noexcept
        : soundEffect_(other.soundEffect_)
        , track_(other.track_)
        , playing_(other.playing_)
        , hasStarted_(other.hasStarted_)
        , State_(other.State_)
        , IsLooped_(other.IsLooped_)
        , isDisposed_(other.isDisposed_)
        , Volume_(other.Volume_)
        , Pan_(other.Pan_)
        , Pitch_(other.Pitch_)
    {
        other.soundEffect_ = nullptr;
        other.track_       = nullptr;
        other.playing_     = false;
        other.hasStarted_  = false;
        other.State_       = SoundState::Stopped;
        other.isDisposed_  = true;
    }

    SoundEffectInstance& SoundEffectInstance::operator=(SoundEffectInstance&& other) noexcept
    {
        if (this != &other)
        {
#ifdef SOUND_ENABLED
            DestroyTrackSafe(track_);
#endif
            soundEffect_ = other.soundEffect_;
            track_       = other.track_;
            playing_     = other.playing_;
            hasStarted_  = other.hasStarted_;
            State_       = other.State_;
            IsLooped_    = other.IsLooped_;
            isDisposed_  = other.isDisposed_;
            Volume_      = other.Volume_;
            Pan_         = other.Pan_;
            Pitch_       = other.Pitch_;

            other.soundEffect_ = nullptr;
            other.track_       = nullptr;
            other.playing_     = false;
            other.hasStarted_  = false;
            other.State_       = SoundState::Stopped;
            other.isDisposed_  = true;
        }
        return *this;
    }

    void SoundEffectInstance::Dispose()
    {
        if (!isDisposed_)
        {
#ifdef SOUND_ENABLED
            DestroyTrackSafe(track_);
#endif
            playing_    = false;
            State_      = SoundState::Stopped;
            isDisposed_ = true;
        }
    }

    void SoundEffectInstance::Play()
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("SoundEffectInstance");
        }

        // Already playing: no-op, matching FNA exactly (a naive re-Play would otherwise restart
        // the track from the beginning instead of leaving ongoing playback untouched).
        if (getStateProperty() == SoundState::Playing)
        {
            return;
        }

        // Once started, IsLooped can no longer be changed (matches FNA's hasStarted gate).
        hasStarted_ = true;

#ifdef SOUND_ENABLED
        // If paused, resume instead of restarting.
        if (State_ == SoundState::Paused)
        {
            MIX_Track* track = AsTrack(track_);
            if (track)
            {
                MIX_ResumeTrack(track);
                State_   = SoundState::Playing;
                playing_ = true;
                return;
            }
        }

        if (!soundEffect_)
        {
            State_   = SoundState::Stopped;
            playing_ = false;
            return;
        }

        auto* audio = static_cast<MIX_Audio*>(soundEffect_->getNativeAudioHandle());
        if (!audio)
        {
            State_   = SoundState::Stopped;
            playing_ = false;
            return;
        }

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();

        MIX_Track* track = AsTrack(track_);
        if (!track)
        {
            track = MIX_CreateTrack(mixer);
            if (!track)
            {
                State_   = SoundState::Stopped;
                playing_ = false;
                return;
            }
            track_ = track;
        }

        if (!MIX_SetTrackAudio(track, audio))
        {
            State_   = SoundState::Stopped;
            playing_ = false;
            return;
        }

        ApplyTrackProperties(track, Volume_, SoundEffect::getMasterVolumeProperty(), Pan_, Pitch_);

        SDL_PropertiesID props = SDL_CreateProperties();
        if (props == 0)
        {
            State_   = SoundState::Stopped;
            playing_ = false;
            return;
        }

        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, IsLooped_ ? -1 : 0);

        const bool ok = MIX_PlayTrack(track, props);
        SDL_DestroyProperties(props);

        if (!ok)
        {
            State_   = SoundState::Stopped;
            playing_ = false;
            return;
        }

        playing_ = true;
        State_   = SoundState::Playing;
#else
        State_   = SoundState::Stopped;
        playing_ = false;
#endif
    }

    void SoundEffectInstance::Stop()
    {
        Stop(true);
    }

    void SoundEffectInstance::Stop(bool immediate)
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track)
        {
            if (immediate)
            {
                MIX_StopTrack(track, 0);
            }
            else
            {
                // Exit loop so the track plays to the end and stops naturally.
                MIX_SetTrackLoops(track, 0);
            }
        }
#endif
        if (immediate)
        {
            playing_ = false;
            State_   = SoundState::Stopped;
        }
    }

    void SoundEffectInstance::Pause()
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track && getStateProperty() == SoundState::Playing)
        {
            MIX_PauseTrack(track);
            State_   = SoundState::Paused;
            playing_ = false;
        }
#endif
    }

    void SoundEffectInstance::Resume()
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track && getStateProperty() == SoundState::Paused)
        {
            MIX_ResumeTrack(track);
            State_   = SoundState::Playing;
            playing_ = true;
        }
#endif
    }

    void SoundEffectInstance::Apply3D(const AudioListener& listener, const AudioEmitter& emitter)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("SoundEffectInstance");
        }

        // SDL3_mixer does not support full 3D spatial audio (Doppler, HRTF, orientation).
        // This is a simplified linear distance/pan approximation.

        const auto& lp = listener.getPositionProperty();
        const auto& ep = emitter.getPositionProperty();

        const float dx = ep.X - lp.X;
        const float dy = ep.Y - lp.Y;
        const float dz = ep.Z - lp.Z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        const float distScale = SoundEffect::getDistanceScaleProperty();
        const float atten = std::clamp(
            1.0f / (1.0f + distance / (distScale > 0.0f ? distScale : 1.0f)), 0.0f, 1.0f);

        const float pan = (distance > 0.0f)
            ? std::clamp(dx / distance, -1.0f, 1.0f)
            : 0.0f;

#ifdef SOUND_ENABLED
        // Applied directly to the underlying track, not through setVolumeProperty()/
        // setPanProperty(): FNA computes a separate 3D output matrix (dspSettings) that combines
        // multiplicatively with the voice's own Volume at the audio-engine level and never
        // touches INTERNAL_volume/INTERNAL_pan, so Volume/Pan continue to report exactly what the
        // caller last set via the setters, unaffected by 3D positioning.
        ApplyTrackProperties(AsTrack(track_), atten * Volume_,
                              SoundEffect::getMasterVolumeProperty(), pan, Pitch_);
#endif
    }

    void SoundEffectInstance::Apply3D(const AudioListener* listeners, int listenerCount,
                                      const AudioEmitter& emitter)
    {
        if (listeners == nullptr)
        {
            throw System::ArgumentNullException("listeners");
        }
        if (listenerCount == 1)
        {
            Apply3D(listeners[0], emitter);
            return;
        }
        throw System::NotSupportedException("Only one listener is supported.");
    }

    bool SoundEffectInstance::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    float SoundEffectInstance::getVolumeProperty() const
    {
        return Volume_;
    }

    void SoundEffectInstance::setVolumeProperty(const float& volume)
    {
        Volume_ = volume; // FNA passes the value straight through, without clamping

#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track)
        {
            MIX_SetTrackGain(track, Volume_ * SoundEffect::getMasterVolumeProperty());
        }
#endif
    }

    void SoundEffectInstance::setVolumeProperty(float&& volume)
    {
        setVolumeProperty(volume);
    }

    float SoundEffectInstance::getPanProperty() const
    {
        return Pan_;
    }

    void SoundEffectInstance::setPanProperty(const float& pan)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("SoundEffectInstance");
        }
        if (pan > 1.0f || pan < -1.0f)
        {
            throw System::ArgumentOutOfRangeException("value");
        }
        Pan_ = pan;

#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track)
        {
            MIX_StereoGains stereo{};
            stereo.left  = (Pan_ < 0.0f) ? 1.0f : (1.0f - Pan_);
            stereo.right = (Pan_ > 0.0f) ? 1.0f : (1.0f + Pan_);
            MIX_SetTrackStereo(track, &stereo);
        }
#endif
    }

    void SoundEffectInstance::setPanProperty(float&& pan)
    {
        setPanProperty(pan);
    }

    float SoundEffectInstance::getPitchProperty() const
    {
        return Pitch_;
    }

    void SoundEffectInstance::setPitchProperty(const float& pitch)
    {
        Pitch_ = (pitch < -1.0f) ? -1.0f : ((pitch > 1.0f) ? 1.0f : pitch);

#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track)
        {
            const float ratio = (Pitch_ < 0.0f)
                ? (1.0f + Pitch_ * 0.5f)
                : (1.0f + Pitch_);
            MIX_SetTrackFrequencyRatio(track, ratio < 0.01f ? 0.01f : ratio);
        }
#endif
    }

    void SoundEffectInstance::setPitchProperty(float&& pitch)
    {
        setPitchProperty(pitch);
    }

    bool SoundEffectInstance::getIsLoopedProperty() const
    {
        return IsLooped_;
    }

    void SoundEffectInstance::setIsLoopedProperty(const bool& looped)
    {
        if (hasStarted_)
        {
            throw System::InvalidOperationException();
        }
        IsLooped_ = looped;
    }

    void SoundEffectInstance::setIsLoopedProperty(bool&& looped)
    {
        setIsLoopedProperty(looped);
    }

    SoundState SoundEffectInstance::getStateProperty() const
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (!track)
        {
            return SoundState::Stopped;
        }
        if (MIX_TrackPaused(track))
        {
            return SoundState::Paused;
        }
        if (MIX_TrackPlaying(track))
        {
            return SoundState::Playing;
        }
        // Track finished playing; sync internal state.
        const_cast<SoundEffectInstance*>(this)->playing_ = false;
        const_cast<SoundEffectInstance*>(this)->State_   = SoundState::Stopped;
        return SoundState::Stopped;
#else
        return State_;
#endif
    }

    GetTypeNameCPP(SoundEffectInstance, "Microsoft.Xna.Framework.Audio.SoundEffectInstance")
}
