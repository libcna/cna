#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"

#include <stdexcept>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    AudioEngine::AudioEngine(const std::string& settingsFile)
        : AudioEngine(settingsFile, System::TimeSpan::Zero, {})
    {
    }

    AudioEngine::AudioEngine(const std::string& settingsFile,
                             System::TimeSpan /*lookAheadTime*/,
                             const std::string& /*rendererId*/)
    {
        if (settingsFile.empty())
        {
            throw std::invalid_argument("settingsFile must not be empty");
        }

        // XACT .XGS settings files are not parsed by the SDL3_mixer backend.
        // The engine initialises as a stub so that code using AudioEngine,
        // AudioCategory, SoundBank and WaveBank can compile and run without
        // crashing, but no XACT audio is produced.
        Init();
    }

    AudioEngine::~AudioEngine()
    {
        Dispose();
    }

    void AudioEngine::Init()
    {
        // Expose one fake renderer so callers can inspect RendererDetails.
        rendererDetails_.emplace_back("SDL3_mixer (stub)", "SDL3_mixer");
    }

    bool AudioEngine::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    const std::vector<RendererDetail>& AudioEngine::getRendererDetailsProperty() const
    {
        return rendererDetails_;
    }

    AudioCategory AudioEngine::GetCategory(const std::string& name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("name must not be empty");
        }
        if (isDisposed_)
        {
            throw std::runtime_error("AudioEngine is disposed");
        }

        // SDL3_mixer has no concept of named categories; return a stub that
        // tracks the name but has no actual mixing effect.
        static unsigned short nextIndex = 0;
        return AudioCategory(this, nextIndex++, name);
    }

    float AudioEngine::GetGlobalVariable(const std::string& name) const
    {
        if (name.empty())
        {
            throw std::invalid_argument("name must not be empty");
        }
        if (isDisposed_)
        {
            throw std::runtime_error("AudioEngine is disposed");
        }

        auto it = globalVariables_.find(name);
        if (it == globalVariables_.end())
        {
            throw std::runtime_error("Invalid global variable name: " + name);
        }
        return it->second;
    }

    void AudioEngine::SetGlobalVariable(const std::string& name, float value)
    {
        if (name.empty())
        {
            throw std::invalid_argument("name must not be empty");
        }
        if (isDisposed_)
        {
            throw std::runtime_error("AudioEngine is disposed");
        }

        globalVariables_[name] = value;
    }

    void AudioEngine::Update()
    {
        // XACT engine work loop — no-op on SDL3_mixer backend.
    }

    void AudioEngine::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            rendererDetails_.clear();
            globalVariables_.clear();
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(AudioEngine, "Microsoft::Xna::Framework::Audio::AudioEngine")
}
