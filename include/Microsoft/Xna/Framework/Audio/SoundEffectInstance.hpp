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
        // -- XactParser.cpp's bit-decode never actually produces one). Not continuously
        // re-evaluated while playing (no per-frame Cue update tick exists, same one-shot-at-
        // Play() narrowing as P9-XACT-006/007's RPC volume/pitch -- see CHECKLIST.md).
        NOXNA void INTERNAL_applyXactTrackFilter(uint8_t filterType, float frequencyHz, uint8_t qfactorRaw);

        // Pure conversion helpers (P9-XACT-011), split out of INTERNAL_applyXactTrackFilter so
        // they're independently unit-testable without a real SDL3_mixer device driving the
        // sample rate. Both match FAudio's exact formulas (FACT_internal.c,
        // FACT_INTERNAL_CalculateFilterFrequency and the inline `1.0f / (qfactor / 3.0f)` at the
        // SOUND_FLAG_COMPLEX track-init site).
        NOXNA static float INTERNAL_calculateFilterCutoff(float frequencyHz, float sampleRate);
        NOXNA static float INTERNAL_calculateFilterOneOverQ(uint8_t qfactorRaw);

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

        // Test-only hook (SoundEffectInstanceTestAccess): runs this instance's filter state
        // through the exact same math the real SDL3_mixer callback uses, but synchronously and
        // directly -- the real callback only fires asynchronously from the mixing thread, which
        // would make a test either flaky or need a real-time wait. No-op if no filter is active.
        void ProcessFilterSamplesForTest(float* pcm, int channels, int samples);

        // Test-only hook (SoundEffectInstanceTestAccess, P9-XACT-011): reads back the active
        // filter's kind (FilterState::Kind cast to int; 0 = none)/frequency/oneOverQ without
        // exposing FilterState's definition outside SoundEffectInstance.cpp.
        void INTERNAL_getFilterStateForTest(int& kind, float& frequency, float& oneOverQ) const;

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
