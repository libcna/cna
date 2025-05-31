//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECTINSTANCE_H
#define SOUNDEFFECTINSTANCE_H
#include "SoundState.h"

#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffectInstance {
        ddata(float, Volume);
        ddata(float, Pan);
        ddata(float, Pitch);
        ddata(bool, IsLooped);
    public:
        SoundState State = SoundState::Stopped;

        SoundEffectInstance();

        void Play();

        void Stop();
    };
}


#endif //SOUNDEFFECTINSTANCE_H
