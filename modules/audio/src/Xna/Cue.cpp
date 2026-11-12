// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Internal/AudioLog.hpp"
#include "Internal/XactRpc.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "Microsoft/Xna/Framework/Audio/InstancePlayLimitException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IndexOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    // ── Helpers ───────────────────────────────────────────────────────────────

    namespace
    {
        // Distinguish missing dependencies from other deferred-preparation errors.
        class MissingWaveBank final : public System::InvalidOperationException
        {
        public:
            MissingWaveBank()
                : System::InvalidOperationException("No wavebank exists for the requested operation.") {}
        };

        // FACT converts the complete cent sum to int16 before its +/-2400 total bound.
        // Truncation also applies to RPC-only tracks, independently of value-event presence.
        float CentsToPitch(float cents)
        {
            return std::clamp(std::trunc(cents), -2400.0f, 2400.0f) / 1200.0f;
        }

        std::mt19937& Rng()
        {
            static std::mt19937 rng{std::random_device{}()};
            return rng;
        }

        CNA::Internal::Audio::XactVariationRandom DefaultVariationRandom()
        {
            return {
                [](uint32_t bound) {
                    return std::uniform_int_distribution<uint32_t>(0, bound - 1)(Rng());
                },
                [] { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()); }
            };
        }

        float CentibelsToAmplitude(float centibels)
        {
            return static_cast<float>(std::pow(10.0, centibels / 2000.0));
        }

        std::size_t SelectTrackVariationIndex(
            const std::vector<CNA::Internal::Audio::XsbTrackVariationEntry>& entries,
            CNA::Internal::Audio::XsbTrackVariationType type)
        {
            CNA::Internal::Audio::XsbWaveRef wave{};
            wave.trackVariationEntries = entries;
            wave.trackVariationType = type;
            CNA::Internal::Audio::XactVariationState state;
            CNA::Internal::Audio::AdvanceXactVariation(wave, state, DefaultVariationRandom());
            return state.candidate;
        }

        struct EffectVariationResult
        {
            float pitchCentsDelta = 0.0f;
            float baseVolumeCentibels = 0.0f;
            bool hasFilterOverride = false;
            float filterFrequencyHz = 0.0f;
            float filterQFactor = 0.0f;
        };

        EffectVariationResult EffectResult(const CNA::Internal::Audio::XactVariationState& state)
        {
            return {state.pitchCentsDelta, state.volumeCentibels,
                state.hasFilterOverride, state.filterFrequencyHz, state.filterOneOverQ};
        }

        EffectVariationResult ApplyEffectVariation(const CNA::Internal::Audio::XsbWaveRef& waveRef)
        {
            // Existing test seam exercises effects alone, with its historical draw order.
            auto effectsOnly = waveRef;
            effectsOnly.trackVariationEntries.clear();
            effectsOnly.soundVolumeCentibels = 0.0f;
            effectsOnly.trackVolumeCentibels = 0.0f;
            CNA::Internal::Audio::XactVariationState state;
            CNA::Internal::Audio::AdvanceXactVariation(effectsOnly, state, DefaultVariationRandom());
            return EffectResult(state);
        }

        // Evaluates one RPC (Runtime Parameter Control) curve at a given variable value, matching
        // FAudio's FACT_INTERNAL_CalculateRPC (FACT_internal.c): clamps to the first/last point's
        // Y outside the curve's domain, otherwise piecewise-interpolates between the bracketing
        // points using the left point's interpolation type (linear/fast/slow/sin-cos).
        using CNA::Internal::Audio::EvaluateRpcCurve;

        // FrameworkResources.InvalidVariableIndex.
        constexpr const char* kInvalidVariableIndex = "The specified variable index is invalid.";

        // Standard per-cue 3D variables that XACT projects always define (used by
        // FACT3DApply); always valid even if the parsed .xgs/.xsb data doesn't declare
        // them, since CNA's XactParser only sees what a hand-authored test fixture
        // includes, unlike the real XACT Auditioning Tool which adds these by default.
        bool IsBuiltInCueVariable(const std::string& name)
        {
            // P10-RPC-003: "AttackTime"/"ReleaseTime" are the same kind of always-present default
            // XACT project variable as the three 3D ones above -- recognized here for the same
            // reason (a hand-authored test fixture's .xgs/.xsb won't declare them the way the real
            // XACT Auditioning Tool does).
            return name == "Distance" || name == "DopplerPitchScalar" || name == "OrientationAngle"
                || name == "AttackTime" || name == "ReleaseTime";
        }

        // P10-RPC-002: the three values FAudio's FACT3DApply (FACT3D.c) writes into a cue's
        // built-in "Distance"/"DopplerPitchScalar"/"OrientationAngle" variables every Apply3D call,
        // computed from F3DAudioCalculate's output (F3DAudio.c). Recomputed independently here
        // (not read back from SoundEffectInstance::Apply3D, which is a private per-instance call
        // with no return value, and whose own Doppler/distance-attenuation are one-shot-applied-
        // to-the-track values, not cue-level state) using the same XNA-space vectors already
        // exposed on AudioListener/AudioEmitter.
        struct Cue3DVariables { float distance; float dopplerFactor; float orientationAngleDegrees; };

        Cue3DVariables ComputeCue3DVariables(
            const Microsoft::Xna::Framework::Audio::AudioListener& listener,
            const Microsoft::Xna::Framework::Audio::AudioEmitter& emitter)
        {
            const auto& lp = listener.getPositionProperty();
            const auto& ep = emitter.getPositionProperty();

            const float dx = ep.X - lp.X;
            const float dy = ep.Y - lp.Y;
            const float dz = ep.Z - lp.Z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            // FAudio's "emitterToListener" vector points from the emitter toward the listener
            // (listener.Position - emitter.Position), i.e. -(dx,dy,dz) here.

            // Raw, pre-global-DopplerScale ratio -- matches SoundEffectInstance.cpp's
            // ComputeDopplerFactor (P9-3D-005) exactly; duplicated rather than shared since it's
            // pure, self-contained math with no shared state, following this file's existing
            // precedent (CentsToPitch/EvaluateRpcCurve above).
            float dopplerFactor = 1.0f;
            const float dopplerScaler = emitter.getDopplerScaleProperty();
            if (dopplerScaler > 0.0f)
            {
                float listenerVelComponent = 0.0f;
                float emitterVelComponent  = 0.0f;
                if (distance != 0.0f)
                {
                    const auto& lv = listener.getVelocityProperty();
                    const auto& ev = emitter.getVelocityProperty();
                    listenerVelComponent = (-dx * lv.X - dy * lv.Y - dz * lv.Z) / distance;
                    emitterVelComponent  = (-dx * ev.X - dy * ev.Y - dz * ev.Z) / distance;
                }

                const float speedOfSound = SoundEffect::getSpeedOfSoundProperty();
                const float scaledSpeedOfSound = speedOfSound / dopplerScaler;
                listenerVelComponent = std::min(listenerVelComponent, scaledSpeedOfSound);
                emitterVelComponent  = std::min(emitterVelComponent, scaledSpeedOfSound);

                dopplerFactor = (speedOfSound - dopplerScaler * listenerVelComponent) /
                                (speedOfSound - dopplerScaler * emitterVelComponent);
                if (std::isnan(dopplerFactor)) dopplerFactor = 1.0f;
                dopplerFactor = std::clamp(dopplerFactor, 0.5f, 4.0f);
            }

            // FAudio's F3DAudioCalculate EMITTER_ANGLE branch: angle between the emitter-to-
            // listener direction and the emitter's own OrientFront, via acos of their normalized
            // dot product; a degenerate (near-zero-distance) case falls back to a fixed PI/2. No
            // NaN guard here, matching FAudio exactly (F3DAudio.c has none for this calculation).
            float orientationAngleRadians;
            if (distance < 1.2e-7f)
            {
                orientationAngleRadians = std::numbers::pi_v<float> / 2.0f;
            }
            else
            {
                const auto& forward = emitter.getForwardProperty();
                const float dp = (-dx * forward.X - dy * forward.Y - dz * forward.Z) / distance;
                orientationAngleRadians = std::acos(dp);
            }
            const float orientationAngleDegrees =
                orientationAngleRadians * (180.0f / std::numbers::pi_v<float>);

            return {distance, dopplerFactor, orientationAngleDegrees};
        }
    }

    // ── Constructor / destructor ──────────────────────────────────────────────

    Cue::Cue(std::string name, SoundBank* bank, uint16_t cueIndex)
        : name_(std::move(name)), bank_(bank), cueIndex_(cueIndex)
    {
        if (bank_ && bank_->engine_)
            variationRandom_ = bank_->engine_->preparationVariationRandom_;
        const auto* xsb = bank_ ? bank_->GetXsbData() : nullptr;
        if (xsb && cueIndex_ < xsb->cues.size())
        {
            const auto& definition = xsb->cues[cueIndex_];
            interactive_ = !definition.isSingleSound && definition.varIndex < xsb->variations.size()
                && xsb->variations[definition.varIndex].type == 3;
        }
        try
        {
            // FACTSoundBank_Prepare defers only interactive sound selection until Play.
            if (!interactive_) PrepareSound();
            // Refresh GetCue forwards category FAIL8ac70008; public Cue.Play swallows it.
            if (hasPreparedSound_ && bank_ && bank_->engine_
                && !bank_->engine_->CanPrepareCategory(categoryIdx_))
                throw InstancePlayLimitException("Failed to start sound effect instance due to instance limit.");
            state_ = State::Prepared;
            // Register lifetime last: a failed constructor must leave no dangling bank entry.
            if (bank_) bank_->RegisterCue(this);
        }
        catch (...)
        {
            for (auto* waveBank : waveBanksUsed_) waveBank->UnregisterCue(this);
            throw;
        }
    }

    Cue::~Cue()
    {
        // A destructor cannot let a Disposing handler's exception out (it is noexcept, and the
        // throw would terminate the process inside an enclosing cascade); it is dropped here.
        try { Dispose(); }
        catch (...) {}
    }

    // ── State properties ──────────────────────────────────────────────────────

    bool Cue::getIsCreatedProperty()   const { return !isDisposed_ && state_ == State::Created;   }
    bool Cue::getIsDisposedProperty()  const { return isDisposed_; }
    // P9-LIFECYCLE-013: matches real FACT (FACTCue_Pause never clears PLAYING) -- IsPaused and
    // IsPlaying can both be true at once, since paused_ is an independent flag on top of Playing.
    bool Cue::getIsPausedProperty()    const { ReconcileState(); return !isDisposed_ && state_ == State::Playing && paused_; }
    bool Cue::getIsPlayingProperty()   const { ReconcileState(); return !isDisposed_ && state_ == State::Playing;   }
    bool Cue::getIsPreparedProperty()  const { return !isDisposed_ && state_ == State::Prepared;  }
    bool Cue::getIsPreparingProperty() const { return !isDisposed_ && state_ == State::Preparing; }
    // Refresh Cue.get_IsStopped IL_0000-0014 reads the native state, which is 0 once the handle is
    // released (GetStatus IL_0048-004e), so a disposed cue reports no state at all.
    bool Cue::getIsStoppedProperty()   const { ReconcileState(); return !isDisposed_ && state_ == State::Stopped;   }
    bool Cue::getIsStoppingProperty()  const { ReconcileState(); return !isDisposed_ && state_ == State::Stopping;  }
    const std::string& Cue::getNameProperty() const { return name_; }

    float Cue::RpcVariableValue(const CNA::Internal::Audio::XgsVariable& variable,
                                 std::size_t clockTrack, bool soundCodes) const
    {
        const bool cueLocal = (variable.accessibility & 0x04) != 0;
        if (cueLocal && variable.name == "NumCueInstances") return numCueInstances_;
        if (cueLocal && variable.name == "AttackTime")
        {
            const auto now = ScheduleNow();
            const auto elapsed = scheduleElapsed_
                + (!paused_ && now > scheduleAnchor_ ? now - scheduleAnchor_
                    : std::chrono::steady_clock::duration::zero());
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                elapsed).count();
            const uint32_t firstTimestamp = soundCodes ? soundRpcAttackOrigin_
                : clockTrack < scheduledTracks_.size()
                && !scheduledTracks_[clockTrack].events.empty()
                ? scheduledTracks_[clockTrack].events.front().authored.timestamp : 0;
            // Native track clocks use authored time; sound clocks use the event instance.
            // The scheduler retains CNA's existing freeze-across-pause policy.
            return static_cast<float>(static_cast<uint32_t>(elapsedMs) - firstTimestamp);
        }
        if (cueLocal && variable.name == "ReleaseTime")
        {
            if (state_ == State::Stopping && releaseRpcMS_ > 0)
                return static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    ScheduleNow() - releaseStart_).count());
            return 0.0f;
        }
        if (cueLocal)
        {
            const auto overridden = variables_.find(variable.name);
            return overridden != variables_.end() ? overridden->second : variable.initialValue;
        }
        auto* engine = bank_ ? bank_->engine_ : nullptr;
        float value;
        return engine && engine->TryGetGlobalVariableValue(variable.name, value)
            ? value : variable.initialValue;
    }

    float Cue::ReverbSend(std::size_t track) const
    {
        auto* engine=bank_?bank_->engine_:nullptr;
        if(!engine||!wetBus_)return 0;
        float centibels=0;
        const auto accumulate=[&](const auto& codes,std::size_t clockTrack,bool soundCodes) {
            for(auto code:codes) {
                const auto* rpc=engine->FindRpcByCode(code);
                if(!rpc||rpc->parameter!=2)continue;
                const auto* variable=engine->FindVariableByIndex(rpc->variable);
                if(!variable||variable->name.empty())continue;
                centibels+=EvaluateRpcCurve(*rpc,RpcVariableValue(*variable,clockTrack,soundCodes));
            }
        };
        // FACT_internal.c:1039,1092-1094 retains independent sound/track contributions.
        accumulate(rpcCodes_,0,true);
        if(track<trackRpcCodes_.size())accumulate(trackRpcCodes_[track],track,false);
        const float gain=std::pow(10.0f,centibels/2000.0f);
        if(!std::isfinite(gain))throw System::InvalidOperationException("Invalid reverb send");
        return gain;
    }

    Cue::RpcResult Cue::EvaluateRpc(std::size_t track) const
    {
        // Retain authored units until the complete per-voice sum. Converting a component
        // here can underflow even when the final gain after cancellation is finite.
        float soundVolumeCentibels = 0.0f;
        float trackVolumeCentibels = 0.0f;
        float soundPitchCents    = 0.0f;
        float trackPitchCents    = 0.0f;
        // P10-FILTER-002/003: -1.0f sentinel ("no RPC curve targets this axis"), matching
        // FAudio's own convention (FACT_internal.c) -- see RpcResult's declaration.
        float rpcFilterFreqHz  = -1.0f;
        float rpcFilterQFactor = -1.0f;

        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng)
        {
            const auto applyCodes = [&](const std::vector<uint32_t>& codes, std::size_t clockTrack,
                                        bool soundCodes)
            {
                for (uint32_t code : codes)
                {
                    const CNA::Internal::Audio::XgsRpc* rpc = eng->FindRpcByCode(code);
                    if (!rpc) continue;

                    const auto* variable = eng->FindVariableByIndex(rpc->variable);
                    if (!variable || variable->name.empty()) continue;
                    const float value = RpcVariableValue(*variable,clockTrack,soundCodes);

                    const float result = EvaluateRpcCurve(*rpc, value);

                    if (rpc->parameter == 0)
                    {
                        auto& volumeCentibels = soundCodes ? soundVolumeCentibels : trackVolumeCentibels;
                        volumeCentibels += result;
                    }
                    else if (rpc->parameter == 1)
                    {
                        auto& pitchCents = soundCodes ? soundPitchCents : trackPitchCents;
                        pitchCents += result;
                    }
                    // P10-FILTER-002/003: matches FAudio's FACT_INTERNAL_UpdateRPCs (FACT_internal.c)
                    // exactly -- "Yes, just overwrite...": unlike volume/pitch above, a filter-
                    // targeting RPC's result plainly overwrites (last curve evaluated wins), never
                    // accumulates across multiple bound curves. `result` here is the curve's own
                    // authored-domain output (desired frequency in Hz for FILTERFREQUENCY, a plain Q
                    // value for FILTERQFACTOR) -- SoundEffectInstance::INTERNAL_applyRpcFilterOverride
                    // does the Hz->normalized-cutoff and Q->oneOverQ conversions, matching how
                    // FACT_INTERNAL_UpdateRPCs itself defers those same conversions to its own callers.
                    else if (rpc->parameter == 3) rpcFilterFreqHz  = result; // RPC_PARAMETER_FILTERFREQUENCY
                    else if (rpc->parameter == 4) rpcFilterQFactor = result; // RPC_PARAMETER_FILTERQFACTOR
                    // Send RPCs are evaluated per voice by ReverbSend; engine DSP RPCs by AudioEngine.
                }
            };
            applyCodes(rpcCodes_, 0, true);
            if (track < trackRpcCodes_.size()) applyCodes(trackRpcCodes_[track], track, false);
        }

        return {soundVolumeCentibels, trackVolumeCentibels, soundPitchCents, trackPitchCents,
            rpcFilterFreqHz, rpcFilterQFactor};
    }

    void Cue::ReconcileState() const
    {
        // P9-STOP-003/004: State::Stopping (an authored/non-immediate Stop() with a real release
        // tail still playing -- see StopInternal()) reconciles to Stopped the same way Playing
        // does, once every active_ instance has actually finished. Deliberately does NOT
        // unregister from waveBanksUsed_/AudioEngine here even in the Stopping case, for the same
        // reason P9-LIFECYCLE-001 never did for the Playing case -- see StopInternal()'s comment.
        if (isDisposed_ || (state_ != State::Playing && state_ != State::Stopping)
            || (active_.empty() && (!schedulerStarted_ || (state_ == State::Playing && unresolvedWave_))))
            return;

        // Reevaluate bound RPCs and value-event pitch; cues with neither retain identity
        // RPC contributions and need no steady-state pitch write.
        const bool hasRpc = !rpcCodes_.empty() || HasValueEvents()
            || std::any_of(trackRpcCodes_.begin(), trackRpcCodes_.end(),
                [](const auto& codes) { return !codes.empty(); });
        if(wetBus_)for(const auto& voice:active_)if(voice.instance)
            voice.instance->SetXactReverbBus(wetBus_,ReverbSend(voice.track));

        // P9-STOP-010: a real authored fadeOutMS is a wall-clock deadline, not something the
        // underlying wave's own natural length has any bearing on (StopInternal()'s LongWaveBank-
        // backed regression fixtures deliberately use a 1-second wave with a ~100ms fade to prove
        // this) -- matches FACT_INTERNAL_UpdateSound's SOUND_STATE_FADE_OUT handling
        // (FACT_internal.c): linear volume ramp down to 0 over fadeOutMS_, then hard-stop once
        // elapsed, regardless of whether the wave itself would still have audio left to play.
        // Same non-negotiable rule as the natural-completion path just above: never touch
        // waveBanksUsed_/AudioEngine's registries from here (mutate-during-iteration hazard,
        // P9-LIFECYCLE-001) -- that unregistration only ever happens from StopInternal() (explicit
        // Stop(Immediate)/Dispose()) or SoundBank's fire-and-forget sweep destroying this Cue.
        if (fadeOutMS_ > 0)
        {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                ScheduleNow() - fadeStart_).count();

            auto* self = const_cast<Cue*>(this);
            if (elapsedMs >= fadeOutMS_)
            {
                self->CancelSchedule();
                self->active_.clear();
                self->state_ = State::Stopped;
                self->paused_ = false;
                self->fadeOutMS_ = 0;
                if (self->bank_) self->bank_->RefreshCueInstanceCount(self->cueIndex_);
                return;
            }

            const float fadeMultiplier = 1.0f
                - static_cast<float>(elapsedMs) / static_cast<float>(fadeOutMS_);
            const AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
            const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;
            for (const auto& pi : active_)
                if (pi.instance)
                {
                    const RpcResult rpc = hasRpc ? EvaluateRpc(pi.track) : RpcResult{};
                    pi.instance->SetXactVolume(VoiceVolume(
                        pi.baseVolumeCentibels,rpc,pi.track,catVol,fadeMultiplier));
                    if (hasRpc)
                    {
                        pi.instance->SetXactPitch(VoicePitch(
                            rpc, pi.effectPitchCentsDelta, pi.track));
                        // P10-FILTER-002/003
                        pi.instance->INTERNAL_applyRpcFilterOverride(rpc.filterFrequencyHz, rpc.filterQFactor);
                    }
                }
            return;
        }

        // P10-RPC-004: this cue's RPC-only release phase (StopInternal() only ever enters this
        // OR the authored-fadeOutMS_ branch above, never both -- mirrors FAudio's Stop()
        // if/else-if between FACT_INTERNAL_BeginFadeOut/BeginReleaseRPC, FACT.c:2434-2448).
        // Unlike the authored fade, no extra volume ramp is applied here: FAudio's
        // SOUND_STATE_RELEASE_RPC holds fadeVolume at a constant 1.0f (FACT_internal.c) and
        // lets the "ReleaseTime"-bound RPC curve itself (already reflected in `rpc` above, via
        // EvaluateRpc()'s live substitution while this phase is active) shape the volume.
        // hasRpc is guaranteed true whenever releaseRpcMS_ > 0, since maxRpcReleaseTime_ can
        // only be positive if a qualifying RPC curve was found in trackRpcCodes_ at Play() time.
        if (state_ == State::Stopping && releaseRpcMS_ > 0)
        {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                ScheduleNow() - releaseStart_).count();

            auto* self = const_cast<Cue*>(this);
            if (elapsedMs >= releaseRpcMS_)
            {
                self->CancelSchedule();
                self->active_.clear();
                self->state_ = State::Stopped;
                self->paused_ = false;
                self->releaseRpcMS_ = 0;
                if (self->bank_) self->bank_->RefreshCueInstanceCount(self->cueIndex_);
                return;
            }

            const AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
            const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;
            for (const auto& pi : active_)
                if (pi.instance)
                {
                    const RpcResult rpc = hasRpc ? EvaluateRpc(pi.track) : RpcResult{};
                    pi.instance->SetXactVolume(VoiceVolume(
                        pi.baseVolumeCentibels,rpc,pi.track,catVol,1.0f));
                    if (hasRpc)
                    {
                        pi.instance->SetXactPitch(VoicePitch(
                            rpc, pi.effectPitchCentsDelta, pi.track));
                        // P10-FILTER-002/003
                        pi.instance->INTERNAL_applyRpcFilterOverride(rpc.filterFrequencyHz, rpc.filterQFactor);
                    }
                }
            return;
        }

        // P9-CATEGORY-007: real category-authored fadeInMS, wall-clock driven the same way
        // P9-STOP-010's fadeOutMS_ ramp is above -- matches FACT_INTERNAL_UpdateSound's
        // SOUND_STATE_FADE_IN handling (FACT_internal.c): linear volume ramp from 0 up to the
        // cue's normal target volume over fadeInMS_, then clear the fade and settle at full
        // volume. Unlike the Stopping/fadeOutMS_ branch above, this does NOT return early --
        // real FACT keeps ticking a fading-in sound's normal per-frame update (including natural-
        // completion) right alongside the fade, so the natural-completion check below still runs
        // this same tick.
        if (state_ == State::Playing && fadeInMS_ > 0)
        {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                ScheduleNow() - fadeInStart_).count();

            auto* self = const_cast<Cue*>(this);
            const AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
            const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;

            if (elapsedMs >= fadeInMS_)
            {
                self->fadeInMS_ = 0;
                for (const auto& pi : active_)
                    if (pi.instance)
                    {
                        const RpcResult rpc = hasRpc ? EvaluateRpc(pi.track) : RpcResult{};
                        pi.instance->SetXactVolume(VoiceVolume(
                            pi.baseVolumeCentibels,rpc,pi.track,catVol,1.0f));
                        if (hasRpc)
                        {
                            pi.instance->SetXactPitch(VoicePitch(
                                rpc, pi.effectPitchCentsDelta, pi.track));
                            // P10-FILTER-002/003
                            pi.instance->INTERNAL_applyRpcFilterOverride(rpc.filterFrequencyHz, rpc.filterQFactor);
                        }
                    }
            }
            else
            {
                const float fadeMultiplier =
                    static_cast<float>(elapsedMs) / static_cast<float>(fadeInMS_);
                for (const auto& pi : active_)
                    if (pi.instance)
                    {
                        const RpcResult rpc = hasRpc ? EvaluateRpc(pi.track) : RpcResult{};
                        pi.instance->SetXactVolume(VoiceVolume(
                            pi.baseVolumeCentibels,rpc,pi.track,catVol,fadeMultiplier));
                        if (hasRpc)
                        {
                            pi.instance->SetXactPitch(VoicePitch(
                                rpc, pi.effectPitchCentsDelta, pi.track));
                            // P10-FILTER-002/003
                            pi.instance->INTERNAL_applyRpcFilterOverride(rpc.filterFrequencyHz, rpc.filterQFactor);
                        }
                    }
            }
        }
        else if (state_ == State::Playing && hasRpc)
        {
            // P9-XACT-016: steady-state continuous re-application -- no fade active, but this
            // cue has RPC bindings, so volume/pitch must keep tracking the bound variable's
            // current value every tick, matching FACT_INTERNAL_UpdateSound's unconditional
            // per-tick FACTWave_SetVolume/SetPitch calls (FACT_internal.c). A plain cue with no
            // RPC bindings never reaches this branch (hasRpc is false), keeping its original
            // zero-per-tick-work steady-state path unchanged.
            const AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
            const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;
            for (const auto& pi : active_)
                if (pi.instance)
                {
                    const RpcResult rpc = hasRpc ? EvaluateRpc(pi.track) : RpcResult{};
                    pi.instance->SetXactVolume(VoiceVolume(
                        pi.baseVolumeCentibels,rpc,pi.track,catVol,1.0f));
                    pi.instance->SetXactPitch(VoicePitch(
                        rpc, pi.effectPitchCentsDelta, pi.track));
                    // P10-FILTER-002/003
                    pi.instance->INTERNAL_applyRpcFilterOverride(rpc.filterFrequencyHz, rpc.filterQFactor);
                }
        }

        if (HasPendingEvents()) return;
        for (const auto& pi : active_)
            if (pi.instance && pi.instance->getStateProperty() != SoundState::Stopped)
                return; // at least one wave reference is still playing (or looping)

        auto* self = const_cast<Cue*>(this);
        self->active_.clear();
        // HND-XACT-01 interactive/{normal,AtoB,BtoA}: Microsoft3.6 wave Stop/Destroy
        // precede cue8/count1 at4500ms. Refresh state getters IL_0000-0014 forward it.
        if (state_ == State::Playing && interactive_) return;
        self->state_ = State::Stopped;
        self->paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
        if (self->bank_) self->bank_->RefreshCueInstanceCount(self->cueIndex_);
    }

    // ── Variables ─────────────────────────────────────────────────────────────

    float Cue::GetVariable(const std::string& name) const
    {
        // XNA GetVariable IL_0000-IL_0017 validates the name before native handle access.
        if (name.empty())
            throw System::ArgumentNullException("name", "This method does not accept null for this parameter.");
        // P9-LIFECYCLE-015: matches Play()/Apply3D()'s own disposed guard in this class. Real
        // FACT has no equivalent guard at all -- FACTCue_GetVariableIndex dereferences
        // pCue->parentBank BEFORE its `pCue == NULL` check (FACT.c), so calling this on a
        // disposed FNA Cue (handle == IntPtr.Zero after OnCueDestroyed()) would crash natively
        // rather than throw a catchable exception. That's an unintentional native bug, not a
        // documented contract worth reproducing -- throwing here is strictly safer and matches
        // this class's own established precedent instead.
        if (isDisposed_)
            throw System::ObjectDisposedException("Cue");

        if (name == "NumCueInstances")
        {
            auto* engine = bank_ ? bank_->engine_ : nullptr;
            if (engine && engine->GetCueVariableInfo(name).valid) return numCueInstances_;
            throw System::IndexOutOfRangeException(kInvalidVariableIndex);
        }

        auto it = variables_.find(name);
        if (it != variables_.end())
            return it->second;

        // P12-VAR-001: matches FACTCue_GetVariableIndex exactly (FACT.c) -- only a variable with
        // BOTH the PUBLIC and CUE accessibility bits set is reachable through the cue-level API
        // at all; an engine-GLOBAL (PUBLIC, non-CUE) variable is a genuinely different domain,
        // never reachable via Cue::GetVariable in real XNA/FACT. Internal curve and variation
        // evaluation use their own lookups to read either domain.
        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng)
        {
            auto info = eng->GetCueVariableInfo(name);
            if (info.valid)
                return info.initialValue; // never explicitly SetVariable()-d on this cue yet
        }
        // An XACT project always authors these; only a hand-made project may lack them. An
        // authored one that is not public cue-scoped is refused like any other.
        if (IsBuiltInCueVariable(name) && !(eng && eng->FindVariable(name)))
            return 0.0f;

        throw System::IndexOutOfRangeException(kInvalidVariableIndex);
    }

    void Cue::SetVariable(const std::string& name, float value)
    {
        // XNA SetVariable IL_0000-IL_0017 validates the name before native handle access.
        if (name.empty())
            throw System::ArgumentNullException("name", "This method does not accept null for this parameter.");
        // P9-LIFECYCLE-015: see GetVariable()'s comment above -- same rationale.
        if (isDisposed_)
            throw System::ObjectDisposedException("Cue");

        // P12-VAR-001: matches FACTCue_SetVariable exactly (FACT.c) -- same PUBLIC+CUE gate as
        // GetVariable above, plus a READONLY rejection and authored [minValue,maxValue] clamp
        // (FAudio's `FAudio_clamp(nValue, var->minValue, var->maxValue)`). Unlike
        // SetGlobalVariable's READONLY case (a silent no-op, since FNA's C# wrapper discards the
        // native call's error code), FACTCue_SetVariable's caller in FNA -- Cue.SetVariable --
        // ALSO never checks its return code, so this is a silent no-op here too, not a throw.
        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng)
        {
            auto info = eng->GetCueVariableInfo(name);
            if (info.valid)
            {
                if (!info.readOnly)
                    variables_[name] = std::clamp(value, info.minValue, info.maxValue);
                return;
            }
        }
        if (IsBuiltInCueVariable(name) && !(eng && eng->FindVariable(name)))
        {
            variables_[name] = value;
            return;
        }

        // Refresh native Cue.SetVariable IL_0054-006a/0085-0094 forwards the invalid index's
        // XACTENGINE_E_INVALIDVARIABLEINDEX, which Framework Helpers.GetExceptionFromResult
        // IL_0189-0193 turns into IndexOutOfRangeException(InvalidVariableIndex).
        throw System::IndexOutOfRangeException(kInvalidVariableIndex);
    }

    float Cue::GetVariableForRpc(const std::string& name) const
    {
        if (name == "NumCueInstances")
        {
            auto* engine = bank_ ? bank_->engine_ : nullptr;
            const auto* variable = engine ? engine->FindVariable(name) : nullptr;
            if (variable && (variable->accessibility & 0x04)) return numCueInstances_;
        }

        auto it = variables_.find(name);
        if (it != variables_.end())
            return it->second;

        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng)
        {
            auto cueInfo = eng->GetCueVariableInfo(name);
            if (cueInfo.valid)
                return cueInfo.initialValue;
            // Internal evaluation reads a cue-scoped variable whether or not it is public.
            const auto* authored = eng->FindVariable(name);
            if (authored != nullptr && (authored->accessibility & 0x04) != 0)
                return authored->initialValue;
            if (authored == nullptr && IsBuiltInCueVariable(name))
                return 0.0f;

            float globalValue = 0.0f;
            if (eng->TryGetGlobalVariableValue(name, globalValue))
                return globalValue;
        }

        return 0.0f;
    }

    // ── 3D ───────────────────────────────────────────────────────────────────

    void Cue::Apply3D(const AudioListener& listener, const AudioEmitter& emitter)
    {
        if (isDisposed_) throw System::ObjectDisposedException("Cue");
        // XNA refuses to start positioning a cue that has already been played unpositioned
        // (Cue.Apply3D IL_001c-IL_0036: !applied3D && played -> InvalidOperationException);
        // once positioned before playing, later Apply3D calls keep updating it.
        if (!has3D_ && played_)
            // XNA 4.0 Refresh: Cue.Apply3D IL_002c-0036 / Apply3DBeforePlaying.
            throw System::InvalidOperationException(
                "You must call Apply3D on a Cue before calling Play to be able to call Apply3D after calling Play.");
        // XACT3DApply checks each of its three SetVariable writes (Xact IL_0060-0063, 008c-008f,
        // 00c4-00c6) and Cue.Apply3D sets applied3D only after success (IL_0086-0088): a project
        // that authors one of them outside the public cue scope refuses before anything changes.
        if (AudioEngine* eng = bank_ ? bank_->engine_ : nullptr)
            for (const char* name : {"Distance", "DopplerPitchScalar", "OrientationAngle"})
                if (eng->FindVariable(name) && !eng->GetCueVariableInfo(name).valid)
                    throw System::IndexOutOfRangeException(kInvalidVariableIndex);

        // SDL3_mixer has no per-cue 3D audio graph (FAudio's FACT3DApply); approximate by
        // applying the same pan/distance-attenuation/Doppler SoundEffectInstance::Apply3D already
        // does (CP-3, P9-3D-005) to every wave reference currently playing under this cue.
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Apply3D(listener, emitter);

        // AUDIO-001 finding 4: cache for Play()'s per-wave-reference loop to seed onto any
        // PlaybackInstance created after this call (see has3D_'s own comment in Cue.hpp) --
        // additional to, not instead of, the immediate forward to already-active instances above.
        has3D_             = true;
        pending3DListener_ = listener;
        pending3DEmitter_  = emitter;

        // P10-RPC-002: matches FAudio's FACT3DApply (FACT3D.c), which unconditionally writes
        // these three built-in variables from its own F3DAudioCalculate output on every Apply3D
        // call, regardless of whether a voice is currently active -- so an RPC curve bound to
        // "Distance"/"OrientationAngle"/"DopplerPitchScalar" now tracks this cue's live 3D state
        // instead of whatever was last manually SetVariable()-d (or the prior hardcoded 0.0f
        // default).
        // XACT3DApply (Xact IL_0039-00c4) and FACT3DApply (FACT3D.c:161-175) write them through
        // the cue's SetVariable: an authored variable keeps its range and read-only flag, and one
        // that is not public cue-scoped is not written.
        const Cue3DVariables vars3d = ComputeCue3DVariables(listener, emitter);
        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        const auto writeSpatial = [this, eng](const char* name, float value)
        {
            if (eng && eng->FindVariable(name))
            {
                const auto info = eng->GetCueVariableInfo(name);
                if (!info.valid || info.readOnly) return;
                value = std::clamp(value, info.minValue, info.maxValue);
            }
            variables_[name] = value;
        };
        writeSpatial("Distance", vars3d.distance);
        writeSpatial("DopplerPitchScalar", vars3d.dopplerFactor);
        writeSpatial("OrientationAngle", vars3d.orientationAngleDegrees);
    }

    // ── Play ──────────────────────────────────────────────────────────────────

    void Cue::PrepareSound()
    {
        using namespace CNA::Internal::Audio;
        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        const XsbData* xsb = bank_ ? bank_->GetXsbData() : nullptr;
        if (!xsb || cueIndex_ >= xsb->cues.size()) return;

        const XsbCue& cueDef = xsb->cues[cueIndex_];

        // Resolve the sound (or pick from variation)
        const XsbSound* sound = nullptr;

        if (cueDef.isSingleSound)
        {
            if (cueDef.soundIndex < xsb->sounds.size())
                sound = &xsb->sounds[cueDef.soundIndex];
        }
        else if (cueDef.varIndex < xsb->variations.size())
        {
            const XsbVariation& var = xsb->variations[cueDef.varIndex];

            if (!var.entries.empty() && var.type == 3) // INTERACTIVE
            {
                // FACT selects an interactive table's entry by locating the one whose
                // [varMin, varMax] range contains the current value of the table's bound
                // variable (FAudio's get_active_variation_index, VARIATION_TABLE_TYPE_INTERACTIVE
                // branch) -- first matching entry in file order wins, matching FAudio's forward
                // linear scan. get_active_variation_index itself explicitly dispatches to
                // FACTCue_GetVariable or FACTAudioEngine_GetGlobalVariable depending on the
                // variable's ACCESSIBILITY_CUE bit (FACT_internal.c) -- GetVariableForRpc
                // reproduces exactly that dual dispatch (P9-XACT-003, P12-VAR-001).
                const std::string* varName = eng ? eng->GetVariableNameByIndex(var.variable) : nullptr;
                if (varName && !varName->empty())
                {
                    const float value = GetVariableForRpc(*varName);
                    for (const auto& e : var.entries)
                    {
                        if (value >= e.varMin && value <= e.varMax)
                        {
                            if (e.isSoundEntry && e.soundIndex < xsb->sounds.size())
                                sound = &xsb->sounds[e.soundIndex];
                            break;
                        }
                    }
                }
                // FAudio: no matching entry (or an unresolvable variable) means
                // get_active_variation_index() returns false and create_sound() aborts entirely
                // -- the cue stays Playing but is silent, not an error. `sound` is left nullptr,
                // which the fallback below already handles the same way.
            }
            else if (!var.entries.empty())
            {
                // FACT selects a variation entry via a weighted lottery over each entry's
                // [weightMin, weightMax) range (FAudio's get_active_variation_index): entries
                // authored with a wider weight range are proportionally more likely to be
                // picked, not merely one-of-N uniformly. This applies to every non-interactive
                // table type (wave/sound/compact_wave) -- FAudio itself uses the identical
                // algorithm for all of them.
                //
                // P11-XACT-004: FAudio's own boundary check is `value > (max - weight)`, correct
                // for its *continuous* float draw (`FACT_INTERNAL_rng() * max`, where landing
                // exactly on an integer boundary has zero probability) but NOT for this discrete
                // integer draw copied verbatim from it -- see P11-XACT-002's `WeightedPickExcluding`
                // (Cue.cpp, same fix, fuller derivation in its comment) for why a strict `>` here
                // systematically steals one unit of probability mass from every entry except the
                // implicit index-0 fallback, collapsing to a total (not just approximate) bias for
                // small/equal weights (e.g. two equal-weight-1 entries never pick index 1 at all).
                // `>=` reproduces FAudio's continuous per-entry probability mass exactly. A prior
                // pass (`P10-VAR-002/005`) explicitly compared this loop against FAudio's C source
                // character-for-character and concluded `>` was a correct byte-for-byte port --
                // structurally true, but that comparison never accounted for FAudio's `next` being
                // continuous while this draw is discrete, which is exactly what `P11-XACT-002`
                // caught while writing an equal-weight regression test for its own copy of this
                // pattern (the audio plan, `P11-XACT-004`).
                uint32_t totalWeight = 0;
                for (const auto& e : var.entries)
                    totalWeight += static_cast<uint32_t>(e.weightMax) - e.weightMin;

                uint16_t pick = 0;
                if (totalWeight == 0)
                {
                    std::uniform_int_distribution<uint16_t> dist(
                        0, static_cast<uint16_t>(var.entries.size() - 1));
                    pick = dist(Rng());
                }
                else
                {
                    std::uniform_int_distribution<uint32_t> valueDist(0, totalWeight - 1);
                    const uint32_t value = valueDist(Rng());
                    uint32_t remaining = totalWeight;
                    for (int32_t i = static_cast<int32_t>(var.entries.size()) - 1; i > 0; --i)
                    {
                        const uint32_t weight =
                            static_cast<uint32_t>(var.entries[i].weightMax) - var.entries[i].weightMin;
                        if (value >= (remaining - weight))
                        {
                            pick = static_cast<uint16_t>(i);
                            break;
                        }
                        remaining -= weight;
                    }
                }

                const XsbVariEntry& ve = var.entries[pick];

                if (ve.isSoundEntry && ve.soundIndex < xsb->sounds.size())
                {
                    sound = &xsb->sounds[ve.soundIndex];
                }
                else if (!ve.isSoundEntry && eng)
                {
                    // Wave-level variation: synthesise a temporary sound entry
                    static thread_local XsbSound tmp;
                    tmp = XsbSound{};
                    tmp.volume      = 1.0f;
                    tmp.pitchCents  = 0;
                    tmp.priority    = 255;
                    tmp.categoryIndex = 0;
                    XsbWaveRef wr{ve.wavebankIndex, ve.waveIndex, 0, 1.0f};
                    tmp.waves = {wr};
                    sound = &tmp;
                }
            }
        }

        if (!sound) return;

        categoryIdx_    = sound->categoryIndex;
        priority_       = sound->priority;
        rpcCodes_       = sound->rpcCodes;    // P9-XACT-016: retained for continuous re-eval
        basePitchCents_ = sound->pitchCents;  // P9-XACT-016
        if(sound->dspCodes.size()>1)throw System::InvalidOperationException("Unsupported DSP chain");
        wetBus_=sound->dspCodes.empty()?0:eng->ResolveDspBus(sound->dspCodes.front());
        trackRpcCodes_.clear();
        for(const auto& track:sound->tracks)trackRpcCodes_.push_back(track.rpcCodes);

        // FACT_internal.c:802-827 derives release only from track RPCs bound to
        // a cue-scoped ReleaseTime volume variable; PUBLIC is not required.
        maxRpcReleaseTime_ = 0;
        if (eng)
        {
            for (const auto& codes : trackRpcCodes_)
                for (uint32_t code : codes)
                {
                    const CNA::Internal::Audio::XgsRpc* candidate = eng->FindRpcByCode(code);
                    if (!candidate || candidate->parameter != 0 || candidate->points.empty()) continue;

                    const auto* variable = eng->FindVariableByIndex(candidate->variable);
                    if (!variable || !(variable->accessibility & 0x04)
                        || variable->name != "ReleaseTime") continue;

                    const float lastX = candidate->points.back().x;
                    if (lastX > static_cast<float>(maxRpcReleaseTime_))
                        maxRpcReleaseTime_ = static_cast<uint32_t>(lastX);
                }
        }

        try { PrepareSchedule(*sound); }
        catch (const MissingWaveBank&)
        {
            if (!interactive_) throw;
            // HND-XACT-01 interactive/missingA,missingB,lateB-playing: native Play
            // succeeds with no wave/event dispatch and no retry after a100ms bank load.
            // Refresh Cue.Play IL_000c-001c sets its played latch after this success.
            scheduledTracks_.clear();
            schedulerStarted_ = false;
            unresolvedWave_ = true;
        }
        hasPreparedSound_ = true;
        preparedReplacementEligible_ = true;
    }

    void Cue::Play()
    {
        if (isDisposed_) throw System::ObjectDisposedException("Cue");
        // P9-LIFECYCLE-010/011: FACTCue_Play (FACT.c) silently rejects a cue whose state already
        // has PLAYING, STOPPING, or STOPPED set -- FNA's Cue.Play() discards the return value, so
        // from the C# caller's perspective this is just a no-op, not an exception. A Cue models
        // exactly one playthrough, not a restartable voice. A paused cue is rejected too: real
        // FACT keeps the PLAYING bit set while paused (pausing never clears it, P9-LIFECYCLE-013),
        // so it's already covered by the State::Playing check below without a separate paused_
        // check. Reconciling first ensures a cue that finished naturally (but whose state_ is
        // still the stale Playing value from before the next query) is correctly treated as
        // already-stopped rather than being resurrected by a stray Play() call.
        ReconcileState();
        if (state_ == State::Playing || state_ == State::Stopping || state_ == State::Stopped)
        {
            played_ = true;
            return;
        }

        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (interactive_) PrepareSound();
        // Refresh Cue.Play IL_000c-001c marks success/ignored limit rejection,
        // but an error from deferred preparation must leave the Apply3D latch untouched.
        played_ = true;
        if (!hasPreparedSound_)
        {
            state_ = State::Playing;
            if (eng) eng->RegisterCue(this);
            return;
        }

        // P9-CATEGORY-005/006/007/008/011: real FACT enforces the cue's OWN instanceLimit first,
        // then the category's, right here before the new sound is actually allowed to start
        // (FACT_internal.c's play_sound() checks cue->data->instanceLimit, then
        // sound->sound->category's, in that exact order). FAIL at either stage rejects this cue
        // outright (matches FACTCue_Stop(cue, IMMEDIATE) on the *new* cue in
        // handle_instance_limit); a fade-in decided at the cue level is overwritten by the
        // category level if that ALSO triggers, matching play_sound()'s sequential
        // `fade_in_ms = ...` assignment (category wins if both fire), not an additive combination.
        uint16_t categoryFadeInMS = 0;
        if (eng && bank_)
        {
            const AudioEngine::InstanceLimitDecision cueDecision =
                eng->CheckCueInstanceLimit(bank_, cueIndex_, this);
            if (!cueDecision.allowed)
            {
                StopInternal(true);
                return;
            }
            categoryFadeInMS = cueDecision.fadeInMS;
        }
        if (eng)
        {
            const AudioEngine::InstanceLimitDecision decision =
                eng->CheckCategoryInstanceLimit(categoryIdx_, this);
            if (!decision.allowed)
            {
                StopInternal(true);
                return;
            }
            if (decision.triggered)
                categoryFadeInMS = decision.fadeInMS; // unconditional overwrite, even if 0
        }

        // Seed fade state before the first zero-time event starts a real voice.
        fadeInMS_ = categoryFadeInMS;
        fadeInStart_ = ScheduleNow();
        // Preparation time is not playback time; keep the original bank ordering.
        scheduleElapsed_ = {};
        scheduleAnchor_ = ScheduleNow();
        soundRpcAttackOrigin_ = 0;
        preparedReplacementEligible_ = true;

        state_ = State::Playing;
        if (eng) eng->RegisterCue(this);
        // AUDIO-LIFECYCLE-001: no bank_->RegisterCue(this) here anymore -- this cue has already
        // been registered with its bank since construction (see Cue::Cue()'s comment), which lets
        // SoundBank::Dispose() force-stop it whether it's still Prepared, currently Playing, or
        // anywhere in between, whether it's a fire-and-forget cue or one the caller obtained via
        // GetCue() and is holding independently (see SoundBank::activeCues_'s comment).

        UpdateSchedule(ScheduleNow());
        if (HasValueEvents()) ReconcileState();
    }

    std::chrono::steady_clock::time_point Cue::ScheduleNow() const
    {
        const auto* eng = bank_ ? bank_->engine_ : nullptr;
        return eng && eng->eventClock_ ? eng->eventClock_() : std::chrono::steady_clock::now();
    }

    bool Cue::HasPendingEvents() const
    {
        for (const auto& track : scheduledTracks_)
            for (const auto& event : track.events)
                if (!event.finished || event.loopsRemaining != 0) return true;
        return false;
    }

    void Cue::CancelSchedule()
    {
        // Retain the owned event records until destruction: event-dispatch callers may
        // currently hold references into this vector. No registry mutation here.
        for (auto& track : scheduledTracks_)
            for (auto& event : track.events)
            { event.finished = true; event.loopsRemaining = 0; }
    }

    CNA::Internal::Audio::XactVariationRandom Cue::VariationRandom() const
    {
        auto random = DefaultVariationRandom();
        if (variationRandom_.bounded)
            random.bounded = [this](uint32_t bound) { return variationRandom_.bounded(bound); };
        if (variationRandom_.unit)
            random.unit = [this] { return variationRandom_.unit(); };
        return random;
    }

    void Cue::RetainWaveBank(WaveBank* bank)
    {
        if (std::find(waveBanksUsed_.begin(), waveBanksUsed_.end(), bank) != waveBanksUsed_.end()) return;
        waveBanksUsed_.push_back(bank);
        try { bank->RegisterCue(this); }
        catch (...) { waveBanksUsed_.pop_back(); throw; }
    }

    void Cue::PrepareWave(ScheduledEvent& event)
    {
        using namespace CNA::Internal::Audio;
        const auto* play = std::get_if<XsbPlayWaveEvent>(&event.authored.payload);
        if (!play) return;
        const auto& wave = play->wave;
        const auto random = VariationRandom();
        SelectXactVariationCandidate(wave, event.variation, random);
        const auto index = wave.trackVariationEntries.empty() ? wave.wavebankIndex
            : wave.trackVariationEntries[event.variation.candidate].wavebankIndex;
        const auto* xsb = bank_ ? bank_->GetXsbData() : nullptr;
        auto* engine = bank_ ? bank_->engine_ : nullptr;
        auto* waveBank = engine && xsb && index < xsb->wavebankNames.size()
            ? engine->FindWaveBank(xsb->wavebankNames[index]) : nullptr;
        // Refresh GetCue/Framework translate native8ac70013 at preparation, before Play.
        if (!waveBank)
        {
            const std::string name = xsb && index < xsb->wavebankNames.size() ? xsb->wavebankNames[index] : "";
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::WARN,
                "[Cue] \"", getNameProperty(), "\" could not find wave bank \"", name, "\" during preparation");
            throw MissingWaveBank{};
        }
        // Microsoft3.6 keeps this resolved bank with the SoundBank, including
        // when a later event's preparation fails; cue registration rolls back separately.
        bank_->RetainResolvedWaveBank(waveBank);
        RetainWaveBank(waveBank);
        ApplyXactVariationEffects(wave, event.variation, random);
        event.wavePrepared = true;
    }

    void Cue::PrepareSchedule(const CNA::Internal::Audio::XsbSound& sound)
    {
        using namespace CNA::Internal::Audio;
        scheduledTracks_.clear();
        scheduleElapsed_ = {};
        soundRpcAttackOrigin_ = 0;
        scheduleAnchor_ = ScheduleNow();
        auto appendWave = [&](std::size_t track, const XsbWaveRef& wave)
        {
            if (wave.wavebankIndex == 0xFF && wave.waveIndex == 0xFFFF)
            { unresolvedWave_ = true; return; }
            XsbEvent event;
            event.payload = XsbPlayWaveEvent{wave};
            scheduledTracks_[track].events.push_back({event, 0, false});
            // A legacy projection has no authored event to report to dispatch observers.
            scheduledTracks_[track].events.back().authoredEvent = false;
            PrepareWave(scheduledTracks_[track].events.back());
        };
        for (std::size_t t = 0; t < sound.tracks.size(); ++t)
        {
            const auto& authored = sound.tracks[t];
            scheduledTracks_.emplace_back();
            if (!authored.complete)
            {
                // Keep the unknown-payload first-wave fallback, but defer audio until Play.
                if (t < sound.waves.size()) appendWave(t, sound.waves[t]);
                continue;
            }
            for (const auto& event : authored.events)
            {
                // Native timing jitter remains unqualified; retain authored timestamps.
                scheduledTracks_.back().events.push_back({event, event.timestamp, false});
                auto& scheduled = scheduledTracks_.back().events.back();
                if (const auto* value = std::get_if<XsbValueEvent>(&event.payload))
                {
                    scheduledTracks_.back().hasValues = true;
                    scheduled.repeatsRemaining = value->repeats;
                }
                PrepareWave(scheduled);
            }
        }
        if (sound.tracks.empty())
        {
            scheduledTracks_.resize(sound.waves.size());
            for (std::size_t t = 0; t < sound.waves.size(); ++t) appendWave(t, sound.waves[t]);
        }
        schedulerStarted_ = !scheduledTracks_.empty();
    }

    void Cue::StopScheduledTrack(std::size_t track, bool immediate)
    {
        // FACT_INTERNAL_StopTrack:1189-1214 cancels every future event in this track.
        unresolvedWave_ = false;
        for (auto& event : scheduledTracks_[track].events)
        { event.finished = true; event.loopsRemaining = 0; }
        for (auto& pi : active_)
            if (pi.track == track && pi.instance) pi.instance->Stop(immediate);
    }

    bool Cue::HasValueEvents() const
    {
        return std::any_of(scheduledTracks_.begin(), scheduledTracks_.end(),
            [](const ScheduledTrack& track) { return track.hasValues; });
    }

    float Cue::VoiceVolume(float baseCentibels,const RpcResult& rpc,std::size_t track,
                          float categoryVolume,float fadeMultiplier) const
    {
        float cents = baseCentibels;
        cents += rpc.soundVolumeCentibels;
        cents += rpc.trackVolumeCentibels;
        if (track < scheduledTracks_.size()) cents += scheduledTracks_[track].volumeCentibels;
        return CentibelsToAmplitude(cents) * categoryVolume * fadeMultiplier;
    }

    float Cue::VoicePitch(const RpcResult& rpc,float effectCents,std::size_t track) const
    {
        // FACT_internal.c:1563-1571: regrouping float RPC sums can cross an integer
        // cent boundary. Effect pitch belongs to the integer base before either scope.
        const auto basePitch = static_cast<int16_t>(
            static_cast<int32_t>(basePitchCents_) + static_cast<int32_t>(effectCents));
        float cents = static_cast<float>(basePitch);
        cents += rpc.soundPitchCents;
        cents += rpc.trackPitchCents;
        if (track < scheduledTracks_.size() && scheduledTracks_[track].hasValues)
        {
            cents += scheduledTracks_[track].pitchCents;
        }
        return CentsToPitch(cents);
    }

    void Cue::ActivateValue(ScheduledEvent& event, ScheduledTrack& track, uint64_t elapsedMS)
    {
        using namespace CNA::Internal::Audio;
        const auto& value = std::get<XsbValueEvent>(event.authored.payload);
        const bool pitch = event.authored.type == XsbEventType::Pitch
                        || event.authored.type == XsbEventType::PitchRepeating;
        float& target = pitch ? track.pitchCents : track.volumeCentibels;
        if (const auto* ramp = std::get_if<XsbRamp>(&value.value))
        {
            // FACT_internal.c:1300-1317,2445. initialSlope already includes x100.
            // FACT:1302 leaves slopeDelta unapplied. Keep its arithmetic for duration0
            // too (0/0 at onset); native qualification of both cases remains open.
            const float endpoint = ramp->initialValue
                + ramp->initialSlope * ramp->duration / 1000.0f * 10.0f;
            const float fraction = std::clamp(
                static_cast<float>(elapsedMS - event.dueMS) / ramp->duration, 0.0f, 1.0f);
            target = (endpoint - ramp->initialValue) * fraction + ramp->initialValue;
            event.finished = elapsedMS > event.dueMS + ramp->duration;
            return;
        }
        const auto& equation = std::get<XsbEquation>(value.value);
        float result = 0.0f;
        // FACT_internal.c:1321-1353: literal wins when both value/random bits are set.
        if (equation.flags & 0x04) result = equation.value1;
        else if (equation.flags & 0x08)
        {
            // stb_frand divides a u32 by 2^32, then FACT casts to float.
            // Float rounding can reach 1 even though the double draw is < 1.
            const float unit = valueRandom_ ? valueRandom_()
                : static_cast<float>(static_cast<double>(Rng()()) / 4294967296.0);
            result = equation.value1 + unit * (equation.value2 - equation.value1);
        }
        target = (equation.flags & 0x01) ? target + result : result;
        if (event.repeatsRemaining > 0)
        {
            // FACT_internal.c:1371-1382: repeats are ADDITIONAL activations;
            // both 0xff and 0xffff are unbounded, one activation per engine update.
            if (event.repeatsRemaining != 0xFF && event.repeatsRemaining != 0xFFFF)
                --event.repeatsRemaining;
            event.dueMS += value.frequency;
            event.finished = false;
        }
    }

    void Cue::UpdateSchedule(std::chrono::steady_clock::time_point now)
    {
        using namespace CNA::Internal::Audio;
        if (isDisposed_ || (state_ != State::Playing && state_ != State::Stopping)
            || paused_ || !schedulerStarted_) return;
        if (state_ == State::Stopping || fadeOutMS_ > 0)
        {
            const bool fadeExpired = fadeOutMS_ > 0 && now - fadeStart_ >= std::chrono::milliseconds(fadeOutMS_);
            const bool releaseExpired = releaseRpcMS_ > 0 && now - releaseStart_ >= std::chrono::milliseconds(releaseRpcMS_);
            if (fadeExpired || releaseExpired) return; // ReconcileState cancels at the deadline.
        }
        if (now > scheduleAnchor_) scheduleElapsed_ += now - scheduleAnchor_;
        scheduleAnchor_ = std::max(now, scheduleAnchor_);
        const uint64_t elapsedMS = std::chrono::duration_cast<std::chrono::milliseconds>(scheduleElapsed_).count();
        // FACT evaluates sound RPCs before events advance their instance timestamps.
        // Keep that origin for reconciliation/getters after this dispatch pass.
        soundRpcAttackOrigin_ = !scheduledTracks_.empty() && !scheduledTracks_[0].events.empty()
            ? static_cast<uint32_t>(scheduledTracks_[0].events[0].dueMS) : 0;
        // FACT_INTERNAL_UpdateSound:1496-1522 scans tracks/events in file order,
        // independently tests due events (also for decreasing authored timestamps).
        for (std::size_t t = 0; t < scheduledTracks_.size(); ++t)
            for (std::size_t e = 0; e < scheduledTracks_[t].events.size(); ++e)
            {
                auto& event = scheduledTracks_[t].events[e];
                if (event.finished || elapsedMS < event.dueMS) continue;
                event.finished = true;
                if (const auto* play = std::get_if<XsbPlayWaveEvent>(&event.authored.payload))
                {
                    const uint8_t count = play->wave.loopCount;
                    const bool trackNewOnLoop = (play->variationInfo & 0x00400000u) != 0;
                    // Only the effect families carry effect variation (a synthetic simple-sound
                    // event is a PlayWave).
                    const bool newEffects = (event.authored.type == XsbEventType::PlayWaveEffectVariation
                        || event.authored.type == XsbEventType::PlayWaveTrackEffectVariation)
                        && (play->wave.effectVariationFlags & 0x0f00) != 0;
                    // Refresh Cue.Play IL_0006 reaches native IXACT3Cue::Play (UnsafeNativeMethods/
                    // Cue::Play IL_0032-0036), so the count is Microsoft XACT3.6 behaviour, measured
                    // on x86/x64 (docs/xact-looping.md; finite-cardinality/ and loop-extensions/
                    // ORACLE.json): every play-wave family (1, 3, 4, 6), ordered, random or shuffle,
                    // plays a finite byte n (0..5, 100, 254) as n+1 passes, whatever the flags byte.
                    // Track NEW_ON_LOOP makes every pass a new complete wave; without it one wave
                    // loops n times, repeating its loop region when one is authored. FAudio plays
                    // max(n,1) waves. An effect NEW_ON_LOOP bit alone natively also holds one wave,
                    // whose effect varies over its passes (HND-XACT-06 pitch cells); a CNA voice
                    // has no per-pass update, so CNA renders each pass as a complete wave with
                    // re-drawn effects and the measured count. 255 keeps FAudio's delegation
                    // (FACT_internal.c:271-278) under the owner's unbounded policy.
                    const bool delegated = !trackNewOnLoop && !newEffects;
                    event.managedLoops = count > 0 && !delegated;
#ifndef SOUND_ENABLED
                    // NULL audio has no completion-producing voice. Preserve its
                    // existing one-shot/property behavior instead of inventing loops.
                    event.managedLoops = false;
#endif
                    // The first wave is not part of n (FAudio's initial GetNextWave consumes
                    // one finite count instead). 255 stays the unbounded sentinel.
                    event.loopsRemaining = event.managedLoops ? count : 0;
                    StartWave(play->wave, t, e, &event);
                }
                else if (const auto* stop = std::get_if<XsbStopEvent>(&event.authored.payload))
                {
                    if (stop->flags & 0x02) StopPlayback((stop->flags & 0x01) != 0);
                    else StopScheduledTrack(t, (stop->flags & 0x01) != 0);
                }
                else if (std::holds_alternative<XsbValueEvent>(event.authored.payload))
                    ActivateValue(event, scheduledTracks_[t], elapsedMS);
                if (dispatchObserver_ && event.authoredEvent) dispatchObserver_(t, e, elapsedMS);
                if (state_ != State::Playing && state_ != State::Stopping) return;
            }
        UpdateWaveLoops();
    }

    void Cue::UpdateWaveLoops()
    {
        using namespace CNA::Internal::Audio;
        // Control thread only; never allocate voices or touch banks in a mixer callback.
        // FACT.c:559-569 prepares repeats; FACT_internal.c:1530-1548 promotes on EOF.
        // This bounded polling path promotes one observed completion per Update.
        for (std::size_t t = 0; t < scheduledTracks_.size(); ++t)
            for (std::size_t e = 0; e < scheduledTracks_[t].events.size(); ++e)
            {
                auto& event = scheduledTracks_[t].events[e];
                if (!event.managedLoops || event.loopsRemaining == 0) continue;
                const auto voice = std::find_if(active_.begin(), active_.end(),
                    [=](const PlaybackInstance& pi) { return pi.track == t && pi.event == e; });
                if (voice == active_.end()) { event.loopsRemaining = 0; continue; }
                if (voice->instance && voice->instance->getStateProperty() != SoundState::Stopped) continue;
                active_.erase(voice);
                // Owner-approved255 remains infinite. FAudio's nondelegated255 instead
                // decrements to254 on initial preparation (FACT_internal.c:449-451).
                if (event.loopsRemaining != 255) --event.loopsRemaining;
                StartWave(std::get<XsbPlayWaveEvent>(event.authored.payload).wave, t, e, &event);
            }
    }

    void Cue::StartWave(const CNA::Internal::Audio::XsbWaveRef& waveRef, std::size_t track,
                        std::size_t event, ScheduledEvent* loopEvent)
    {
        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        const auto* xsb = bank_ ? bank_->GetXsbData() : nullptr;
        const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;
        const RpcResult rpc = EvaluateRpc(track);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            ScheduleNow() - fadeInStart_).count();
        // RPC-only release replaces fade-in and keeps its own fade multiplier at1.
        float fadeMultiplier = state_ != State::Playing || fadeInMS_ == 0 ? 1.0f : std::clamp(
            static_cast<float>(elapsed) / fadeInMS_, 0.0f, 1.0f);
        if (fadeOutMS_ > 0)
            fadeMultiplier = std::clamp(1.0f - std::chrono::duration<float, std::milli>(
                ScheduleNow() - fadeStart_).count() / fadeOutMS_, 0.0f, 1.0f);
        if (!eng || !xsb) return;

        // P11-XACT-002: a PlayWaveTrackVariation-family event's real candidate wave, chosen
        // via FAudio's own selection algorithm, instead of waveRef.wavebankIndex/waveIndex's
        // always-entry-0 parse-time fallback. Empty entries means a plain PlayWave/
        // PlayWaveEffectVariation event -- use waveRef's own fields directly, unchanged.
        CNA::Internal::Audio::XactVariationState localVariation;
        auto& variation = loopEvent ? loopEvent->variation : localVariation;
        const auto random = VariationRandom();
        const bool prepared = loopEvent && std::exchange(loopEvent->wavePrepared, false);
        // A finite event without track NEW_ON_LOOP natively holds its first candidate for every
        // pass (HND-XACT-05 nol0, HND-XACT-06 type6nol0/randomnol0/shufflenol0); a pass CNA
        // renders as a separate wave for an effect NEW_ON_LOOP must not reselect it. FAudio
        // reselects regardless (FACT_internal.c:193-247); 255 keeps that.
        const bool holdCandidate = loopEvent && variation.started && waveRef.loopCount != 255
            && !(std::get<CNA::Internal::Audio::XsbPlayWaveEvent>(loopEvent->authored.payload).variationInfo
                 & 0x00400000u);
        if (!prepared && !holdCandidate)
            CNA::Internal::Audio::SelectXactVariationCandidate(waveRef, variation, random);
        uint8_t  effectiveWavebankIndex = waveRef.wavebankIndex;
        uint16_t effectiveWaveIndex     = waveRef.waveIndex;
        if (!waveRef.trackVariationEntries.empty())
        {
            const std::size_t picked = variation.candidate;
            effectiveWavebankIndex = waveRef.trackVariationEntries[picked].wavebankIndex;
            effectiveWaveIndex     = waveRef.trackVariationEntries[picked].waveIndex;
        }

        const std::string& wbName = (effectiveWavebankIndex < xsb->wavebankNames.size())
            ? xsb->wavebankNames[effectiveWavebankIndex] : "";

        // AUD-11-016: previously a silent `continue` with zero cue-level context on either
        // failure path -- WaveBank::GetSoundEffect() logs its own wave-index-only diagnostic
        // (no cue identity, no wave-bank name in most of its messages), but nothing here ever
        // named *which cue* tried to play the missing/failed wave, the actual "truthful state"
        // gap this task's acceptance criterion is about (a game developer debugging a missing
        // sound has no way to trace it back to the cue that requested it from these logs
        // alone). Both branches below are genuinely reachable in practice: an authored .xsb
        // referencing a wave bank that was never loaded/registered (a real content-authoring
        // mistake, not just theoretical), and a wave bank that loaded but a specific entry
        // failed to decode (AUDIO-ADPCM-001's exact original symptom).
        WaveBank* wb = wbName.empty() ? nullptr : eng->FindWaveBank(wbName);
        if (!wb)
        {
            unresolvedWave_ = true;
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::WARN,
                "[Cue] \"", getNameProperty(), "\" could not find wave bank \"", wbName,
                "\" for wave ", effectiveWaveIndex, " -- skipping");
            return;
        }

        const SoundEffect* sf = wb->GetSoundEffect(effectiveWaveIndex);
        if (!sf)
        {
            unresolvedWave_ = true;
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::WARN,
                "[Cue] \"", getNameProperty(), "\" wave ", effectiveWaveIndex, " in bank \"",
                wbName, "\" failed to load -- skipping");
            return;
        }

        if (!prepared) CNA::Internal::Audio::ApplyXactVariationEffects(waveRef, variation, random);
        const EffectVariationResult effect = EffectResult(variation);

        auto inst = std::make_unique<SoundEffectInstance>(sf->CreateInstance());
        const float combinedVol = VoiceVolume(effect.baseVolumeCentibels,rpc,track,catVol,fadeMultiplier);
        // Include the current fade before playback so the first audio does not depend
        // on a later reconciliation pass to establish the configured gain.
        inst->SetXactVolume(combinedVol);
        inst->SetXactPitch(
            VoicePitch(rpc, effect.pitchCentsDelta, track));
        inst->SetXactLoopCount(loopEvent && loopEvent->managedLoops ? 0 : waveRef.loopCount);

        // AUDIO-ORDER-001: seed this brand-new instance with the last Apply3D() this cue
        // received, if any, BEFORE inst->Play() rather than after -- otherwise a wave
        // reference triggered after Apply3D() (e.g. this cue's very first Play(), or a later
        // PlayWaveEffectVariation-family retrigger) would start unspatialized for its first
        // few output frames until Apply3D() ran again a moment later, unlike an instance that
        // was already active when Apply3D() itself ran (see the for loop above).
        if (has3D_) inst->Apply3D(pending3DListener_, pending3DEmitter_);

        // P14-ORDER-002: wire the track's real filter into the real SDL3_mixer filter callback
        // BEFORE inst->Play() rather than after -- SoundEffectInstance::INTERNAL_apply*Filter
        // are now order-independent of Play() (they establish/update the pending filter state
        // in filterState_ regardless of whether a live track exists yet; only the actual
        // SDL3_mixer callback registration is deferred to Play()'s own
        // EnsureTrackDspState()/INTERNAL_applyComposedTrackProperties() call if there's no
        // track yet), so doing this first means this track's very first output frame already
        // has the authored filter applied, instead of a brief window where it could play
        // unfiltered until a moment later -- the same "configure everything, then start"
        // ordering P14-ORDER-001 already applied to volume/pitch/3D state. Effect variation's
        // randomized frequency/Q, when authored, *replaces* the plain per-track authored base
        // value (matches FAudio's own "Initial Filter Variation" branch, a straight overwrite,
        // not additive); RPC continues to override either base live every tick exactly as
        // before, unaffected by which one established it.
        if (waveRef.filterType != 0xFF)
        {
            if (effect.hasFilterOverride)
            {
                inst->INTERNAL_applyEffectVariationFilter(
                    waveRef.filterType, effect.filterFrequencyHz, effect.filterQFactor);
            }
            else
            {
                inst->INTERNAL_applyXactTrackFilter(
                    waveRef.filterType,
                    static_cast<float>(waveRef.filterFrequencyHz),
                    waveRef.filterQFactorRaw);
            }
        }

        // P10-FILTER-002/003: apply this cue's initial RPC filter frequency/Q evaluation
        // (`rpc`, computed above) right after the base filter is established -- continuously
        // re-evaluated thereafter from ReconcileState() every tick, same continuous-tick
        // pattern P9-XACT-016 already established for volume/pitch. A no-op (see
        // INTERNAL_applyRpcFilterOverride's own guard) for a wave reference with no filter at
        // all, or when this cue has no RPC codes bound (rpc.filterFrequencyHz/filterQFactor
        // are both -1.0f in that case). P14-ORDER-002: also order-independent of Play() now,
        // same as the base filter above.
        inst->INTERNAL_applyRpcFilterOverride(rpc.filterFrequencyHz, rpc.filterQFactor);

        inst->SetXactReverbBus(wetBus_,ReverbSend(track));
        inst->Play();
        unresolvedWave_ = false;

        active_.push_back({std::move(inst), track, event, effect.baseVolumeCentibels,
            effect.pitchCentsDelta});

        bank_->RetainResolvedWaveBank(wb);
        RetainWaveBank(wb);

        // HND-XACT-02, Refresh-selected Microsoft3.6 x86/x64: ordered type3
        // NEW_ON_LOOP prepares one successor before the active wave stops. Missing
        // successor banks terminate the sequence, even if loaded before that EOF.
        // The native88-cell receipt does not qualify random/effect draw timing.
        // Reuse the preparation slot; a prefetched wave must not start a voice or
        // advance selection again when promoted. HND-XACT-05 settles the count: the
        // last of the n+1 waves prepares no successor (raw3: four PREPARED, four PLAY).
        using namespace CNA::Internal::Audio;
        if (loopEvent && loopEvent->managedLoops && loopEvent->loopsRemaining > 0
            && loopEvent->authored.type == XsbEventType::PlayWaveTrackVariation
            && waveRef.trackVariationType == XsbTrackVariationType::Ordered
            && !waveRef.trackVariationEntries.empty()
            && (std::get<XsbPlayWaveEvent>(loopEvent->authored.payload).variationInfo & 0x00400000u))
        {
            try { PrepareWave(*loopEvent); }
            catch (const MissingWaveBank&)
            {
                loopEvent->loopsRemaining = 0;
                loopEvent->wavePrepared = false;
            }
        }
    }

    // ── Pause / Resume / Stop ─────────────────────────────────────────────────

    void Cue::Pause()
    {
        if (isDisposed_) return;
        ReconcileState(); // a naturally-finished cue must not be resurrected into Paused
        if (state_ != State::Playing || paused_) return; // P9-LIFECYCLE-013: idempotent, like FACTCue_Pause
        const auto now = ScheduleNow();
        if (now > scheduleAnchor_) scheduleElapsed_ += now - scheduleAnchor_;
        scheduleAnchor_ = now;
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Pause();
        paused_ = true;
    }

    void Cue::Resume()
    {
        if (isDisposed_) return;
        if (state_ != State::Playing || !paused_) return;
        // FACTWave_Pause(false), FACT.c:2102-2108, leaves stopped waves stopped.
        // SoundEffectInstance::Resume restarts them, so only resume paused voices;
        // a completed event's next variation is promoted by UpdateWaveLoops instead.
        for (auto& pi : active_)
            if (pi.instance && pi.instance->getStateProperty() == SoundState::Paused)
                pi.instance->Resume();
        // Microsoft retail XACT3.6 excludes paused time from marker scheduling.
        // FAudio's resume accounting differs; see docs/xact-pause-clock.md.
        scheduleAnchor_ = ScheduleNow();
        paused_ = false;
    }

    void Cue::Stop(AudioStopOptions options)
    {
        // FACTCue_Stop (FACT.c:2393-2403): a public authored stop leaves a release already under
        // way to its own deadline. An authored cue-wide Stop event restarts it instead (FAudio's
        // event path calls BeginFadeOut/BeginReleaseRPC directly, FACT_internal.c:1224-1252), so
        // the guard is here and not in StopPlayback.
        ReconcileState();
        if (!isDisposed_ && state_ == State::Stopping && options != AudioStopOptions::Immediate)
            return;
        StopInternal(options == AudioStopOptions::Immediate);
    }

    void Cue::StopPlayback(bool immediate)
    {
        if (isDisposed_) return;
        // FACTCue_Stop (FACT.c:2385-2391): a stopped cue stays stopped (nothing is left to release).
        // FACTCue_Stop:2407-2414: a paused cue cannot render a release.
        immediate = immediate || paused_ || state_ == State::Prepared || state_ == State::Stopped;
        // Stop exits pending event-level loops too; release may still dispatch
        // other future authored events until its deadline (GAP-01b).
        for (auto& track : scheduledTracks_)
            for (auto& event : track.events) event.loopsRemaining = 0;
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Stop(immediate);

        // XA-6: pi.instance->Stop(false) above already does the right thing (just exits the
        // loop, leaving the track playing its release/tail) -- but destroying every instance
        // right after (the old unconditional active_.clear()) immediately hard-stops them via
        // ~SoundEffectInstance()'s Dispose() cascade regardless, making AsAuthored behave
        // identically to Immediate. Only actually destroy them here for an immediate stop, where
        // there is no tail to let ring out; a non-immediate stop leaves them owned by active_
        // until this Cue is later disposed (matches FNA: the cue doesn't relinquish its native
        // voice until the release genuinely finishes, not the instant Stop(AsAuthored) is called).
        //
        // P9-STOP-001/002/003/004/010, P10-RPC-004: real FACT (FACTCue_Stop, FACT.c) does NOT
        // transition to FACT_STATE_STOPPED for a non-immediate stop unless there is nothing to
        // release -- "the three ways a Cue might be stopped immediately" are: an explicit
        // immediate request, being already paused, or (fadeOutMS == 0 AND no RPC-release time
        // authored). A "simple" cue's format has no fadeOutMS field at all (always 0, see
        // XactParser.cpp), so Stop(AsAuthored) on one only gets a real tail via the RPC-release
        // path below, never the authored-fade one. Only a real, nonzero, authored fadeOutMS, or
        // (failing that) a positive maxRpcReleaseTime_, gets a real tail -- everything else (no
        // fade authored and no RPC-release time) hard-stops right away, matching FACT's own
        // immediate-stop condition exactly.
        uint16_t fadeOutMS = 0;
        if (!immediate && (!active_.empty() || schedulerStarted_) && bank_)
        {
            if (const CNA::Internal::Audio::XsbData* xsb = bank_->GetXsbData())
                if (cueIndex_ < xsb->cues.size())
                    fadeOutMS = xsb->cues[cueIndex_].fadeOutMS;
        }

        const bool hasRealTail = !immediate && (!active_.empty() || schedulerStarted_) && fadeOutMS > 0;
        if (hasRealTail)
        {
            state_ = State::Stopping;
            paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
            fadeStart_ = ScheduleNow();
            fadeOutMS_ = fadeOutMS;
            if (bank_) bank_->RefreshCueInstanceCount(cueIndex_);
            // Deliberately do NOT touch waveBanksUsed_/AudioEngine's registry here (P9-STOP-005/
            // 009): the old code unregistered immediately regardless of `immediate`, which made
            // WaveBank::IsInUse/AudioEngine's category operations lose track of this cue while it
            // was still audibly playing its tail. Unregistration now only happens once the cue is
            // actually immediate-stopped or disposed -- ReconcileState() itself never touches
            // these registries either (same mutate-during-iteration hazard as P9-LIFECYCLE-001).
            return;
        }

        // P10-RPC-004: matches FAudio's FACTCue_Stop's else-if (FACT.c:2434-2448) -- when there's
        // no authored fadeOutMS but this cue's resolved sound had an RPC curve bound to
        // "ReleaseTime" (RPC_PARAMETER_VOLUME), captured as maxRpcReleaseTime_ at Play() time,
        // Stop() enters a distinct RPC-only release phase (FAudio's SOUND_STATE_RELEASE_RPC)
        // instead of hard-stopping immediately. Same registry-unregistration deferral rationale
        // as the authored-fade branch above.
        const bool hasReleaseRpcTail = !immediate && (!active_.empty() || schedulerStarted_) && maxRpcReleaseTime_ > 0;
        if (hasReleaseRpcTail)
        {
            state_ = State::Stopping;
            paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
            releaseStart_ = ScheduleNow();
            releaseRpcMS_ = maxRpcReleaseTime_;
            if (bank_) bank_->RefreshCueInstanceCount(cueIndex_);
            return;
        }

        CancelSchedule();
        active_.clear();
        state_ = State::Stopped;
        paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
        fadeOutMS_ = 0;
        releaseRpcMS_ = 0; // P10-RPC-004: abort any in-progress RPC-only release phase too
        if (bank_) bank_->RefreshCueInstanceCount(cueIndex_);
    }

    void Cue::StopInternal(bool immediate)
    {
        if (isDisposed_) return;
        StopPlayback(immediate);
        if (state_ != State::Stopped) return;
        for (auto* wb : waveBanksUsed_)
            if (wb) wb->UnregisterCue(this);
        waveBanksUsed_.clear();

        if (bank_ && bank_->engine_)
            bank_->engine_->UnregisterCue(this);
        // AUDIO-LIFECYCLE-001: bank_->UnregisterCue(this) intentionally does NOT happen here
        // anymore -- this cue stays registered with its bank for its whole C++ lifetime (from
        // construction until Dispose(), see Cue::Dispose() below), not just while playing. A
        // caller can hold a stopped-but-undisposed Cue indefinitely; it must still be reachable
        // by SoundBank::Dispose()'s force-stop cascade so its bank_ pointer never dangles.
    }

    void Cue::ForceFadeOutForInstanceLimit(uint16_t fadeOutMS)
    {
        // Microsoft3.6 keeps Playing (and any Paused bit) during an instance-limit fade.
        // Only an authored Stop enters Stopping; see docs/xact-instance-limits.md.
        if (!isDisposed_ && state_ == State::Prepared)
        {
            preparedReplacementEligible_ = false;
            return;
        }
        if (isDisposed_ || state_ != State::Playing) return;

        if (fadeOutMS == 0)
        {
            StopInternal(true);
            return;
        }

        fadeStart_ = ScheduleNow();
        fadeOutMS_ = fadeOutMS;
        fadeInMS_  = 0; // a cue being evicted can't also still be fading in
        if (bank_) bank_->RefreshCueInstanceCount(cueIndex_);
    }

    void Cue::ApplyCategoryVolume(float catVol)
    {
        float fadeMultiplier = 1.0f;
        // FACT_internal.c:1422-1471,1551-1562: category changes retain the current
        // authored fade, rather than briefly writing an unfaded gain to the voice.
        if (fadeOutMS_ > 0)
            fadeMultiplier = std::clamp(1.0f - std::chrono::duration<float, std::milli>(
                ScheduleNow() - fadeStart_).count() / fadeOutMS_, 0.0f, 1.0f);
        else if (state_ == State::Playing && fadeInMS_ > 0)
            fadeMultiplier = std::clamp(std::chrono::duration<float, std::milli>(
                ScheduleNow() - fadeInStart_).count() / fadeInMS_, 0.0f, 1.0f);
        for (auto& pi : active_)
            if (pi.instance)
            {
                const auto rpc = EvaluateRpc(pi.track);
                pi.instance->SetXactVolume(VoiceVolume(
                    pi.baseVolumeCentibels,rpc,pi.track,catVol,fadeMultiplier));
            }
    }

    void Cue::INTERNAL_seedRngForTest(unsigned int seed)
    {
        Rng().seed(seed);
    }

    std::size_t Cue::INTERNAL_selectTrackVariationIndexForTest(
        const std::vector<CNA::Internal::Audio::XsbTrackVariationEntry>& entries,
        CNA::Internal::Audio::XsbTrackVariationType type)
    {
        return SelectTrackVariationIndex(entries, type);
    }

    void Cue::INTERNAL_applyEffectVariationForTest(
        const CNA::Internal::Audio::XsbWaveRef& waveRef,
        float& pitchCentsDelta, float& volumeAmplitudeMultiplier,
        bool& hasFilterOverride, float& filterFrequencyHz, float& filterQFactor)
    {
        const EffectVariationResult result = ApplyEffectVariation(waveRef);
        pitchCentsDelta          = result.pitchCentsDelta;
        volumeAmplitudeMultiplier = CentibelsToAmplitude(result.baseVolumeCentibels);
        hasFilterOverride        = result.hasFilterOverride;
        filterFrequencyHz        = result.filterFrequencyHz;
        filterQFactor            = result.filterQFactor;
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void Cue::Dispose()
    {
        if (!isDisposed_)
        {
            StopInternal(true);
            // AUDIO-LIFECYCLE-001: pairs with the RegisterCue(this) call in the constructor --
            // this cue now stays registered with its bank for its entire lifetime (Prepared,
            // Playing, or Stopped), so unregistering only happens here, at genuine disposal, not
            // earlier when it merely stops playing (see StopInternal()'s comment).
            if (bank_) bank_->UnregisterCue(this);
            isDisposed_ = true;
            // XNA Dispose(bool) IL_0018-IL_0039 releases the cue before event IL_0055.
            Disposing.Raise(this, System::EventArgs::Empty);
        }
    }

    GetTypeNameCPP(Cue, "Microsoft.Xna.Framework.Audio.Cue")
}
