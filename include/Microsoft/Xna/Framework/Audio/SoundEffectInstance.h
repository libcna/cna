//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECTINSTANCE_H
#define SOUNDEFFECTINSTANCE_H
#include "SoundEffect.h"
#include "SoundState.h"

#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffectInstance {

    private: SoundEffect* soundEffect;
        ddata(float, Volume);
        ddata(float, Pan);
        ddata(float, Pitch);
        ddata(bool, IsLooped);
    public:
        SoundState State = SoundState::Stopped;

    private: SoundEffectInstance::SoundEffectInstance(SoundEffect* soundEffect);
        friend void SoundEffect::SoundEffectInstance();
    public:

        void Play();

        void Stop();
    };
}


#endif //SOUNDEFFECTINSTANCE_H
