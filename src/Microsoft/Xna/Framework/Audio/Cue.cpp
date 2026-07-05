// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
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
            return name == "Distance" || name == "DopplerPitchScalar" || name == "OrientationAngle";
        }
    }

    // ── Constructor / destructor ──────────────────────────────────────────────

    Cue::Cue(std::string name, SoundBank* bank, uint16_t cueIndex)
        : name_(std::move(name)), bank_(bank), cueIndex_(cueIndex)
    {
        state_ = State::Prepared;
    }

    Cue::~Cue()
    {
        Dispose();
    }

    // ── State properties ──────────────────────────────────────────────────────

    bool Cue::getIsCreatedProperty()   const { return !isDisposed_ && state_ == State::Created;   }
    bool Cue::getIsDisposedProperty()  const { return isDisposed_; }
    bool Cue::getIsPausedProperty()    const { ReconcileState(); return !isDisposed_ && state_ == State::Paused;    }
    bool Cue::getIsPlayingProperty()   const { ReconcileState(); return !isDisposed_ && state_ == State::Playing;   }
    bool Cue::getIsPreparedProperty()  const { return !isDisposed_ && state_ == State::Prepared;  }
    bool Cue::getIsPreparingProperty() const { return !isDisposed_ && state_ == State::Preparing; }
    bool Cue::getIsStoppedProperty()   const { ReconcileState(); return isDisposed_  || state_ == State::Stopped;   }
    bool Cue::getIsStoppingProperty()  const { ReconcileState(); return !isDisposed_ && state_ == State::Stopping;  }
    const std::string& Cue::getNameProperty() const { return name_; }

    void Cue::ReconcileState() const
    {
        // P9-STOP-003/004: State::Stopping (an authored/non-immediate Stop() with a real release
        // tail still playing -- see StopInternal()) reconciles to Stopped the same way Playing
        // does, once every active_ instance has actually finished. Deliberately does NOT
        // unregister from waveBanksUsed_/AudioEngine here even in the Stopping case, for the same
        // reason P9-LIFECYCLE-001 never did for the Playing case -- see StopInternal()'s comment.
        if (isDisposed_ || (state_ != State::Playing && state_ != State::Stopping) || active_.empty())
            return;

        for (const auto& pi : active_)
            if (pi.instance && pi.instance->getStateProperty() != SoundState::Stopped)
                return; // at least one wave reference is still playing (or looping)

        auto* self = const_cast<Cue*>(this);
        self->active_.clear();
        self->state_ = State::Stopped;
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

        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        if (eng && eng->IsValidVariableName(name))
            return eng->GetGlobalVariable(name);

        throw System::InvalidOperationException("Invalid variable name!");
    }

    void Cue::SetVariable(const std::string& name, float value)
    {
        // P9-LIFECYCLE-015: see GetVariable()'s comment above -- same rationale.
        if (isDisposed_)
            throw System::ObjectDisposedException("Cue");
        if (name.empty())
            throw System::ArgumentNullException("name");

        AudioEngine* eng = bank_ ? bank_->engine_ : nullptr;
        bool valid = variables_.find(name) != variables_.end()
                  || IsBuiltInCueVariable(name)
                  || (eng && eng->IsValidVariableName(name));
        if (!valid)
            throw System::InvalidOperationException("Invalid variable name!");

        variables_[name] = value;
    }

    // ── 3D ───────────────────────────────────────────────────────────────────

    void Cue::Apply3D(const AudioListener& listener, const AudioEmitter& emitter)
    {
        if (isDisposed_) throw System::ObjectDisposedException("Cue");

        // SDL3_mixer has no per-cue 3D audio graph (FAudio's FACT3DApply); approximate by
        // applying the same pan/distance-attenuation SoundEffectInstance::Apply3D already does
        // (CP-3) to every wave reference currently playing under this cue. Doppler stays
        // unapplied, matching the accepted deviation documented in CHECKLIST.md.
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Apply3D(listener, emitter);
    }

    // ── Play ──────────────────────────────────────────────────────────────────

    void Cue::Play()
    {
        if (isDisposed_) throw System::ObjectDisposedException("Cue");

        // P9-LIFECYCLE-010/011: FACTCue_Play (FACT.c) silently rejects a cue whose state already
        // has PLAYING, STOPPING, or STOPPED set -- FNA's Cue.Play() discards the return value, so
        // from the C# caller's perspective this is just a no-op, not an exception. A Cue models
        // exactly one playthrough, not a restartable voice. PAUSED is included here too: real
        // FACT keeps the PLAYING bit set while paused (pausing never clears it), so a paused cue
        // is rejected as well. Reconciling first ensures a cue that finished naturally (but whose
        // state_ is still the stale Playing value from before the next query) is correctly
        // treated as already-stopped rather than being resurrected by a stray Play() call.
        ReconcileState();
        if (state_ == State::Playing || state_ == State::Paused ||
            state_ == State::Stopping || state_ == State::Stopped)
            return;

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
                // linear scan. GetVariable() already implements the right cue-local-then-global
                // fallback FACT uses to resolve a variable's current value, so it's reused here
                // instead of duplicating that logic (P9-XACT-003).
                const std::string* varName = eng ? eng->GetVariableNameByIndex(var.variable) : nullptr;
                if (varName && !varName->empty())
                {
                    const float value = GetVariable(*varName);
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
                        if (value > (remaining - weight))
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
            return;
        }

        categoryIdx_ = sound->categoryIndex;
        float catVol = eng ? eng->GetCategoryVolume(categoryIdx_) : 1.0f;

        // P9-XACT-006/007: RPC (Runtime Parameter Control) volume/pitch, evaluated once here
        // against each bound variable's *current* value -- not continuously re-evaluated while
        // playing like real FACT's per-tick FACT_INTERNAL_UpdateRPCs (see CHECKLIST.md). Volume
        // curves are authored in centibels (same unit as a sound's own base volume before
        // amplitude conversion), summed across every bound curve, then converted to a multiplier
        // once -- mathematically identical to FAudio summing centibels and converting once,
        // since 10^((a+b)/2000) == 10^(a/2000) * 10^(b/2000). Pitch curves are authored directly
        // in cents, so they're summed with the sound's own pitchCents before one CentsToPitch().
        float rpcVolumeCentibels = 0.0f;
        float rpcPitchCents      = 0.0f;
        if (eng)
        {
            for (uint32_t code : sound->rpcCodes)
            {
                const XgsRpc* rpc = eng->FindRpcByCode(code);
                if (!rpc) continue;

                const std::string* varName =
                    eng->GetVariableNameByIndex(static_cast<int16_t>(rpc->variable));
                if (!varName || varName->empty()) continue;

                const float value  = GetVariable(*varName);
                const float result = EvaluateRpcCurve(*rpc, value);

                if (rpc->parameter == 0)      rpcVolumeCentibels += result; // RPC_PARAMETER_VOLUME
                else if (rpc->parameter == 1) rpcPitchCents += result;      // RPC_PARAMETER_PITCH
                // REVERBSEND/FILTERFREQUENCY/FILTERQFACTOR/DSP-preset (>=5): unsupported, see
                // CHECKLIST.md.
            }
        }
        const float rpcVolumeMultiplier =
            static_cast<float>(std::pow(10.0, rpcVolumeCentibels / 2000.0));
        const float pitch = CentsToPitch(static_cast<float>(sound->pitchCents) + rpcPitchCents);

        // Spawn one SoundEffectInstance per wave reference
        for (const auto& waveRef : sound->waves)
        {
            if (!eng) continue;

            const std::string& wbName = (waveRef.wavebankIndex < xsb->wavebankNames.size())
                ? xsb->wavebankNames[waveRef.wavebankIndex] : "";

            WaveBank* wb = wbName.empty() ? nullptr : eng->FindWaveBank(wbName);
            if (!wb) continue;

            const SoundEffect* sf = wb->GetSoundEffect(waveRef.waveIndex);
            if (!sf) continue;

            auto inst = std::make_unique<SoundEffectInstance>(sf->CreateInstance());
            float combinedVol = std::clamp(waveRef.volume * catVol * rpcVolumeMultiplier, 0.0f, 1.0f);
            inst->setVolumeProperty(combinedVol);
            inst->setPitchProperty(pitch);
            inst->setIsLoopedProperty(waveRef.loopCount > 0);
            inst->Play();

            // P9-XACT-011: wire the track's real parsed XACT filter (if any) into the real
            // SDL3_mixer filter callback. One-shot at Play() time, not continuously
            // re-evaluated -- same narrowing as the RPC volume/pitch wiring above (CHECKLIST.md).
            if (waveRef.filterType != 0xFF)
            {
                inst->INTERNAL_applyXactTrackFilter(
                    waveRef.filterType,
                    static_cast<float>(waveRef.filterFrequencyHz),
                    waveRef.filterQFactorRaw);
            }

            active_.push_back({std::move(inst), waveRef.volume});

            if (std::find(waveBanksUsed_.begin(), waveBanksUsed_.end(), wb) == waveBanksUsed_.end())
            {
                wb->RegisterCue(this);
                waveBanksUsed_.push_back(wb);
            }
        }

        state_ = State::Playing;
        if (eng) eng->RegisterCue(this);
    }

    // ── Pause / Resume / Stop ─────────────────────────────────────────────────

    void Cue::Pause()
    {
        if (isDisposed_) return;
        ReconcileState(); // a naturally-finished cue must not be resurrected into Paused
        if (state_ != State::Playing) return;
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Pause();
        state_ = State::Paused;
    }

    void Cue::Resume()
    {
        if (isDisposed_) return;
        if (state_ != State::Paused) return;
        for (auto& pi : active_)
            if (pi.instance) pi.instance->Resume();
        state_ = State::Playing;
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
        // P9-STOP-001/002/003/004: real FACT (FACTCue_Stop, FACT.c) does NOT transition to
        // FACT_STATE_STOPPED for a non-immediate stop unless there is nothing to release --
        // "the three ways a Cue might be stopped immediately" are: an explicit immediate request,
        // being already paused, or `playingSound == NULL` (no wave, nothing to tail off). With a
        // real tail, FACT instead begins a fade-out/RPC-release and only reaches STOPPED once
        // that finishes. CNA doesn't parse/model authored fadeOutMS/RPC-release timing (that data
        // isn't even retained by XactParser -- see plan_audio.md's P9-STOP-010 for the documented
        // gap), but it DOES know whether there's a real tail to wait for (`active_` non-empty) --
        // model that with the already-defined but previously-unused State::Stopping, promoted to
        // Stopped by ReconcileState() once every active_ instance actually finishes playing.
        const bool hasRealTail = !immediate && !active_.empty();
        if (hasRealTail)
        {
            state_ = State::Stopping;
            // Deliberately do NOT touch waveBanksUsed_/AudioEngine's registry here (P9-STOP-005/
            // 009): the old code unregistered immediately regardless of `immediate`, which made
            // WaveBank::IsInUse/AudioEngine's category operations lose track of this cue while it
            // was still audibly playing its tail. Unregistration now only happens once the cue is
            // actually immediate-stopped or disposed -- ReconcileState() itself never touches
            // these registries either (same mutate-during-iteration hazard as P9-LIFECYCLE-001).
            return;
        }

        active_.clear();
        state_ = State::Stopped;

        for (auto* wb : waveBanksUsed_)
            if (wb) wb->UnregisterCue(this);
        waveBanksUsed_.clear();

        if (bank_ && bank_->engine_)
            bank_->engine_->UnregisterCue(this);
    }

    void Cue::ApplyCategoryVolume(float catVol)
    {
        for (auto& pi : active_)
            if (pi.instance) pi.instance->setVolumeProperty(std::clamp(pi.baseVolume * catVol, 0.0f, 1.0f));
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void Cue::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            StopInternal(true);
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(Cue, "Microsoft.Xna.Framework.Audio.Cue")
}
