//
// Created by robertvokac on 5/24/25.
//
#pragma once

#include <SDL3_mixer/SDL_mixer.h>

namespace Microsoft::Xna::Framework::Audio {


    class SoundEffectI {
    protected:
        virtual ~SoundEffectI() = default;

    public:
        virtual Mix_Chunk *GetChunk() const = 0;

    };
}

