//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include <stdexcept>

namespace Microsoft::Xna::Framework::Audio {
    IMPL_PROP(float, MasterVolume, getter1, setter1, member1, static0, constret1, ref0, constmet0, SoundEffect, 0.0f)

    Audio::SoundEffect::SoundEffect(const std::string &assetName) {
#ifdef SOUND_ENABLED
        chunk = Mix_LoadWAV(assetName.c_str());
        if (!chunk) {
            throw std::runtime_error("Failed to load WAV file: " + assetName + " Error: " + SDL_GetError());
        }
#endif

    }

    SoundEffect::~SoundEffect() {
#ifdef SOUND_ENABLED
        if (chunk) {
            Mix_FreeChunk(chunk);
            chunk = nullptr;
        }
#endif
    }

    // Copy constructor
    SoundEffect::SoundEffect(const SoundEffect& other) {
#ifdef SOUND_ENABLED
        if (other.chunk) {
            chunk = Mix_QuickLoad_WAV(reinterpret_cast<Uint8*>(other.chunk->abuf));
            if (!chunk) {
                throw std::runtime_error("Failed to copy SoundEffect");
            }
        }
#endif
    }

    // Copy assignment operator
    SoundEffect& SoundEffect::operator=(const SoundEffect& other) {
#ifdef SOUND_ENABLED
        if (this != &other) {
            if (chunk) Mix_FreeChunk(chunk);
            chunk = nullptr;

            if (other.chunk) {
                chunk = Mix_QuickLoad_WAV(reinterpret_cast<Uint8*>(other.chunk->abuf));
                if (!chunk) {
                    throw std::runtime_error("Failed to copy SoundEffect");
                }
            }
        }
#endif
        return *this;
    }

    // Move semantics
    SoundEffect::SoundEffect(SoundEffect&& other) noexcept : chunk(other.chunk) {
#ifdef SOUND_ENABLED
        other.chunk = nullptr;
#endif
    }

    SoundEffect& SoundEffect::operator=(SoundEffect&& other) noexcept {
#ifdef SOUND_ENABLED
        if (this != &other) {
            if (chunk) Mix_FreeChunk(chunk);
            chunk = other.chunk;
            other.chunk = nullptr;
        }
#endif
        return *this;
    }

    SoundEffectInstance SoundEffect::CreateInstance() {
        return SoundEffectInstance(this);
    }
}
