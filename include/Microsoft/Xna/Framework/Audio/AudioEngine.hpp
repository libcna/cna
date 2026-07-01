// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioCategory;
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
        static constexpr int ContentVersion = 46;

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
         * @param settingsFile    Path to the .XGS XACT global settings file.
         * @param lookAheadTime   Scheduling look-ahead duration.
         * @param rendererId      Renderer identifier string, or empty for the default renderer.
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

    private:
        friend class AudioCategory;
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

        // Category state access
        float GetCategoryVolume(unsigned short idx) const;
        bool  IsCategoryPaused(unsigned short idx) const;

        // Category mutations (called by AudioCategory)
        void SetCategoryVolumeInternal(unsigned short idx, float vol);
        void PauseCategoryInternal(unsigned short idx);
        void ResumeCategoryInternal(unsigned short idx);
        void StopCategoryInternal(unsigned short idx, bool immediate);

        // Active-cue registration for category operations
        void RegisterCue(Cue* cue);
        void UnregisterCue(Cue* cue);

        // Used by Cue::GetVariable/SetVariable to validate a per-cue-instance variable
        // name against the engine's parsed global variable set (XACT per-cue variables
        // are the same named global variables, just individually overridable per cue).
        bool IsValidVariableName(const std::string& name) const;
    };
}
