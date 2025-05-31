//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECTINSTANCE_H
#define SOUNDEFFECTINSTANCE_H
#include "SoundState.h"

#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffectInstance {
    public:
        SoundState State = SoundState::Stopped;

    private:
        float VolumeProperty_ = 1.0f;

    public:
        [[nodiscard]] float VolumeProperty() const;

    public:
        void VolumeProperty(float v);

    private:
        float PanProperty_ = 0.0f;

    public:
        [[nodiscard]] float PanProperty() const;

    public:
        void PanProperty(float v);

    private:
        float PitchProperty_ = 0.0f;

    public:
        [[nodiscard]] float PitchProperty() const;

    public:
        void PitchProperty(float v);

    private:
        bool IsLoopedProperty_ = false;

    public:
        [[nodiscard]] bool IsLoopedProperty() const;

    public:
        void IsLoopedProperty(bool v);


        SoundEffectInstance();

        void Play();

        void Stop();
    };
}


#endif //SOUNDEFFECTINSTANCE_H
