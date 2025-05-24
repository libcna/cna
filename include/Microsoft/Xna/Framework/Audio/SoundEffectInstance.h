//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECTINSTANCE_H
#define SOUNDEFFECTINSTANCE_H
#include "SoundState.h"
#include "NeoSdk/Property.h"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffectInstance {
    public:
        SoundState State = SoundState::Stopped;
        NeoSdk::Property<float> Volume;
        NeoSdk::Property<float> Pan;
        NeoSdk::Property<float> Pitch;
        NeoSdk::Property<bool> IsLooped;

        SoundEffectInstance();

        void Play();

        void Stop();
    };
}


#endif //SOUNDEFFECTINSTANCE_H
