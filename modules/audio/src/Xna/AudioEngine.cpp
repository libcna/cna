// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Internal/AudioLog.hpp"
#include "Internal/WetBus.hpp"
#include "Internal/XactRpc.hpp"
#include "CNA/Internal/Audio/MixerEngine.hpp"
#include <array>
#include <cmath>
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "TitleContentBytes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/IndexOutOfRangeException.hpp"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    // ── Internal impl ─────────────────────────────────────────────────────────

    struct AudioEngine::XactEngineImpl
    {
        CNA::Internal::Audio::XgsData xgs;
        std::vector<std::uint64_t> wetBuses;
        std::uint64_t wetGeneration=0;

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

    namespace
    {
        // P12-VAR-001: matches FACT_internal.h's ACCESSIBILITY_* bit values exactly
        // (PUBLIC=0x1, READONLY=0x2, CUE=0x4, RESERVED=0x8 unused here).
        constexpr uint8_t kAccessibilityPublic   = 0x1;
        constexpr uint8_t kAccessibilityReadOnly = 0x2;
        constexpr uint8_t kAccessibilityCue      = 0x4;

        // P12-CATEGORY-001: matches FACT.c's FACT_INTERNAL_IsInCategory exactly -- true if
        // `cueCategory` (a cue's own, exact category) IS `target`, or `target` is any ancestor of
        // `cueCategory` in the parent-category chain (XgsCategory::parentIndex, 0xFFFF = root/no
        // parent). This is what lets Pause/Resume/Stop on a PARENT category reach cues belonging
        // to any of its DESCENDANT categories too, not just cues in that exact category.
        bool IsInCategory(
            const std::vector<CNA::Internal::Audio::XgsCategory>& categories,
            uint16_t cueCategory, uint16_t target)
        {
            if (cueCategory == target) return true;

            uint16_t idx = cueCategory;
            while (idx < categories.size() && categories[idx].parentIndex != 0xFFFF)
            {
                idx = categories[idx].parentIndex;
                if (idx == target) return true;
            }
            return false;
        }
    }

    // TQ-084: XNA Refresh .ctors use NullNotAllowed/RequireNonNullAudioEngine,
    // InvalidContentVersion and CouldNotCreateResource from FrameworkResources.
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
        // and SDL3_mixer has no FACT-style scheduling look-ahead to configure (the audio plan XA-4).
        if (settingsFile.empty())
            throw System::ArgumentNullException("settingsFile",
                "This method does not accept null for this parameter.");

        Init(settingsFile);
    }

    AudioEngine::~AudioEngine()
    {
        Dispose();
    }

    void AudioEngine::Init(const std::string& settingsFile)
    {
#if defined(CNA_AUDIO_PLATFORM_ALSA)
        // X11-0151: CNA's own mixer, playing through ALSA.
        rendererDetails_.push_back(RendererDetail(std::string("CNA mixer (ALSA)"), std::string("ALSA")));
#else
        rendererDetails_.push_back(RendererDetail(std::string("SDL3_mixer"), std::string("SDL3_mixer")));
#endif

        xactImpl_ = std::make_unique<XactEngineImpl>();

        // Try to parse the .XGS file. FNA's AudioEngine ctor reads settingsFile via
        // TitleContainer.ReadToPointer, which does a File.Exists check and throws
        // FileNotFoundException before ever reaching FACT (TitleContainer.cs) -- match that here
        // (P9-HARDWARE-003) rather than silently continuing as a stub.
        std::vector<uint8_t> data =
            CNA::Internal::Audio::ReadTitleContentBytes(settingsFile);

        // XNA .ctor(string,TimeSpan,string) IL_0038-IL_007e checks the signature
        // before native initialization, so bad headers are ArgumentException, not parse errors.
        if (data.size() <= 4 || data[0] != 'X' || data[1] != 'G' ||
            data[2] != 'S' || data[3] != 'F')
            throw System::ArgumentException("XACT could not load the data provided. Make sure you are using the correct version of the XACT tool.");

        // Refresh AudioEngine .ctor IL_00c1-00cb reports failed native creation
        // using CouldNotCreateResource, regardless of the parser diagnostic.
        try
        {
            xactImpl_->xgs = CNA::Internal::Audio::ParseXgs(data);

            // Authored category gain enters currentVolume only through SetVolume.
            std::size_t n = xactImpl_->xgs.categories.size();
            xactImpl_->categoryVolumes.assign(n, 1.0f);
            xactImpl_->categoryPaused.assign(n, false);

            // Initialize global variables
            for (auto& v : xactImpl_->xgs.variables)
                xactImpl_->globalVariables[v.name] = v.initialValue;

            CNA::Internal::Audio::LogAudio(CNA::LogLevel::DEBUG,
                "[AudioEngine] Loaded XGS: ", settingsFile, " (", n, " categories, ",
                xactImpl_->xgs.variables.size(), " variables)");
        }
        catch (const std::exception& ex)
        {
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                "[AudioEngine] XGS parse error: ", ex.what());
            throw System::InvalidOperationException("This resource could not be created.");
        }
    }

    void AudioEngine::EnsureDspBuses()
    {
        using namespace CNA::Internal::Audio;
        if(!xactImpl_||xactImpl_->xgs.dspPresets.empty())return;
#ifdef SOUND_ENABLED
        if(!xactImpl_->wetBuses.empty()&&xactImpl_->wetGeneration==GetMixerEngineGeneration())return;
        xactImpl_->wetBuses.clear();
        try {
            // Reserve before acquiring any bus so vector growth cannot leak a just-created handle.
            if(xactImpl_->xgs.dspPresets.size()>32)throw std::runtime_error("Too many DSP presets");
            xactImpl_->wetBuses.reserve(xactImpl_->xgs.dspPresets.size());
            for(const auto& preset:xactImpl_->xgs.dspPresets) {
                if(preset.parameters.size()!=22)throw std::runtime_error("Unsupported XACT DSP preset");
                ReverbParameters values{};
                for(std::size_t i=0;i<22;++i) {
                    const auto& p=preset.parameters[i];
                    if(!std::isfinite(p.value)||!std::isfinite(p.minValue)||!std::isfinite(p.maxValue)||
                       p.minValue>p.maxValue||p.value<p.minValue||p.value>p.maxValue)
                        throw std::runtime_error("Invalid XACT DSP bounds");
                    values[i]=p.value;
                }
                xactImpl_->wetBuses.push_back(CreateReverbBus(values));
            }
            xactImpl_->wetGeneration=GetMixerEngineGeneration();
        } catch(...) {
            for(auto bus:xactImpl_->wetBuses)ReleaseReverbBus(bus);
            xactImpl_->wetBuses.clear();
            throw System::InvalidOperationException("This resource could not be created.");
        }
#else
        throw System::InvalidOperationException("This resource could not be created.");
#endif
    }

    std::uint64_t AudioEngine::ResolveDspBus(std::uint32_t code)
    {
        if(!xactImpl_)throw System::InvalidOperationException("This resource could not be created.");
        const auto found=xactImpl_->xgs.dspCodeMap.find(code);
        if(found==xactImpl_->xgs.dspCodeMap.end())
            throw System::InvalidOperationException("This resource could not be created.");
        EnsureDspBuses();
        return xactImpl_->wetBuses.at(found->second);
    }

    void AudioEngine::UpdateDspPresets()
    {
        using namespace CNA::Internal::Audio;
        if(!xactImpl_||xactImpl_->xgs.dspPresets.empty())return;
        EnsureDspBuses();
        // FACT_internal.c:1124-1175: global curves target parameter-5 in each preset,
        // clamp to its own authored bounds, then publish the effect snapshot.
        for(std::size_t n=0;n<xactImpl_->xgs.dspPresets.size();++n) {
            const auto& preset=xactImpl_->xgs.dspPresets[n]; ReverbParameters values{};
            for(std::size_t i=0;i<22;++i)values[i]=preset.parameters[i].value;
            for(const auto& rpc:xactImpl_->xgs.rpcs) {
                if(rpc.parameter<5||rpc.parameter>=27||rpc.variable>=xactImpl_->xgs.variables.size())continue;
                const auto& variable=xactImpl_->xgs.variables[rpc.variable];
                if(variable.accessibility&4)continue; // cue-local state cannot change an engine bus
                const auto value=xactImpl_->globalVariables.find(variable.name);
                if(value==xactImpl_->globalVariables.end())continue;
                const auto i=rpc.parameter-5; const auto& parameter=preset.parameters[i];
                const float result=EvaluateRpcCurve(rpc,value->second);
                if(!std::isfinite(result))throw System::InvalidOperationException("Invalid DSP RPC result");
                values[i]=std::clamp(result,parameter.minValue,parameter.maxValue);
            }
            try { UpdateReverbBus(xactImpl_->wetBuses[n],values); }
            catch(const std::exception&) { throw System::InvalidOperationException("This resource could not be created."); }
        }
    }

    // ── Properties ────────────────────────────────────────────────────────────

    bool AudioEngine::getIsDisposedProperty() const { return isDisposed_; }

    const std::vector<RendererDetail>& AudioEngine::getRendererDetailsProperty() const
    {
        return rendererDetails_;
    }

    // ── Public API ────────────────────────────────────────────────────────────

    // Refresh AudioCategory .ctor IL_0043-005a maps missing/released category to resource failure.
    AudioCategory AudioEngine::GetCategory(const std::string& name)
    {
        if (name.empty())
            throw System::ArgumentNullException("name",
                "This method does not accept null for this parameter.");

        if (xactImpl_)
        {
            auto it = xactImpl_->xgs.categoryNameMap.find(name);
            if (it != xactImpl_->xgs.categoryNameMap.end())
                return AudioCategory(this, it->second, name);
        }

        throw System::InvalidOperationException("This resource could not be created.");
    }

    // Refresh native Engine.Get/SetGlobalVariable return E_INVALIDARG on a released engine;
    // Helpers.GetExceptionFromResult IL_0235-023a maps that to plain ArgumentException.
    // Invalid names map INVALIDVARIABLEINDEX via IL_0189-0193 (FNA AudioEngine.cs:285-334).
    float AudioEngine::GetGlobalVariable(const std::string& name) const
    {
        if (name.empty())
            throw System::ArgumentNullException("name",
                "This method does not accept null for this parameter.");
        if (isDisposed_)
            throw System::ArgumentException();

        // P12-VAR-001: matches FACTAudioEngine_GetGlobalVariableIndex exactly (FACT.c) --
        // PUBLIC must be set AND CUE must be clear. A CUE-scoped variable is a genuinely
        // different domain (only reachable via Cue::GetVariable/SetVariable, GetCueVariableInfo
        // below), not just a stricter check on the same one.
        const auto* v = FindVariable(name);
        if (!v || !(v->accessibility & kAccessibilityPublic) || (v->accessibility & kAccessibilityCue))
            throw System::IndexOutOfRangeException("The specified variable index is invalid.");

        if (xactImpl_)
        {
            auto it = xactImpl_->globalVariables.find(name);
            if (it != xactImpl_->globalVariables.end())
                return it->second;
        }
        return v->initialValue;
    }

    void AudioEngine::SetGlobalVariable(const std::string& name, float value)
    {
        if (name.empty())
            throw System::ArgumentNullException("name",
                "This method does not accept null for this parameter.");
        if (isDisposed_)
            throw System::ArgumentException();

        // P12-VAR-001: same name-validity gate as GetGlobalVariable above.
        const auto* v = FindVariable(name);
        if (!v || !(v->accessibility & kAccessibilityPublic) || (v->accessibility & kAccessibilityCue))
            throw System::IndexOutOfRangeException("The specified variable index is invalid.");

        // Matches FACTAudioEngine_SetGlobalVariable's own READONLY rejection (FACT.c) -- FNA's
        // C# wrapper (AudioEngine.cs SetGlobalVariable) never checks this native call's return
        // code, so in real XNA a READONLY variable's SetGlobalVariable() call is a silent no-op,
        // not an exception.
        if (v->accessibility & kAccessibilityReadOnly)
            return;

        // Matches FACT's own `FAudio_clamp(nValue, var->minValue, var->maxValue)`.
        if (xactImpl_)
            xactImpl_->globalVariables[name] = std::clamp(value, v->minValue, v->maxValue);
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
        // Cue-level Playing/Paused/Stopped reconciliation itself is also live and per-call (see
        // Cue::ReconcileState()), so a query never sees stale state -- but P9-STOP-010's real
        // authored-fadeOutMS volume ramp needs an actual per-frame tick to be *audible* as it
        // progresses, not just correct whenever next queried: without this, a cue nobody happens
        // to query mid-fade would jump straight from full volume to silent the next time
        // something did query it, instead of smoothly ramping down in real time. Real FACT's own
        // mixer thread ticks every active fade continuously; CNA's equivalent tick point is here,
        // matching the same "call Update() every frame" contract FNA games already follow.
        if (!xactImpl_) return;
        UpdateDspPresets();
        const auto now = eventClock_ ? eventClock_() : std::chrono::steady_clock::now();
        for (auto* cue : xactImpl_->activeCues)
            if (cue)
            {
                cue->UpdateSchedule(now);
                cue->ReconcileState();
            }
        for (auto* sb : xactImpl_->soundBanks)
            if (sb) sb->SweepFireAndForget();
    }

    void AudioEngine::Dispose()
    {
        Dispose(true);
    }

    void AudioEngine::Dispose(bool disposing)
    {
        if (!isDisposed_)
        {
            // XNA Dispose(bool) IL_0018-IL_001a marks disposal before dependent callbacks;
            // its own Disposing event follows NotifyDestroyedEngine and native release.
            isDisposed_ = true;

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
#ifdef SOUND_ENABLED
            // Retire callback-owned storage before the impl, through the mixer barrier.
            if(xactImpl_ && xactImpl_->wetGeneration==CNA::Internal::Audio::GetMixerEngineGeneration())
                for(auto bus:xactImpl_->wetBuses)CNA::Internal::Audio::ReleaseReverbBus(bus);
#endif
            xactImpl_.reset();

            for (auto* cue : cues)
                if (cue) cue->Dispose();
            for (auto* sb : soundBanks)
                if (sb) sb->Dispose();
            for (auto* wb : waveBanks)
                if (wb) wb->Dispose();

            if (disposing)
                Disposing.Raise(this, System::EventArgs::Empty);
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

        // P12-CATEGORY-001: matches FACTAudioEngine_SetVolume exactly (FACT.c) -- `vol` is a
        // MULTIPLIER on this category's own authored base volume (XgsCategory::volume), not a
        // raw overwrite (the bug this task fixes: the old code discarded the authored base
        // entirely). Cascades to every DIRECT child category (parentIndex == idx), recursively,
        // passing each child's own PRE-cascade categoryVolumes value as the new "vol" parameter
        // -- this is FACT's own real formula, not a simplification: a repeated SetVolume on an
        // ancestor compounds a descendant's own authored volume against itself on every cascade,
        // which looks unusual but is genuinely what real FACT does (replicated here rather than
        // "fixed", matching this project's established behavior-fidelity precedent for other
        // upstream FAudio quirks, e.g. handle_instance_limit's QUEUE/REPLACE_OLDEST collapse).
        xactImpl_->categoryVolumes[idx] = xactImpl_->xgs.categories[idx].volume * vol;

        // P9-CATEGORY-001: snapshot before iterating -- ApplyCategoryVolume() itself never
        // mutates activeCues today, but this stays consistent with the other three category
        // operations below (one of which has a real reentrant-mutation bug) rather than relying
        // on "this particular callee happens not to touch the registry" staying true forever.
        std::vector<Cue*> cues = xactImpl_->activeCues;
        for (auto* cue : cues)
            if (cue && cue->categoryIdx_ == idx)
                cue->ApplyCategoryVolume(xactImpl_->categoryVolumes[idx]);

        for (std::size_t i = 0; i < xactImpl_->xgs.categories.size(); ++i)
            if (xactImpl_->xgs.categories[i].parentIndex == idx)
                SetCategoryVolumeInternal(static_cast<unsigned short>(i), xactImpl_->categoryVolumes[i]);
    }

    void AudioEngine::PauseCategoryInternal(unsigned short idx)
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryPaused.size()) return;
        xactImpl_->categoryPaused[idx] = true;
        // P12-CATEGORY-001: matches FACTAudioEngine_Pause exactly (FACT.c) -- reaches every cue
        // whose OWN category is `idx` OR a DESCENDANT of `idx` (FACT_INTERNAL_IsInCategory walks
        // the cue's category up its parent chain), not just cues in the exact category `idx`.
        std::vector<Cue*> cues = xactImpl_->activeCues; // P9-CATEGORY-001, see SetCategoryVolumeInternal
        for (auto* cue : cues)
            if (cue && IsInCategory(xactImpl_->xgs.categories, cue->categoryIdx_, idx)
                    && cue->getIsPlayingProperty())
                cue->Pause();
    }

    void AudioEngine::ResumeCategoryInternal(unsigned short idx)
    {
        if (!xactImpl_ || idx >= xactImpl_->categoryPaused.size()) return;
        xactImpl_->categoryPaused[idx] = false;
        // P12-CATEGORY-001: see PauseCategoryInternal above -- same category-hierarchy reach.
        std::vector<Cue*> cues = xactImpl_->activeCues; // P9-CATEGORY-001, see SetCategoryVolumeInternal
        for (auto* cue : cues)
            if (cue && IsInCategory(xactImpl_->xgs.categories, cue->categoryIdx_, idx)
                    && cue->getIsPausedProperty())
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
        // P12-CATEGORY-001: see PauseCategoryInternal above -- same category-hierarchy reach.
        std::vector<Cue*> cues = xactImpl_->activeCues;
        for (auto* cue : cues)
            if (cue && IsInCategory(xactImpl_->xgs.categories, cue->categoryIdx_, idx))
                cue->Stop(opt);
    }

    // ── Category instance-limit enforcement ──────────────────────────────────

    AudioEngine::InstanceLimitDecision
    AudioEngine::CheckCategoryInstanceLimit(unsigned short idx, Cue* newCue) const
    {
        if (!xactImpl_ || idx >= xactImpl_->xgs.categories.size())
            return {true, false, 0};

        const CNA::Internal::Audio::XgsCategory& cat = xactImpl_->xgs.categories[idx];

        // Retain release tails in CNA's derived count. Native stored byte counters also
        // decrement unplayed prepared sounds and rejected plays; those lifecycle differences
        // remain open in docs/xact-instance-limits.md. Both exclude stopping victims.
        std::size_t liveCount = 0;
        for (const Cue* cue : xactImpl_->activeCues)
            if (cue && cue != newCue && cue->categoryIdx_ == idx
                && (cue->state_ == Cue::State::Playing || cue->state_ == Cue::State::Stopping))
                ++liveCount;

        if (liveCount < cat.instanceLimit)
            return {true, false, 0};

        // max_instance_behavior values (FACT_internal.h): 0=FAIL, 1=QUEUE, 2=REPLACE_OLDEST,
        // 3=REPLACE_QUIETEST, 4=REPLACE_LOWEST_PRIORITY.
        if (cat.maxInstanceBehavior == 0)
            return {false, true, 0}; // FAIL: reject the new cue outright, matching FACTCue_Stop(IMMEDIATE)

        // FACT.c:1164-1177 retains preparation order in the bank registry;
        // the engine registry above instead follows Play order.
        Cue* victim = nullptr;
        uint8_t victimPriority = 0xFF;
        for (Cue* cue : newCue->bank_->activeCues_)
        {
            if (!cue || cue == newCue || cue->categoryIdx_ != idx
                || cue->state_ != Cue::State::Playing)
                continue;

            if (cat.maxInstanceBehavior == 4) // REPLACE_LOWEST_PRIORITY
            {
                if (cue->priority_ < victimPriority)
                {
                    victim = cue;
                    victimPriority = cue->priority_;
                }
            }
            else if (cat.maxInstanceBehavior == 3) // Native QUIETEST stub: last eligible cue.
            {
                victim = cue;
            }
            else if (cat.maxInstanceBehavior == 1 || cat.maxInstanceBehavior == 2)
            {
                victim = cue;
                break;
            }
        }

        if (victim)
            victim->ForceFadeOutForInstanceLimit(cat.fadeOutMS);

        return {true, true, cat.fadeInMS};
    }

    AudioEngine::InstanceLimitDecision
    AudioEngine::CheckCueInstanceLimit(SoundBank* bank, unsigned short cueIndex, Cue* newCue) const
    {
        if (!xactImpl_ || !bank) return {true, false, 0};

        const CNA::Internal::Audio::XsbData* xsb = bank->GetXsbData();
        if (!xsb || cueIndex >= xsb->cues.size()) return {true, false, 0};

        const CNA::Internal::Audio::XsbCue& cueDef = xsb->cues[cueIndex];

        // Count this bank/definition's Playing and Stopping cues. This derived count retains
        // tails but does not reproduce native prepared/rejected-sound counter decrements.
        std::size_t liveCount = 0;
        for (const Cue* cue : xactImpl_->activeCues)
            if (cue && cue != newCue && cue->bank_ == bank && cue->cueIndex_ == cueIndex
                && (cue->state_ == Cue::State::Playing || cue->state_ == Cue::State::Stopping))
                ++liveCount;

        if (liveCount < cueDef.instanceLimit)
            return {true, false, 0};

        if (cueDef.maxInstanceBehavior == 0)
            return {false, true, 0}; // FAIL: reject the new cue outright, matching FACTCue_Stop(IMMEDIATE)

        // The bank's lifetime registry preserves native preparation order independently
        // of Play order; FACT_internal.c:558-564 also scopes this to the same definition.
        Cue* victim = nullptr;
        uint8_t victimPriority = 0xFF;
        for (Cue* cue : bank->activeCues_)
        {
            if (!cue || cue == newCue || cue->bank_ != bank || cue->cueIndex_ != cueIndex
                || cue->state_ != Cue::State::Playing)
                continue;

            if (cueDef.maxInstanceBehavior == 4) // REPLACE_LOWEST_PRIORITY
            {
                if (cue->priority_ < victimPriority)
                {
                    victim = cue;
                    victimPriority = cue->priority_;
                }
            }
            else if (cueDef.maxInstanceBehavior == 3) // Native QUIETEST stub: last eligible cue.
            {
                victim = cue;
            }
            else if (cueDef.maxInstanceBehavior == 1 || cueDef.maxInstanceBehavior == 2)
            {
                victim = cue;
                break;
            }
        }

        if (victim)
            victim->ForceFadeOutForInstanceLimit(cueDef.fadeOutMS);

        return {true, true, cueDef.fadeInMS};
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
        if (cue->bank_) cue->bank_->RefreshCueInstanceCount(cue->cueIndex_);
    }

    void AudioEngine::UnregisterCue(Cue* cue)
    {
        if (!xactImpl_ || !cue) return;
        auto& v = xactImpl_->activeCues;
        v.erase(std::remove(v.begin(), v.end(), cue), v.end());
        if (cue->bank_) cue->bank_->RefreshCueInstanceCount(cue->cueIndex_);
    }

    const CNA::Internal::Audio::XgsVariable* AudioEngine::FindVariable(const std::string& name) const
    {
        if (!xactImpl_) return nullptr;
        auto it = xactImpl_->xgs.variableNameMap.find(name);
        if (it == xactImpl_->xgs.variableNameMap.end()) return nullptr;
        return &xactImpl_->xgs.variables[it->second];
    }

    const CNA::Internal::Audio::XgsVariable* AudioEngine::FindVariableByIndex(
        SharpRuntime::ushortcs index) const
    {
        if (!xactImpl_ || static_cast<std::size_t>(index) >= xactImpl_->xgs.variables.size())
            return nullptr;
        return &xactImpl_->xgs.variables[index];
    }

    bool AudioEngine::IsValidVariableName(const std::string& name) const
    {
        const auto* v = FindVariable(name);
        return v && (v->accessibility & kAccessibilityPublic) && (v->accessibility & kAccessibilityCue);
    }

    AudioEngine::CueVariableInfo AudioEngine::GetCueVariableInfo(const std::string& name) const
    {
        const auto* v = FindVariable(name);
        if (!v || !(v->accessibility & kAccessibilityPublic) || !(v->accessibility & kAccessibilityCue))
            return {};
        return CueVariableInfo{
            true,
            (v->accessibility & kAccessibilityReadOnly) != 0,
            v->minValue, v->maxValue, v->initialValue
        };
    }

    bool AudioEngine::TryGetGlobalVariableValue(const std::string& name, float& outValue) const
    {
        const auto* v = FindVariable(name);
        if (!v || !(v->accessibility & kAccessibilityPublic) || (v->accessibility & kAccessibilityCue))
            return false;

        if (xactImpl_)
        {
            auto it = xactImpl_->globalVariables.find(name);
            if (it != xactImpl_->globalVariables.end())
            {
                outValue = it->second;
                return true;
            }
        }
        outValue = v->initialValue;
        return true;
    }

    std::size_t AudioEngine::ActiveCueCountForTest() const
    {
        return xactImpl_ ? xactImpl_->activeCues.size() : 0;
    }

    GetTypeNameCPP(AudioEngine, "Microsoft.Xna.Framework.Audio.AudioEngine")
}
