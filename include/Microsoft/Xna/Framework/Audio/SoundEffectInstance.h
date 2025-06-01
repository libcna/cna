//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECTINSTANCE_H
#define SOUNDEFFECTINSTANCE_H

#include <atomic>

#include "SoundEffect.h"
#include "SoundState.h"

#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffectInstance {
friend class SoundEffect;
    private: SoundEffect* soundEffect;

        int channel = -1;  // SDL_mixer channel
        std::atomic<bool> playing = false;

        ddata(float, Volume);
        ddata(float, Pan);
        ddata(float, Pitch);
        ddata(bool, IsLooped);

    private: bool IsPlaying() const { return playing; }

    public:
        SoundState State = SoundState::Stopped;

    private: SoundEffectInstance::SoundEffectInstance(SoundEffect* soundEffect);

    public:

        void Play();

        void Stop();
        ~SoundEffectInstance();
    };
}

#endif //SOUNDEFFECTINSTANCE_H
