//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECTINSTANCE_H
#define SOUNDEFFECTINSTANCE_H

#include <atomic>

#include "SoundEffectI.h"
#include "SoundState.h"

#include "CNA/Prop.h"


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

#endif //SOUNDEFFECTINSTANCE_H
