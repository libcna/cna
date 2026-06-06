#pragma once

#include <string>
#include <unordered_map>

#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioListener;
    class SoundBank;

    /// Represents a named sound cue from a SoundBank.
    ///
    /// NOTE: XACT cue playback requires .XSB/.XWB files and is not supported by
    /// the SDL3_mixer backend. All playback methods are accepted without error
    /// but produce no audio output.
    class Cue final : public System::Object, public System::IDisposable
    {
    public:
        /// Raised when this cue is about to be disposed.
        System::EventHandler<System::EventArgs> Disposing;

        ~Cue() override;

        Cue(const Cue&) = delete;
        Cue& operator=(const Cue&) = delete;

        [[nodiscard]] bool getIsCreatedProperty()   const;
        [[nodiscard]] bool getIsDisposedProperty()  const;
        [[nodiscard]] bool getIsPausedProperty()    const;
        [[nodiscard]] bool getIsPlayingProperty()   const;
        [[nodiscard]] bool getIsPreparedProperty()  const;
        [[nodiscard]] bool getIsPreparingProperty() const;
        [[nodiscard]] bool getIsStoppedProperty()   const;
        [[nodiscard]] bool getIsStoppingProperty()  const;
        [[nodiscard]] const std::string& getNameProperty() const;

        void Apply3D(const AudioListener& listener, const AudioEmitter& emitter);

        [[nodiscard]] float GetVariable(const std::string& name) const;
        void SetVariable(const std::string& name, float value);

        void Play();
        void Pause();
        void Resume();
        void Stop(AudioStopOptions options);

        void Dispose() override;

    private:
        friend class SoundBank;
        Cue(std::string name, SoundBank* bank);

        enum class State { Created, Preparing, Prepared, Playing, Pausing, Paused, Stopping, Stopped };

        std::string name_;
        SoundBank*  bank_;
        State       state_      = State::Created;
        bool        isDisposed_ = false;
        std::unordered_map<std::string, float> variables_;

        GetTypeNameHPP()
    };
}
