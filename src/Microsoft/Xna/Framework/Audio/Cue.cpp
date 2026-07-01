// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
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
        if (name.empty()) throw std::invalid_argument("name must not be empty");
        auto it = variables_.find(name);
        if (it == variables_.end()) throw std::runtime_error("Invalid variable: " + name);
        return it->second;
    }

    void Cue::SetVariable(const std::string& name, float value)
    {
        if (name.empty()) throw std::invalid_argument("name must not be empty");
        variables_[name] = value;
    }

    // ── 3D ───────────────────────────────────────────────────────────────────

    void Cue::Apply3D(const AudioListener& /*listener*/, const AudioEmitter& /*emitter*/)
    {
        if (isDisposed_) throw std::runtime_error("Cue is disposed");
        // SDL3_mixer has no per-cue 3D audio; would need Apply3D on each instance.
    }

    // ── Play ──────────────────────────────────────────────────────────────────

    void Cue::Play()
    {
        if (isDisposed_) throw std::runtime_error("Cue is disposed");

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
                uint16_t pick = 0;

                if (var.type == 0 || var.type == 4) // wave variation — pick randomly
                {
                    std::uniform_int_distribution<uint16_t> dist(
                        0, static_cast<uint16_t>(var.entries.size() - 1));
                    pick = dist(Rng());
                }
                else if (var.type == 1) // sound variation
                {
                    std::uniform_int_distribution<uint16_t> dist(
                        0, static_cast<uint16_t>(var.entries.size() - 1));
                    pick = dist(Rng());
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

            active_.push_back({std::move(inst)});

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
        active_.clear();
        state_ = State::Stopped;

        for (auto* wb : waveBanksUsed_)
            if (wb) wb->UnregisterCue(this);
        waveBanksUsed_.clear();

        if (bank_ && bank_->engine_)
            bank_->engine_->UnregisterCue(this);
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

    GetTypeNameCPP(Cue, "Microsoft::Xna::Framework::Audio::Cue")
}
