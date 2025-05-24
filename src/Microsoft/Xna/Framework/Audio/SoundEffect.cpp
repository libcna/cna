//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffect.h"

namespace Microsoft::Xna::Framework::Audio {


    NeoSdk::Property<float> SoundEffect::MasterVolume{ []() { return 0.0f; }};

    Audio::SoundEffect::SoundEffect()
     {

    }
}

