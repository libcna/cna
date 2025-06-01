//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.h"

#include "Microsoft/Xna/Framework/Audio/SoundEffect.h"

namespace Microsoft::Xna::Framework::Audio {
    idata(float, Volume, SoundEffectInstance);
    idata(float, Pan, SoundEffectInstance);
    idata(float, Pitch, SoundEffectInstance);
    idata(bool, IsLooped, SoundEffectInstance);

    SoundEffectInstance::SoundEffectInstance(SoundEffect *soundEffect): Volume_(0), Pan_(0), Pitch_(0),
                                                                        IsLooped_(false), soundEffect(soundEffect) {
    }
}
