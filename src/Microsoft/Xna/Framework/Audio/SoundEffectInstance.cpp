// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
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
    // T-4C: per-track DSP state (see SoundEffectInstance.hpp's filterState_ for the ownership/
    // move-safety rationale). Kind/frequency/oneOverQ are written by INTERNAL_apply*Filter (main
    // thread) and read by FilterMixCallback (SDL3_mixer's mixing thread) -- guarded by
    // MIX_LockMixer/UnlockMixer in the writer, relying on SDL3_mixer's own documented guarantee
    // that "the SDL audio device thread [holds this same lock] while actual mixing is in
    // progress" (so the callback itself must NOT also lock -- it would be redundant at best).
    // yl/yb are the filter's per-channel recursive state and are touched ONLY by the mixing
    // thread inside the callback, never by the setters, so they need no synchronization at all.
    // P11-PAN-001 (RFC-1): also holds the crossfeed pan value, since SDL3_mixer only exposes one
    // "cooked" callback slot per track (CHECKLIST.md CP-19) -- this struct is that slot's entire
    // shared state, filter and pan alike, not just the filter anymore. `pan` is written under the
    // same MIX_LockMixer/UnlockMixer discipline as frequency/oneOverQ above.
    struct FilterState
    {
        enum class Kind { None, LowPass, HighPass, BandPass };
        Kind  kind        = Kind::None;
        float frequency   = 0.0f;
        float oneOverQ    = 1.0f;
        // P10-FILTER-002/003: the filter's own base (XACT-authored, or explicitly-set) frequency/
        // oneOverQ, distinct from the two fields above (the CURRENTLY effective, possibly RPC-
        // overridden values the mixing thread actually reads) -- matches FAudio's
        // `activeWave.baseFrequency`/`baseQFactor` fallback (FACT_internal.c) for whichever axis
        // has no RPC curve targeting it on a given tick. Set once, alongside `frequency`/
        // `oneOverQ`, by whichever INTERNAL_apply*Filter first establishes this filter; never
        // written by INTERNAL_applyRpcFilterOverride.
        float baseFrequency = 0.0f;
        float baseOneOverQ  = 1.0f;
        float yl[2]       = {0.0f, 0.0f};
        float yb[2]       = {0.0f, 0.0f};
        // P11-PAN-001: current stereo pan, range [-1,1], matching Pan_/Apply3D's own pan. 0.0f
        // (the FilterState default and the Pan property's own default) is the crossfeed matrix's
        // identity, so a never-panned track costs nothing extra in the callback.
        float pan         = 0.0f;
    };

#ifdef SOUND_ENABLED
    namespace
    {
        MIX_Track* AsTrack(void* p)
        {
            return static_cast<MIX_Track*>(p);
        }

        // P12-PITCH-001: the real implementation behind
        // SoundEffectInstance::INTERNAL_calculatePitchRatio (a thin forwarding shim, defined
        // further down) -- lives here, not as a class member body, purely so ApplyTrackProperties
        // below and SoundEffect::Play()'s fire-and-forget path (a friend of SoundEffectInstance,
        // SoundEffect.cpp) can both call it without needing a class-member-only path. Matches
        // FNA exactly (SoundEffectInstance.cs:589-591):
        // `FAudioSourceVoice_SetFrequencyRatio(handle, (float)Math.Pow(2.0, INTERNAL_pitch) *
        // doppler, 0)` -- Pitch's whole [-1,1] range is explicitly octave-based ("-1 octave to +1
        // octave"), an exponential curve, NOT a linear multiplier (a prior version of this file
        // used `(pitch<0)?(1+pitch*0.5f):(1+pitch)`, which only agrees with `2^pitch` at
        // pitch=-1,0,1 and is audibly wrong -- up to ~6%/1 semitone off -- everywhere else;
        // P12-AUDIT-001 found this via a fresh audit, since every pre-existing test only ever
        // exercised the default Pitch=0, where the two formulas coincidentally agree).
        float ComputePitchRatio(float pitch)
        {
            return std::pow(2.0f, pitch);
        }

        // CP-16: master volume is applied once, globally, via MIX_SetMixerGain (SDL3_mixer's own
        // master gain stage), not baked into each track's own gain here -- doing both would
        // double-apply it, and only the mixer-level gain re-applies live to already-playing
        // tracks without this function needing to be called again.
        // P9-3D-005: `doppler` is a multiplier applied on top of the pitch-derived ratio, matching
        // FNA's UpdatePitch() (`(2^INTERNAL_pitch) * doppler`, SoundEffectInstance.cs) -- defaults
        // to 1.0f (no-op) for every caller except Apply3D.
        // P11-PAN-001 (RFC-1): `filterState` receives the pan value instead of MIX_SetTrackStereo
        // computing per-channel gains directly -- SDL3_mixer's own stereo gain has no crossfeed
        // term (CHECKLIST.md CP-19), so it's fixed to unity here and CNA owns 100% of the stereo
        // image via the crossfeed matrix applied in the shared filter/pan cooked callback
        // (ApplyPanCrossfeed below). `filterState` may be null (SOUND_ENABLED-less builds aside,
        // this only happens if EnsureTrackDspState's allocation somehow failed) -- in that case
        // the pan write is simply skipped, matching this function's existing null-`track` guard.
        void ApplyTrackProperties(MIX_Track* track, FilterState* filterState,
                                   float volume, float pan, float pitch, float doppler = 1.0f)
        {
            if (!track) return;

            MIX_SetTrackGain(track, volume);

            static const MIX_StereoGains kUnityStereo{1.0f, 1.0f};
            MIX_SetTrackStereo(track, &kUnityStereo);

            if (filterState)
            {
                MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
                MIX_LockMixer(mixer);
                filterState->pan = pan;
                MIX_UnlockMixer(mixer);
            }

            const float ratio = ComputePitchRatio(pitch) * doppler;
            MIX_SetTrackFrequencyRatio(track, ratio < 0.01f ? 0.01f : ratio);
        }

        // P9-3D-005: matches FAudio's F3DAudio.c CalculateDoppler exactly -- a relative-velocity
        // frequency-ratio formula computed purely from Position/Velocity (both already exposed on
        // AudioListener/AudioEmitter), needing no native 3D audio API. Unlike stereo crossfeed
        // panning (CP-19) or true elevation/HRTF, real Doppler doesn't need anything SDL3_mixer
        // can't already do (MIX_SetTrackFrequencyRatio already exists for the Pitch property).
        // `toListener*` is the emitter-to-listener direction vector (FAudio's own naming);
        // `dopplerScaler` is the per-emitter AudioEmitter.DopplerScale (distinct from the global
        // SoundEffect.DopplerScale multiplier applied by the caller afterward).
        float ComputeDopplerFactor(
            float speedOfSound, float dopplerScaler,
            float toListenerX, float toListenerY, float toListenerZ, float distance,
            const Microsoft::Xna::Framework::Vector3& listenerVelocity,
            const Microsoft::Xna::Framework::Vector3& emitterVelocity)
        {
            if (dopplerScaler <= 0.0f) return 1.0f;

            float listenerVelComponent = 0.0f;
            float emitterVelComponent  = 0.0f;
            if (distance != 0.0f)
            {
                listenerVelComponent = (
                    toListenerX * listenerVelocity.X +
                    toListenerY * listenerVelocity.Y +
                    toListenerZ * listenerVelocity.Z
                ) / distance;
                emitterVelComponent = (
                    toListenerX * emitterVelocity.X +
                    toListenerY * emitterVelocity.Y +
                    toListenerZ * emitterVelocity.Z
                ) / distance;
            }

            const float scaledSpeedOfSound = speedOfSound / dopplerScaler;
            listenerVelComponent = std::min(listenerVelComponent, scaledSpeedOfSound);
            emitterVelComponent  = std::min(emitterVelComponent, scaledSpeedOfSound);

            float dopplerFactor = (speedOfSound - dopplerScaler * listenerVelComponent) /
                                   (speedOfSound - dopplerScaler * emitterVelComponent);
            if (std::isnan(dopplerFactor)) dopplerFactor = 1.0f;

            // "Limit the pitch shifting to 2 octaves up and 1 octave down" (F3DAudio.c's own
            // comment).
            return std::clamp(dopplerFactor, 0.5f, 4.0f);
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
        void ApplyFilter(FilterState& state, float* pcm, int channels, int samples)
        {
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
                        default: break; // Not reachable -- caller already checked kind != None.
                    }
                }
            }
        }

        // P11-PAN-001 (RFC-1): the real implementation behind
        // SoundEffectInstance::INTERNAL_calculatePanCrossfeedMatrix (a thin forwarding shim,
        // defined further down) -- lives here, not as a class member body, purely so
        // ApplyPanCrossfeed below (an anonymous-namespace free function, called from the
        // real-time mixing callback) can call it without needing class-member access. Matches
        // FNA's SetPanMatrixCoefficients exactly (SoundEffectInstance.cs,
        // dspSettings.SrcChannelCount == 2 && DstChannelCount == 2 branch): hard panning does NOT
        // eliminate an entire channel -- the two source channels are blended together on
        // whichever output speaker `pan` favors, and the OTHER speaker goes silent, rather than
        // each speaker only ever hearing its own matching input channel (CHECKLIST.md CP-19, the
        // deviation this method fixes).
        void ComputePanCrossfeedMatrix(float pan, float& ll, float& rl, float& lr, float& rr)
        {
            if (pan <= 0.0f)
            {
                // Left speaker blends left/right channels; right speaker gets less of the right
                // channel (and none of the left).
                ll = 0.5f * pan + 1.0f;
                rl = 0.5f * -pan;
                lr = 0.0f;
                rr = pan + 1.0f;
            }
            else
            {
                // Left speaker gets less of the left channel (and none of the right); right
                // speaker blends right/left channels.
                ll = -pan + 1.0f;
                rl = 0.0f;
                lr = 0.5f * pan;
                rr = 0.5f * -pan + 1.0f;
            }
        }

        // Applies the crossfeed matrix above directly to interleaved stereo PCM. Only meaningful
        // for `channels == 2` -- SDL3_mixer forces every track to true stereo output before the
        // cooked callback runs (ApplyTrackProperties's unity MIX_SetTrackStereo call), so this is
        // always satisfied for a real callback invocation; guarded defensively anyway, matching
        // this file's existing style. `pan == 0.0f` (the common, never-panned case) skips the
        // transform entirely -- the matrix would reduce to the identity {1,0,0,1} anyway, so this
        // is a pure optimization, not a behavior branch.
        void ApplyPanCrossfeed(float pan, int channels, float* pcm, int samples)
        {
            if (channels != 2 || pan == 0.0f) return;

            float ll, rl, lr, rr;
            ComputePanCrossfeedMatrix(pan, ll, rl, lr, rr);

            for (int i = 0; i + 2 <= samples; i += 2)
            {
                const float l = pcm[i];
                const float r = pcm[i + 1];
                pcm[i]     = l * ll + r * rl;
                pcm[i + 1] = l * lr + r * rr;
            }
        }

        // Runs this track's entire shared cooked-callback DSP chain: the filter first (if any),
        // then the crossfeed pan matrix (P11-PAN-001, RFC-1) -- both are just float-PCM
        // transforms on the same buffer, run in sequence, matching the RFC-1 design sketch
        // (plan_audio.md P10-PAN-003). Unlike the old ProcessFilterState this replaces, this must
        // NOT bail out early when there's no filter -- pan crossfeed still needs to run for every
        // track, filtered or not.
        void ProcessFilterState(FilterState& state, float* pcm, int channels, int samples)
        {
            if (channels <= 0) return;

            if (state.kind != FilterState::Kind::None)
            {
                ApplyFilter(state, pcm, channels, samples);
            }

            ApplyPanCrossfeed(state.pan, channels, pcm, samples);
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

        EnsureTrackDspState(); // P11-PAN-001: must exist before ApplyTrackProperties writes pan
        ApplyTrackProperties(track, filterState_.get(), Volume_, Pan_, Pitch_);

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
                // MAX_FRAME_NUMBER treats this position as EOF for the whole track. Combined with
                // LOOP_START_FRAME_NUMBER above, this matches FNA/XAudio2's LoopBegin/LoopLength
                // exactly: the intro plays once, then only [loopStart_, loopStart_+loopLength_)
                // repeats -- confirmed against real decoded audio via a raw SDL3_mixer callback
                // (P10-LOOP-003/004, SoundEffectInstanceTests.cpp's
                // BoundedLoopRegionPlaysIntroOnceThenRepeatsOnlyTheLoopRegion), correcting an
                // earlier, never-actually-decoded-audio-verified assumption that this truncated
                // the pre-loop intro too (see plan_audio.md's P10-LOOP-003/004 note).
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
        if (!track)
        {
            // P9-VALIDATION-010: matches FNA (SoundEffectInstance.cs Resume(): "XNA4 just plays
            // if we've not started yet") -- when there's no active voice/track, Resume() calls
            // Play() instead of being a no-op. In CNA's model this covers both "never played"
            // (track_ starts null) and "disposed" (Dispose() nulls track_): for a disposed
            // instance this makes Resume() surface Play()'s own ObjectDisposedException instead
            // of silently doing nothing, which is safer than FNA's real behavior (FNA's Play()
            // has no disposed guard at all, so it would silently resurrect a disposed instance).
            Play();
            return;
        }
        if (getStateProperty() == SoundState::Paused)
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

    void SoundEffectInstance::INTERNAL_applyLowPassFilter(float cutoff, float oneOverQ)
    {
#ifdef SOUND_ENABLED
        if (!track_) return; // matches FNA's `handle == IntPtr.Zero` guard
        if (!filterState_) filterState_ = std::make_unique<FilterState>();

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_LockMixer(mixer);
        filterState_->kind         = FilterState::Kind::LowPass;
        filterState_->frequency    = cutoff;
        filterState_->oneOverQ     = oneOverQ;
        filterState_->baseFrequency = cutoff;
        filterState_->baseOneOverQ  = oneOverQ;
        MIX_UnlockMixer(mixer);

        MIX_SetTrackCookedCallback(AsTrack(track_), FilterMixCallback, filterState_.get());
#else
        (void)cutoff; (void)oneOverQ;
#endif
    }

    void SoundEffectInstance::INTERNAL_applyHighPassFilter(float cutoff, float oneOverQ)
    {
#ifdef SOUND_ENABLED
        if (!track_) return;
        if (!filterState_) filterState_ = std::make_unique<FilterState>();

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_LockMixer(mixer);
        filterState_->kind         = FilterState::Kind::HighPass;
        filterState_->frequency    = cutoff;
        filterState_->oneOverQ     = oneOverQ;
        filterState_->baseFrequency = cutoff;
        filterState_->baseOneOverQ  = oneOverQ;
        MIX_UnlockMixer(mixer);

        MIX_SetTrackCookedCallback(AsTrack(track_), FilterMixCallback, filterState_.get());
#else
        (void)cutoff; (void)oneOverQ;
#endif
    }

    void SoundEffectInstance::INTERNAL_applyBandPassFilter(float center, float oneOverQ)
    {
#ifdef SOUND_ENABLED
        if (!track_) return;
        if (!filterState_) filterState_ = std::make_unique<FilterState>();

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_LockMixer(mixer);
        filterState_->kind         = FilterState::Kind::BandPass;
        filterState_->frequency    = center;
        filterState_->oneOverQ     = oneOverQ;
        filterState_->baseFrequency = center;
        filterState_->baseOneOverQ  = oneOverQ;
        MIX_UnlockMixer(mixer);

        MIX_SetTrackCookedCallback(AsTrack(track_), FilterMixCallback, filterState_.get());
#else
        (void)center; (void)oneOverQ;
#endif
    }

    float SoundEffectInstance::INTERNAL_calculateFilterCutoff(float frequencyHz, float sampleRate)
    {
        // Matches FAudio's FACT_INTERNAL_CalculateFilterFrequency (FACT_internal.c) exactly: the
        // min() guards against the formula behaving badly as the cutoff approaches the sample
        // rate (their comment credits @Woflox).
        if (sampleRate <= 0.0f) return 0.0f;
        return 2.0f * std::sin(
            std::numbers::pi_v<float> * std::min(frequencyHz / sampleRate, 0.5f));
    }

    float SoundEffectInstance::INTERNAL_calculateFilterOneOverQ(uint8_t qfactorRaw)
    {
        // Matches FAudio's inline `1.0f / (track->qfactor / 3.0f)` at the SOUND_FLAG_COMPLEX
        // track-init site (FACT_internal.c), clamped to at most 1.0f (their own comment: "the
        // 0.67 Q Factor causes problems ... just clamp it for now"). qfactorRaw == 0 would
        // divide by zero -- FAudio never emits that from real XACT tool output, but guard it here
        // by falling back to the same default as "no filter" (OneOverQ = 1.0f).
        if (qfactorRaw == 0) return 1.0f;
        return std::min(3.0f / static_cast<float>(qfactorRaw), 1.0f);
    }

    float SoundEffectInstance::INTERNAL_calculatePitchRatio(float pitch)
    {
        // Forwards to ComputePitchRatio (anonymous namespace, top of this file) -- the single
        // canonical implementation, shared with the real-time-callback-adjacent
        // ApplyTrackProperties() and (via friendship) SoundEffect::Play()'s fire-and-forget path,
        // so there is exactly one copy of this math to keep in sync with FNA (P12-PITCH-001).
        return ComputePitchRatio(pitch);
    }

    Microsoft::Xna::Framework::Vector3 SoundEffectInstance::INTERNAL_calculateListenerRight(
        const Microsoft::Xna::Framework::Vector3& forward,
        const Microsoft::Xna::Framework::Vector3& up)
    {
        using Microsoft::Xna::Framework::Vector3;

        // P9-3D-010: matches F3DAudio.c's listenerBasis.right construction (Cross of the
        // listener's own orientation vectors), verified against XNA's own Vector3.Right constant
        // for the default orientation (Forward=(0,0,-1), Up=(0,1,0)): Cross(Forward, Up) reduces
        // to exactly (1,0,0) in that case. Falls back to world Vector3.Right if Forward/Up are
        // degenerate (parallel or zero-length) rather than dividing by a near-zero length --
        // malformed orientation input is undefined in real X3DAudio too (its VECTOR_BASE_CHECK
        // assertion requires an orthonormal basis), so this is purely a defensive guard against
        // NaN, not an attempt to define new real behavior for invalid input.
        const Vector3 right = Vector3::Cross(forward, up);
        const float len = right.Length();
        return (len > 1e-6f) ? (right / len) : Vector3::Right;
    }

    float SoundEffectInstance::INTERNAL_calculatePan(float rightDisplacement, float distance)
    {
        // A simplified linear pan approximation (SDL3_mixer has no true angular panning/HRTF):
        // only the listener-relative rightward displacement matters (Apply3D projects the
        // emitter's position onto the listener's own Forward/Up-derived right axis before
        // calling this, P9-3D-010), normalized by distance and clamped to the Pan property's own
        // [-1,1] range. `distance == 0` (same position) has no meaningful direction, so it
        // centers (0.0f) rather than dividing by zero.
        return (distance > 0.0f) ? std::clamp(rightDisplacement / distance, -1.0f, 1.0f) : 0.0f;
    }

    void SoundEffectInstance::INTERNAL_calculatePanCrossfeedMatrix(
        float pan, float& ll, float& rl, float& lr, float& rr)
    {
        // Forwards to ComputePanCrossfeedMatrix (anonymous namespace, top of this file) -- the
        // single canonical implementation, shared with the real-time mixing callback
        // (ApplyPanCrossfeed) so there is exactly one copy of this math to keep in sync with FNA.
        ComputePanCrossfeedMatrix(pan, ll, rl, lr, rr);
    }

    void SoundEffectInstance::INTERNAL_applyXactTrackFilter(
        uint8_t filterType, float frequencyHz, uint8_t qfactorRaw)
    {
#ifdef SOUND_ENABLED
        if (!track_) return;

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        SDL_AudioSpec spec{};
        if (!MIX_GetMixerFormat(mixer, &spec) || spec.freq <= 0) return;

        const float cutoff   = INTERNAL_calculateFilterCutoff(frequencyHz, static_cast<float>(spec.freq));
        const float oneOverQ = INTERNAL_calculateFilterOneOverQ(qfactorRaw);

        switch (filterType)
        {
            case 0: INTERNAL_applyLowPassFilter(cutoff, oneOverQ); break;
            case 1: INTERNAL_applyBandPassFilter(cutoff, oneOverQ); break;
            case 2: INTERNAL_applyHighPassFilter(cutoff, oneOverQ); break;
            default: break; // Not reachable via XactParser.cpp's bit-decode; defensive only.
        }
#else
        (void)filterType; (void)frequencyHz; (void)qfactorRaw;
#endif
    }

    void SoundEffectInstance::INTERNAL_applyEffectVariationFilter(
        uint8_t filterType, float frequencyHz, float oneOverQ)
    {
#ifdef SOUND_ENABLED
        if (!track_) return;

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        SDL_AudioSpec spec{};
        if (!MIX_GetMixerFormat(mixer, &spec) || spec.freq <= 0) return;

        const float cutoff = INTERNAL_calculateFilterCutoff(frequencyHz, static_cast<float>(spec.freq));

        switch (filterType)
        {
            case 0: INTERNAL_applyLowPassFilter(cutoff, oneOverQ); break;
            case 1: INTERNAL_applyBandPassFilter(cutoff, oneOverQ); break;
            case 2: INTERNAL_applyHighPassFilter(cutoff, oneOverQ); break;
            default: break; // Not reachable via XactParser.cpp's bit-decode; defensive only.
        }
#else
        (void)filterType; (void)frequencyHz; (void)oneOverQ;
#endif
    }

    void SoundEffectInstance::INTERNAL_applyRpcFilterOverride(float rpcFrequencyHz, float rpcQFactor)
    {
#ifdef SOUND_ENABLED
        // Matches FAudio's own guard (FACT_internal.c: `if (sound->sound->tracks[i].filter !=
        // 0xFF)`) -- an RPC targeting filter frequency/Q is a no-op for a track with no filter
        // at all, same as this method's caller (Cue::ReconcileState()) applying it uniformly to
        // every active wave reference regardless of whether that particular one has a filter.
        if (!track_ || !filterState_ || filterState_->kind == FilterState::Kind::None) return;

        // rpcFrequencyHz needs the real device sample rate to convert Hz -> SDL3_mixer's
        // normalized cutoff domain (same conversion INTERNAL_applyXactTrackFilter uses); if the
        // mixer format can't be read, fall back to the base frequency for this tick rather than
        // silently misinterpreting a raw Hz value as an already-normalized one.
        float frequency = filterState_->baseFrequency;
        if (rpcFrequencyHz >= 0.0f)
        {
            MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
            SDL_AudioSpec spec{};
            if (MIX_GetMixerFormat(mixer, &spec) && spec.freq > 0)
                frequency = INTERNAL_calculateFilterCutoff(rpcFrequencyHz, static_cast<float>(spec.freq));
        }

        // Matches FAudio's `data->rpcFilterQFactor = 1.0f / rpcResult;` (FACT_internal.c) --
        // direct reciprocal, no clamp (unlike INTERNAL_calculateFilterOneOverQ's raw-byte-decode
        // clamp, which only applies to XACT-authored per-track filter data, not an RPC curve's
        // own already-in-Q-units output).
        const float oneOverQ = (rpcQFactor >= 0.0f) ? (1.0f / rpcQFactor) : filterState_->baseOneOverQ;

        MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
        MIX_LockMixer(mixer);
        filterState_->frequency = frequency;
        filterState_->oneOverQ  = oneOverQ;
        MIX_UnlockMixer(mixer);
#else
        (void)rpcFrequencyHz; (void)rpcQFactor;
#endif
    }

    void SoundEffectInstance::EnsureTrackDspState()
    {
#ifdef SOUND_ENABLED
        if (!track_) return;
        if (!filterState_) filterState_ = std::make_unique<FilterState>();
        MIX_SetTrackCookedCallback(AsTrack(track_), FilterMixCallback, filterState_.get());
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

    void SoundEffectInstance::INTERNAL_getFilterStateForTest(
        int& kind, float& frequency, float& oneOverQ) const
    {
#ifdef SOUND_ENABLED
        if (!filterState_)
        {
            kind = static_cast<int>(FilterState::Kind::None);
            frequency = 0.0f;
            oneOverQ  = 1.0f;
            return;
        }
        kind      = static_cast<int>(filterState_->kind);
        frequency = filterState_->frequency;
        oneOverQ  = filterState_->oneOverQ;
#else
        kind = 0; frequency = 0.0f; oneOverQ = 1.0f;
#endif
    }

    void SoundEffectInstance::INTERNAL_setPanStateForTest(float pan)
    {
#ifdef SOUND_ENABLED
        if (!filterState_) filterState_ = std::make_unique<FilterState>();
        filterState_->pan = pan;
#else
        (void)pan;
#endif
    }

    float SoundEffectInstance::INTERNAL_getPanStateForTest() const
    {
#ifdef SOUND_ENABLED
        return filterState_ ? filterState_->pan : 0.0f;
#else
        return 0.0f;
#endif
    }

    void SoundEffectInstance::Apply3D(const AudioListener& listener, const AudioEmitter& emitter)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("SoundEffectInstance");
        }

        is3D_ = true; // CP-20: latches setPanProperty() out of writing the real track output

        // SDL3_mixer does not support full 3D spatial audio (HRTF, orientation/cone). Pan and
        // distance attenuation are simplified linear approximations; Doppler pitch shift
        // (P9-3D-005) is computed exactly, since it doesn't need any native 3D audio API.

        const auto& lp = listener.getPositionProperty();
        const auto& ep = emitter.getPositionProperty();

        const float dx = ep.X - lp.X;
        const float dy = ep.Y - lp.Y;
        const float dz = ep.Z - lp.Z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        // P9-3D-003: matches FAudio's F3DAudio.c ComputeDistanceAttenuation's no-custom-curve
        // branch exactly ("the default (if emitter is NULL) volume curve is a *computed*
        // inverse law"): full volume (no attenuation at all) for any distance within
        // CurveDistanceScaler (XNA's DistanceScale), inverse-distance falloff only beyond it --
        // NOT a continuous falloff starting at distance 0. The previous `1/(1+x)` formula
        // attenuated far too aggressively close to the listener (e.g. already at half volume
        // exactly at distance == DistanceScale, where real XNA/FNA is still at full volume).
        const float distScale = SoundEffect::getDistanceScaleProperty();
        const float normalizedDistance = distance / (distScale > 0.0f ? distScale : 1.0f);
        const float atten = (normalizedDistance >= 1.0f)
            ? std::clamp(1.0f / normalizedDistance, 0.0f, 1.0f)
            : 1.0f;

        // P9-3D-010: project the emitter's relative position onto the listener's own right axis
        // (Forward x Up) instead of raw world-space X, so pan correctly reflects which way the
        // listener is actually facing, not an assumption that they always face -Z. Matches
        // F3DAudio.c's own ComputeEmitterChannelCoefficients, which projects the emitter-relative
        // vector onto listenerBasis.right (itself derived from OrientFront/OrientTop) before
        // computing azimuth -- same idea, without F3DAudio's own multi-speaker energy-diffusion
        // math, which has no equivalent in SDL3_mixer's single stereo-gain-pair model anyway
        // (already an accepted deviation, CHECKLIST.md CP-19). Verified against XNA's own
        // Vector3.Right constant: for the default orientation (Forward=(0,0,-1), Up=(0,1,0)),
        // Cross(Forward, Up) reduces to exactly (1,0,0), so this is a strict generalization of
        // the previous world-X-only approximation -- an unrotated listener (the common case,
        // and the only case the old code handled) gets bit-identical pan values to before.
        const auto right = INTERNAL_calculateListenerRight(
            listener.getForwardProperty(), listener.getUpProperty());
        const float rightDisplacement = dx * right.X + dy * right.Y + dz * right.Z;

        const float pan = INTERNAL_calculatePan(rightDisplacement, distance);

        // P9-3D-005: matches FNA's UpdatePitch() exactly ("doppler = dspSettings.DopplerFactor *
        // dopplerScale" when the global SoundEffect.DopplerScale is nonzero, else 1.0f/no-op).
        // dx/dy/dz above are emitter-minus-listener; ComputeDopplerFactor wants the
        // emitter-to-listener direction (FAudio's own naming), i.e. the negation.
        const float globalDopplerScale = SoundEffect::getDopplerScaleProperty();
        const float doppler = (globalDopplerScale != 0.0f)
            ? ComputeDopplerFactor(
                  SoundEffect::getSpeedOfSoundProperty(),
                  emitter.getDopplerScaleProperty(),
                  -dx, -dy, -dz, distance,
                  listener.getVelocityProperty(),
                  emitter.getVelocityProperty()) * globalDopplerScale
            : 1.0f;

#ifdef SOUND_ENABLED
        // Applied directly to the underlying track, not through setVolumeProperty()/
        // setPanProperty()/setPitchProperty(): FNA computes a separate 3D output matrix
        // (dspSettings) that combines multiplicatively with the voice's own Volume/Pitch at the
        // audio-engine level and never touches INTERNAL_volume/INTERNAL_pan/INTERNAL_pitch, so
        // Volume/Pan/Pitch continue to report exactly what the caller last set via the setters,
        // unaffected by 3D positioning. One-shot at this call, like atten/pan above -- not
        // persisted and reapplied by later setVolumeProperty()/setPitchProperty() calls, matching
        // how those setters already overwrite the 3D-adjusted track gain/ratio outright (a real
        // game calls Apply3D() every frame to keep 3D properties fresh, the same assumption
        // atten/pan already rely on).
        EnsureTrackDspState(); // P11-PAN-001: must exist before ApplyTrackProperties writes pan
        ApplyTrackProperties(AsTrack(track_), filterState_.get(), atten * Volume_, pan, Pitch_, doppler);
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
        // P11-PAN-001 (RFC-1): unlike the old direct MIX_SetTrackStereo call this replaces, pan
        // is written into the shared cooked-callback DSP state instead -- SDL3_mixer's own stereo
        // gain was already fixed to unity by Play()/Apply3D's ApplyTrackProperties call, which
        // also guarantees filterState_ is non-null for any track that's actually playing.
        MIX_Track* track = AsTrack(track_);
        if (track && filterState_)
        {
            MIX_Mixer* mixer = CNA::Internal::Audio::GetMixer();
            MIX_LockMixer(mixer);
            filterState_->pan = Pan_;
            MIX_UnlockMixer(mixer);
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
            const float ratio = INTERNAL_calculatePitchRatio(Pitch_);
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
