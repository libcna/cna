#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Audio
{
    SoundBank::SoundBank(AudioEngine* audioEngine, const std::string& filename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
        {
            throw std::invalid_argument("audioEngine must not be null");
        }
        if (filename.empty())
        {
            throw std::invalid_argument("filename must not be empty");
        }

        // XACT .XSB files are not parsed by the SDL3_mixer backend.
        // The bank is initialised as a stub.
        (void)filename;
    }

    SoundBank::~SoundBank()
    {
        Dispose();
    }

    bool SoundBank::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    bool SoundBank::getIsInUseProperty() const
    {
        return false;
    }

    Cue* SoundBank::GetCue(const std::string& name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("name must not be empty");
        }
        if (isDisposed_)
        {
            throw std::runtime_error("SoundBank is disposed");
        }

        // Returns a stub Cue; no actual XACT sound is played.
        return new Cue(name, this);
    }

    void SoundBank::PlayCue(const std::string& name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("name must not be empty");
        }
        if (isDisposed_)
        {
            throw std::runtime_error("SoundBank is disposed");
        }

        // XACT cue playback is not supported on SDL3_mixer backend; no-op.
    }

    void SoundBank::PlayCue(const std::string& name,
                            const AudioListener& /*listener*/,
                            const AudioEmitter& /*emitter*/)
    {
        PlayCue(name);
    }

    void SoundBank::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(SoundBank, "Microsoft::Xna::Framework::Audio::SoundBank")
}
