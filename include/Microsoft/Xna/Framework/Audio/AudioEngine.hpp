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

    /// XACT audio engine. Parses .XGS global settings files and coordinates
    /// WaveBank, SoundBank and Cue objects.
    class AudioEngine : public System::Object, public System::IDisposable
    {
    public:
        static constexpr int ContentVersion = 46;

        System::EventHandler<System::EventArgs> Disposing;

        explicit AudioEngine(const std::string& settingsFile);
        AudioEngine(const std::string& settingsFile,
                    System::TimeSpan lookAheadTime,
                    const std::string& rendererId);

        ~AudioEngine() override;

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] const std::vector<RendererDetail>& getRendererDetailsProperty() const;

        [[nodiscard]] AudioCategory GetCategory(const std::string& name);
        [[nodiscard]] float GetGlobalVariable(const std::string& name) const;
        void SetGlobalVariable(const std::string& name, float value);

        void Update();
        void Dispose() override;

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

        GetTypeNameHPP()
    };
}
