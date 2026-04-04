//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iostream>
#include <ostream>
#include <SDL3/SDL_log.h>
#include <SDL3_mixer/SDL_mixer.h>


namespace Microsoft::Xna::Framework::Audio {

    IMPL_PROP(float, Volume, getter1, setter0, member0, static0, constret1, ref1, constmet1, SoundEffectInstance, nothing)
    IMPL_PROP(float, Pan, getter1, setter0, member0, static0, constret1, ref1, constmet1, SoundEffectInstance, nothing)
    IMPL_PROP(float, Pitch, getter1, setter1, member0, static0, constret1, ref1, constmet1, SoundEffectInstance, nothing)
    IMPL_PROP(bool, IsLooped, getter1, setter0, member0, static0, constret1, ref1, constmet1, SoundEffectInstance, nothing)

    SoundEffectInstance::~SoundEffectInstance() {
        Stop();
    }

    SoundEffectInstance::SoundEffectInstance(Audio::SoundEffectI *sound_effect): Volume_(1.0f), Pan_(0.0f), Pitch_(0.0f),
                                                                        IsLooped_(false), soundEffect(sound_effect) {
    }

    void SoundEffectInstance::Play() {
#ifdef SOUND_ENABLED
        if (playing) return;

        int loops = IsLooped_ ? -1 : 0;
        channel = Mix_PlayChannel(-1, soundEffect->GetChunk(), loops);
        if (channel == -1) {
            std::cerr << "Mix_PlayChannel failed: " << SDL_GetError() << std::endl;
            State = SoundState::Stopped;
            return;
        }

        // Setting volume (0–128)
        Mix_Volume(channel, static_cast<int>(Volume_ * 128));

        // Set position - pan (0=left, 255=right)
        // SDL_mixer supports stereo panning via Mix_SetPanning
        // Pan_ range from -1.0 (left) to 1.0 (right)
        Uint8 left = 255;
        Uint8 right = 255;
        if (Pan_ < 0.0f) {
            right = static_cast<Uint8>(255 * (1.0f + Pan_));
        } else if (Pan_ > 0.0f) {
            left = static_cast<Uint8>(255 * (1.0f - Pan_));
        }
        Mix_SetPanning(channel, left, right);

        playing = true;
        State = SoundState::Playing;
#endif
    }

    void SoundEffectInstance::Stop() {
#ifdef SOUND_ENABLED
        if (!playing) return;

        if (channel != -1) {
            Mix_HaltChannel(channel);
            channel = -1;
        }

        playing = false;
        State = SoundState::Stopped;
#endif
    }

    void SoundEffectInstance::setVolumeProperty(const float& volume) {
        Volume_ = volume < 0.f ? 0.f : (volume > 1.f ? 1.f : volume);
        if (channel != -1) {
            Mix_Volume(channel, static_cast<int>(Volume_ * 128));
        }
    }

    void SoundEffectInstance::setVolumeProperty(float&&) {
        throw std::runtime_error("setVolumeProperty(float&&) not implemented");
    }

    void SoundEffectInstance::setPanProperty(const float& pan) {
        Pan_ = pan < -1.f ? -1.f : (pan > 1.f ? 1.f : pan);
        if (channel != -1) {
            Uint8 left = 255;
            Uint8 right = 255;
            if (Pan_ < 0.0f) {
                right = static_cast<Uint8>(255 * (1.0f + Pan_));
            } else if (Pan_ > 0.0f) {
                left = static_cast<Uint8>(255 * (1.0f - Pan_));
            }
            Mix_SetPanning(channel, left, right);
        }
    }
    void SoundEffectInstance::setPanProperty(float&&) {
        throw std::runtime_error("setPanProperty(float&&) not implemented");
    }
    void SoundEffectInstance::setIsLoopedProperty(const bool &looped)
     {
        IsLooped_ = looped;
        // Looping is set when playback starts, so you need to restart playback if you change it at runtime
        if (playing) {
            Stop();
            Play();
        }
    }



}
