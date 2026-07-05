// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    // ── Internal impl ─────────────────────────────────────────────────────────

    struct AudioEngine::XactEngineImpl
    {
        CNA::Internal::Audio::XgsData xgs;

        // Per-category runtime state
        std::vector<float> categoryVolumes;  // linear [0..1]
        std::vector<bool>  categoryPaused;

        // Wavebank registry (bank name → pointer)
        std::unordered_map<std::string, WaveBank*> waveBanks;

        // SoundBank registry, symmetric to waveBanks above (XA-8) -- no natural unique key like
        // WaveBank's bank name, so this is just a flat list of every live SoundBank.
        std::vector<SoundBank*> soundBanks;

        // Active cues (for category-level operations)
        std::vector<Cue*> activeCues;

        // Global variables (backed by xgs.variables initial values)
        std::unordered_map<std::string, float> globalVariables;
    };

    // ── Constructors ──────────────────────────────────────────────────────────

    AudioEngine::AudioEngine(const std::string& settingsFile)
        : AudioEngine(settingsFile, System::TimeSpan::Zero, {})
    {
    }

    AudioEngine::AudioEngine(const std::string& settingsFile,
                             System::TimeSpan /*lookAheadTime*/,
                             const std::string& /*rendererId*/)
    {
        // lookAheadTime/rendererId are intentionally unused: CNA has exactly one backend
        // (SDL3_mixer, see getRendererDetailsProperty()), so there is nothing to select between,
        // and SDL3_mixer has no FACT-style scheduling look-ahead to configure (plan_audio.md XA-4).
        if (settingsFile.empty())
            throw System::ArgumentNullException("settingsFile");

        Init(settingsFile);
    }

    AudioEngine::~AudioEngine()
    {
        Dispose();
    }

    void AudioEngine::Init(const std::string& settingsFile)
    {
        rendererDetails_.push_back(RendererDetail(std::string("SDL3_mixer"), std::string("SDL3_mixer")));

        xactImpl_ = std::make_unique<XactEngineImpl>();

        // Try to parse the .XGS file. FNA's AudioEngine ctor reads settingsFile via
        // TitleContainer.ReadToPointer, which does a File.Exists check and throws
        // FileNotFoundException before ever reaching FACT (TitleContainer.cs) -- match that here
        // (P9-HARDWARE-003) rather than silently continuing as a stub.
        std::ifstream f(settingsFile, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + settingsFile + "'.", settingsFile);
        }

        auto sz = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> data(static_cast<std::size_t>(sz));
        f.read(reinterpret_cast<char*>(data.data()), sz);

        // FNA checks FACTAudioEngine_Initialize's return code and throws
        // InvalidOperationException("Engine initialization failed!") on a nonzero result
        // (AudioEngine.cs) -- an existing-but-corrupt settings file hits that same path here.
        try
        {
            xactImpl_->xgs = CNA::Internal::Audio::ParseXgs(data);

            // Initialize per-category volumes
            std::size_t n = xactImpl_->xgs.categories.size();
            xactImpl_->categoryVolumes.assign(n, 1.0f);
            xactImpl_->categoryPaused.assign(n, false);

            // Apply default volumes from XGS data
            for (std::size_t i = 0; i < n; ++i)
                xactImpl_->categoryVolumes[i] = xactImpl_->xgs.categories[i].volume;

            // Initialize global variables
            for (auto& v : xactImpl_->xgs.variables)
                xactImpl_->globalVariables[v.name] = v.initialValue;

            std::cerr << "[AudioEngine] Loaded XGS: " << settingsFile
                      << " (" << n << " categories, "
                      << xactImpl_->xgs.variables.size() << " variables)\n";
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[AudioEngine] XGS parse error: " << ex.what() << "\n";
            throw System::InvalidOperationException("Engine initialization failed!");
        }
    }

    // ── Properties ────────────────────────────────────────────────────────────

    bool AudioEngine::getIsDisposedProperty() const { return isDisposed_; }

    const std::vector<RendererDetail>& AudioEngine::getRendererDetailsProperty() const
    {
        return rendererDetails_;
    }

    // ── Public API ────────────────────────────────────────────────────────────

    AudioCategory AudioEngine::GetCategory(const std::string& name)
    {
        if (name.empty())
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("AudioEngine");

        if (xactImpl_)
        {
            auto it = xactImpl_->xgs.categoryNameMap.find(name);
            if (it != xactImpl_->xgs.categoryNameMap.end())
                return AudioCategory(this, it->second, name);
        }

        throw System::InvalidOperationException("Invalid category name!");
    }

    float AudioEngine::GetGlobalVariable(const std::string& name) const
    {
        if (name.empty())
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("AudioEngine");

        if (xactImpl_)
        {
            auto it = xactImpl_->globalVariables.find(name);
            if (it != xactImpl_->globalVariables.end())
                return it->second;
        }
        throw System::InvalidOperationException("Invalid variable name!");
    }

    void AudioEngine::SetGlobalVariable(const std::string& name, float value)
    {
        if (name.empty())
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("AudioEngine");

        if (xactImpl_)
        {
            auto it = xactImpl_->globalVariables.find(name);
            if (it == xactImpl_->globalVariables.end())
                throw System::InvalidOperationException("Invalid variable name!");
            it->second = value;
        }
    }

    const std::string* AudioEngine::GetVariableNameByIndex(SharpRuntime::shortcs index) const
    {
        if (!xactImpl_ || index < 0
            || static_cast<std::size_t>(index) >= xactImpl_->xgs.variables.size())
            return nullptr;
        return &xactImpl_->xgs.variables[static_cast<std::size_t>(index)].name;
    }

    const CNA::Internal::Audio::XgsRpc* AudioEngine::FindRpcByCode(SharpRuntime::uintcs code) const
    {
        if (!xactImpl_) return nullptr;
        auto it = xactImpl_->xgs.rpcCodeMap.find(code);
        if (it == xactImpl_->xgs.rpcCodeMap.end()) return nullptr;
        return &xactImpl_->xgs.rpcs[it->second];
    }

    void AudioEngine::Update()
    {
        // P9-LIFECYCLE-008/009: mirrors FNA's AudioEngine.Update() -> FACTAudioEngine_DoWork,
        // which is what actually destroys managed/fire-and-forget cues once FACT_STATE_STOPPED
        // (FACT_internal.c), instead of only sweeping lazily on a bank's next PlayCue() call.
        // Cue-level Playing/Paused/Stopped reconciliation itself is live and per-call (see
        // Cue::ReconcileState()), so it needs no help from Update() to stay accurate.
        if (!xactImpl_) return;
        for (auto* sb : xactImpl_->soundBanks)
            if (sb) sb->SweepFireAndForget();
    }

    void AudioEngine::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);

            // XA-8: cascade disposal to every WaveBank/SoundBank/Cue this engine created,
            // matching FNA's native OnXACTNotification(WAVEBANKDESTROYED/SOUNDBANKDESTROYED/
            // CUEDESTROYED), which immediately flips IsDisposed on every dependent wrapper once
            // the native engine goes away. Snapshot the registries into local vectors and reset
            // xactImpl_ FIRST: each object's own Dispose() below calls back into
            // Unregister{WaveBank,SoundBank,Cue}(), which would otherwise mutate these same
            // containers while we're iterating them; with xactImpl_ already null, those
            // reentrant calls become harmless no-ops (see their own null guards).
            std::vector<WaveBank*> waveBanks;
            std::vector<SoundBank*> soundBanks;
            std::vector<Cue*> cues;
            if (xactImpl_)
            {
                waveBanks.reserve(xactImpl_->waveBanks.size());
                for (auto& [name, wb] : xactImpl_->waveBanks)
                    waveBanks.push_back(wb);
                soundBanks = xactImpl_->soundBanks;
                cues       = xactImpl_->activeCues;
            }

            rendererDetails_.clear();
            xactImpl_.reset();

            for (auto* cue : cues)
                if (cue) cue->Dispose();
            for (auto* sb : soundBanks)
                if (sb) sb->Dispose();
            for (auto* wb : waveBanks)
                if (wb) wb->Dispose();

            isDisposed_ = true;
        }
    }

    // ── WaveBank registry ─────────────────────────────────────────────────────

    void AudioEngine::RegisterWaveBank(WaveBank* wb)
    {
        if (!wb || !xactImpl_) return;
        xactImpl_->waveBanks[wb->getBankName()] = wb;
    }

    void AudioEngine::UnregisterWaveBank(WaveBank* wb)
    {
        if (!wb || !xactImpl_) return;
        auto it = xactImpl_->waveBanks.find(wb->getBankName());
        if (it != xactImpl_->waveBanks.end() && it->second == wb)
            xactImpl_->waveBanks.erase(it);
    }

    WaveBank* AudioEngine::FindWaveBank(const std::string& bankName) const
    {
        if (!xactImpl_) return nullptr;
        auto it = xactImpl_->waveBanks.find(bankName);
        return (it != xactImpl_->waveBanks.end()) ? it->second : nullptr;
    }

    // ── SoundBank registry ────────────────────────────────────────────────────

    void AudioEngine::RegisterSoundBank(SoundBank* sb)
    {
        if (!sb || !xactImpl_) return;
        xactImpl_->soundBanks.push_back(sb);
    }

    void AudioEngine::UnregisterSoundBank(SoundBank* sb)
    {
        if (!sb || !xactImpl_) return;
        auto& v = xactImpl_->soundBanks;
        v.erase(std::remove(v.begin(), v.end(), sb), v.end());
    }

    // ── Category state ────────────────────────────────────────────────────────

    float AudioEngine::GetCategoryVolume(unsigned short idx) const
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryVolumes.size()) return 1.0f;
        return xactImpl_->categoryVolumes[idx];
    }

    bool AudioEngine::IsCategoryPaused(unsigned short idx) const
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryPaused.size()) return false;
        return xactImpl_->categoryPaused[idx];
    }

    void AudioEngine::SetCategoryVolumeInternal(unsigned short idx, float vol)
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryVolumes.size()) return;
        xactImpl_->categoryVolumes[idx] = vol;
        // P9-CATEGORY-001: snapshot before iterating -- ApplyCategoryVolume() itself never
        // mutates activeCues today, but this stays consistent with the other three category
        // operations below (one of which has a real reentrant-mutation bug) rather than relying
        // on "this particular callee happens not to touch the registry" staying true forever.
        std::vector<Cue*> cues = xactImpl_->activeCues;
        for (auto* cue : cues)
            if (cue && cue->categoryIdx_ == idx)
                cue->ApplyCategoryVolume(vol);
    }

    void AudioEngine::PauseCategoryInternal(unsigned short idx)
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryPaused.size()) return;
        xactImpl_->categoryPaused[idx] = true;
        std::vector<Cue*> cues = xactImpl_->activeCues; // P9-CATEGORY-001, see SetCategoryVolumeInternal
        for (auto* cue : cues)
            if (cue && cue->categoryIdx_ == idx && cue->getIsPlayingProperty())
                cue->Pause();
    }

    void AudioEngine::ResumeCategoryInternal(unsigned short idx)
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryPaused.size()) return;
        xactImpl_->categoryPaused[idx] = false;
        std::vector<Cue*> cues = xactImpl_->activeCues; // P9-CATEGORY-001, see SetCategoryVolumeInternal
        for (auto* cue : cues)
            if (cue && cue->categoryIdx_ == idx && cue->getIsPausedProperty())
                cue->Resume();
    }

    void AudioEngine::StopCategoryInternal(unsigned short idx, bool immediate)
    {
        if (!xactImpl_) return;
        auto opt = immediate ? AudioStopOptions::Immediate : AudioStopOptions::AsAuthored;
        // P9-CATEGORY-001: MUST snapshot here, unlike the three methods above this is a real bug,
        // not just defensive symmetry -- Cue::Stop() cascades to StopInternal(), which
        // unconditionally calls AudioEngine::UnregisterCue(), erasing the cue from this exact
        // xactImpl_->activeCues vector. Iterating the live vector directly means the erase
        // invalidates the range-for's cached end() iterator mid-loop; with 3+ cues in the same
        // category this reliably skips stopping at least one of them (verified by hand-tracing
        // std::remove's element shifts, and by a real regression test -- see
        // StopStopsAllActiveCuesInCategoryNotJustSomeOfThem in AudioCategoryTests.cpp).
        std::vector<Cue*> cues = xactImpl_->activeCues;
        for (auto* cue : cues)
            if (cue && cue->categoryIdx_ == idx)
                cue->Stop(opt);
    }

    // ── Cue registration ──────────────────────────────────────────────────────

    void AudioEngine::RegisterCue(Cue* cue)
    {
        if (!xactImpl_ || !cue) return;
        // P9-LIFECYCLE-011: Cue::Play() already rejects being called twice on an already
        // Playing/Paused/Stopping/Stopped cue (see Cue::Play()'s ReconcileState() + state guard),
        // so this path shouldn't be reachable in practice; guarded anyway as defense-in-depth for
        // the registry itself, since a duplicate entry here would make every category-wide
        // operation (Pause/Resume/Stop/SetVolume) apply itself twice to the same cue.
        auto& v = xactImpl_->activeCues;
        if (std::find(v.begin(), v.end(), cue) != v.end()) return;
        v.push_back(cue);
    }

    void AudioEngine::UnregisterCue(Cue* cue)
    {
        if (!xactImpl_ || !cue) return;
        auto& v = xactImpl_->activeCues;
        v.erase(std::remove(v.begin(), v.end(), cue), v.end());
    }

    bool AudioEngine::IsValidVariableName(const std::string& name) const
    {
        return xactImpl_ && xactImpl_->globalVariables.find(name) != xactImpl_->globalVariables.end();
    }

    std::size_t AudioEngine::ActiveCueCountForTest() const
    {
        return xactImpl_ ? xactImpl_->activeCues.size() : 0;
    }

    GetTypeNameCPP(AudioEngine, "Microsoft.Xna.Framework.Audio.AudioEngine")
}
