#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

#include <utility>

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#endif

namespace Microsoft::Xna::Framework::Audio {

#ifdef SOUND_ENABLED
    namespace {
        MIX_Mixer* g_mixer = nullptr;

        MIX_Mixer* EnsureMixer()
        {
            if (!MIX_Init()) {
                return nullptr;
            }

            if (!g_mixer) {
                SDL_AudioSpec spec{};
                spec.format = SDL_AUDIO_S16;
                spec.channels = 2;
                spec.freq = 44100;

                g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
            }

            return g_mixer;
        }

        MIX_Track* AsTrack(void* p)
        {
            return static_cast<MIX_Track*>(p);
        }

        void ApplyTrackVolumeAndPan(MIX_Track* track, float volume, float masterVolume, float pan)
        {
            if (!track) {
                return;
            }

            const float gain = volume * masterVolume;
            MIX_SetTrackGain(track, gain);

            MIX_StereoGains stereo{};
            stereo.left = 1.0f;
            stereo.right = 1.0f;

            if (pan < 0.0f) {
                stereo.right = 1.0f + pan;
            } else if (pan > 0.0f) {
                stereo.left = 1.0f - pan;
            }

            MIX_SetTrackStereo(track, &stereo);
        }

        void DestroyTrackIfNeeded(void*& trackPtr)
        {
            MIX_Track* track = AsTrack(trackPtr);
            if (track) {
                MIX_StopTrack(track, 0);
                MIX_DestroyTrack(track);
                trackPtr = nullptr;
            }
        }
    }
#endif

    SoundEffectInstance::SoundEffectInstance()
        : soundEffect_(nullptr)
    {
    }

    SoundEffectInstance::SoundEffectInstance(const SoundEffect& soundEffect)
        : soundEffect_(&soundEffect)
    {
    }

    SoundEffectInstance::~SoundEffectInstance()
    {
#ifdef SOUND_ENABLED
        DestroyTrackIfNeeded(track_);
#endif
        playing_ = false;
        State_ = SoundState::Stopped;
    }

    SoundEffectInstance::SoundEffectInstance(SoundEffectInstance&& other) noexcept
        : soundEffect_(other.soundEffect_)
        , track_(other.track_)
        , playing_(other.playing_)
        , Volume_(other.Volume_)
        , Pan_(other.Pan_)
        , Pitch_(other.Pitch_)
        , IsLooped_(other.IsLooped_)
        , State_(other.State_)
    {
        other.soundEffect_ = nullptr;
        other.track_ = nullptr;
        other.playing_ = false;
        other.State_ = SoundState::Stopped;
    }

    SoundEffectInstance& SoundEffectInstance::operator=(SoundEffectInstance&& other) noexcept
    {
        if (this != &other) {
#ifdef SOUND_ENABLED
            DestroyTrackIfNeeded(track_);
#endif
            soundEffect_ = other.soundEffect_;
            track_ = other.track_;
            playing_ = other.playing_;
            Volume_ = other.Volume_;
            Pan_ = other.Pan_;
            Pitch_ = other.Pitch_;
            IsLooped_ = other.IsLooped_;
            State_ = other.State_;

            other.soundEffect_ = nullptr;
            other.track_ = nullptr;
            other.playing_ = false;
            other.State_ = SoundState::Stopped;
        }
        return *this;
    }

    void SoundEffectInstance::Play()
    {
#ifdef SOUND_ENABLED
        if (!soundEffect_) {
            State_ = SoundState::Stopped;
            playing_ = false;
            return;
        }

        MIX_Mixer* mixer = EnsureMixer();
        if (!mixer) {
            State_ = SoundState::Stopped;
            playing_ = false;
            return;
        }

        auto* audio = static_cast<MIX_Audio*>(soundEffect_->getNativeAudioHandle());
        if (!audio) {
            State_ = SoundState::Stopped;
            playing_ = false;
            return;
        }

        MIX_Track* track = AsTrack(track_);
        if (!track) {
            track = MIX_CreateTrack(mixer);
            if (!track) {
                State_ = SoundState::Stopped;
                playing_ = false;
                return;
            }
            track_ = track;
        }

        if (!MIX_SetTrackAudio(track, audio)) {
            State_ = SoundState::Stopped;
            playing_ = false;
            return;
        }

        ApplyTrackVolumeAndPan(track, Volume_, SoundEffect::getMasterVolumeProperty(), Pan_);

        if (Pitch_ != 0.0f) {
            const float ratio = (Pitch_ < 0.0f)
                ? (1.0f + Pitch_ * 0.5f)
                : (1.0f + Pitch_);
            MIX_SetTrackFrequencyRatio(track, ratio < 0.01f ? 0.01f : ratio);
        } else {
            MIX_SetTrackFrequencyRatio(track, 1.0f);
        }

        SDL_PropertiesID props = SDL_CreateProperties();
        if (props == 0) {
            State_ = SoundState::Stopped;
            playing_ = false;
            return;
        }

        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, IsLooped_ ? -1 : 0);

        const bool ok = MIX_PlayTrack(track, props);
        SDL_DestroyProperties(props);

        if (!ok) {
            State_ = SoundState::Stopped;
            playing_ = false;
            return;
        }

        playing_ = true;
        State_ = SoundState::Playing;
#else
        State_ = SoundState::Stopped;
        playing_ = false;
#endif
    }

    void SoundEffectInstance::Stop()
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track) {
            MIX_StopTrack(track, 0);
        }
#endif
        playing_ = false;
        State_ = SoundState::Stopped;
    }

    void SoundEffectInstance::Pause()
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track && State_ == SoundState::Playing) {
            MIX_PauseTrack(track);
            State_ = SoundState::Paused;
            playing_ = false;
        }
#endif
    }

    void SoundEffectInstance::Resume()
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track && State_ == SoundState::Paused) {
            MIX_ResumeTrack(track);
            State_ = SoundState::Playing;
            playing_ = true;
        }
#endif
    }

    float SoundEffectInstance::getVolumeProperty() const
    {
        return Volume_;
    }

    void SoundEffectInstance::setVolumeProperty(const float& volume)
    {
        Volume_ = (volume < 0.0f) ? 0.0f : ((volume > 1.0f) ? 1.0f : volume);

#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        ApplyTrackVolumeAndPan(track, Volume_, SoundEffect::getMasterVolumeProperty(), Pan_);
#endif
    }

    void SoundEffectInstance::setVolumeProperty(float&& volume)
    {
        setVolumeProperty(volume);
    }

    float SoundEffectInstance::getPanProperty() const
    {
        return Pan_;
    }

    void SoundEffectInstance::setPanProperty(const float& pan)
    {
        Pan_ = (pan < -1.0f) ? -1.0f : ((pan > 1.0f) ? 1.0f : pan);

#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        ApplyTrackVolumeAndPan(track, Volume_, SoundEffect::getMasterVolumeProperty(), Pan_);
#endif
    }

    void SoundEffectInstance::setPanProperty(float&& pan)
    {
        setPanProperty(pan);
    }

    float SoundEffectInstance::getPitchProperty() const
    {
        return Pitch_;
    }

    void SoundEffectInstance::setPitchProperty(const float& pitch)
    {
        Pitch_ = (pitch < -1.0f) ? -1.0f : ((pitch > 1.0f) ? 1.0f : pitch);

#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track) {
            const float ratio = (Pitch_ < 0.0f)
                ? (1.0f + Pitch_ * 0.5f)
                : (1.0f + Pitch_);
            MIX_SetTrackFrequencyRatio(track, ratio < 0.01f ? 0.01f : ratio);
        }
#endif
    }

    void SoundEffectInstance::setPitchProperty(float&& pitch)
    {
        setPitchProperty(pitch);
    }

    bool SoundEffectInstance::getIsLoopedProperty() const
    {
        return IsLooped_;
    }

    void SoundEffectInstance::setIsLoopedProperty(const bool& looped)
    {
        IsLooped_ = looped;

#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (track && (State_ == SoundState::Playing || State_ == SoundState::Paused)) {
            MIX_SetTrackLoops(track, IsLooped_ ? -1 : 0);
        }
#endif
    }

    void SoundEffectInstance::setIsLoopedProperty(bool&& looped)
    {
        setIsLoopedProperty(looped);
    }

    SoundState SoundEffectInstance::getStateProperty() const
    {
#ifdef SOUND_ENABLED
        MIX_Track* track = AsTrack(track_);
        if (!track) {
            return SoundState::Stopped;
        }
        if (MIX_TrackPaused(track)) {
            return SoundState::Paused;
        }
        if (MIX_TrackPlaying(track)) {
            return SoundState::Playing;
        }
        return SoundState::Stopped;
#else
        return State_;
#endif
    }

    GetTypeNameCPP(SoundEffectInstance, SoundEffectInstance)
}