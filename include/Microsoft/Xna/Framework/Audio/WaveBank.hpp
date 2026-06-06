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

    /// Manages a bank of wave data loaded from a .XWB WaveBank file.
    class WaveBank : public System::Object, public System::IDisposable
    {
    public:
        System::EventHandler<System::EventArgs> Disposing;

        WaveBank(AudioEngine* audioEngine, const std::string& nonStreamingWaveBankFilename);
        WaveBank(AudioEngine* audioEngine,
                 const std::string& streamingWaveBankFilename,
                 SharpRuntime::intcs offset,
                 SharpRuntime::shortcs packetSize);

        ~WaveBank() override;

        WaveBank(const WaveBank&) = delete;
        WaveBank& operator=(const WaveBank&) = delete;

        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] bool getIsPreparedProperty() const;
        [[nodiscard]] bool getIsInUseProperty() const;

        void Dispose() override;

    private:
        friend class AudioEngine;
        friend class Cue;

        AudioEngine* engine_;
        bool isDisposed_ = false;

        struct XactWaveBankImpl;
        std::unique_ptr<XactWaveBankImpl> xactImpl_;

        /// Returns the bank name parsed from the .XWB file header.
        const std::string& getBankName() const;

        /// Returns a SoundEffect for the given wave index (lazily created).
        /// Returns nullptr if the index is out of range or audio is unavailable.
        const SoundEffect* GetSoundEffect(unsigned short waveIndex);

        GetTypeNameHPP()
    };
}
