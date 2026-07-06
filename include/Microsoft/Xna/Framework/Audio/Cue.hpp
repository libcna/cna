// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioListener;
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

        // P9-LIFECYCLE-013: real FACT (FACTCue_Pause, FACT.c) only ever sets/clears the PAUSED
        // bit -- it never touches PLAYING, so a cue can be IsPlaying==true and IsPaused==true at
        // the same time in real XNA/FNA. Modeled as an independent flag layered on top of
        // State::Playing (rather than a separate mutually-exclusive State::Paused value) so
        // getIsPlayingProperty()/getIsPausedProperty() can both be true simultaneously, matching
        // FACT's bitmask semantics instead of a single-value enum.
        bool        paused_     = false;

        // P9-STOP-010: real authored fadeOutMS timing for Stop(AsAuthored). Only meaningful
        // while state_ == State::Stopping and fadeOutMS_ > 0 -- see StopInternal()/
        // ReconcileState() for how these drive the linear volume ramp and the eventual
        // Stopping -> Stopped transition, matching FAudio's SOUND_STATE_FADE_OUT handling
        // (FACT_INTERNAL_UpdateSound, FACT_internal.c) instead of waiting for the underlying
        // wave to naturally finish.
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

        // P10-RPC-003: real elapsed-time tracking for the built-in "AttackTime" RPC variable,
        // matching FAudio's FACT_INTERNAL_UpdateRPCs (FACT_internal.c), which feeds the elapsed
        // milliseconds since the cue started playing directly as an "AttackTime"-bound RPC
        // curve's x-domain value. Captured once in Play(), right before every path that
        // transitions to State::Playing. Not pause-adjusted, matching fadeStart_/fadeInStart_'s
        // same existing simplification.
        std::chrono::steady_clock::time_point playStart_{};

        // P10-RPC-004: this cue's RPC-only release duration in milliseconds, computed once in
        // Play() by scanning rpcCodes_ for a curve bound to a variable literally named
        // "ReleaseTime" that targets RPC_PARAMETER_VOLUME, taking the max curve-point x value
        // across all matches -- matches FAudio's FACT_internal.c:790-815
        // (`cue->maxRpcReleaseTime`). StopInternal() consults this when the cue has no authored
        // fadeOutMS, to decide whether Stop(AsAuthored) enters a real RPC-only release phase
        // (FAudio's SOUND_STATE_RELEASE_RPC) instead of hard-stopping immediately.
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
        // rpcCodes_ mirrors XsbSound::rpcCodes (whole-sound level only, same simplification
        // already accepted for the one-shot version); basePitchCents_ is the sound's own
        // authored pitchCents, recombined with a freshly-evaluated RPC pitch result every tick.
        std::vector<uint32_t> rpcCodes_;
        int16_t     basePitchCents_ = 0;

        std::unordered_map<std::string, float> variables_;

        struct PlaybackInstance
        {
            std::unique_ptr<SoundEffectInstance> instance;
            float baseVolume = 1.0f; // waveRef.volume, before category volume is combined in
        };
        std::vector<PlaybackInstance> active_;

        // WaveBanks this cue has registered with (see WaveBank::RegisterCue), so their
        // IsInUse can see this cue while it is playing; unregistered in StopInternal.
        std::vector<WaveBank*> waveBanksUsed_;

        void StopInternal(bool immediate);

        // P9-CATEGORY-005/007: called by AudioEngine::CheckCategoryInstanceLimit() when this cue
        // is chosen as the victim to make room for a new cue exceeding its category's
        // instanceLimit. Reuses the exact Stopping/fadeOutMS_ ramp ReconcileState() already
        // implements for Stop(AsAuthored) (P9-STOP-010) -- a real FACT category-authored
        // fadeOutMS produces the identical audible fade-out shape as a per-cue authored one, only
        // the trigger differs. A zero fadeOutMS hard-stops immediately, matching
        // FACT_INTERNAL_BeginFadeOut's effectively-instant behavior for fadeOutMS == 0
        // (FACT_internal.c).
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

        // P9-XACT-016: result of evaluating every curve in rpcCodes_ against each curve's bound
        // variable's *current* value -- volumeMultiplier is an amplitude ratio (1.0f == no-op),
        // pitch is already in XNA's [-1,1] range (CentsToPitch already applied). Called once at
        // Play() time and continuously thereafter from ReconcileState().
        struct RpcResult { float volumeMultiplier; float pitch; };
        [[nodiscard]] RpcResult EvaluateRpc() const;

        // Re-applies a new category volume to all currently active instances, recombining it
        // with each instance's stored baseVolume (see AudioEngine::SetCategoryVolumeInternal).
        void ApplyCategoryVolume(float catVol);

        // P10-VAR-004: reseeds the file-local RNG (Cue.cpp's anonymous-namespace `Rng()`, shared
        // by every Cue -- matching FAudio's own single process-wide RNG state, FACT_INTERNAL_rng)
        // used for non-interactive weighted variation selection. Test-only: lets a test compute
        // an expected pick independently (seeding an identical std::mt19937 + distribution in the
        // test itself) and cross-check it against Cue::Play()'s real, deterministic-for-a-known-
        // seed outcome, instead of relying on an unseeded std::random_device draw every run.
        NOXNA static void INTERNAL_seedRngForTest(unsigned int seed);

        // Tests need to observe which sound a variation table selected (via the category
        // index it carries) without a real WaveBank/audio device backing playback.
        NOXNA friend struct CueTestAccess;
    };
}
