//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include <string>
#include <SDL3_mixer/SDL_mixer.h>

#include "SoundEffectI.hpp"
#include "SoundEffectInstance.hpp"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffect : public SoundEffectI {
        friend class SoundEffectInstance;
        DEF_PROP(float, MasterVolume, getter1, setter1, member0, static1, constret1, ref0, constmet0)

    private:
        Mix_Chunk *chunk = nullptr;

    public:
        SoundEffect(const std::string &assetName);

        ~SoundEffect();

        SoundEffect(const SoundEffect &);

        SoundEffect &operator=(const SoundEffect &);

        SoundEffect(SoundEffect &&other) noexcept;

        SoundEffect &operator=(SoundEffect &&other) noexcept;

        SoundEffectInstance CreateInstance();

    private:

    private:
        Mix_Chunk *GetChunk() const { return chunk; }
    };
}

