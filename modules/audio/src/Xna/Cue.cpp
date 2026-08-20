// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    // ── Helpers ───────────────────────────────────────────────────────────────

    namespace
    {
        // Convert XACT pitch (cents, range -1200..+1200) to XNA [-1..+1]. Takes a float (rather
        // than the underlying int16_t storage) so a sound's base pitch and an RPC pitch curve's
        // result (P9-XACT-007) can be summed in cents before converting once, matching FAudio's
        // own additive-then-convert-once combination (FACT_internal.c update_sound_data).
        float CentsToPitch(float cents)
        {
            return std::clamp(cents / 1200.0f, -1.0f, 1.0f);
        }

        std::mt19937& Rng()
        {
            static std::mt19937 rng{std::random_device{}()};
            return rng;
        }

        // P11-XACT-002: weighted-random pick among entries[0..size), optionally excluding one
        // index (pass entries.size() to exclude nothing), reused by SelectTrackVariationIndex
        // below for every algorithm that needs a weighted draw. Matches FAudio's own
        // weighted-lottery scan direction (FACT_internal.c's repeated downward scan, comparing
        // against `max - weight`), both in FACT_INTERNAL_GetNextWave and the sound-level
        // variation table Cue::Play() already implements elsewhere in this file. FAudio's own
        // `next` is a *continuous* float draw (`FACT_INTERNAL_rng() * max`), so its boundary
        // check `next > (max - weight)` has zero probability of landing exactly on the boundary;
        // this uses a discrete integer draw instead, so the boundary comparison must be `>=` (not
        // `>`) to give the boundary value itself to the entry being scanned, or every entry after
        // the first one checked (i.e. every index below the highest, when weights are small
        // enough for exact ties to be reachable at all) silently loses its lowest-value roll to
        // whichever entry is checked immediately after it -- e.g. two equal-weight-1 entries with
        // a plain `>` and dist(0, total-1) = dist(0, 1) never selects the higher index at all
        // (next=0 falls through to entry 0, and next=1 also matches entry 0's own `next > 0`
        // check before it ever gets excluded), a total, not approximate, bias.
        std::size_t WeightedPickExcluding(
            const std::vector<CNA::Internal::Audio::XsbTrackVariationEntry>& entries,
            std::size_t exclude)
        {
            uint32_t total = 0;
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (i != exclude) total += entries[i].weight;

            if (total == 0)
            {
                // Matches FAudio's own degenerate all-zero-weight behavior: its scan's
                // `next > (max - weight)` is always `0 > 0` (false) when max stays 0 throughout,
                // so the loop falls through without ever assigning a new valuei -- CNA has no
                // equivalent "leave it unchanged" fallback the first time (there is no prior
                // value yet), so this returns the first non-excluded entry as the closest
                // defined equivalent.
                for (std::size_t i = 0; i < entries.size(); ++i)
                    if (i != exclude) return i;
                return 0;
            }

            std::uniform_int_distribution<uint32_t> dist(0, total - 1);
            const uint32_t next = dist(Rng());
            uint32_t remaining = total;
            for (std::size_t i = entries.size(); i > 0; --i)
            {
                const std::size_t idx = i - 1;
                if (idx == exclude) continue;
                if (next >= (remaining - entries[idx].weight)) return idx;
                remaining -= entries[idx].weight;
            }
            return 0;
        }

        // P11-XACT-002: selects one candidate index from a PlayWaveTrackVariation-family event's
        // full entry list, matching FAudio's real per-instance selection exactly -- a genuine
        // two-step process (FACT_internal.c:730-762's one-time initial value, immediately
        // followed by FACT_INTERNAL_GetNextWave's own unconditional re-selection, FACT_internal.c
        // :199-247), not a single pick. CNA only ever resolves a track-variation event once per
        // Cue::Play() (matching the existing "first PlayWave event" simplification -- this does
        // NOT implement true per-loop-iteration re-selection, which would need a much larger
        // per-frame XACT event-scheduling rewrite CNA doesn't have), so this reproduces exactly
        // the first composite value a fresh FAudio sound instance would compute.
        std::size_t SelectTrackVariationIndex(
            const std::vector<CNA::Internal::Audio::XsbTrackVariationEntry>& entries,
            CNA::Internal::Audio::XsbTrackVariationType type)
        {
            using CNA::Internal::Audio::XsbTrackVariationType;

            // Step 1: one-time initial value. Pure Ordered starts at -1 (as uint32_t, wraps to
            // entries.size()-1 here so step 2's +1 lands on exactly 0); every other algorithm
            // does an unconditional weighted pick (exclude = entries.size(), i.e. exclude
            // nothing -- nothing has been selected yet).
            std::size_t valuei = (type == XsbTrackVariationType::Ordered)
                ? (entries.size() - 1)
                : WeightedPickExcluding(entries, entries.size());

            // Step 2: GetNextWave's own selection, unconditionally re-run on top of step 1.
            switch (type)
            {
                case XsbTrackVariationType::Ordered:
                case XsbTrackVariationType::OrderedFromRandom:
                    valuei = (valuei + 1) % entries.size();
                    break;
                case XsbTrackVariationType::Random:
                    valuei = WeightedPickExcluding(entries, entries.size());
                    break;
                case XsbTrackVariationType::RandomNoRepeats:
                case XsbTrackVariationType::Shuffle:
                    valuei = WeightedPickExcluding(entries, valuei);
                    break;
            }
            return valuei;
        }

        // P11-XACT-003: gating bits for a PlayWaveEffectVariation-family event's variationFlags,
        // matching FAudio's own constants exactly (FACT_internal.h). CNA only ever resolves a
        // track's wave once per fresh Cue::Play() (the same "first activation" simplification
        // P11-XACT-002 documents), so only the top-level per-axis gate is needed here -- the
        // separate _ADD/_NEW_ON_LOOP bits (FACT_internal.h) only affect FAudio's behavior on a
        // *later* loop iteration re-triggering the same track event, which CNA has no equivalent
        // per-frame XACT event-scheduling system to ever reach.
        constexpr uint16_t kVariationFlagPitch       = 0x1000;
        constexpr uint16_t kVariationFlagVolume      = 0x2000;
        constexpr uint16_t kVariationFlagFrequencyQ  = 0xC000;

        // Converts XACT centibels to an amplitude ratio (matches FAudio's
        // FACT_INTERNAL_CalculateAmplitudeRatio, FACT_internal.c: pow(10, centibels/2000)) -- the
        // same formula EvaluateRpc() applies to RPC volume curves, needed again here for
        // PlayWaveEffectVariation's per-play randomized volume offset (P11-XACT-003), which
        // arrives in the same centibel units (XactParser.cpp's ReadVolByte).
        float CentibelsToAmplitude(float centibels)
        {
            return static_cast<float>(std::pow(10.0, centibels / 2000.0));
        }

        // P11-XACT-003: one-time "Initial Variation" draw for a PlayWaveEffectVariation-family
        // wave reference, matching FAudio's FACT_INTERNAL_GetNextWave (FACT_internal.c:309-425)
        // exactly for the activeWave.wave == NULL branch -- CNA's per-track single-resolution
        // model (see kVariationFlagPitch's comment above) only ever reaches that branch, so the
        // NEW_ON_LOOP/_ADD combination logic for a later loop iteration is out of scope, not
        // simplified away.
        struct EffectVariationResult
        {
            float pitchCentsDelta        = 0.0f; // added to the sound's own base pitch, in cents
            float volumeAmplitudeMultiplier = 1.0f; // multiplied into the wave's combined amplitude
            bool  hasFilterOverride      = false;
            float filterFrequencyHz      = 0.0f; // desired Hz, only valid if hasFilterOverride
            // Already the final OneOverQ coefficient (this axis's own reciprocal already taken
            // below, matching FAudio's `rngQFactor = 1.0f / (...)` -- NOT a plain Q value the
            // caller still needs to invert). Only valid if hasFilterOverride.
            float filterQFactor          = 0.0f;
        };

        EffectVariationResult ApplyEffectVariation(const CNA::Internal::Audio::XsbWaveRef& waveRef)
        {
            EffectVariationResult out;
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);

            if (waveRef.effectVariationFlags & kVariationFlagPitch)
            {
                // FACT_internal.c:311-314: rngPitch = int16(rng() * (max-min)) + min; the sound's
                // own base pitch is summed in by the caller (Cue::Play() already has
                // basePitchCents_/rpc.pitchCentsBeforeConversion for that), not here.
                const auto rngPitch = static_cast<int16_t>(
                    unit(Rng()) * static_cast<float>(waveRef.effectMaxPitch - waveRef.effectMinPitch));
                out.pitchCentsDelta = static_cast<float>(rngPitch + waveRef.effectMinPitch);
            }

            if (waveRef.effectVariationFlags & kVariationFlagVolume)
            {
                // FACT_internal.c:343-346: rngVolume = rng() * (max-min) + min, in centibels,
                // additively combined with the sound/track's own base volume (also both already
                // in centibel-additive form before FAudio's single final amplitude conversion).
                // waveRef.volume is CNA's own pre-multiplied amplitude equivalent of that same
                // sound+track centibel sum (XactParser.cpp's `wr.volume *= sound.volume`), so
                // multiplying by CentibelsToAmplitude(rngVolume) here reproduces the identical
                // final amplitude as FAudio's additive-then-single-convert approach exactly:
                // 10^((a+b)/2000) == 10^(a/2000) * 10^(b/2000).
                const float rngVolumeCentibels = unit(Rng()) *
                    (waveRef.effectMaxVolume - waveRef.effectMinVolume) + waveRef.effectMinVolume;
                out.volumeAmplitudeMultiplier = CentibelsToAmplitude(rngVolumeCentibels);
            }

            if (waveRef.effectVariationFlags & kVariationFlagFrequencyQ)
            {
                // FACT_internal.c:383-393: two independent draws, Q first then frequency; both a
                // straight replacement of the track's plain authored base filter, not additive
                // (matches the "Initial Filter Variation" branch exactly, no clamp on either
                // axis). rngQFactor's own reciprocal is taken right here, matching FAudio's
                // `1.0f / (...)` exactly -- the result is already the final OneOverQ coefficient,
                // unlike the raw-XACT-byte per-track qfactor's own /3-clamp formula
                // (INTERNAL_calculateFilterOneOverQ), which starts from a byte, not a plain float.
                const float rngQFactor = 1.0f / (unit(Rng()) *
                    (waveRef.effectMaxQFactor - waveRef.effectMinQFactor) + waveRef.effectMinQFactor);
                const float rngFrequencyHz = unit(Rng()) *
                    (waveRef.effectMaxFrequency - waveRef.effectMinFrequency) + waveRef.effectMinFrequency;
                out.hasFilterOverride = true;
                out.filterQFactor     = rngQFactor;
                out.filterFrequencyHz = rngFrequencyHz;
            }

            return out;
        }

        // Evaluates one RPC (Runtime Parameter Control) curve at a given variable value, matching
        // FAudio's FACT_INTERNAL_CalculateRPC (FACT_internal.c): clamps to the first/last point's
        // Y outside the curve's domain, otherwise piecewise-interpolates between the bracketing
        // points using the left point's interpolation type (linear/fast/slow/sin-cos).
        float EvaluateRpcCurve(const CNA::Internal::Audio::XgsRpc& rpc, float var)
        {
            if (rpc.points.empty()) return 0.0f;
            if (var <= rpc.points.front().x) return rpc.points.front().y;
            if (var >= rpc.points.back().x)  return rpc.points.back().y;

            float result = 0.0f;
            for (std::size_t i = 0; i + 1 < rpc.points.size(); ++i)
            {
                result = rpc.points[i].y;
                if (var >= rpc.points[i].x && var <= rpc.points[i + 1].x)
                {
                    const float maxX = rpc.points[i + 1].x - rpc.points[i].x;
                    const float maxY = rpc.points[i + 1].y - rpc.points[i].y;
                    const float t    = (maxX != 0.0f) ? (var - rpc.points[i].x) / maxX : 0.0f;

                    switch (rpc.points[i].type)
                    {
                        case 1: // FAST
                            result += maxY * (1.0f - std::pow(1.0f - std::pow(t, 1.0f / 1.5f), 1.5f));
                            break;
                        case 2: // SLOW
                            result += maxY * (1.0f - std::pow(1.0f - std::pow(t, 1.5f), 1.0f / 1.5f));
                            break;
                        case 3: // SINCOS
                            if (maxY > 0.0f)
                                result += maxY * (1.0f - std::pow(1.0f - std::sqrt(t), 2.0f));
                            else
                                result += maxY * (1.0f - std::sqrt(1.0f - std::pow(t, 2.0f)));
                            break;
                        default: // 0 == LINEAR
                            result += maxY * t;
                            break;
                    }
                    break;
                }
            }
            return result;
        }

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
        state_ = State::Prepared;

        // AUDIO-LIFECYCLE-001 (external audit, 2026-07-16): register with the bank from the
        // moment this Cue exists, not just once Play() happens to run -- P12-BANK-001's own
        // SoundBank::activeCues_ cascade only ever walks cues that are registered, so a cue
        // obtained via SoundBank::GetCue() but never Play()'d was invisible to
        // SoundBank::Dispose()'s force-stop cascade, letting it outlive its bank with a
        // now-dangling bank_ pointer -- exactly what P12-BANK-001 was supposed to prevent.
        // Matches real FACT: a cue's native handle exists (and is destroy()-reachable by its
        // SoundBank) from FACTSoundBank_GetCue, independent of whether FACTCue_Play has run yet.
        if (bank_) bank_->RegisterCue(this);
    }

    Cue::~Cue()
    {
        Dispose();
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
    bool Cue::getIsStoppedProperty()   const { ReconcileState(); return isDisposed_  || state_ == State::Stopped;   }
    bool Cue::getIsStoppingProperty()  const { ReconcileState(); return !isDisposed_ && state_ == State::Stopping;  }
    const std::string& Cue::getNameProperty() const { return name_; }

    Cue::RpcResult Cue::EvaluateRpc() const
    {
        // P9-XACT-006/007/016: matches FAudio's FACT_INTERNAL_UpdateRPCs (FACT_internal.c) --
        // volume curves are authored in centibels (summed across every bound curve, then
        // converted to a single amplitude multiplier once: 10^((a+b)/2000) == 10^(a/2000) *
        // 10^(b/2000)), pitch curves are authored in cents (summed with the sound's own
        // basePitchCents_ before one CentsToPitch()). Originally evaluated only once at Play()
        // time (P9-XACT-006/007's documented narrowing); now also called continuously from
        // ReconcileState() every tick (P9-XACT-016), so this always reflects each bound
        // variable's *current* value, matching FACT's own per-tick recompute instead of a
        // Play()-time snapshot.
        float rpcVolumeCentibels = 0.0f;
        float rpcPitchCents      = 0.0f;
        // P10-FILTER-002/003: -1.0f sentinel ("no RPC curve targets this axis"), matching
        // FAudio's own convention (FACT_internal.c) -- see RpcResult's declaration.
        float rpcFilterFreqHz  = -1.0f;
        float rpcFilterQFactor = -1.0f;

        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng)
        {
            for (uint32_t code : rpcCodes_)
            {
                const CNA::Internal::Audio::XgsRpc* rpc = eng->FindRpcByCode(code);
                if (!rpc) continue;

                const std::string* varName =
                    eng->GetVariableNameByIndex(static_cast<int16_t>(rpc->variable));
                if (!varName || varName->empty()) continue;

                // P10-RPC-003: matches FAudio's FACT_INTERNAL_UpdateRPCs (FACT_internal.c), which
                // substitutes a live-computed value for an RPC curve bound to these two specific
                // variable names -- NOT persisted back into variables_/observable via
                // GetVariable() the way P10-RPC-002's three 3D variables are (FACTCue_GetVariable,
                // FACT.c, reads variableValues[] directly with no such special case; only RPC
                // curve evaluation itself ever sees these live values).
                float value;
                if (*varName == "AttackTime")
                {
                    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - playStart_).count();
                    value = static_cast<float>(elapsedMs);
                }
                else if (*varName == "ReleaseTime")
                {
                    // P10-RPC-004: matches FAudio's FACT_INTERNAL_UpdateRPCs (FACT_internal.c) --
                    // only substitutes a nonzero live value while genuinely inside the RPC-only
                    // release phase (state_ == Stopping AND releaseRpcMS_ > 0, entered from
                    // StopInternal() when Stop() has no authored fadeOutMS but
                    // maxRpcReleaseTime_ > 0, matching FAudio's SOUND_STATE_RELEASE_RPC) -- 0.0f
                    // in every other state, including Playing and an authored-fadeOutMS_
                    // Stopping tail.
                    if (state_ == State::Stopping && releaseRpcMS_ > 0)
                    {
                        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - releaseStart_).count();
                        value = static_cast<float>(elapsedMs);
                    }
                    else
                    {
                        value = 0.0f;
                    }
                }
                else
                {
                    value = GetVariableForRpc(*varName);
                }

                const float result = EvaluateRpcCurve(*rpc, value);

                if (rpc->parameter == 0)      rpcVolumeCentibels += result; // RPC_PARAMETER_VOLUME
                else if (rpc->parameter == 1) rpcPitchCents += result;      // RPC_PARAMETER_PITCH
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
                // REVERBSEND/DSP-preset (>=5): unsupported, see CHECKLIST.md.
            }
        }

        const float volumeMultiplier =
            static_cast<float>(std::pow(10.0, rpcVolumeCentibels / 2000.0));
        const float pitchCentsSum = static_cast<float>(basePitchCents_) + rpcPitchCents;
        const float pitch = CentsToPitch(pitchCentsSum);
        return {volumeMultiplier, pitch, pitchCentsSum, rpcFilterFreqHz, rpcFilterQFactor};
    }

    void Cue::ReconcileState() const
    {
        // P9-STOP-003/004: State::Stopping (an authored/non-immediate Stop() with a real release
        // tail still playing -- see StopInternal()) reconciles to Stopped the same way Playing
        // does, once every active_ instance has actually finished. Deliberately does NOT
        // unregister from waveBanksUsed_/AudioEngine here even in the Stopping case, for the same
        // reason P9-LIFECYCLE-001 never did for the Playing case -- see StopInternal()'s comment.
        if (isDisposed_ || (state_ != State::Playing && state_ != State::Stopping) || active_.empty())
            return;

        // P9-XACT-016: continuous RPC volume/pitch re-evaluation. A cue with no RPC codes bound
        // skips the evaluation entirely and uses the identity multiplier/no pitch reapplication --
        // rpcVolumeMultiplier would always be exactly 1.0f anyway (FAudio's own
        // `if (rpc_codes->count > 0)` guard in FACT_INTERNAL_UpdateRPCs leaves rpcVolume/rpcPitch
        // at zero forever when nothing is bound), so this is a pure optimization keeping a
        // non-RPC cue's steady-state tick at zero extra work, not a behavior difference.
        const bool hasRpc = !rpcCodes_.empty();
        const RpcResult rpc = hasRpc ? EvaluateRpc() : RpcResult{1.0f, 0.0f};

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
        if (state_ == State::Stopping && fadeOutMS_ > 0)
        {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - fadeStart_).count();

            auto* self = const_cast<Cue*>(this);
            if (elapsedMs >= fadeOutMS_)
            {
                self->active_.clear();
                self->state_ = State::Stopped;
                self->paused_ = false;
                self->fadeOutMS_ = 0;
                return;
            }

            const float fadeMultiplier = 1.0f
                - static_cast<float>(elapsedMs) / static_cast<float>(fadeOutMS_);
            const AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
            const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;
            for (const auto& pi : active_)
                if (pi.instance)
                {
                    // P9-XACT-016: rpc.volumeMultiplier folds in here too -- previously this
                    // branch recombined only baseVolume*catVol*fadeMultiplier, silently dropping
                    // whatever RPC volume multiplier had been baked in at Play() time the moment
                    // a fade-out began (a real gap, independent of one-shot-vs-continuous RPC,
                    // that this same touch-point now fixes). rpc.volumeMultiplier is exactly
                    // 1.0f when hasRpc is false, so this is a no-op for the common non-RPC case.
                    pi.instance->setVolumeProperty(std::clamp(
                        pi.baseVolume * pi.effectVolumeMultiplier * catVol * rpc.volumeMultiplier
                            * fadeMultiplier, 0.0f, 1.0f));
                    if (hasRpc)
                    {
                        pi.instance->setPitchProperty(CentsToPitch(
                            rpc.pitchCentsBeforeConversion + pi.effectPitchCentsDelta));
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
        // only be positive if a qualifying RPC curve was found in rpcCodes_ at Play() time.
        if (state_ == State::Stopping && releaseRpcMS_ > 0)
        {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - releaseStart_).count();

            auto* self = const_cast<Cue*>(this);
            if (elapsedMs >= releaseRpcMS_)
            {
                self->active_.clear();
                self->state_ = State::Stopped;
                self->paused_ = false;
                self->releaseRpcMS_ = 0;
                return;
            }

            const AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
            const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;
            for (const auto& pi : active_)
                if (pi.instance)
                {
                    pi.instance->setVolumeProperty(std::clamp(
                        pi.baseVolume * pi.effectVolumeMultiplier * catVol * rpc.volumeMultiplier,
                        0.0f, 1.0f));
                    if (hasRpc)
                    {
                        pi.instance->setPitchProperty(CentsToPitch(
                            rpc.pitchCentsBeforeConversion + pi.effectPitchCentsDelta));
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
                std::chrono::steady_clock::now() - fadeInStart_).count();

            auto* self = const_cast<Cue*>(this);
            const AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
            const float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;

            if (elapsedMs >= fadeInMS_)
            {
                self->fadeInMS_ = 0;
                for (const auto& pi : active_)
                    if (pi.instance)
                    {
                        pi.instance->setVolumeProperty(std::clamp(
                            pi.baseVolume * pi.effectVolumeMultiplier * catVol * rpc.volumeMultiplier,
                            0.0f, 1.0f));
                        if (hasRpc)
                        {
                            pi.instance->setPitchProperty(CentsToPitch(
                                rpc.pitchCentsBeforeConversion + pi.effectPitchCentsDelta));
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
                        pi.instance->setVolumeProperty(std::clamp(
                            pi.baseVolume * pi.effectVolumeMultiplier * catVol * rpc.volumeMultiplier
                                * fadeMultiplier, 0.0f, 1.0f));
                        if (hasRpc)
                        {
                            pi.instance->setPitchProperty(CentsToPitch(
                                rpc.pitchCentsBeforeConversion + pi.effectPitchCentsDelta));
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
                    pi.instance->setVolumeProperty(std::clamp(
                        pi.baseVolume * pi.effectVolumeMultiplier * catVol * rpc.volumeMultiplier,
                        0.0f, 1.0f));
                    pi.instance->setPitchProperty(CentsToPitch(
                        rpc.pitchCentsBeforeConversion + pi.effectPitchCentsDelta));
                    // P10-FILTER-002/003
                    pi.instance->INTERNAL_applyRpcFilterOverride(rpc.filterFrequencyHz, rpc.filterQFactor);
                }
        }

        for (const auto& pi : active_)
            if (pi.instance && pi.instance->getStateProperty() != SoundState::Stopped)
                return; // at least one wave reference is still playing (or looping)

        auto* self = const_cast<Cue*>(this);
        self->active_.clear();
        self->state_ = State::Stopped;
        self->paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
    }

    // ── Variables ─────────────────────────────────────────────────────────────

    float Cue::GetVariable(const std::string& name) const
    {
        // P9-LIFECYCLE-015: matches Play()/Apply3D()'s own disposed guard in this class. Real
        // FACT has no equivalent guard at all -- FACTCue_GetVariableIndex dereferences
        // pCue->parentBank BEFORE its `pCue == NULL` check (FACT.c), so calling this on a
        // disposed FNA Cue (handle == IntPtr.Zero after OnCueDestroyed()) would crash natively
        // rather than throw a catchable exception. That's an unintentional native bug, not a
        // documented contract worth reproducing -- throwing here is strictly safer and matches
        // this class's own established precedent instead.
        if (isDisposed_)
            throw System::ObjectDisposedException("Cue");
        if (name.empty())
            throw System::ArgumentNullException("name");

        auto it = variables_.find(name);
        if (it != variables_.end())
            return it->second;

        if (IsBuiltInCueVariable(name))
            return 0.0f;

        // P12-VAR-001: matches FACTCue_GetVariableIndex exactly (FACT.c) -- only a variable with
        // BOTH the PUBLIC and CUE accessibility bits set is reachable through the cue-level API
        // at all; an engine-GLOBAL (PUBLIC, non-CUE) variable is a genuinely different domain,
        // never reachable via Cue::GetVariable in real XNA/FACT (see GetVariableForRpc for the
        // one place that DOES need to read either domain, internal RPC/variation evaluation).
        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng)
        {
            auto info = eng->GetCueVariableInfo(name);
            if (info.valid)
                return info.initialValue; // never explicitly SetVariable()-d on this cue yet
        }

        throw System::InvalidOperationException("Invalid variable name!");
    }

    void Cue::SetVariable(const std::string& name, float value)
    {
        // P9-LIFECYCLE-015: see GetVariable()'s comment above -- same rationale.
        if (isDisposed_)
            throw System::ObjectDisposedException("Cue");
        if (name.empty())
            throw System::ArgumentNullException("name");

        if (IsBuiltInCueVariable(name))
        {
            variables_[name] = value;
            return;
        }

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

        // Preserves this class's own established precedent (P9-LIFECYCLE-015 etc.) of throwing
        // rather than silently no-op-ing for a name that was never valid at all, matching the
        // FACTVARIABLEINDEX_INVALID case FNA's C# SetVariable() itself does check and throw for.
        throw System::InvalidOperationException("Invalid variable name!");
    }

    float Cue::GetVariableForRpc(const std::string& name) const
    {
        auto it = variables_.find(name);
        if (it != variables_.end())
            return it->second;

        if (IsBuiltInCueVariable(name))
            return 0.0f;

        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng)
        {
            auto cueInfo = eng->GetCueVariableInfo(name);
            if (cueInfo.valid)
                return cueInfo.initialValue;

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
        const Cue3DVariables vars3d = ComputeCue3DVariables(listener, emitter);
        variables_["Distance"]           = vars3d.distance;
        variables_["DopplerPitchScalar"] = vars3d.dopplerFactor;
        variables_["OrientationAngle"]   = vars3d.orientationAngleDegrees;
    }

    // ── Play ──────────────────────────────────────────────────────────────────

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
            return;

        // P10-RPC-003: captured once here, before every path below that ends in
        // state_ = State::Playing (including the "no parsed XSB data"/"no sound resolved" early
        // returns further down) -- matches FAudio's own per-cue start timestamp
        // (FACT_internal.c's cue->start, set in FACTCue_Play).
        playStart_ = std::chrono::steady_clock::now();

        // Resolve waves to play
        using namespace CNA::Internal::Audio;

        const XsbData* xsb = nullptr;
        AudioEngine*   eng = bank_ ? bank_->engine_ : nullptr;

        if (bank_)
            xsb = bank_->GetXsbData();

        if (!xsb || cueIndex_ >= xsb->cues.size())
        {
            // No parsed data — update state only
            state_ = State::Playing;
            if (eng) eng->RegisterCue(this);
            // AUDIO-LIFECYCLE-001: no bank_->RegisterCue(this) here anymore -- the constructor
            // already registered this cue with its bank for its whole lifetime, not just while
            // playing (see Cue::Cue()'s comment).
            return;
        }

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
                // pattern (`plans/plan_audio.md`, `P11-XACT-004`).
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

        if (!sound)
        {
            state_ = State::Playing;
            if (eng) eng->RegisterCue(this);
            // AUDIO-LIFECYCLE-001: see the other early-return exit point's identical comment above.
            return;
        }

        categoryIdx_    = sound->categoryIndex;
        priority_       = sound->priority;
        rpcCodes_       = sound->rpcCodes;    // P9-XACT-016: retained for continuous re-eval
        basePitchCents_ = sound->pitchCents;  // P9-XACT-016
        float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;

        // P10-RPC-004: matches FAudio's FACT_internal.c:790-815 -- scans this sound's RPC codes
        // (whole-sound level, not per-track, the same simplification already accepted for the
        // one-shot/continuous RPC evaluation itself) for one bound to a variable literally named
        // "ReleaseTime" that targets RPC_PARAMETER_VOLUME, and captures the max curve-point x
        // value as this cue's RPC-only release duration. StopInternal() consults this when the
        // cue has no authored fadeOutMS.
        maxRpcReleaseTime_ = 0;
        if (eng)
        {
            for (uint32_t code : rpcCodes_)
            {
                const CNA::Internal::Audio::XgsRpc* candidate = eng->FindRpcByCode(code);
                if (!candidate || candidate->parameter != 0 || candidate->points.empty()) continue;

                const std::string* varName = eng->GetVariableNameByIndex(
                    static_cast<int16_t>(candidate->variable));
                if (!varName || *varName != "ReleaseTime") continue;

                const auto lastX = static_cast<uint32_t>(candidate->points.back().x);
                if (lastX > maxRpcReleaseTime_) maxRpcReleaseTime_ = lastX;
            }
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
                state_ = State::Stopped;
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
                state_ = State::Stopped;
                return;
            }
            if (decision.triggered)
                categoryFadeInMS = decision.fadeInMS; // unconditional overwrite, even if 0
        }

        // P9-XACT-006/007/016: RPC (Runtime Parameter Control) volume/pitch. Originally evaluated
        // only once, here at Play() time (P9-XACT-006/007's documented narrowing); now also
        // re-evaluated continuously from ReconcileState() every tick (P9-XACT-016), so this
        // initial call is just the first evaluation, not the only one.
        const RpcResult rpc = EvaluateRpc();

        // Spawn one SoundEffectInstance per wave reference
        for (const auto& waveRef : sound->waves)
        {
            if (!eng) continue;

            // P11-XACT-002: a PlayWaveTrackVariation-family event's real candidate wave, chosen
            // via FAudio's own selection algorithm, instead of waveRef.wavebankIndex/waveIndex's
            // always-entry-0 parse-time fallback. Empty entries means a plain PlayWave/
            // PlayWaveEffectVariation event -- use waveRef's own fields directly, unchanged.
            uint8_t  effectiveWavebankIndex = waveRef.wavebankIndex;
            uint16_t effectiveWaveIndex     = waveRef.waveIndex;
            if (!waveRef.trackVariationEntries.empty())
            {
                const std::size_t picked =
                    SelectTrackVariationIndex(waveRef.trackVariationEntries, waveRef.trackVariationType);
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
                std::cerr << "[Cue] \"" << getNameProperty() << "\" could not find wave bank \""
                          << wbName << "\" for wave " << effectiveWaveIndex << " -- skipping\n";
                continue;
            }

            const SoundEffect* sf = wb->GetSoundEffect(effectiveWaveIndex);
            if (!sf)
            {
                std::cerr << "[Cue] \"" << getNameProperty() << "\" wave " << effectiveWaveIndex
                          << " in bank \"" << wbName << "\" failed to load -- skipping\n";
                continue;
            }

            // P11-XACT-003: PlayWaveEffectVariation-family per-play pitch/volume/filter
            // randomization, drawn once per fresh Play() (see ApplyEffectVariation's own comment
            // for why that's the only branch of FAudio's real algorithm CNA's per-track
            // single-resolution model can reach). A no-op (all-default EffectVariationResult) for
            // every plain PlayWave/PlayWaveTrackVariation wave reference.
            const EffectVariationResult effect = ApplyEffectVariation(waveRef);

            auto inst = std::make_unique<SoundEffectInstance>(sf->CreateInstance());
            float combinedVol = std::clamp(
                waveRef.volume * effect.volumeAmplitudeMultiplier * catVol * rpc.volumeMultiplier,
                0.0f, 1.0f);
            // AUDIO-ORDER-001 (external audit, 2026-07-16): if this Play() is starting a category
            // fade-in, begin this instance already silent instead of setVolumeProperty(combinedVol)
            // followed by a separate trailing loop (after every instance in this cue is already
            // playing) forcing it back down to 0 -- see the fadeInMS_/fadeInStart_ setup below,
            // which ReconcileState() then ramps up to combinedVol over categoryFadeInMS. Setting
            // the correct starting volume here, before inst->Play(), means this track's very first
            // output frame is already at the right volume; SoundEffectInstance::Play() applies
            // Volume/Pitch/Pan/spatial state as its own first real track-property write, before
            // MIX_PlayTrack actually starts the track (SoundEffectInstance.cpp's Play()), matching
            // FNA's own "configure the voice fully, then start it" ordering (SoundEffectInstance.cs)
            // instead of a brief window where the track could audibly start at full volume first.
            inst->setVolumeProperty(categoryFadeInMS > 0 ? 0.0f : combinedVol);
            inst->setPitchProperty(
                CentsToPitch(rpc.pitchCentsBeforeConversion + effect.pitchCentsDelta));
            inst->setIsLoopedProperty(waveRef.loopCount > 0);

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

            inst->Play();

            active_.push_back({std::move(inst), waveRef.volume,
                effect.volumeAmplitudeMultiplier, effect.pitchCentsDelta});

            if (std::find(waveBanksUsed_.begin(), waveBanksUsed_.end(), wb) == waveBanksUsed_.end())
            {
                wb->RegisterCue(this);
                waveBanksUsed_.push_back(wb);
            }
        }

        state_ = State::Playing;
        if (eng) eng->RegisterCue(this);
        // AUDIO-LIFECYCLE-001: no bank_->RegisterCue(this) here anymore -- this cue has already
        // been registered with its bank since construction (see Cue::Cue()'s comment), which lets
        // SoundBank::Dispose() force-stop it whether it's still Prepared, currently Playing, or
        // anywhere in between, whether it's a fire-and-forget cue or one the caller obtained via
        // GetCue() and is holding independently (see SoundBank::activeCues_'s comment).

        // P9-CATEGORY-007: start the fade-in ramp -- ReconcileState() (ticked by
        // AudioEngine::Update() and every state getter) brings each instance up to its own
        // combinedVol over categoryFadeInMS from here, same as the fade-out ramp starts at full
        // volume in StopInternal()/ForceFadeOutForInstanceLimit(). AUDIO-ORDER-001: the per-instance
        // silent starting volume itself is now set inline in the loop above (before each
        // inst->Play()), not in a separate pass here after every instance already started playing.
        if (categoryFadeInMS > 0)
        {
            fadeInMS_ = categoryFadeInMS;
            fadeInStart_ = std::chrono::steady_clock::now();
        }
    }

    // ── Pause / Resume / Stop ─────────────────────────────────────────────────

    void Cue::Pause()
    {
        if (isDisposed_) return;
        ReconcileState(); // a naturally-finished cue must not be resurrected into Paused
        if (state_ != State::Playing || paused_) return; // P9-LIFECYCLE-013: idempotent, like FACTCue_Pause
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Pause();
        paused_ = true;
    }

    void Cue::Resume()
    {
        if (isDisposed_) return;
        if (state_ != State::Playing || !paused_) return;
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Resume();
        paused_ = false;
    }

    void Cue::Stop(AudioStopOptions options)
    {
        StopInternal(options == AudioStopOptions::Immediate);
    }

    void Cue::StopInternal(bool immediate)
    {
        if (isDisposed_) return;
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
        if (!immediate && !active_.empty() && bank_)
        {
            if (const CNA::Internal::Audio::XsbData* xsb = bank_->GetXsbData())
                if (cueIndex_ < xsb->cues.size())
                    fadeOutMS = xsb->cues[cueIndex_].fadeOutMS;
        }

        const bool hasRealTail = !immediate && !active_.empty() && fadeOutMS > 0;
        if (hasRealTail)
        {
            state_ = State::Stopping;
            paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
            fadeStart_ = std::chrono::steady_clock::now();
            fadeOutMS_ = fadeOutMS;
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
        const bool hasReleaseRpcTail = !immediate && !active_.empty() && maxRpcReleaseTime_ > 0;
        if (hasReleaseRpcTail)
        {
            state_ = State::Stopping;
            paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
            releaseStart_ = std::chrono::steady_clock::now();
            releaseRpcMS_ = maxRpcReleaseTime_;
            return;
        }

        active_.clear();
        state_ = State::Stopped;
        paused_ = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
        fadeOutMS_ = 0;
        releaseRpcMS_ = 0; // P10-RPC-004: abort any in-progress RPC-only release phase too

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
        // Mirrors StopInternal()'s own "hasRealTail" split: a real, nonzero, authored fadeOutMS
        // gets a real Stopping tail (P9-STOP-010's ramp, just category-triggered instead of
        // Stop()-triggered); a zero fadeOutMS has nothing to fade, so hard-stop immediately,
        // matching FACT_INTERNAL_BeginFadeOut's effectively-instant behavior for fadeOutMS == 0.
        if (isDisposed_ || state_ != State::Playing) return;

        if (fadeOutMS == 0)
        {
            StopInternal(true);
            return;
        }

        state_     = State::Stopping;
        paused_    = false; // P9-LIFECYCLE-013: irrelevant once state_ != Playing; reset for hygiene
        fadeStart_ = std::chrono::steady_clock::now();
        fadeOutMS_ = fadeOutMS;
        fadeInMS_  = 0; // a cue being evicted can't also still be fading in
    }

    void Cue::ApplyCategoryVolume(float catVol)
    {
        for (auto& pi : active_)
            if (pi.instance)
                pi.instance->setVolumeProperty(std::clamp(
                    pi.baseVolume * pi.effectVolumeMultiplier * catVol, 0.0f, 1.0f));
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
        volumeAmplitudeMultiplier = result.volumeAmplitudeMultiplier;
        hasFilterOverride        = result.hasFilterOverride;
        filterFrequencyHz        = result.filterFrequencyHz;
        filterQFactor            = result.filterQFactor;
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void Cue::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            StopInternal(true);
            // AUDIO-LIFECYCLE-001: pairs with the RegisterCue(this) call in the constructor --
            // this cue now stays registered with its bank for its entire lifetime (Prepared,
            // Playing, or Stopped), so unregistering only happens here, at genuine disposal, not
            // earlier when it merely stops playing (see StopInternal()'s comment).
            if (bank_) bank_->UnregisterCue(this);
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(Cue, "Microsoft.Xna.Framework.Audio.Cue")
}
