// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    namespace
    {
        // Force-sweep a still-playing fire-and-forget cue after this long, purely as a safety
        // net against unbounded growth (see FireAndForget's comment in SoundBank.hpp) -- far
        // longer than any normal one-shot SFX, so it never affects ordinary playback.
        constexpr std::chrono::minutes kFireAndForgetSafetyNet{5};
    }

    // ── Internal impl ─────────────────────────────────────────────────────────

    struct SoundBank::XactSoundBankImpl
    {
        CNA::Internal::Audio::XsbData data;

        explicit XactSoundBankImpl(CNA::Internal::Audio::XsbData d)
            : data(std::move(d))
        {}
    };

    // ── Constructors ──────────────────────────────────────────────────────────

    SoundBank::SoundBank(AudioEngine* audioEngine, const std::string& filename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
            throw System::ArgumentNullException("audioEngine");
        if (filename.empty())
            throw System::ArgumentNullException("filename");

        // FNA's SoundBank ctor reads filename via TitleContainer.ReadToPointer, which throws
        // FileNotFoundException on a missing file before ever reaching FACT (SoundBank.cs) --
        // match that here (P9-HARDWARE-003). Corrupt-but-existing content stays a silent stub
        // below: FNA never checks FACTAudioEngine_CreateSoundBank's return code either.
        const std::string resolvedFilename = CNA::Internal::ResolveExistingXnaPath(filename);
        std::ifstream f(resolvedFilename, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + filename + "'.", filename);
        }
        auto sz = f.tellg(); f.seekg(0);
        std::vector<uint8_t> raw(static_cast<std::size_t>(sz));
        f.read(reinterpret_cast<char*>(raw.data()), sz);

        try
        {
            auto xsb = CNA::Internal::Audio::ParseXsb(raw);
            std::cerr << "[SoundBank] Loaded XSB: " << filename
                      << " cues=" << xsb.cues.size()
                      << " sounds=" << xsb.sounds.size() << "\n";
            xactImpl_ = std::make_unique<XactSoundBankImpl>(std::move(xsb));
            engine_->RegisterSoundBank(this); // XA-8: lets AudioEngine::Dispose() cascade here
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[SoundBank] XSB parse error (" << filename << "): " << ex.what() << "\n";
        }
    }

    SoundBank::~SoundBank()
    {
        Dispose();
    }

    // ── Properties ────────────────────────────────────────────────────────────

    bool SoundBank::getIsDisposedProperty() const { return isDisposed_; }

    bool SoundBank::getIsInUseProperty() const
    {
        // XA-7: a paused cue is still in use -- FACT_STATE_INUSE (which FNA's IsInUse reflects)
        // stays set while paused, it only clears once the cue is genuinely stopped. Checking
        // IsPlaying alone made a paused cue look unused. activeCues_ (P12-BANK-001) covers both
        // fire-and-forget and caller-owned GetCue() cues, matching WaveBank's identical check.
        for (const auto* cue : activeCues_)
            if (cue && (cue->getIsPlayingProperty() || cue->getIsPausedProperty()))
                return true;
        return false;
    }

    const CNA::Internal::Audio::XsbData* SoundBank::GetXsbData() const
    {
        return xactImpl_ ? &xactImpl_->data : nullptr;
    }

    // ── GetCue ────────────────────────────────────────────────────────────────

    Cue* SoundBank::GetCue(const std::string& name)
    {
        if (name.empty())
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("SoundBank");

        if (xactImpl_)
        {
            auto it = xactImpl_->data.cueNameMap.find(name);
            if (it != xactImpl_->data.cueNameMap.end())
                return new Cue(name, this, it->second);
        }

        throw System::InvalidOperationException("Invalid cue name!");
    }

    // ── PlayCue ───────────────────────────────────────────────────────────────

    void SoundBank::PlayCue(const std::string& name)
    {
        PlayCueInternal(name, nullptr, nullptr);
    }

    void SoundBank::PlayCue(const std::string& name,
                             const AudioListener& listener,
                             const AudioEmitter& emitter)
    {
        PlayCueInternal(name, &listener, &emitter);
    }

    void SoundBank::SweepFireAndForget()
    {
        // Sweep fire-and-forget cues that have finished playing (destroying a Cue whose sound
        // has already stopped is effectively a no-op) plus any still-playing entry past the
        // safety-net timeout -- NOT simply "older than N seconds", which would cut off any
        // one-shot or music cue longer than that regardless of whether it was still playing.
        auto now = std::chrono::steady_clock::now();
        fireAndForget_.erase(
            std::remove_if(
                fireAndForget_.begin(), fireAndForget_.end(),
                [&now](const FireAndForget& faf)
                {
                    // XA-7: a paused cue is still alive too -- only a genuinely stopped cue
                    // (neither playing nor paused) should be swept unconditionally. Without
                    // this, pausing a fire-and-forget cue's category made the very next
                    // PlayCue() on this bank silently destroy it.
                    if (faf.cue && (faf.cue->getIsPlayingProperty() || faf.cue->getIsPausedProperty()))
                    {
                        return now - faf.created >= kFireAndForgetSafetyNet;
                    }
                    return true;
                }),
            fireAndForget_.end());
    }

    void SoundBank::RegisterCue(Cue* cue)
    {
        if (!cue) return;
        activeCues_.push_back(cue);
    }

    void SoundBank::UnregisterCue(Cue* cue)
    {
        activeCues_.erase(std::remove(activeCues_.begin(), activeCues_.end(), cue), activeCues_.end());
    }

    void SoundBank::PlayCueInternal(const std::string& name,
                                     const AudioListener* listener,
                                     const AudioEmitter* emitter)
    {
        if (name.empty())
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("SoundBank");

        SweepFireAndForget();

        std::unique_ptr<Cue> cue(GetCue(name));
        // AUDIO-ORDER-001 (external audit, 2026-07-16): Apply3D() before Play(), not after -- FNA
        // computes the 3D dsp settings before FACTSoundBank_Play3D so the cue is positioned from
        // its very first output frame; calling Apply3D() after Play() only matched that in
        // spirit (synchronously, before the next real audio callback), not literally. Now that
        // Cue::Apply3D() persists its result (has3D_/pending3DListener_/pending3DEmitter_) even
        // with no active instances yet, and Cue::Play()'s per-wave loop seeds every new instance
        // from it before that instance's own Play() call, calling this first means every instance
        // this Play() creates starts already positioned, matching FNA's ordering exactly instead
        // of approximately.
        if (listener && emitter)
            cue->Apply3D(*listener, *emitter);
        cue->Play();
        // Keep the Cue alive so its SoundEffectInstances (and their SDL3_mixer
        // tracks) are not destroyed before the sound has had a chance to play.
        fireAndForget_.push_back({std::move(cue), std::chrono::steady_clock::now()});
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void SoundBank::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            if (engine_) engine_->UnregisterSoundBank(this); // XA-8
            fireAndForget_.clear(); // stops any still-playing fire-and-forget cues; each one's
                                     // destructor also unregisters itself from activeCues_.

            // P12-BANK-001: force-stop every cue still associated with this bank, including ones
            // the caller obtained via GetCue() and is still holding -- matches
            // FACTSoundBank_Destroy (FACT.c:1311-1327). Snapshot first: Cue::Dispose() below
            // calls back into UnregisterCue(), which would otherwise invalidate a live range-for
            // over activeCues_ mid-iteration (same hazard as AudioEngine::StopCategoryInternal).
            std::vector<Cue*> cues = activeCues_;
            for (auto* cue : cues)
                if (cue) cue->Dispose(); // idempotent; safe even if the caller already disposed it
            activeCues_.clear();

            xactImpl_.reset();
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(SoundBank, "Microsoft.Xna.Framework.Audio.SoundBank")
}
