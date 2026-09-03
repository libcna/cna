// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "TitleContentBytes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <exception>
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
        // and SDL3_mixer has no FACT-style scheduling look-ahead to configure (plans/plan_audio.md XA-4).
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
        std::vector<uint8_t> data =
            CNA::Internal::Audio::ReadTitleContentBytes(settingsFile);

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

        // P12-VAR-001: matches FACTAudioEngine_GetGlobalVariableIndex exactly (FACT.c) --
        // PUBLIC must be set AND CUE must be clear. A CUE-scoped variable is a genuinely
        // different domain (only reachable via Cue::GetVariable/SetVariable, GetCueVariableInfo
        // below), not just a stricter check on the same one.
        const auto* v = FindVariable(name);
        if (!v || !(v->accessibility & kAccessibilityPublic) || (v->accessibility & kAccessibilityCue))
            throw System::InvalidOperationException("Invalid variable name!");

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
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("AudioEngine");

        // P12-VAR-001: same name-validity gate as GetGlobalVariable above.
        const auto* v = FindVariable(name);
        if (!v || !(v->accessibility & kAccessibilityPublic) || (v->accessibility & kAccessibilityCue))
            throw System::InvalidOperationException("Invalid variable name!");

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
        for (auto* cue : xactImpl_->activeCues)
            if (cue) cue->ReconcileState();
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

        // P9-CATEGORY-005: matches FACT_internal.c's play_sound() -- category->instanceCount is
        // incremented when a sound starts and only decremented once its instance is actually
        // destroyed (FACT_INTERNAL_DestroySound), so a cue still fading out its release tail
        // (State::Stopping) still counts here, even though it's excluded from the victim search
        // below (handle_instance_limit()'s cursor loop skips FACT_STATE_STOPPING/STOPPED cues).
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

        // P9-CATEGORY-010: FAudio's own handle_instance_limit() (FACT_internal.c) treats QUEUE
        // and REPLACE_OLDEST identically (its own source carries a "FIXME: How does QUEUE differ
        // from REPLACE_OLDEST?" comment) and its REPLACE_QUIETEST branch is an unfinished stub
        // that -- despite the name -- just keeps overwriting `replaced` with whatever cue it last
        // saw, i.e. behaves exactly like REPLACE_OLDEST too. CNA matches FAudio's real shipped
        // behavior rather than implementing a "more correct" quietest search FAudio itself never
        // does: all three collapse to "evict the oldest still-Playing cue in this category" here.
        // REPLACE_LOWEST_PRIORITY is the one behavior FAudio fully implements, so CNA does too.
        Cue* victim = nullptr;
        uint8_t victimPriority = 0xFF;
        for (Cue* cue : xactImpl_->activeCues)
        {
            if (!cue || cue == newCue || cue->categoryIdx_ != idx || cue->state_ != Cue::State::Playing)
                continue;

            if (cat.maxInstanceBehavior == 4) // REPLACE_LOWEST_PRIORITY
            {
                if (!victim || cue->priority_ < victimPriority)
                {
                    victim = cue;
                    victimPriority = cue->priority_;
                }
            }
            else // QUEUE / REPLACE_OLDEST / REPLACE_QUIETEST
            {
                // activeCues is append-ordered (RegisterCue only ever push_back()s), so the
                // first same-category match still Playing is the oldest one.
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

        // P9-CATEGORY-011: matches FACT_internal.c's play_sound() -- cue->data->instanceCount is
        // this specific cue definition's own live-instance count (same SoundBank + same cue
        // index), incremented per Play() and only decremented once a sound instance is actually
        // destroyed -- so, same as the category check above, a Stopping cue still counts.
        std::size_t liveCount = 0;
        for (const Cue* cue : xactImpl_->activeCues)
            if (cue && cue != newCue && cue->bank_ == bank && cue->cueIndex_ == cueIndex
                && (cue->state_ == Cue::State::Playing || cue->state_ == Cue::State::Stopping))
                ++liveCount;

        if (liveCount < cueDef.instanceLimit)
            return {true, false, 0};

        if (cueDef.maxInstanceBehavior == 0)
            return {false, true, 0}; // FAIL: reject the new cue outright, matching FACTCue_Stop(IMMEDIATE)

        // P9-CATEGORY-011: unlike the category-level victim search above, FAudio's own
        // handle_instance_limit(cue, NULL) (FACT_internal.c) applies NO category filter and NO
        // same-cue-definition filter when the *cue-level* limit is what triggered -- its
        // `if (category && ...)` guard is unconditionally false here since it's called with a
        // NULL category, so the search scans every live cue in the whole SoundBank
        // (cue->parentBank->cueList) and may evict a cue from a completely different definition
        // or category. This looks like a genuine oversight in FAudio (a cue-level instanceLimit
        // conceptually ought to only compete against other instances of the same named cue), but
        // CNA matches it exactly per this project's behavior-fidelity mandate instead of
        // "fixing" upstream FAudio's own shipped behavior.
        Cue* victim = nullptr;
        uint8_t victimPriority = 0xFF;
        for (Cue* cue : xactImpl_->activeCues)
        {
            if (!cue || cue == newCue || cue->bank_ != bank || cue->state_ != Cue::State::Playing)
                continue;

            if (cueDef.maxInstanceBehavior == 4) // REPLACE_LOWEST_PRIORITY
            {
                if (!victim || cue->priority_ < victimPriority)
                {
                    victim = cue;
                    victimPriority = cue->priority_;
                }
            }
            else // QUEUE / REPLACE_OLDEST / REPLACE_QUIETEST -- see CheckCategoryInstanceLimit()
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
    }

    void AudioEngine::UnregisterCue(Cue* cue)
    {
        if (!xactImpl_ || !cue) return;
        auto& v = xactImpl_->activeCues;
        v.erase(std::remove(v.begin(), v.end(), cue), v.end());
    }

    const CNA::Internal::Audio::XgsVariable* AudioEngine::FindVariable(const std::string& name) const
    {
        if (!xactImpl_) return nullptr;
        auto it = xactImpl_->xgs.variableNameMap.find(name);
        if (it == xactImpl_->xgs.variableNameMap.end()) return nullptr;
        return &xactImpl_->xgs.variables[it->second];
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
