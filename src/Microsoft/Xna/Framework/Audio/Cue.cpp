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
        // Convert XACT pitch (cents, range -1200..+1200) to XNA [-1..+1]
        float CentsToPitch(int16_t cents)
        {
            return std::clamp(static_cast<float>(cents) / 1200.0f, -1.0f, 1.0f);
        }

        std::mt19937& Rng()
        {
            static std::mt19937 rng{std::random_device{}()};
            return rng;
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
    bool Cue::getIsPausedProperty()    const { return !isDisposed_ && state_ == State::Paused;    }
    bool Cue::getIsPlayingProperty()   const { return !isDisposed_ && state_ == State::Playing;   }
    bool Cue::getIsPreparedProperty()  const { return !isDisposed_ && state_ == State::Prepared;  }
    bool Cue::getIsPreparingProperty() const { return !isDisposed_ && state_ == State::Preparing; }
    bool Cue::getIsStoppedProperty()   const { return isDisposed_  || state_ == State::Stopped;   }
    bool Cue::getIsStoppingProperty()  const { return !isDisposed_ && state_ == State::Stopping;  }
    const std::string& Cue::getNameProperty() const { return name_; }

    // ── Variables ─────────────────────────────────────────────────────────────

    float Cue::GetVariable(const std::string& name) const
    {
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

            if (!var.entries.empty())
            {
                // FACT selects a variation entry via a weighted lottery over each entry's
                // [weightMin, weightMax) range (FAudio's get_active_variation_index): entries
                // authored with a wider weight range are proportionally more likely to be
                // picked, not merely one-of-N uniformly. This applies to every non-interactive
                // table type (wave/sound/compact_wave) -- FAudio itself uses the identical
                // algorithm for all of them. Interactive tables (type==3) instead select by a
                // per-cue/global variable's value range, but the parser does not yet retain
                // that range in XsbVariEntry, so those (and any other degenerate all-zero-
                // weight table) fall back to a uniform pick (documented deviation, see
                // CHECKLIST.md).
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
        float pitch  = CentsToPitch(sound->pitchCents);

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
            float combinedVol = std::clamp(waveRef.volume * catVol, 0.0f, 1.0f);
            inst->setVolumeProperty(combinedVol);
            inst->setPitchProperty(pitch);
            inst->setIsLoopedProperty(waveRef.loopCount > 0);
            inst->Play();

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
        if (immediate)
        {
            active_.clear();
        }

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
