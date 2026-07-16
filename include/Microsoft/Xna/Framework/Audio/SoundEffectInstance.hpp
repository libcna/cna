// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioListener;
    class SoundEffect;
    class Cue;

    // T-4C: DSP filter state (kind/coefficients/recursive state), fully defined only in
    // SoundEffectInstance.cpp where SDL3_mixer's callback-registration types are available.
    // Namespace-scope (not a nested class of SoundEffectInstance) so the free-function
    // SDL3_mixer callback that reads it doesn't need friend access -- it's still effectively
    // private, since nothing outside SoundEffectInstance.cpp ever names it.
    struct FilterState;

    /** @brief Controls playback of a sound effect instance, including volume, pitch, pan, and looping. */
    class SoundEffectInstance : public System::Object, public System::IDisposable
    {
        friend class SoundEffect;
        // Cue::Play() wires real per-track XACT filter data into INTERNAL_applyXactTrackFilter
        // (P9-XACT-011) -- see that method's declaration below.
        NOXNA friend class Cue;
        // Tests need read access to the underlying MIX_Track handle to verify Play() idempotency
        // (that a repeated call while already playing doesn't restart the track).
        NOXNA friend struct SoundEffectInstanceTestAccess;

    protected:
        /** @brief Default constructor for use by DynamicSoundEffectInstance. */
        SoundEffectInstance();

        // These members are protected so DynamicSoundEffectInstance can manage its own state.
        void* track_        = nullptr;
        bool  playing_      = false;
        bool  hasStarted_   = false; // true once Play() has been called; never reset (gates IsLooped)
        SoundState State_   = SoundState::Stopped;

        // P13-DYNAMIC-001: recomposes and writes this instance's full set of live track properties
        // (gain, pan, frequency ratio) from Volume_/Pan_/Pitch_ together with the persisted
        // spatial state (attenuation_/dopplerFactor_/spatialPan_, private below) -- the single call
        // site Play(), setVolumeProperty(), setPitchProperty(), setPanProperty(), and Apply3D() all
        // share (AUDIO-001), so a spatial attenuation/pan/Doppler value set by Apply3D survives
        // every one of those calls instead of being overwritten by whichever runs next. Pan uses
        // spatialPan_ once is3D_ has latched, otherwise the plain Pan_ property, matching CP-20.
        // No-op if there is no live track yet (matches every INTERNAL_apply*'s existing null-track
        // guard). Protected (not private) so DynamicSoundEffectInstance::Play() can call it too,
        // now that both classes share the same `track_` (P13-DYNAMIC-001 -- previously
        // DynamicSoundEffectInstance managed its own separate `dynamicTrack_` and never applied any
        // of this).
        void INTERNAL_applyComposedTrackProperties();

    private:
        // Keeps the sound effect's underlying audio resource alive for the lifetime of this
        // instance, independent of whether the originating SoundEffect object itself still
        // exists (e.g. after `SoundEffect(path).CreateInstance()` on a temporary) -- CP-7.
        // Type-erased because SoundEffect::Impl is private and defined only in SoundEffect.cpp.
        std::shared_ptr<void> soundEffectKeepAlive_;
        void* nativeAudioHandle_ = nullptr; // MIX_Audio*, cached while soundEffect was alive

        // Cached from the originating SoundEffect at construction time, same rationale as
        // nativeAudioHandle_ above -- Play() must never dereference the SoundEffect itself
        // (CP-7), so its loop region is copied out while it's definitely still alive rather
        // than read through a stored reference/pointer (CP-17).
        SharpRuntime::uintcs loopStart_  = 0;
        SharpRuntime::uintcs loopLength_ = 0;

        bool  IsLooped_     = false;
        bool  isDisposed_   = false;
        float Volume_       = 1.0f;
        float Pan_          = 0.0f;
        float Pitch_        = 0.0f;

        // CP-20: once Apply3D has been called, matches FNA's `is3D` latch (SoundEffectInstance.cs)
        // -- setPanProperty() still updates the Pan_ property (callers must keep reading back
        // what they last set), but stops writing the real track output, since Apply3D's own pan
        // approximation is what should keep governing the actual output until Apply3D runs
        // again. Never reset back to false once set (matches FNA: is3D is only ever set to true).
        bool  is3D_         = false;

        // AUDIO-001: Apply3D's derived attenuation/pan/Doppler must survive later Play()/Volume/
        // Pitch calls, and a call made before this instance has ever had a track (no track yet to
        // write to) -- matches FNA's own persistent per-instance dspSettings/is3D state
        // (SoundEffectInstance.cs), which Play() and UpdatePitch() both read back from on every
        // subsequent call, not just the one Apply3D() itself made. Defaults are the neutral no-op
        // values, so an instance that never calls Apply3D computes byte-for-byte the same gain/
        // pan/frequency-ratio as before this fix (Volume_ * 1.0f == Volume_, etc). Never reset
        // once Apply3D has run, matching is3D_'s own latch semantics above.
        float attenuation_   = 1.0f;
        float dopplerFactor_ = 1.0f;
        float spatialPan_    = 0.0f;

        // Heap-allocated (not inline) so its address is stable across a move of *this* -- the
        // SDL3_mixer callback holds a raw pointer to it as userdata, and a unique_ptr move
        // transfers ownership without changing that address, so no callback re-registration is
        // needed after moving a SoundEffectInstance with an active filter (T-4C).
        std::unique_ptr<FilterState> filterState_;

        // SDL3_mixer has no aux-send/return bus (no equivalent to FAudio's shared ReverbVoice),
        // so this stays a documented no-op (T-4C). Matches FNA: SoundEffectInstance.cs never has
        // any caller for this method either -- FACT applies XACT reverb routing natively,
        // invisible to the C# layer -- so there is no observable behavior gap versus reference.
        void INTERNAL_applyReverb(float rvGain);

        // Real state-variable filter (T-4C), matching FAudio's own algorithm exactly (see
        // FAudio_internal.c's FAudio_INTERNAL_FilterVoice) via an SDL3_mixer per-track "cooked"
        // callback. `cutoff`/`center` are the pre-computed normalized frequency FAudio expects
        // (2*sin(pi*cutoffHz/sampleRate), range [0,1]), not raw Hz -- matches
        // FAudioFilterParameters::Frequency exactly, since these methods just forward it. No-op
        // if the track hasn't been created yet (matches FNA's `handle == IntPtr.Zero` guard);
        // not sticky across Stop(true)/replay, again matching FNA (the filter lives on the
        // voice/track, not the instance). `oneOverQ` defaults to 1.0f, matching every call site
        // in FNA's own (dead-code, never-called) SoundEffectInstance.cs equivalents -- P9-XACT-011
        // adds real per-track Q fidelity for XACT-driven playback via
        // INTERNAL_applyXactTrackFilter below, without changing this default for any other caller.
        void INTERNAL_applyLowPassFilter(float cutoff, float oneOverQ = 1.0f);
        void INTERNAL_applyHighPassFilter(float cutoff, float oneOverQ = 1.0f);
        void INTERNAL_applyBandPassFilter(float center, float oneOverQ = 1.0f);

        // P9-XACT-011: real entry point for a Cue-driven per-track XACT filter (parsed by
        // XactParser.cpp into XsbWaveRef::filterType/filterFrequencyHz/filterQFactorRaw).
        // `filterType` matches FAudioFilterType (0=low-pass, 1=band-pass, 2=high-pass);
        // `frequencyHz` is the raw authored Hz value, converted to SDL3_mixer's expected
        // normalized cutoff via the real device sample rate (INTERNAL_calculateFilterCutoff);
        // `qfactorRaw` is the raw XACT Q-factor byte, converted via
        // INTERNAL_calculateFilterOneOverQ. No-op for an unrecognized filterType (defensive only
        // -- XactParser.cpp's bit-decode never actually produces one). Establishes this filter's
        // base frequency/Q; itself only ever called once, at Play() time -- live RPC-driven
        // frequency/Q targeting on top of this base is a separate, continuously-reapplied concern
        // (P10-FILTER-002/003, see INTERNAL_applyRpcFilterOverride below).
        NOXNA void INTERNAL_applyXactTrackFilter(uint8_t filterType, float frequencyHz, uint8_t qfactorRaw);

        // P11-XACT-003: same role as INTERNAL_applyXactTrackFilter above (establishes this
        // filter's base frequency/Q; itself only ever called once, at Play() time), but for a
        // PlayWaveEffectVariation-family event's randomized override. Unlike
        // INTERNAL_applyXactTrackFilter's raw XACT Q-factor byte, `oneOverQ` here is already the
        // final coefficient -- the caller (Cue.cpp's ApplyEffectVariation) already took the
        // reciprocal of the authored min/maxQFactor draw itself (matching FAudio's own
        // `rngQFactor = 1.0f / (...)`, assigned directly to `activeWave.baseQFactor` with no
        // further transformation downstream) -- so this method must NOT take another reciprocal.
        NOXNA void INTERNAL_applyEffectVariationFilter(uint8_t filterType, float frequencyHz, float oneOverQ);

        // P10-FILTER-002/003: continuous per-tick RPC targeting for filter frequency/Q, called
        // every tick from Cue::ReconcileState() (the same continuous-tick infra P9-XACT-016
        // already built for volume/pitch) whenever this cue has RPC bindings, plus once more at
        // Play() right after the base filter is established. `rpcFrequencyHz`/`rpcQFactor` use a
        // negative sentinel (matching FAudio's own `>= 0.0f` checks, FACT_internal.c) meaning "no
        // RPC curve targets this axis this tick" -- that axis falls back to the filter's base
        // (XACT-authored) value instead, exactly like real FAudio's per-tick fallback between
        // `rpcData.rpcFilterFreq`/`rpcFilterQFactor` and `activeWave.baseFrequency`/`baseQFactor`.
        // A no-op if this instance has no active filter at all (`waveRef.filterType == 0xFF`) --
        // matches FAudio's own `if (sound->sound->tracks[i].filter != 0xFF)` guard around the
        // whole filter-update block. Never touches `kind` or re-registers the cooked callback
        // (already registered by INTERNAL_applyXactTrackFilter) -- only the two coefficient
        // floats change, the same coefficient-only-write pattern the existing INTERNAL_apply*
        // setters already use, so `yl`/`yb`'s recursive filter state is never disturbed
        // (P10-FILTER-004: no click/pop from a live update, since nothing about the callback
        // registration or recursive state changes, only the coefficients it reads).
        NOXNA void INTERNAL_applyRpcFilterOverride(float rpcFrequencyHz, float rpcQFactor);

        // P11-PAN-001 (RFC-1): lazily allocates filterState_ if this is the first DSP-affecting
        // call for this instance (matching the existing INTERNAL_apply*Filter lazy-allocation
        // pattern) and (re)registers the shared cooked callback on `track_`. Unlike the filter
        // setters, this must run for EVERY playing track, not just filtered ones, since the
        // crossfeed pan matrix below now lives in the same callback and needs to run
        // unconditionally. Idempotent -- safe to call on every Play()/Apply3D(), matching
        // MIX_SetTrackCookedCallback's own documented "may be called... at any time" contract.
        // No-op if track_ hasn't been created yet.
        void EnsureTrackDspState();

        // Pure conversion helpers (P9-XACT-011), split out of INTERNAL_applyXactTrackFilter so
        // they're independently unit-testable without a real SDL3_mixer device driving the
        // sample rate. Both match FAudio's exact formulas (FACT_internal.c,
        // FACT_INTERNAL_CalculateFilterFrequency and the inline `1.0f / (qfactor / 3.0f)` at the
        // SOUND_FLAG_COMPLEX track-init site).
        NOXNA static float INTERNAL_calculateFilterCutoff(float frequencyHz, float sampleRate);
        NOXNA static float INTERNAL_calculateFilterOneOverQ(uint8_t qfactorRaw);

        // P12-PITCH-001: converts the XNA `Pitch` property (range [-1,1], "-1 octave to +1
        // octave") to the playback-rate ratio SDL3_mixer's `MIX_SetTrackFrequencyRatio` expects.
        // Matches FNA exactly (SoundEffectInstance.cs:589-591): `(float)Math.Pow(2.0,
        // INTERNAL_pitch)`, an exponential octave curve -- NOT a linear multiplier. Split out as
        // a pure function, matching INTERNAL_calculatePan/INTERNAL_calculateFilterCutoff above,
        // so it's independently unit-testable and has exactly one implementation shared by
        // setPitchProperty(), Play()/Apply3D()'s ApplyTrackProperties(), and
        // SoundEffect::Play(volume,pitch,pan)'s fire-and-forget path (a friend of this class).
        NOXNA static float INTERNAL_calculatePitchRatio(float pitch);

        // P9-3D-010: computes the listener's own world-space right axis from Forward/Up
        // (Cross(Forward, Up), normalized; falls back to Vector3::Right if degenerate), split out
        // so `Apply3D`'s orientation-aware pan projection is independently unit-testable without
        // a real SoundEffectInstance/track. Used by `Apply3D` to project the emitter's relative
        // position before calling `INTERNAL_calculatePan` below.
        NOXNA static Microsoft::Xna::Framework::Vector3 INTERNAL_calculateListenerRight(
            const Microsoft::Xna::Framework::Vector3& forward,
            const Microsoft::Xna::Framework::Vector3& up);

        // P9-3D-007/P9-3D-010: `Apply3D`'s pan approximation (listener-relative rightward
        // displacement over distance, clamped to [-1,1]), split out so it's independently
        // unit-testable -- SDL3_mixer has no `MIX_GetTrackStereo` getter (unlike gain/frequency-
        // ratio), so the *result* of `Apply3D`'s pan computation can't be verified by reading the
        // track back; this pure function can be tested directly instead. `rightDisplacement` is
        // the emitter's position projected onto the listener's own Forward/Up-derived right axis
        // (computed by the caller, `Apply3D`, via `INTERNAL_calculateListenerRight` above), not
        // raw world-space X.
        NOXNA static float INTERNAL_calculatePan(float rightDisplacement, float distance);

        // P11-PAN-001 (RFC-1): computes FNA's exact 4-coefficient stereo crossfeed pan matrix
        // for a 2-source-channel/2-destination-channel track (SoundEffectInstance.cs's
        // SetPanMatrixCoefficients, the `dspSettings.SrcChannelCount == 2` branch) -- split out
        // as a pure function so it's independently unit-testable, matching
        // INTERNAL_calculatePan/INTERNAL_calculateFilterCutoff above. `ll`/`rl`/`lr`/`rr` name
        // each coefficient as "destination-channel-from-source-channel" (e.g. `rl` = the left
        // output's contribution from the right input channel); FNA's own `outputMatrix[0..3]`
        // uses the same left-speaker-first, source-major layout. This same matrix also produces
        // FNA's separate mono-source formula exactly when fed a duplicated-mono signal
        // (L == R), so no separate mono branch is needed by the caller.
        NOXNA static void INTERNAL_calculatePanCrossfeedMatrix(
            float pan, float& ll, float& rl, float& lr, float& rr);

        // Test-only hook (SoundEffectInstanceTestAccess): runs this instance's filter state
        // through the exact same math the real SDL3_mixer callback uses, but synchronously and
        // directly -- the real callback only fires asynchronously from the mixing thread, which
        // would make a test either flaky or need a real-time wait. No-op if no filter/pan state
        // is active at all (P11-PAN-001: now also applies the crossfeed pan matrix, a no-op at
        // the default pan of 0.0f).
        void ProcessFilterSamplesForTest(float* pcm, int channels, int samples);

        // Test-only hook (SoundEffectInstanceTestAccess, P9-XACT-011): reads back the active
        // filter's kind (FilterState::Kind cast to int; 0 = none)/frequency/oneOverQ without
        // exposing FilterState's definition outside SoundEffectInstance.cpp.
        void INTERNAL_getFilterStateForTest(int& kind, float& frequency, float& oneOverQ) const;

        // Test-only hooks (SoundEffectInstanceTestAccess, P11-PAN-001): read/write the DSP
        // state's pan value directly, without a live track -- lets ProcessFilterSamplesForTest's
        // crossfeed math be exercised in isolation from Play()/setPanProperty()'s SDL3_mixer
        // side effects. The setter lazily allocates filterState_ (matching
        // INTERNAL_applyLowPassFilter's own lazy-allocation pattern) so it works even before any
        // filter has ever been applied.
        void INTERNAL_setPanStateForTest(float pan);
        [[nodiscard]] float INTERNAL_getPanStateForTest() const;

        /**
         * @brief Constructs a SoundEffectInstance bound to the given sound effect.
         *
         * FNA's equivalent constructor is `internal`; only SoundEffect::CreateInstance() (a
         * friend) is meant to call this. Direct external construction is not part of the
         * public API and must not compile.
         *
         * @param soundEffect The sound effect to bind this instance to.
         */
        explicit SoundEffectInstance(const SoundEffect& soundEffect);

    public:
        /** @brief Destroys the instance and releases its audio track. */
        ~SoundEffectInstance() override;

        SoundEffectInstance(const SoundEffectInstance&) = delete;
        SoundEffectInstance& operator=(const SoundEffectInstance&) = delete;

        /** @brief Move-constructs a SoundEffectInstance, transferring ownership of the audio track. */
        NOXNA SoundEffectInstance(SoundEffectInstance&& other) noexcept;

        /** @brief Move-assigns a SoundEffectInstance, transferring ownership of the audio track. */
        NOXNA SoundEffectInstance& operator=(SoundEffectInstance&& other) noexcept;

        /** @brief Starts or resumes playback of this instance. */
        virtual void Play();

        /** @brief Stops playback of this instance immediately. */
        virtual void Stop();

        /**
         * @brief Stops playback of this instance.
         *
         * @param immediate If true, cuts off immediately; if false, allows release tails.
         * @throws System::InvalidOperationException if called on a DynamicSoundEffectInstance
         *         with @p immediate false (there is no authored loop to release into).
         */
        virtual void Stop(bool immediate);

        /** @brief Pauses playback of this instance. */
        virtual void Pause();

        /** @brief Resumes a paused instance. */
        virtual void Resume();

        /** @brief Releases this sound effect instance. */
        void Dispose() override;

        /**
         * @brief Applies 3D spatial audio properties using listener and emitter positions.
         *
         * SDL3_mixer does not support full 3D audio; this is a distance and pan approximation
         * applied directly to the underlying track. It does not modify the Volume or Pan
         * properties, which continue to report only what was last set through their setters.
         *
         * @param listener Position and orientation of the audio listener.
         * @param emitter  Position and orientation of the sound emitter.
         * @throws System::ObjectDisposedException if the instance has been disposed.
         */
        void Apply3D(const AudioListener& listener, const AudioEmitter& emitter);

        /**
         * @brief Multi-listener overload; only a single listener is supported.
         *
         * @param listeners     Array of listener descriptions.
         * @param listenerCount Number of listeners (must be 1).
         * @param emitter       Position and orientation of the sound emitter.
         * @throws System::ArgumentNullException if @p listeners is null.
         * @throws System::NotSupportedException if @p listenerCount is not 1.
         */
        void Apply3D(const AudioListener* listeners, int listenerCount, const AudioEmitter& emitter);

        /**
         * @brief Gets whether this instance has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] virtual bool getIsDisposedProperty() const;

        /**
         * @brief Gets the playback volume. Range [0, 1].
         *
         * @return Current volume.
         */
        [[nodiscard]] float getVolumeProperty() const;

        /**
         * @brief Sets the playback volume. Values are passed through unclamped (matching FNA).
         *
         * @param volume New volume value.
         */
        void setVolumeProperty(const float& volume);

        /** @brief Sets the playback volume (move overload). */
        NOXNA void setVolumeProperty(float&& volume);

        /**
         * @brief Gets the stereo pan. Range [-1 (left), 1 (right)].
         *
         * @return Current pan value.
         */
        [[nodiscard]] float getPanProperty() const;

        /**
         * @brief Sets the stereo pan. Range [-1 (left), 1 (right)].
         *
         * @param pan New pan value.
         * @throws System::ObjectDisposedException if the instance has been disposed.
         * @throws System::ArgumentOutOfRangeException if @p pan is outside [-1, 1].
         */
        void setPanProperty(const float& pan);

        /** @brief Sets the stereo pan (move overload). */
        NOXNA void setPanProperty(float&& pan);

        /**
         * @brief Gets the pitch adjustment. Range [-1, 1].
         *
         * @return Current pitch adjustment.
         */
        [[nodiscard]] float getPitchProperty() const;

        /**
         * @brief Sets the pitch adjustment. Range [-1, 1].
         *
         * @param pitch New pitch value.
         */
        void setPitchProperty(const float& pitch);

        /** @brief Sets the pitch adjustment (move overload). */
        NOXNA void setPitchProperty(float&& pitch);

        /**
         * @brief Gets whether the sound loops continuously.
         *
         * @return true if looping; otherwise false.
         */
        [[nodiscard]] virtual bool getIsLoopedProperty() const;

        /**
         * @brief Sets whether the sound loops continuously.
         *
         * @param looped New loop flag.
         * @throws System::InvalidOperationException if the instance has already been played.
         */
        virtual void setIsLoopedProperty(const bool& looped);

        /** @brief Sets whether the sound loops (move overload). */
        NOXNA virtual void setIsLoopedProperty(bool&& looped);

        /**
         * @brief Gets the current playback state.
         *
         * @return Current SoundState.
         */
        [[nodiscard]] virtual SoundState getStateProperty() const;

        GetTypeNameHPP()
    };
}
