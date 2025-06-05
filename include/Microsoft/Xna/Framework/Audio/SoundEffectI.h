//
// Created by robertvokac on 5/24/25.
//
#ifndef SOUNDEFFECTI_H
#define SOUNDEFFECTI_H

#include <SDL3_mixer/SDL_mixer.h>

namespace Microsoft::Xna::Framework::Audio {


    class SoundEffectI {
    protected:
        virtual ~SoundEffectI() = default;

    public:
        virtual Mix_Chunk *GetChunk() const = 0;

    };
}


#endif // SOUNDEFFECTI_H
