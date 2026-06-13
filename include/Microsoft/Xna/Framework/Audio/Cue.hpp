// SPDX-License-Identifier: MS-PL
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

    /** @brief Represents a named sound cue loaded from a SoundBank. */
    class Cue final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when this cue is disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /** @brief Destroys the cue and releases its playback instance. */
        ~Cue() override;

        Cue(const Cue&) = delete;
        Cue& operator=(const Cue&) = delete;

        /** @brief Gets whether the cue has been created but not yet prepared. */
        [[nodiscard]] bool getIsCreatedProperty()   const;

        /** @brief Gets whether the cue has been disposed. */
        [[nodiscard]] bool getIsDisposedProperty()  const;

        /** @brief Gets whether the cue is currently paused. */
        [[nodiscard]] bool getIsPausedProperty()    const;

        /** @brief Gets whether the cue is currently playing. */
        [[nodiscard]] bool getIsPlayingProperty()   const;

        /** @brief Gets whether the cue is prepared and ready to play. */
        [[nodiscard]] bool getIsPreparedProperty()  const;

        /** @brief Gets whether the cue is in the process of being prepared. */
        [[nodiscard]] bool getIsPreparingProperty() const;

        /** @brief Gets whether the cue is fully stopped. */
        [[nodiscard]] bool getIsStoppedProperty()   const;

        /** @brief Gets whether the cue is in the process of stopping. */
        [[nodiscard]] bool getIsStoppingProperty()  const;

        /**
         * @brief Gets the name of this cue as defined in the SoundBank.
         *
         * @return Cue name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Applies 3D positional audio settings to this cue.
         *
         * @param listener Position and orientation of the audio listener.
         * @param emitter  Position and orientation of the sound emitter.
         */
        void Apply3D(const AudioListener& listener, const AudioEmitter& emitter);

        /**
         * @brief Gets the value of a per-cue XACT variable.
         *
         * @param name Variable name as defined in the SoundBank.
         * @return Current value of the variable.
         */
        [[nodiscard]] float GetVariable(const std::string& name) const;

        /**
         * @brief Sets the value of a per-cue XACT variable.
         *
         * @param name  Variable name.
         * @param value New value.
         */
        void SetVariable(const std::string& name, float value);

        /** @brief Starts playback of this cue. */
        void Play();

        /** @brief Pauses playback of this cue. */
        void Pause();

        /** @brief Resumes a paused cue. */
        void Resume();

        /**
         * @brief Stops playback of this cue.
         *
         * @param options Whether to stop immediately or after release phases finish.
         */
        void Stop(AudioStopOptions options);

        /** @brief Releases this cue and all associated playback resources. */
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
