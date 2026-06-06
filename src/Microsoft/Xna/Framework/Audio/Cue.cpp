#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"

#include <stdexcept>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    Cue::Cue(std::string name, SoundBank* bank)
        : name_(std::move(name)), bank_(bank), state_(State::Prepared)
    {
    }

    Cue::~Cue()
    {
        Dispose();
    }

    bool Cue::getIsCreatedProperty() const
    {
        return !isDisposed_ && state_ == State::Created;
    }

    bool Cue::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    bool Cue::getIsPausedProperty() const
    {
        return !isDisposed_ && state_ == State::Paused;
    }

    bool Cue::getIsPlayingProperty() const
    {
        return !isDisposed_ && state_ == State::Playing;
    }

    bool Cue::getIsPreparedProperty() const
    {
        return !isDisposed_ && state_ == State::Prepared;
    }

    bool Cue::getIsPreparingProperty() const
    {
        return !isDisposed_ && state_ == State::Preparing;
    }

    bool Cue::getIsStoppedProperty() const
    {
        return isDisposed_ || state_ == State::Stopped;
    }

    bool Cue::getIsStoppingProperty() const
    {
        return !isDisposed_ && state_ == State::Stopping;
    }

    const std::string& Cue::getNameProperty() const
    {
        return name_;
    }

    void Cue::Apply3D(const AudioListener& /*listener*/, const AudioEmitter& /*emitter*/)
    {
        // SDL3_mixer has no XACT 3D cue support; no-op.
        if (isDisposed_)
        {
            throw std::runtime_error("Cue is disposed");
        }
    }

    float Cue::GetVariable(const std::string& name) const
    {
        if (name.empty())
        {
            throw std::invalid_argument("name must not be empty");
        }
        auto it = variables_.find(name);
        if (it == variables_.end())
        {
            throw std::runtime_error("Invalid variable name: " + name);
        }
        return it->second;
    }

    void Cue::SetVariable(const std::string& name, float value)
    {
        if (name.empty())
        {
            throw std::invalid_argument("name must not be empty");
        }
        variables_[name] = value;
    }

    void Cue::Play()
    {
        if (isDisposed_)
        {
            throw std::runtime_error("Cue is disposed");
        }
        // XACT cue playback is not supported on SDL3_mixer backend; state is
        // tracked so IsPlaying/IsStopped return consistent values.
        state_ = State::Playing;
    }

    void Cue::Pause()
    {
        if (isDisposed_) return;
        if (state_ == State::Playing)
        {
            state_ = State::Paused;
        }
    }

    void Cue::Resume()
    {
        if (isDisposed_) return;
        if (state_ == State::Paused)
        {
            state_ = State::Playing;
        }
    }

    void Cue::Stop(AudioStopOptions /*options*/)
    {
        if (isDisposed_) return;
        state_ = State::Stopped;
    }

    void Cue::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            state_      = State::Stopped;
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(Cue, "Microsoft::Xna::Framework::Audio::Cue")
}
