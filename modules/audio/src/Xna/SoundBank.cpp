// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Internal/AudioLog.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/InstancePlayLimitException.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "TitleContentBytes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    // ── Internal impl ─────────────────────────────────────────────────────────

    struct SoundBank::XactSoundBankImpl
    {
        CNA::Internal::Audio::XsbData data;
        std::vector<std::size_t> publishedInstanceCounts;

        explicit XactSoundBankImpl(CNA::Internal::Audio::XsbData d)
            : data(std::move(d))
            , publishedInstanceCounts(data.cues.size(),0)
        {}
    };

    // TQ-084: XNA Refresh .ctors use NullNotAllowed/RequireNonNullAudioEngine,
    // InvalidContentVersion and CouldNotCreateResource from FrameworkResources.
    // ── Constructors ──────────────────────────────────────────────────────────

    SoundBank::SoundBank(AudioEngine* audioEngine, const std::string& filename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
            throw System::ArgumentNullException("audioEngine", "You must pass in a valid audio engine.");
        if (filename.empty())
            throw System::ArgumentNullException("filename",
                "This method does not accept null for this parameter.");

        // FNA's SoundBank ctor reads filename via TitleContainer.ReadToPointer, which throws
        // FileNotFoundException on a missing file before ever reaching FACT (SoundBank.cs) --
        // match that here (P9-HARDWARE-003).
        std::vector<uint8_t> raw =
            CNA::Internal::Audio::ReadTitleContentBytes(filename);

        // XNA .ctor IL_0052-IL_0098 validates SDBK before creating a native resource;
        // unlike FNA's unchecked creation result, malformed content must fail construction.
        if (raw.size() <= 4 || raw[0] != 'S' || raw[1] != 'D' ||
            raw[2] != 'B' || raw[3] != 'K')
            throw System::ArgumentException("XACT could not load the data provided. Make sure you are using the correct version of the XACT tool.");

        // XNA native CreateHandle IL_001c-IL_0038 refuses a released engine; its
        // managed constructor IL_00c3-IL_00d6 reports resource creation failure.
        if (audioEngine->getIsDisposedProperty())
            throw System::InvalidOperationException("This resource could not be created.");

        try
        {
            auto xsb = CNA::Internal::Audio::ParseXsb(raw);
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::DEBUG,
                "[SoundBank] Loaded XSB: ", filename, " cues=", xsb.cues.size(), " sounds=",
                xsb.sounds.size());
            xactImpl_ = std::make_unique<XactSoundBankImpl>(std::move(xsb));
            engine_->RegisterSoundBank(this); // XA-8: lets AudioEngine::Dispose() cascade here
        }
        catch (const std::exception& ex)
        {
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                "[SoundBank] XSB parse error (", filename, "): ", ex.what());
            // XNA .ctor IL_00c3-IL_00d6 rejects a failed native resource creation.
            throw System::InvalidOperationException("This resource could not be created.");
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
        // Refresh tests native INUSE, not playback counts. Microsoft3.6 retains
        // this flag for a Stopped cue until its handle is destroyed (TQ-XACT-31).
        return !isDisposed_ && !activeCues_.empty();
    }

    const CNA::Internal::Audio::XsbData* SoundBank::GetXsbData() const
    {
        return xactImpl_ ? &xactImpl_->data : nullptr;
    }

    // ── GetCue ────────────────────────────────────────────────────────────────

    Cue* SoundBank::GetCue(const std::string& name)
    {
        if (name.empty())
            throw System::ArgumentNullException("name",
                "This method does not accept null for this parameter.");

        // A bank being disposed (a Disposing handler of one of its cues) is already released.
        if (xactImpl_ && !isDisposed_)
        {
            auto it = xactImpl_->data.cueNameMap.find(name);
            if (it != xactImpl_->data.cueNameMap.end())
                return new Cue(name, this, it->second);
        }

        // Refresh GetCue IL_0035-005d formats CueNotFound for E_INVALIDARG;
        // native GetCue IL_013c-014c returns that same result for a released bank.
        throw System::ArgumentException("An error occurred trying to play the cue named \"" + name + "\". Is the cue name correct?");
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
        // Sweep the fire-and-forget cues that have finished (destroying a Cue whose sound has
        // already stopped is effectively a no-op), never by age: a music cue or a long one-shot
        // plays for as long as it is authored to.
        fireAndForget_.erase(
            std::remove_if(
                fireAndForget_.begin(), fireAndForget_.end(),
                [](const FireAndForget& faf)
                {
                    // XA-7: a paused cue is still alive too -- only a genuinely stopped cue
                    // (neither playing nor paused) is swept. Without this, pausing a
                    // fire-and-forget cue's category made the very next PlayCue() on this bank
                    // silently destroy it.
                    return !(faf.cue && (faf.cue->getIsPlayingProperty() || faf.cue->getIsPausedProperty()
                                         || faf.cue->getIsStoppingProperty()));
                }),
            fireAndForget_.end());
    }

    void SoundBank::RegisterCue(Cue* cue)
    {
        if (!cue) return;
        activeCues_.push_back(cue);
    }

    void SoundBank::RetainResolvedWaveBank(WaveBank* bank)
    {
        if (std::find(resolvedWaveBanks_.begin(), resolvedWaveBanks_.end(), bank) != resolvedWaveBanks_.end()) return;
        resolvedWaveBanks_.push_back(bank);
        try { bank->RegisterSoundBank(this); }
        catch (...) { resolvedWaveBanks_.pop_back(); throw; }
    }

    void SoundBank::ForgetResolvedWaveBank(WaveBank* bank)
    {
        resolvedWaveBanks_.erase(std::remove(resolvedWaveBanks_.begin(), resolvedWaveBanks_.end(), bank),
                                 resolvedWaveBanks_.end());
    }

    void SoundBank::UnregisterCue(Cue* cue)
    {
        activeCues_.erase(std::remove(activeCues_.begin(), activeCues_.end(), cue), activeCues_.end());
    }

    void SoundBank::RefreshCueInstanceCount(SharpRuntime::ushortcs cueIndex)
    {
        if (isDisposed_ || !xactImpl_ || cueIndex >= xactImpl_->publishedInstanceCounts.size()) return;
        const auto* variable = engine_ ? engine_->FindVariable("NumCueInstances") : nullptr;
        if (!variable || !(variable->accessibility & 0x04)) return;

        std::size_t count = 0;
        for (const Cue* cue : activeCues_)
            if (cue && !cue->isDisposed_ && cue->cueIndex_ == cueIndex
                && cue->state_ == Cue::State::Playing)
                ++count;

        // Microsoft3.6 publishes on count changes, including to prepared/stopped handles.
        // FAudio reads its stored counter directly instead. Never reconcile state here:
        // reconciliation evaluates RPCs that can themselves read this cached variable.
        auto& published = xactImpl_->publishedInstanceCounts[cueIndex];
        if (published == count) return;
        published = count;
        for (Cue* cue : activeCues_)
            if (cue && !cue->isDisposed_ && cue->cueIndex_ == cueIndex)
                cue->numCueInstances_ = static_cast<float>(count);
    }

    void SoundBank::PlayCueInternal(const std::string& name,
                                     const AudioListener* listener,
                                     const AudioEmitter* emitter)
    {
        if (name.empty())
            throw System::ArgumentNullException("name",
                "This method does not accept null for this parameter.");

        SweepFireAndForget();

        // XNA's PlayCue overloads use InvalidOperationException for missing names
        // (IL_0051 / IL_0081), unlike GetCue's ArgumentException.
        if (!xactImpl_ || isDisposed_ || !xactImpl_->data.cueNameMap.contains(name))
            throw System::InvalidOperationException("An error occurred trying to play the cue named \"" + name + "\". Is the cue name correct?");

        std::unique_ptr<Cue> cue;
        // Refresh PlayCue IL_002f-0034 / IL_005f-0064 swallow8ac70008;
        // other preparation failures use CueNotFound, unlike GetCue's NoWaveBank.
        try { cue.reset(GetCue(name)); }
        catch (const InstancePlayLimitException&) { return; }
        catch (const System::InvalidOperationException&)
        {
            throw System::InvalidOperationException("An error occurred trying to play the cue named \"" + name + "\". Is the cue name correct?");
        }
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
        if (cue->getIsStoppedProperty()) return;
        // Keep the Cue alive so its SoundEffectInstances (and their SDL3_mixer
        // tracks) are not destroyed before the sound has had a chance to play.
        fireAndForget_.push_back({std::move(cue)});
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void SoundBank::Dispose()
    {
        Dispose(true);
    }

    void SoundBank::Dispose(bool disposing)
    {
        if (!isDisposed_)
        {
            // XNA Dispose(bool) IL_0018-IL_003f precedes Disposing at IL_005b.
            isDisposed_ = true;
            if (engine_) engine_->UnregisterSoundBank(this); // XA-8
            fireAndForget_.clear(); // stops any still-playing fire-and-forget cues; each one's
                                     // destructor also unregisters itself from activeCues_.

            // P12-BANK-001: force-stop every cue still associated with this bank, including ones
            // the caller obtained via GetCue() and is still holding -- matches
            // FACTSoundBank_Destroy (FACT.c:1311-1327). Snapshot first: Cue::Dispose() below
            // calls back into UnregisterCue(), which would otherwise invalidate a live range-for
            // over activeCues_ mid-iteration (same hazard as AudioEngine::StopCategoryInternal).
            // A cue's Disposing handler may destroy another cue (skipped through its lifetime
            // token) or throw; the cascade still detaches every cue, and the first exception is
            // rethrown once this bank references nothing.
            std::vector<std::pair<Cue*, std::weak_ptr<char>>> cues;
            for (auto* cue : activeCues_)
                if (cue) cues.emplace_back(cue, cue->lifetime_);
            std::exception_ptr failure;
            for (auto& [cue, alive] : cues)
            {
                if (alive.expired()) continue;
                try { cue->Dispose(); } // idempotent; safe even if the caller already disposed it
                catch (...) { if (!failure) failure = std::current_exception(); }
            }
            activeCues_.clear();

            // Cue callbacks may have disposed banks already; snapshot only after
            // those cascades have removed their reciprocal registrations.
            auto resolved = std::move(resolvedWaveBanks_);
            resolvedWaveBanks_.clear();
            for (auto* bank : resolved) bank->UnregisterSoundBank(this);

            xactImpl_.reset();
            if (failure) std::rethrow_exception(failure);
            if (disposing)
                Disposing.Raise(this, System::EventArgs::Empty);
        }
    }

    GetTypeNameCPP(SoundBank, "Microsoft.Xna.Framework.Audio.SoundBank")
}
