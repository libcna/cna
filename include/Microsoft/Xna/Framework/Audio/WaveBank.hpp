#pragma once

#include <string>

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEngine;

    /// Manages a bank of wave data loaded from a .XWB WaveBank file.
    ///
    /// NOTE: XACT .XWB files are not supported by the SDL3_mixer backend.
    /// Both constructors (in-memory and streaming) initialise as stubs.
    class WaveBank : public System::Object, public System::IDisposable
    {
    public:
        /// Raised when this bank is about to be disposed.
        System::EventHandler<System::EventArgs> Disposing;

        /// Constructs a non-streaming (in-memory) WaveBank.
        WaveBank(AudioEngine* audioEngine, const std::string& nonStreamingWaveBankFilename);

        /// Constructs a streaming WaveBank.
        WaveBank(AudioEngine* audioEngine,
                 const std::string& streamingWaveBankFilename,
                 SharpRuntime::intcs offset,
                 SharpRuntime::shortcs packetSize);

        ~WaveBank() override;

        WaveBank(const WaveBank&) = delete;
        WaveBank& operator=(const WaveBank&) = delete;

        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Always true on SDL3_mixer backend (stub is immediately prepared).
        [[nodiscard]] bool getIsPreparedProperty() const;

        /// Always false on SDL3_mixer backend (no cues play from this bank).
        [[nodiscard]] bool getIsInUseProperty() const;

        void Dispose() override;

    private:
        AudioEngine* engine_;
        bool isDisposed_ = false;

        GetTypeNameHPP()
    };
}
