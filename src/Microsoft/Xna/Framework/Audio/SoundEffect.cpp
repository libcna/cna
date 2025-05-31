//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffect.h"

namespace Microsoft::Xna::Framework::Audio {
    static float MasterVolume_ = 1.0f;

    float SoundEffect::MasterVolumeProperty() {
        return MasterVolume_;
    }

    void SoundEffect::MasterVolumeProperty(float value) {
        MasterVolume_ = value;
    }

    Audio::SoundEffect::SoundEffect() {
    }
}
