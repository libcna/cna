// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Audio/XactVariation.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class SoundBank;
    class SoundEffectInstance;
    class WaveBank;

    /** @brief Represents a named sound cue loaded from a SoundBank. */
    class Cue final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when this cue is disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /** @brief Destroys the cue and releases its playback instance. */
        ~Cue() override;

        Cue(const Cue&) = delete;
        Cue& operator=(const Cue&) = delete;

        /** @brief Gets whether the cue has been created but not yet prepared. */
        [[nodiscard]] bool getIsCreatedProperty()   const;

        /** @brief Gets whether the cue has been disposed. */
        [[nodiscard]] bool getIsDisposedProperty()  const;

        /** @brief Gets whether the cue is currently paused. */
        [[nodiscard]] bool getIsPausedProperty()    const;

        /** @brief Gets whether the cue is currently playing. */
        [[nodiscard]] bool getIsPlayingProperty()   const;

        /** @brief Gets whether the cue is prepared and ready to play. */
        [[nodiscard]] bool getIsPreparedProperty()  const;

        /** @brief Gets whether the cue is in the process of being prepared. */
        [[nodiscard]] bool getIsPreparingProperty() const;

        /** @brief Gets whether the cue is fully stopped. */
        [[nodiscard]] bool getIsStoppedProperty()   const;

        /** @brief Gets whether the cue is in the process of stopping. */
        [[nodiscard]] bool getIsStoppingProperty()  const;

        /**
         * @brief Gets the name of this cue as defined in the SoundBank.
         *
         * @return Cue name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Applies 3D positional audio settings to this cue.
         *
         * @param listener Position and orientation of the audio listener.
         * @param emitter  Position and orientation of the sound emitter.
         * @throws System::ObjectDisposedException if the cue has been disposed.
         * @throws System::InvalidOperationException if the cue has been played without first
         *         being positioned with Apply3D.
         */
        void Apply3D(const AudioListener& listener, const AudioEmitter& emitter);

        /**
         * @brief Gets the value of a per-cue XACT variable.
         *
         * @param name Variable name as defined in the SoundBank, or one of the built-in
         *        cue variables ("Distance", "DopplerPitchScalar", "OrientationAngle",
         *        "AttackTime", "ReleaseTime").
         * @return Current value of the variable.
         * @throws System::ObjectDisposedException if the cue has been disposed.
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::InvalidOperationException if @p name is not a valid variable name.
         */
        [[nodiscard]] float GetVariable(const std::string& name) const;

        /**
         * @brief Sets the value of a per-cue XACT variable.
         *
         * @param name  Variable name as defined in the SoundBank, or one of the built-in
         *        cue variables ("Distance", "DopplerPitchScalar", "OrientationAngle",
         *        "AttackTime", "ReleaseTime").
         * @param value New value.
         * @throws System::ObjectDisposedException if the cue has been disposed.
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::InvalidOperationException if @p name is not a valid variable name.
         */
        void SetVariable(const std::string& name, float value);

        /**
         * @brief Starts playback of this cue.
         *
         * @throws System::ObjectDisposedException if the cue has been disposed.
         */
        void Play();

        /** @brief Pauses playback of this cue. */
        void Pause();

        /** @brief Resumes a paused cue. */
        void Resume();

        /**
         * @brief Stops playback of this cue.
         *
         * @param options Whether to stop immediately or after release phases finish.
         */
        void Stop(AudioStopOptions options);

        /** @brief Releases this cue and all associated playback resources. */
        void Dispose() override;

        GetTypeNameHPP()

    private:
        friend class SoundBank;
        friend class AudioEngine;
        Cue(std::string name, SoundBank* bank, uint16_t cueIndex);

        enum class State { Created, Preparing, Prepared, Playing, Stopping, Stopped };

        std::string name_;
        SoundBank*  bank_;
        uint16_t    cueIndex_   = 0xFFFF;
        uint16_t    categoryIdx_= 0xFFFF;
        State       state_      = State::Created;
        bool        isDisposed_ = false;

        /** @brief Last count published for this cue definition; a fresh handle starts at zero. */
        CNAEXT float numCueInstances_ = 0.0f;

        // P9-LIFECYCLE-013: real FACT (FACTCue_Pause, FACT.c) only ever sets/clears the PAUSED
        // bit -- it never touches PLAYING, so a cue can be IsPlaying==true and IsPaused==true at
        // the same time in real XNA/FNA. Modeled as an independent flag layered on top of
        // State::Playing (rather than a separate mutually-exclusive State::Paused value) so
        // getIsPlayingProperty()/getIsPausedProperty() can both be true simultaneously, matching
        // FACT's bitmask semantics instead of a single-value enum.
        bool        paused_     = false;

        // Authored Stop fades use Stopping; instance-limit fades retain Playing/Paused.
        // A positive duration drives the wall-clock gain ramp and retirement deadline
        // independently of those public states; the wave's natural length is irrelevant.
        std::chrono::steady_clock::time_point fadeStart_{};
        uint16_t    fadeOutMS_  = 0;

        // P9-CATEGORY-005/007/008: real category-level instanceLimit enforcement (see
        // AudioEngine::CheckCategoryInstanceLimit(), called from Play()). fadeInMS_/fadeInStart_
        // drive a linear fade-in ramp the same way fadeStart_/fadeOutMS_ already drive the
        // fade-out ramp above, matching FACT_INTERNAL_UpdateSound's SOUND_STATE_FADE_IN handling
        // (FACT_internal.c) -- only meaningful while state_ == State::Playing and fadeInMS_ > 0.
        // priority_ is this cue's resolved sound priority (XsbSound::priority), captured at
        // Play() time so a category's REPLACE_LOWEST_PRIORITY behavior can compare it against
        // other currently-playing cues in the same category without re-reading the .xsb.
        std::chrono::steady_clock::time_point fadeInStart_{};
        uint16_t    fadeInMS_   = 0;
        uint8_t     priority_   = 0;

        // RPC-only release duration: max last-point x across track-scoped VOLUME curves
        // bound to cue-local ReleaseTime (FACT_internal.c:802-827). StopInternal uses this
        // when there is no authored fadeOutMS, entering SOUND_STATE_RELEASE_RPC.
        uint32_t    maxRpcReleaseTime_ = 0;

        // P10-RPC-004: only meaningful while state_ == State::Stopping AND releaseRpcMS_ > 0 --
        // this cue's genuine RPC-only release phase, distinct from the authored fadeOutMS_/
        // fadeStart_ tail above. Entered from StopInternal() when maxRpcReleaseTime_ > 0 but no
        // authored fadeOutMS was authored. Unlike the authored fade, ReconcileState() applies no
        // extra volume ramp of its own here -- FAudio's SOUND_STATE_RELEASE_RPC holds fadeVolume
        // at a constant 1.0f (FACT_internal.c), leaving a "ReleaseTime"-bound RPC curve itself
        // (evaluated live by EvaluateRpc() while in this phase) to shape the volume, if any.
        std::chrono::steady_clock::time_point releaseStart_{};
        uint32_t    releaseRpcMS_ = 0;

        // P9-XACT-016: retained (captured once, at Play() time, from the resolved XsbSound) so
        // ReconcileState() can continuously re-evaluate bound RPC (Runtime Parameter Control)
        // curves every tick instead of only once at Play() -- matches FACT_INTERNAL_UpdateRPCs
        // (FACT_internal.c), which recomputes fresh every engine tick rather than diffing.
        // rpcCodes_ retains sound bindings; trackRpcCodes_ retains each track independently.
        // EvaluateRpc and ReverbSend both consume these lists through RpcVariableValue.
        // basePitchCents_ is the sound's own
        // authored pitchCents, recombined with a freshly-evaluated RPC pitch result every tick.
        std::vector<uint32_t> rpcCodes_;
        std::vector<std::vector<uint32_t>> trackRpcCodes_;
        std::uint64_t wetBus_ = 0;
        float ReverbSend(std::size_t track) const;
        int16_t     basePitchCents_ = 0;

        std::unordered_map<std::string, float> variables_;

        struct PlaybackInstance
        {
            std::unique_ptr<SoundEffectInstance> instance;
            std::size_t track = 0;
            std::size_t event = static_cast<std::size_t>(-1);
            float baseVolumeCentibels = 0.0f;
            // Retain this iteration's native base volume and pitch offset for live updates.
            float effectPitchCentsDelta = 0.0f;
        };
        std::vector<PlaybackInstance> active_;

        // GAP-01b: owned IR and per-event state, never pointers into a temporary sound.
        struct ScheduledEvent
        {
            CNA::Internal::Audio::XsbEvent authored;
            uint64_t dueMS = 0;
            bool finished = false;
            uint16_t repeatsRemaining = 0;
            // Event-level repeats use complete one-shot waves; plain voice loops
            // keep GAP-02's prefix/region/tail contract untouched.
            uint16_t loopsRemaining = 0;
            bool managedLoops = false;
            CNA::Internal::Audio::XactVariationState variation;
        };
        struct ScheduledTrack
        {
            std::vector<ScheduledEvent> events;
            float pitchCents = 0.0f;
            float volumeCentibels = 0.0f;
            bool hasValues = false;
        };
        std::vector<ScheduledTrack> scheduledTracks_;
        bool schedulerStarted_ = false;
        // Preserve the existing Playing-but-silent state if no wave can resolve.
        // A later valid event or track Stop clears it; pending work still dispatches.
        bool unresolvedWave_ = false;
        std::chrono::steady_clock::time_point scheduleAnchor_{};
        std::chrono::steady_clock::duration scheduleElapsed_{};
        // Snapshot before dispatch: repeats can advance the first event during this pass.
        uint32_t soundRpcAttackOrigin_ = 0;
        // Set only by the existing friend test accessor. No public callback/API.
        std::function<void(std::size_t, std::size_t, uint64_t)> dispatchObserver_;

        // Per-cue test injection; production draws only when a random equation activates.
        std::function<float()> valueRandom_;
        CNA::Internal::Audio::XactVariationRandom variationRandom_;
        [[nodiscard]] bool HasValueEvents() const;
        void ActivateValue(ScheduledEvent& event, ScheduledTrack& track, uint64_t elapsedMS);

        void StartWave(const CNA::Internal::Audio::XsbWaveRef& waveRef, std::size_t track,
                       std::size_t event = static_cast<std::size_t>(-1),
                       ScheduledEvent* loopEvent = nullptr);
        void UpdateWaveLoops();
        void PrepareSchedule(const CNA::Internal::Audio::XsbSound& sound);
        void UpdateSchedule(std::chrono::steady_clock::time_point now);
        void CancelSchedule();
        void StopScheduledTrack(std::size_t track, bool immediate);
        [[nodiscard]] bool HasPendingEvents() const;
        [[nodiscard]] std::chrono::steady_clock::time_point ScheduleNow() const;


        // AUDIO-001 finding 4: retained so a wave reference that only starts playing AFTER this
        // cue's last Apply3D() call (a brand new PlaybackInstance a later Play()/effect-variation
        // trigger creates) starts with the same spatial state already-active instances got,
        // instead of silently missing it -- Apply3D() below only ever forwards to active_ at the
        // moment it runs, so with nothing cached here a cue Apply3D()'d before its first Play()
        // (active_ still empty) would lose the call entirely. Matches real FACT, where Apply3D's
        // result lives on the cue's own native handle (created and persistent from the moment the
        // C# Cue object is constructed, independent of whether any wave has actually started
        // playing yet), not on a per-voice object that may not exist yet. Never reset back to
        // false once set, matching SoundEffectInstance::is3D_'s own latch semantics.
        bool has3D_ = false;
        // XNA's Cue.played: set by the first Play(), never cleared. Apply3D on a cue that has
        // played without ever being positioned is refused (Cue.Apply3D IL_001c-IL_0036).
        bool played_ = false;
        AudioListener pending3DListener_;
        AudioEmitter  pending3DEmitter_;

        // WaveBanks this cue has registered with (see WaveBank::RegisterCue), so their
        // IsInUse can see this cue while it is playing; unregistered in StopInternal.
        std::vector<WaveBank*> waveBanksUsed_;

        void StopInternal(bool immediate);
        void StopPlayback(bool immediate);

        // Limit eviction keeps Microsoft's Playing/Paused flags while the common
        // fade marker excludes this victim from another selection. A zero duration
        // stops immediately; authored Stop has its separate Stopping state.
        void ForceFadeOutForInstanceLimit(uint16_t fadeOutMS);

        // P9-LIFECYCLE-001/002: lazily reconciles state_ from Playing to Stopped once every
        // instance in active_ has finished naturally (no explicit Stop() call) -- matches FNA,
        // where FACTCue_GetState always reflects FAudio's live per-voice state (kept current by
        // FAudio's own mixer thread), not a value that only updates on explicit calls. Called
        // from every state getter that can observe Playing, so a natural finish is visible on
        // the very next query instead of being stuck forever. Only mutates active_/state_ (never
        // waveBanksUsed_/AudioEngine's registries), since this runs from const getters that may
        // themselves be invoked while a caller is mid-iteration over those other registries
        // (e.g. WaveBank::getIsInUseProperty()); the actual unregistration happens later, from
        // StopInternal() (explicit Stop()/Dispose(), or SoundBank's fire-and-forget sweep).
        void ReconcileState() const;

        // Stored-variable lookup for interactive variation selection. Curve evaluation
        // uses RpcVariableValue instead; this lookup does not substitute intrinsic clocks.
        [[nodiscard]] float GetVariableForRpc(const std::string& name) const;

        /**
         * @brief Resolves an RPC variable using its scope and the scheduler clock.
         *
         * @param variable Authored variable metadata.
         * @param clockTrack Track whose first event supplies the attack-time origin.
         * @param soundCodes Whether to use the sound's pre-dispatch mutable origin.
         * @return Current stored or intrinsic variable value for curve evaluation.
         */
        [[nodiscard]] float RpcVariableValue(const CNA::Internal::Audio::XgsVariable& variable,
                                              std::size_t clockTrack, bool soundCodes) const;

        // P9-XACT-016/P10-FILTER-002/003: volume/pitch/filter result of evaluating rpcCodes_ against
        // each curve's bound variable's current value. Keep each scope's authored-unit
        // sums separate until per-voice composition and its single amplitude conversion.
        // filterFrequencyHz/filterQFactor default to -1.0f (matching FAudio's own sentinel,
        // FACT_internal.c) meaning "no RPC curve targets this axis" -- unlike volume/pitch, a
        // filter-targeting RPC's result is a plain overwrite (last curve evaluated wins), never
        // summed across multiple bound curves, matching FAudio's own "Yes, just overwrite..."
        // comment. Called once at Play() time and continuously thereafter from ReconcileState().
        struct RpcResult
        {
            float soundVolumeCentibels = 0.0f;
            float trackVolumeCentibels = 0.0f;
            float soundPitchCents = 0.0f;
            float trackPitchCents = 0.0f;
            float filterFrequencyHz = -1.0f;
            float filterQFactor     = -1.0f;
        };
        [[nodiscard]] RpcResult EvaluateRpc(std::size_t track) const;
        /**
         * @brief Composes native per-voice centibels before one amplitude conversion.
         *
         * @param baseCentibels This iteration's sound/track/effect volume base.
         * @param rpc Separately accumulated sound and track curve results.
         * @param track Track supplying the final event-volume contribution.
         * @param categoryVolume Current linear category gain.
         * @param fadeMultiplier Current linear fade gain.
         * @return Combined linear gain before the final FACT wave bound.
         */
        CNAEXT [[nodiscard]] float VoiceVolume(float baseCentibels,const RpcResult& rpc,
            std::size_t track,float categoryVolume,float fadeMultiplier) const;
        /**
         * @brief Composes pitch in native base/effect, sound RPC, track RPC, event order.
         *
         * @param rpc Separately accumulated sound and track curve results.
         * @param effectCents Integer-valued effect variation offset in cents.
         * @param track Track whose value events supply the final contribution.
         * @return Truncated and bounded total pitch in octaves.
         */
        [[nodiscard]] float VoicePitch(const RpcResult& rpc,float effectCents,std::size_t track) const;

        // Re-applies a new category volume to all currently active instances, recombining it
        // with each instance's stored centibel base (see AudioEngine::SetCategoryVolumeInternal).
        void ApplyCategoryVolume(float catVol);

        // P10-VAR-004: reseeds the file-local RNG (Cue.cpp's anonymous-namespace `Rng()`, shared
        // by every Cue -- matching FAudio's own single process-wide RNG state, FACT_INTERNAL_rng)
        // used for non-interactive weighted variation selection. Test-only: lets a test compute
        // an expected pick independently (seeding an identical std::mt19937 + distribution in the
        // test itself) and cross-check it against Cue::Play()'s real, deterministic-for-a-known-
        // seed outcome, instead of relying on an unseeded std::random_device draw every run.
        CNAEXT static void INTERNAL_seedRngForTest(unsigned int seed);

        // P11-XACT-002: test-only entry point for the PlayWaveTrackVariation-family selection
        // algorithm (Cue.cpp's anonymous-namespace SelectTrackVariationIndex), so its exact
        // Ordered/OrderedFromRandom/Random/RandomNoRepeats/Shuffle behavior can be unit-tested
        // directly against a synthetic entry list, without needing a full XACT fixture wired
        // through Cue::Play() for every case. Reuses the same shared Rng() INTERNAL_seedRngForTest
        // above reseeds, for deterministic-for-a-known-seed test outcomes.
        CNAEXT static std::size_t INTERNAL_selectTrackVariationIndexForTest(
            const std::vector<CNA::Internal::Audio::XsbTrackVariationEntry>& entries,
            CNA::Internal::Audio::XsbTrackVariationType type);

        // P11-XACT-003: test-only entry point for the PlayWaveEffectVariation-family randomization
        // algorithm (Cue.cpp's anonymous-namespace ApplyEffectVariation), so its exact pitch/
        // volume/filter draws can be unit-tested directly against a synthetic XsbWaveRef, without
        // needing a full XACT fixture wired through Cue::Play() for every axis/flag combination.
        // Out-params (not a return-by-value struct) since ApplyEffectVariation's own result type
        // is anonymous-namespace-private to Cue.cpp. Reuses the same shared Rng()
        // INTERNAL_seedRngForTest above reseeds, for deterministic-for-a-known-seed test outcomes.
        CNAEXT static void INTERNAL_applyEffectVariationForTest(
            const CNA::Internal::Audio::XsbWaveRef& waveRef,
            float& pitchCentsDelta, float& volumeAmplitudeMultiplier,
            bool& hasFilterOverride, float& filterFrequencyHz, float& filterQFactor);

        // Tests need to observe which sound a variation table selected (via the category
        // index it carries) without a real WaveBank/audio device backing playback.
        CNAEXT friend struct CueTestAccess;
    };
}
