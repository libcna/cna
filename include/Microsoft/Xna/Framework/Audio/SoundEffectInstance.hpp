//
// Created by robertvokac on 5/24/25.
//

#pragma once

#include <atomic>

#include "SoundEffectI.hpp"
#include "SoundState.hpp"

#include "CNA/Prop.hpp"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffectInstance {

    private: SoundEffectI* soundEffect;

        int channel = -1;  // SDL_mixer channel
        std::atomic<bool> playing = false;

        DEF_PROP(float, Volume, getter1, setter1, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(float, Pan, getter1, setter1, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(float, Pitch, getter1, setter1, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(bool, IsLooped, getter1, setter1, member1, static0, constret1, ref1, constmet1)

    private: bool IsPlaying() const { return playing; }

    public:
        SoundState State = SoundState::Stopped;

    public:
        explicit SoundEffectInstance(Audio::SoundEffectI * sound_effect);

    public:

        void Play();

        void Stop();
        ~SoundEffectInstance();
    };
}

