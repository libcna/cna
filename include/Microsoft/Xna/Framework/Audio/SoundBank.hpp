#pragma once

#include <memory>
#include <string>

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioEngine;
    class AudioListener;
    class Cue;

    /// Manages a collection of named cues loaded from a .XSB SoundBank file.
    ///
    /// NOTE: XACT .XSB files are not supported by the SDL3_mixer backend.
    /// The bank initialises as a stub; GetCue() returns stubs and PlayCue()
    /// is a no-op.
    class SoundBank : public System::Object, public System::IDisposable
    {
    public:
        /// Raised when this bank is about to be disposed.
        System::EventHandler<System::EventArgs> Disposing;

        SoundBank(AudioEngine* audioEngine, const std::string& filename);
        ~SoundBank() override;

        SoundBank(const SoundBank&) = delete;
        SoundBank& operator=(const SoundBank&) = delete;

        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Always false on SDL3_mixer backend (no cues actually play).
        [[nodiscard]] bool getIsInUseProperty() const;

        /// Returns a stub Cue for the given name.
        [[nodiscard]] Cue* GetCue(const std::string& name);

        /// No-op on SDL3_mixer backend.
        void PlayCue(const std::string& name);

        /// No-op on SDL3_mixer backend (3D audio not applied).
        void PlayCue(const std::string& name,
                     const AudioListener& listener,
                     const AudioEmitter& emitter);

        void Dispose() override;

    private:
        AudioEngine* engine_;
        bool isDisposed_ = false;

        GetTypeNameHPP()
    };
}
