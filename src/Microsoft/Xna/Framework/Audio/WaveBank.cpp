#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Audio
{
    WaveBank::WaveBank(AudioEngine* audioEngine,
                       const std::string& nonStreamingWaveBankFilename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
        {
            throw std::invalid_argument("audioEngine must not be null");
        }
        if (nonStreamingWaveBankFilename.empty())
        {
            throw std::invalid_argument("filename must not be empty");
        }

        // XACT .XWB files are not parsed by the SDL3_mixer backend.
        (void)nonStreamingWaveBankFilename;
    }

    WaveBank::WaveBank(AudioEngine* audioEngine,
                       const std::string& streamingWaveBankFilename,
                       SharpRuntime::intcs /*offset*/,
                       SharpRuntime::shortcs /*packetSize*/)
        : WaveBank(audioEngine, streamingWaveBankFilename)
    {
    }

    WaveBank::~WaveBank()
    {
        Dispose();
    }

    bool WaveBank::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    bool WaveBank::getIsPreparedProperty() const
    {
        return !isDisposed_;
    }

    bool WaveBank::getIsInUseProperty() const
    {
        return false;
    }

    void WaveBank::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(WaveBank, "Microsoft::Xna::Framework::Audio::WaveBank")
}
