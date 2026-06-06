#pragma once

#include <memory>
#include <string>

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace CNA::Internal::Audio { struct XsbData; }

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioEngine;
    class AudioListener;
    class Cue;

    /// Manages a collection of named cues loaded from a .XSB SoundBank file.
    class SoundBank : public System::Object, public System::IDisposable
    {
    public:
        System::EventHandler<System::EventArgs> Disposing;

        SoundBank(AudioEngine* audioEngine, const std::string& filename);
        ~SoundBank() override;

        SoundBank(const SoundBank&) = delete;
        SoundBank& operator=(const SoundBank&) = delete;

        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] bool getIsInUseProperty() const;

        [[nodiscard]] Cue* GetCue(const std::string& name);

        void PlayCue(const std::string& name);
        void PlayCue(const std::string& name,
                     const AudioListener& listener,
                     const AudioEmitter& emitter);

        void Dispose() override;

    private:
        friend class Cue;

        AudioEngine* engine_;
        bool isDisposed_ = false;

        struct XactSoundBankImpl;
        std::unique_ptr<XactSoundBankImpl> xactImpl_;

        const CNA::Internal::Audio::XsbData* GetXsbData() const;

        GetTypeNameHPP()
    };
}
