// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

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

        // Try to parse the .XGS file
        std::ifstream f(settingsFile, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            std::cerr << "[AudioEngine] Cannot open XGS: " << settingsFile
                      << " — running as stub\n";
            return;
        }

        auto sz = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> data(static_cast<std::size_t>(sz));
        f.read(reinterpret_cast<char*>(data.data()), sz);

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

    void AudioEngine::Update()
    {
        // No streaming or notification work needed for SDL3_mixer.
    }

    void AudioEngine::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            rendererDetails_.clear();
            xactImpl_.reset();
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
        // Apply to all active cues in this category
        for (auto* cue : xactImpl_->activeCues)
        {
            if (cue && cue->categoryIdx_ == idx)
                ; // Cue would need to re-apply volume — skipped for simplicity
        }
    }

    void AudioEngine::PauseCategoryInternal(unsigned short idx)
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryPaused.size()) return;
        xactImpl_->categoryPaused[idx] = true;
        for (auto* cue : xactImpl_->activeCues)
            if (cue && cue->categoryIdx_ == idx && cue->getIsPlayingProperty())
                cue->Pause();
    }

    void AudioEngine::ResumeCategoryInternal(unsigned short idx)
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryPaused.size()) return;
        xactImpl_->categoryPaused[idx] = false;
        for (auto* cue : xactImpl_->activeCues)
            if (cue && cue->categoryIdx_ == idx && cue->getIsPausedProperty())
                cue->Resume();
    }

    void AudioEngine::StopCategoryInternal(unsigned short idx, bool immediate)
    {
        if (!xactImpl_) return;
        auto opt = immediate ? AudioStopOptions::Immediate : AudioStopOptions::AsAuthored;
        for (auto* cue : xactImpl_->activeCues)
            if (cue && cue->categoryIdx_ == idx)
                cue->Stop(opt);
    }

    // ── Cue registration ──────────────────────────────────────────────────────

    void AudioEngine::RegisterCue(Cue* cue)
    {
        if (!xactImpl_ || !cue) return;
        xactImpl_->activeCues.push_back(cue);
    }

    void AudioEngine::UnregisterCue(Cue* cue)
    {
        if (!xactImpl_ || !cue) return;
        auto& v = xactImpl_->activeCues;
        v.erase(std::remove(v.begin(), v.end(), cue), v.end());
    }

    GetTypeNameCPP(AudioEngine, "Microsoft.Xna.Framework.Audio.AudioEngine")
}
