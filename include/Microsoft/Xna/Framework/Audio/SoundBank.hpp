// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

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

    /** @brief Manages a collection of named cues loaded from a .XSB SoundBank file. */
    class SoundBank : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when the bank is disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /**
         * @brief Constructs a SoundBank by loading a .XSB file with the given audio engine.
         *
         * @param audioEngine The audio engine that owns this bank.
         * @param filename    Path to the .XSB SoundBank file.
         */
        SoundBank(AudioEngine* audioEngine, const std::string& filename);

        /** @brief Destroys the sound bank and releases all held cue resources. */
        ~SoundBank() override;

        SoundBank(const SoundBank&) = delete;
        SoundBank& operator=(const SoundBank&) = delete;

        /**
         * @brief Gets whether this sound bank has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets whether this sound bank is still in use by active cues.
         *
         * @return true if any cue from this bank is still active; otherwise false.
         */
        [[nodiscard]] bool getIsInUseProperty() const;

        /**
         * @brief Returns a new Cue instance for the cue with the given name.
         *
         * @param name Cue name as defined in the .XSB file.
         * @return Pointer to the new Cue (caller is responsible for disposal).
         */
        [[nodiscard]] Cue* GetCue(const std::string& name);

        /**
         * @brief Plays the named cue as a fire-and-forget sound.
         *
         * @param name Cue name as defined in the .XSB file.
         */
        void PlayCue(const std::string& name);

        /**
         * @brief Plays the named cue as a fire-and-forget sound with 3D positioning.
         *
         * @param name     Cue name as defined in the .XSB file.
         * @param listener Position and orientation of the audio listener.
         * @param emitter  Position and orientation of the sound emitter.
         */
        void PlayCue(const std::string& name,
                     const AudioListener& listener,
                     const AudioEmitter& emitter);

        /** @brief Releases the sound bank and all its cue resources. */
        void Dispose() override;

    private:
        friend class Cue;

        AudioEngine* engine_;
        bool isDisposed_ = false;

        struct XactSoundBankImpl;
        std::unique_ptr<XactSoundBankImpl> xactImpl_;

        const CNA::Internal::Audio::XsbData* GetXsbData() const;

        // Fire-and-forget cues from PlayCue() — kept alive until the sound
        // duration has elapsed, then swept on the next PlayCue() call.
        struct FireAndForget
        {
            std::unique_ptr<Cue> cue;
            std::chrono::steady_clock::time_point created;
        };
        std::vector<FireAndForget> fireAndForget_;

        GetTypeNameHPP()
    };
}
