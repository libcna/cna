//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Audio/SoundEffect.h"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.h"
#include <stdexcept>

namespace Microsoft::Xna::Framework::Audio {
    idatastatic(float, MasterVolume, SoundEffect, 0.0f)

    Audio::SoundEffect::SoundEffect(const std::string &assetName) {
        chunk = Mix_LoadWAV(assetName.c_str());
        if (!chunk) {
            throw std::runtime_error("Failed to load WAV file: " + assetName + " Error: " + SDL_GetError());
        }
    }

    SoundEffect::~SoundEffect() {
        if (chunk) {
            Mix_FreeChunk(chunk);
            chunk = nullptr;
        }
    }


    // Move semantics
    SoundEffect::SoundEffect(SoundEffect&& other) noexcept : chunk(other.chunk) {
        other.chunk = nullptr;
    }

    SoundEffect& SoundEffect::operator=(SoundEffect&& other) noexcept {
        if (this != &other) {
            if (chunk) Mix_FreeChunk(chunk);
            chunk = other.chunk;
            other.chunk = nullptr;
        }
        return *this;
    }

    SoundEffectInstance SoundEffect::CreateInstance() {
        return SoundEffectInstance(this);
    }
}
