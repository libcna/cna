//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.h"

namespace Microsoft::Xna::Framework::Audio {
    float SoundEffectInstance::VolumeProperty() const { return VolumeProperty_; }
    void SoundEffectInstance::VolumeProperty(const float v) { VolumeProperty_ = v; }
    float SoundEffectInstance::PanProperty() const { return PanProperty_; }
    void SoundEffectInstance::PanProperty(float v) { PanProperty_ = v; }
    float SoundEffectInstance::PitchProperty() const { return PitchProperty_; }
    void SoundEffectInstance::PitchProperty(float v) { PitchProperty_ = v; }
    bool SoundEffectInstance::IsLoopedProperty() const { return IsLoopedProperty_; }
    void SoundEffectInstance::IsLoopedProperty(bool v) { IsLoopedProperty_ = v; }

    // float SoundEffectInstance::VolumeProperty() {return Volume_;}
    // void SoundEffectInstance::VolumeProperty(float value) {Volume_ = value;}


    SoundEffectInstance::SoundEffectInstance() {
    }
}
