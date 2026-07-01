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
    class WaveBank;

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
         * @throws System::ObjectDisposedException if the cue has been disposed.
         */
        void Apply3D(const AudioListener& listener, const AudioEmitter& emitter);

        /**
         * @brief Gets the value of a per-cue XACT variable.
         *
         * @param name Variable name as defined in the SoundBank, or one of the built-in
         *        3D variables ("Distance", "DopplerPitchScalar", "OrientationAngle").
         * @return Current value of the variable.
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::InvalidOperationException if @p name is not a valid variable name.
         */
        [[nodiscard]] float GetVariable(const std::string& name) const;

        /**
         * @brief Sets the value of a per-cue XACT variable.
         *
         * @param name  Variable name as defined in the SoundBank, or one of the built-in
         *        3D variables ("Distance", "DopplerPitchScalar", "OrientationAngle").
         * @param value New value.
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::InvalidOperationException if @p name is not a valid variable name.
         */
        void SetVariable(const std::string& name, float value);

        /**
         * @brief Starts playback of this cue.
         *
         * @throws System::ObjectDisposedException if the cue has been disposed.
         */
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

        GetTypeNameHPP()

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

        // WaveBanks this cue has registered with (see WaveBank::RegisterCue), so their
        // IsInUse can see this cue while it is playing; unregistered in StopInternal.
        std::vector<WaveBank*> waveBanksUsed_;

        void StopInternal(bool immediate);
    };
}
