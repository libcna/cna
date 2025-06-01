//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iostream>
#include <ostream>
#include <SDL3/SDL_log.h>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.h"

namespace Microsoft::Xna::Framework::Audio {
    float SoundEffectInstance::getVolume() const { return Volume_; }

    float SoundEffectInstance::getPan() const { return Pan_; }

    float SoundEffectInstance::getPitch() const { return Pitch_; }
    void SoundEffectInstance::setPitch(const float &v) { Pitch_ = v; };
    bool SoundEffectInstance::getIsLooped() const { return IsLooped_; }


    SoundEffectInstance::SoundEffectInstance(SoundEffect *soundEffect): Volume_(1.0f), Pan_(0.0f), Pitch_(0.0f),
                                                                        IsLooped_(false), soundEffect(soundEffect)
    {
    }

    SoundEffectInstance::~SoundEffectInstance() {
        Stop();
    }



    void SoundEffectInstance::Play() {
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
    }

    void SoundEffectInstance::Stop() {
        if (!playing) return;

        if (channel != -1) {
            Mix_HaltChannel(channel);
            channel = -1;
        }

        playing = false;
        State = SoundState::Stopped;
    }

    void SoundEffectInstance::setVolume(const float& volume) {
        Volume_ = volume < 0.f ? 0.f : (volume > 1.f ? 1.f : volume);
        if (channel != -1) {
            Mix_Volume(channel, static_cast<int>(Volume_ * 128));
        }
    }

    void SoundEffectInstance::setPan(const float& pan) {
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
    void SoundEffectInstance::setIsLooped(const bool &looped)
     {
        IsLooped_ = looped;
        // Looping is set when playback starts, so you need to restart playback if you change it at runtime
        if (playing) {
            Stop();
            Play();
        }
    }



}
