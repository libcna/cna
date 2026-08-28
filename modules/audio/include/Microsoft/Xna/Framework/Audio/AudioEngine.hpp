// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal::Audio { struct XgsRpc; struct XgsVariable; }

namespace Microsoft::Xna::Framework::Audio
{
    struct AudioCategory;
    class WaveBank;
    class SoundBank;
    class Cue;

    /**
     * @brief XACT audio engine that parses .XGS global settings files and coordinates
     * WaveBank, SoundBank, and Cue objects.
     */
    class AudioEngine : public System::Object, public System::IDisposable
    {
    public:
        /** @brief XACT content version this engine targets. */
        static constexpr SharpRuntime::intcs ContentVersion = 46;

        /** @brief Raised when the engine is about to be disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /**
         * @brief Constructs an AudioEngine from a global settings file.
         *
         * @param settingsFile Path to the .XGS XACT global settings file.
         */
        explicit AudioEngine(const std::string& settingsFile);

        /**
         * @brief Constructs an AudioEngine with explicit look-ahead time and renderer selection.
         *
         * CNA has a single backend (SDL3_mixer), so @p lookAheadTime and @p rendererId are
         * accepted for API compatibility but currently have no effect — any value (including
         * an unrecognized renderer ID) behaves identically to the single-argument constructor.
         *
         * @param settingsFile    Path to the .XGS XACT global settings file.
         * @param lookAheadTime   Scheduling look-ahead duration. Currently unused.
         * @param rendererId      Renderer identifier string, or empty for the default renderer.
         *                        Currently unused; any value is accepted.
         */
        AudioEngine(const std::string& settingsFile,
                    System::TimeSpan lookAheadTime,
                    const std::string& rendererId);

        /** @brief Destroys the audio engine and releases all XACT resources. */
        ~AudioEngine() override;

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        /**
         * @brief Gets whether this engine has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the list of available audio renderer devices.
         *
         * @return Read-only reference to the renderer detail list.
         */
        [[nodiscard]] const std::vector<RendererDetail>& getRendererDetailsProperty() const;

        /**
         * @brief Returns the AudioCategory with the given name.
         *
         * @param name Category name as defined in the .XGS file.
         * @return The matching AudioCategory.
         */
        [[nodiscard]] AudioCategory GetCategory(const std::string& name);

        /**
         * @brief Gets the value of a global XACT variable.
         *
         * @param name Variable name as defined in the .XGS file.
         * @return Current value of the variable.
         */
        [[nodiscard]] float GetGlobalVariable(const std::string& name) const;

        /**
         * @brief Sets the value of a global XACT variable.
         *
         * @param name  Variable name.
         * @param value New value.
         */
        void SetGlobalVariable(const std::string& name, float value);

        /** @brief Processes pending XACT notifications and updates internal state. */
        void Update();

        /** @brief Releases all audio resources and marks the engine disposed. */
        void Dispose() override;

        GetTypeNameHPP()

        // P9-CATEGORY-005/006/007/008/011: result of a category- or cue-level instanceLimit
        // check (Cue::Play() calls CheckCategoryInstanceLimit()/CheckCueInstanceLimit() below).
        // `allowed` is false only for maxInstanceBehavior == FAIL once the category/cue is
        // already at its instanceLimit. `triggered` is true whenever the limit was actually at
        // or past capacity (whether or not that resulted in an eviction) -- matching
        // FACT_internal.c's play_sound(), where a triggered check unconditionally overwrites
        // fade_in_ms with the (possibly zero) authored fadeInMS, discarding whatever an earlier
        // check set it to; `fadeInMS` is only meaningful when `triggered` is true.
        CNAEXT struct InstanceLimitDecision
        {
            bool     allowed;
            bool     triggered;
            uint16_t fadeInMS;
        };

    private:
        friend struct AudioCategory;
        friend class WaveBank;
        friend class SoundBank;
        friend class Cue;

        struct XactEngineImpl;
        std::unique_ptr<XactEngineImpl> xactImpl_;

        bool isDisposed_ = false;
        std::vector<RendererDetail> rendererDetails_;

        void Init(const std::string& settingsFile);

        // WaveBank registry (called by WaveBank constructor/destructor)
        void RegisterWaveBank(WaveBank* wb);
        void UnregisterWaveBank(WaveBank* wb);
        WaveBank* FindWaveBank(const std::string& bankName) const;

        // SoundBank registry (called by SoundBank constructor/destructor), symmetric to the
        // WaveBank registry above -- lets Dispose() cascade to every SoundBank it created,
        // matching FNA's native OnXACTNotification(SOUNDBANKDESTROYED) (XA-8).
        void RegisterSoundBank(SoundBank* sb);
        void UnregisterSoundBank(SoundBank* sb);

        // Category state access
        float GetCategoryVolume(unsigned short idx) const;
        bool  IsCategoryPaused(unsigned short idx) const;

        // Category mutations (called by AudioCategory)
        void SetCategoryVolumeInternal(unsigned short idx, float vol);
        void PauseCategoryInternal(unsigned short idx);
        void ResumeCategoryInternal(unsigned short idx);
        void StopCategoryInternal(unsigned short idx, bool immediate);

        // P9-CATEGORY-005/006/007/008: called by Cue::Play() right after it resolves the new
        // cue's category/priority. Counts this category's currently live (Playing or Stopping,
        // i.e. not yet actually destroyed) cues against XgsCategory::instanceLimit; if at the
        // limit, applies XgsCategory::maxInstanceBehavior (see AudioEngine.cpp for the exact
        // FACT_internal.c-matched semantics of each value) and may fade out a victim cue via
        // Cue::ForceFadeOutForInstanceLimit(). `newCue` is excluded from its own count (it isn't
        // registered into activeCues yet at the point Play() calls this).
        InstanceLimitDecision CheckCategoryInstanceLimit(unsigned short idx, Cue* newCue) const;

        // P9-CATEGORY-011: called by Cue::Play() before CheckCategoryInstanceLimit() above,
        // matching FACT_internal.c's play_sound() checking cue->data->instanceLimit before
        // sound->sound->category's -- same shape as the category check, but scoped to this
        // specific cue definition (same SoundBank + cueIndex) for the *count*, matching
        // FACTCueData::instanceCount. The *victim search*, when eviction is needed, is scoped
        // only to the same SoundBank (matching FAudio's handle_instance_limit(cue, NULL), which
        // iterates cue->parentBank->cueList with no category or cue-definition filter at all --
        // a real FAudio quirk, replicated here rather than "fixed", see AudioEngine.cpp).
        InstanceLimitDecision CheckCueInstanceLimit(SoundBank* bank, unsigned short cueIndex, Cue* newCue) const;

        // Active-cue registration for category operations
        void RegisterCue(Cue* cue);
        void UnregisterCue(Cue* cue);

        // P12-VAR-001: true only for a variable whose authored XgsVariable::accessibility has
        // BOTH the PUBLIC and CUE bits set -- matches FACTCue_GetVariableIndex exactly (FACT.c).
        // This is a genuinely SEPARATE variable domain from GetGlobalVariable/SetGlobalVariable
        // below (which require PUBLIC set and CUE clear, matching the complementary
        // FACTAudioEngine_GetGlobalVariableIndex) -- a variable is either engine-global or
        // cue-scoped, never both, in real XACT/FACT. Used by Cue::GetVariable/SetVariable's
        // fallback (via GetCueVariableInfo below), not GetGlobalVariable/SetGlobalVariable.
        bool IsValidVariableName(const std::string& name) const;

        // P12-VAR-001: everything Cue::GetVariable/SetVariable need to correctly fall back to a
        // cue-scoped (PUBLIC+CUE) variable declared in the engine's XGS data, without exposing
        // XgsVariable's definition to Cue.cpp (XactEngineImpl, and therefore the parsed
        // CNA::Internal::Audio::XgsVariable objects it holds, is only fully defined in
        // AudioEngine.cpp). `valid` is false for anything that isn't a PUBLIC+CUE variable
        // (matches IsValidVariableName above). `initialValue` is what a fresh Cue instance that
        // has never explicitly SetVariable()-d this name should read back -- matches real FACT's
        // per-cue `variableValues[i]` array, which every FACTCue_Create initializes to
        // `engine->variables[i].initialValue` (FACT.c) -- CNA's own `Cue::variables_` is a sparse
        // override map instead of a dense per-variable array, so this is the fallback for "not
        // yet overridden on this specific cue instance" rather than a literal port of that array.
        struct CueVariableInfo
        {
            bool  valid        = false;
            bool  readOnly     = false;
            float minValue     = 0.0f;
            float maxValue     = 0.0f;
            float initialValue = 0.0f;
        };
        CueVariableInfo GetCueVariableInfo(const std::string& name) const;

        // P12-VAR-001: non-throwing counterpart to GetGlobalVariable above, used internally by
        // Cue::GetVariableForRpc for the one case GetCueVariableInfo doesn't cover -- an RPC
        // curve or INTERACTIVE variation table bound to an engine-GLOBAL (not cue-scoped)
        // variable. Returns false (leaving outValue untouched) for anything that isn't a valid
        // PUBLIC, non-CUE variable name -- same gate as GetGlobalVariable, just non-throwing.
        bool TryGetGlobalVariableValue(const std::string& name, float& outValue) const;

        // P12-VAR-001: resolves a name to its parsed XgsVariable (accessibility/min/max/initial),
        // or nullptr if not declared at all. Shared lookup behind IsValidVariableName/
        // GetCueVariableInfo/GetGlobalVariable/SetGlobalVariable above.
        const CNA::Internal::Audio::XgsVariable* FindVariable(const std::string& name) const;

        // Resolves an interactive variation table's variable index (XsbVariation::variable,
        // parsed straight from the .xsb) to its declared name in the engine's XGS variable
        // table, or nullptr if out of range. Used by Cue::Play() to evaluate INTERACTIVE
        // (type==3) variation-table selection by reusing Cue::GetVariable()'s existing
        // cue-local-then-global fallback instead of duplicating it (P9-XACT-003).
        const std::string* GetVariableNameByIndex(SharpRuntime::shortcs index) const;

        // Resolves an RPC code (absolute XGS-file byte offset, from XsbSound::rpcCodes) to its
        // parsed curve, or nullptr if not found. Used by Cue::Play() for one-shot RPC
        // volume/pitch evaluation (P9-XACT-006/007).
        const CNA::Internal::Audio::XgsRpc* FindRpcByCode(SharpRuntime::uintcs code) const;

        // P9-LIFECYCLE-012: exposes the active-cue registry's size to AudioEngineTestAccess so a
        // regression test can prove repeated category operations never duplicate an
        // already-registered cue. XactEngineImpl (and therefore activeCues) is only defined in
        // AudioEngine.cpp, so a test-only friend struct can't read it directly the way
        // SoundBankTestAccess reads SoundBank::fireAndForget_.
        CNAEXT [[nodiscard]] std::size_t ActiveCueCountForTest() const;

        CNAEXT friend struct AudioEngineTestAccess;
    };
}
