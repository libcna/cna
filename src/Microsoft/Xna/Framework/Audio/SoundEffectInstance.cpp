//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.h"

namespace Microsoft::Xna::Framework::Audio {
    SoundEffectInstance::SoundEffectInstance() : Volume([this]() { return 0.0f; }, [this](float value) {
                                                 }),
                                                 Pan([this]() { return 0.0f; }),
                                                 Pitch([this]() { return 0.0f; }),
                                                 IsLooped([this]() { return true; }) {
    }
}
