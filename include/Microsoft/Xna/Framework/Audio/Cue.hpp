#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
    class SoundEffectInstance;

    /// Represents a named sound cue from a SoundBank.
    class Cue final : public System::Object, public System::IDisposable
    {
    public:
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
        friend class AudioEngine;
        Cue(std::string name, SoundBank* bank, uint16_t cueIndex);

        enum class State { Created, Preparing, Prepared, Playing, Pausing, Paused, Stopping, Stopped };

        std::string name_;
        SoundBank*  bank_;
        uint16_t    cueIndex_   = 0xFFFF;
        uint16_t    categoryIdx_= 0xFFFF;
        State       state_      = State::Created;
        bool        isDisposed_ = false;

        std::unordered_map<std::string, float> variables_;

        struct PlaybackInstance
        {
            std::unique_ptr<SoundEffectInstance> instance;
        };
        std::vector<PlaybackInstance> active_;

        void StopInternal(bool immediate);

        GetTypeNameHPP()
    };
}
