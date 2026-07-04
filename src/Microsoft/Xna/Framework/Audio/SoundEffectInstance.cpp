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
    // T-4C: DSP filter state (see SoundEffectInstance.hpp's filterState_ for the ownership/
    // move-safety rationale). Kind/frequency/oneOverQ are written by INTERNAL_apply*Filter (main
    // thread) and read by FilterMixCallback (SDL3_mixer's mixing thread) -- guarded by
    // MIX_LockMixer/UnlockMixer in the writer, relying on SDL3_mixer's own documented guarantee
    // that "the SDL audio device thread [holds this same lock] while actual mixing is in
    // progress" (so the callback itself must NOT also lock -- it would be redundant at best).
    // yl/yb are the filter's per-channel recursive state and are touched ONLY by the mixing
    // thread inside the callback, never by the setters, so they need no synchronization at all.
    struct FilterState
    {
        enum class Kind { None, LowPass, HighPass, BandPass };
        Kind  kind        = Kind::None;
        float frequency   = 0.0f;
        float oneOverQ    = 1.0f;
        float yl[2]       = {0.0f, 0.0f};
        float yb[2]       = {0.0f, 0.0f};
    };

#ifdef SOUND_ENABLED
    namespace
    {
        MIX_Track* AsTrack(void* p)
        {
            return static_cast<MIX_Track*>(p);
        }

        // CP-16: master volume is applied once, globally, via MIX_SetMixerGain (SDL3_mixer's own
        // master gain stage), not baked into each track's own gain here -- doing both would
        // double-apply it, and only the mixer-level gain re-applies live to already-playing
        // tracks without this function needing to be called again.
        void ApplyTrackProperties(MIX_Track* track, float volume, float pan, float pitch)
        {
            if (!track) return;

            MIX_SetTrackGain(track, volume);

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

        // T-4C: FAudio's exact state-variable filter (Chamberlin SVF; see FAudio_internal.c's
        // FAudio_INTERNAL_FilterVoice). Pure math, independent of SDL3_mixer, so it can also be
        // driven directly and synchronously by SoundEffectInstanceTestAccess -- MIX_Track's real
        // callback only fires asynchronously from the mixing thread, which would make a test
        // either flaky or need a real-time wait.
        void ProcessFilterState(FilterState& state, float* pcm, int channels, int samples)
        {
            if (state.kind == FilterState::Kind::None) return;
            if (channels <= 0) return;
            const float f  = state.frequency;
            const float q1 = state.oneOverQ;

            for (int i = 0; i + channels <= samples; i += channels)
            {
                for (int c = 0; c < channels && c < 2; ++c)
                {
                    float& yl = state.yl[c];
                    float& yb = state.yb[c];
                    const float x = pcm[i + c];

                    yl = yl + f * yb;
                    const float yh = x - yl - q1 * yb;
                    yb = f * yh + yb;

                    switch (state.kind)
                    {
                        case FilterState::Kind::LowPass:  pcm[i + c] = yl; break;
                        case FilterState::Kind::HighPass: pcm[i + c] = yh; break;
                        case FilterState::Kind::BandPass: pcm[i + c] = yb; break;
                        default: break; // Kind::None already returned above
                    }
                }
            }
        }

        // SDL3_mixer trampoline: fires as a per-track "cooked" callback (after gain/pan/3D are
        // applied, right before this track's audio is mixed into the output -- the closest
        // SDL3_mixer equivalent to FAudio's per-voice filter). `userdata` is the instance's
        // FilterState*, kept alive by its own unique_ptr (see SoundEffectInstance.hpp)
        // independent of the SoundEffectInstance's own address, so this stays valid even if the
        // instance is later moved.
        void SDLCALL FilterMixCallback(void* userdata, MIX_Track* /*track*/,
                                        const SDL_AudioSpec* spec, float* pcm, int samples)
        {
            ProcessFilterState(*static_cast<FilterState*>(userdata), pcm, spec->channels, samples);
        }
    }
#endif

    SoundEffectInstance::SoundEffectInstance()
    {
    }

    SoundEffectInstance::SoundEffectInstance(const SoundEffect& soundEffect)
        // Capture the underlying audio resource (via SoundEffect's private impl_) and the
        // native handle now, while soundEffect is definitely alive -- Play() must never
        // dereference the SoundEffect itself, since a common chaining pattern like
        // SoundEffect(path).CreateInstance() destroys it immediately (CP-7).
        : soundEffectKeepAlive_(soundEffect.impl_)
        , nativeAudioHandle_(soundEffect.getNativeAudioHandle())
        , loopStart_(soundEffect.loopStart_)
        , loopLength_(soundEffect.loopLength_)
    {
        // Register for SoundEffect::Dispose()'s cascade (T-3G, matches FNA's
        // parentEffect.Instances.Add(selfReference) in SoundEffectInstance's ctor).
        SoundEffect::RegisterInstance(soundEffectKeepAlive_, this);
    }

    SoundEffectInstance::~SoundEffectInstance()
    {
        if (!isDisposed_)
        {
            Dispose();
        }
    }

    SoundEffectInstance::SoundEffectInstance(SoundEffectInstance&& other) noexcept
        : soundEffectKeepAlive_(std::move(other.soundEffectKeepAlive_))
        , nativeAudioHandle_(other.nativeAudioHandle_)
        , loopStart_(other.loopStart_)
        , loopLength_(other.loopLength_)
        , track_(other.track_)
        , playing_(other.playing_)
        , hasStarted_(other.hasStarted_)
        , State_(other.State_)
        , IsLooped_(other.IsLooped_)
        , isDisposed_(other.isDisposed_)
        , Volume_(other.Volume_)
        , Pan_(other.Pan_)
        , Pitch_(other.Pitch_)
        , is3D_(other.is3D_)
        // filterState_ is heap-owned; moving the unique_ptr transfers ownership without moving
        // the FilterState object's address, so the callback registered on `track_` (also just
        // transferred, unchanged) stays valid with no re-registration needed (T-4C).
        , filterState_(std::move(other.filterState_))
    {
        // Re-point Dispose()-cascade tracking from &other to &this (T-3G) -- other's own address
        // must stop being cascade-targeted, since it no longer represents a live instance once
        // moved-from (only skip this for an already-disposed `other`, which was never tracked,
        // or has already unregistered itself).
        if (soundEffectKeepAlive_ && !isDisposed_)
        {
            SoundEffect::UnregisterInstance(soundEffectKeepAlive_, &other);
            SoundEffect::RegisterInstance(soundEffectKeepAlive_, this);
        }

        other.nativeAudioHandle_ = nullptr;
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
            // Unregister *this* from whatever SoundEffect it was previously tracked by --
            // once soundEffectKeepAlive_ is overwritten below, *this* address represents a
            // different (or no) instance, and the old SoundEffect must not cascade to it (T-3G).
            if (soundEffectKeepAlive_ && !isDisposed_)
                SoundEffect::UnregisterInstance(soundEffectKeepAlive_, this);

            soundEffectKeepAlive_ = std::move(other.soundEffectKeepAlive_);
            nativeAudioHandle_ = other.nativeAudioHandle_;
            loopStart_   = other.loopStart_;
            loopLength_  = other.loopLength_;
            track_       = other.track_;
            playing_     = other.playing_;
            hasStarted_  = other.hasStarted_;
            State_       = other.State_;
            IsLooped_    = other.IsLooped_;
            isDisposed_  = other.isDisposed_;
            Volume_      = other.Volume_;
            Pan_         = other.Pan_;
            Pitch_       = other.Pitch_;
            is3D_        = other.is3D_;
            // See the move constructor's identical rationale for why no callback
            // re-registration is needed here (T-4C).
            filterState_ = std::move(other.filterState_);

            // Re-point tracking from &other to &this in the SoundEffect whose keepAlive we just
            // took over (see the move constructor's identical rationale).
            if (soundEffectKeepAlive_ && !isDisposed_)
            {
                SoundEffect::UnregisterInstance(soundEffectKeepAlive_, &other);
                SoundEffect::RegisterInstance(soundEffectKeepAlive_, this);
            }

            other.nativeAudioHandle_ = nullptr;
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
            if (soundEffectKeepAlive_)
            {
                SoundEffect::UnregisterInstance(soundEffectKeepAlive_, this);
                soundEffectKeepAlive_.reset();
            }
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

        auto* audio = static_cast<MIX_Audio*>(nativeAudioHandle_);
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

        ApplyTrackProperties(track, Volume_, Pan_, Pitch_);

        SDL_PropertiesID props = SDL_CreateProperties();
        if (props == 0)
        {
            State_   = SoundState::Stopped;
            playing_ = false;
            return;
        }

        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, IsLooped_ ? -1 : 0);

        // CP-17: apply the authored loop region (matches FNA's LoopBegin/LoopLength, only
        // meaningful while IsLooped -- see SoundEffectInstance.cs's Play()). loopStart_==0 &&
        // loopLength_==0 (the common case: no explicit loop region was ever given) leaves both
        // properties at their SDL3_mixer defaults, which loop the entire track -- unchanged
        // behavior for every effect that never had a loop region authored.
        if (IsLooped_ && (loopStart_ != 0 || loopLength_ != 0))
        {
            SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER,
                                   static_cast<Sint64>(loopStart_));
            if (loopLength_ != 0)
            {
                // SDL3_mixer has no separate "loop end" property distinct from "track end" --
                // MAX_FRAME_NUMBER treats this position as EOF for the whole track, which also
                // (unlike FNA/XAudio2's LoopBegin/LoopLength) truncates the very first, pre-loop
                // playthrough at the loop's end instead of only subsequent iterations. Accepted
                // as the closest achievable match; see CHECKLIST.md.
                SDL_SetNumberProperty(props, MIX_PROP_PLAY_MAX_FRAME_NUMBER,
                                       static_cast<Sint64>(loopStart_) + static_cast<Sint64>(loopLength_));
            }
        }

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

    void SoundEffectInstance::INTERNAL_applyReverb(float /*rvGain*/)
    {
        // SDL3_mixer has no aux-send/return bus; documented no-op (T-4C, see the declaration's
        // comment in SoundEffectInstance.hpp for why this matches FNA's own dead-code status).
    }

    void SoundEffectInstance::INTERNAL_applyLowPassFilter(float cutoff)
    {
#ifdef SOUND_ENABLED
        if (!track_) return; // matches FNA's `handle == IntPtr.Zero` guard
        if (!filterState_) filterState_ = std::make_unique<FilterState>();

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_LockMixer(mixer);
        filterState_->kind      = FilterState::Kind::LowPass;
        filterState_->frequency = cutoff;
        filterState_->oneOverQ  = 1.0f; // matches FNA: hardcoded, not exposed as a parameter
        MIX_UnlockMixer(mixer);

        MIX_SetTrackCookedCallback(AsTrack(track_), FilterMixCallback, filterState_.get());
#else
        (void)cutoff;
#endif
    }

    void SoundEffectInstance::INTERNAL_applyHighPassFilter(float cutoff)
    {
#ifdef SOUND_ENABLED
        if (!track_) return;
        if (!filterState_) filterState_ = std::make_unique<FilterState>();

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_LockMixer(mixer);
        filterState_->kind      = FilterState::Kind::HighPass;
        filterState_->frequency = cutoff;
        filterState_->oneOverQ  = 1.0f;
        MIX_UnlockMixer(mixer);

        MIX_SetTrackCookedCallback(AsTrack(track_), FilterMixCallback, filterState_.get());
#else
        (void)cutoff;
#endif
    }

    void SoundEffectInstance::INTERNAL_applyBandPassFilter(float center)
    {
#ifdef SOUND_ENABLED
        if (!track_) return;
        if (!filterState_) filterState_ = std::make_unique<FilterState>();

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_LockMixer(mixer);
        filterState_->kind      = FilterState::Kind::BandPass;
        filterState_->frequency = center;
        filterState_->oneOverQ  = 1.0f;
        MIX_UnlockMixer(mixer);

        MIX_SetTrackCookedCallback(AsTrack(track_), FilterMixCallback, filterState_.get());
#else
        (void)center;
#endif
    }

    void SoundEffectInstance::ProcessFilterSamplesForTest(float* pcm, int channels, int samples)
    {
#ifdef SOUND_ENABLED
        if (filterState_) ProcessFilterState(*filterState_, pcm, channels, samples);
#else
        (void)pcm; (void)channels; (void)samples;
#endif
    }

    void SoundEffectInstance::Apply3D(const AudioListener& listener, const AudioEmitter& emitter)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("SoundEffectInstance");
        }

        is3D_ = true; // CP-20: latches setPanProperty() out of writing the real track output

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
        ApplyTrackProperties(AsTrack(track_), atten * Volume_, pan, Pitch_);
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
            MIX_SetTrackGain(track, Volume_);
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

        // CP-20: once Apply3D has run at least once, its own pan approximation is what should
        // keep governing the real track output -- matches FNA's `if (is3D) return;` in Pan's
        // setter (SoundEffectInstance.cs). The property itself still always reports what was
        // last set, above.
        if (is3D_)
        {
            return;
        }

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
