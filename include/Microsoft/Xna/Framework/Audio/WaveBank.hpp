// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEngine;
    class SoundEffect;

    /** @brief Manages a bank of wave data loaded from a .XWB WaveBank file. */
    class WaveBank : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when the bank is disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /**
         * @brief Constructs a WaveBank by loading a non-streaming .XWB file.
         *
         * @param audioEngine                  The audio engine that owns this bank.
         * @param nonStreamingWaveBankFilename Path to the .XWB WaveBank file.
         */
        WaveBank(AudioEngine* audioEngine, const std::string& nonStreamingWaveBankFilename);

        /**
         * @brief Constructs a WaveBank for streaming from a .XWB file.
         *
         * @param audioEngine                The audio engine that owns this bank.
         * @param streamingWaveBankFilename  Path to the streaming .XWB WaveBank file.
         * @param offset                     Byte offset into the file where the wave data begins.
         * @param packetSize                 Size of streaming packets.
         */
        WaveBank(AudioEngine* audioEngine,
                 const std::string& streamingWaveBankFilename,
                 SharpRuntime::intcs offset,
                 SharpRuntime::shortcs packetSize);

        /** @brief Destroys the wave bank and releases all wave resources. */
        ~WaveBank() override;

        WaveBank(const WaveBank&) = delete;
        WaveBank& operator=(const WaveBank&) = delete;

        /**
         * @brief Gets whether this wave bank has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets whether this wave bank has finished loading and is ready for playback.
         *
         * @return true if prepared; otherwise false.
         */
        [[nodiscard]] bool getIsPreparedProperty() const;

        /**
         * @brief Gets whether any cue is currently using this wave bank.
         *
         * @return true if in use; otherwise false.
         */
        [[nodiscard]] bool getIsInUseProperty() const;

        /** @brief Releases this wave bank and all its wave data. */
        void Dispose() override;

    private:
        friend class AudioEngine;
        friend class Cue;

        AudioEngine* engine_;
        bool isDisposed_ = false;

        struct XactWaveBankImpl;
        std::unique_ptr<XactWaveBankImpl> xactImpl_;

        /** @brief Returns the bank name parsed from the .XWB file header. */
        const std::string& getBankName() const;

        /**
         * @brief Returns a SoundEffect for the given wave index (lazily created).
         *
         * Returns nullptr if the index is out of range or audio is unavailable.
         *
         * @param waveIndex Index into the wave bank.
         * @return Pointer to the SoundEffect, or nullptr.
         */
        const SoundEffect* GetSoundEffect(unsigned short waveIndex);

        GetTypeNameHPP()
    };
}
