//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECT_H
#define SOUNDEFFECT_H
#include <string>

#include "SoundEffectInstance.h"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffect {
        ddatastatic(float, MasterVolume);

    public:
        SoundEffect(const std::string &assetName);

        SoundEffectInstance CreateInstance();
    };
}


#endif //SOUNDEFFECT_H
