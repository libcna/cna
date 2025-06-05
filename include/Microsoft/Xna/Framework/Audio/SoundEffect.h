//
// Created by robertvokac on 5/24/25.
//

#ifndef SOUNDEFFECT_H
#define SOUNDEFFECT_H
#include <string>
#include <SDL3_mixer/SDL_mixer.h>

#include "SoundEffectI.h"
#include "SoundEffectInstance.h"


namespace Microsoft::Xna::Framework::Audio {
    class SoundEffect : public SoundEffectI {
        friend class SoundEffectInstance;
        ddatastatic(float, MasterVolume);

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


#endif //SOUNDEFFECT_H
