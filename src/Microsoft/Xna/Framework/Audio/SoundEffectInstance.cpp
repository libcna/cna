//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.h"

namespace Microsoft::Xna::Framework::Audio {
    idata(float, Volume, SoundEffectInstance);
    idata(float, Pan, SoundEffectInstance);
    idata(float, Pitch, SoundEffectInstance);
    idata(bool, IsLooped, SoundEffectInstance);

    // float SoundEffectInstance::VolumeProperty() {return Volume_;}
    // void SoundEffectInstance::VolumeProperty(float value) {Volume_ = value;}


    SoundEffectInstance::SoundEffectInstance() {
    }
}
