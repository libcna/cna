// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/WavDecoder.hpp"

#include <limits>
#include <stdexcept>

#include <SDL3/SDL.h>

namespace CNA::Internal::Audio
{
    namespace
    {
        struct SdlBuffer
        {
            std::uint8_t* value = nullptr;
            ~SdlBuffer() { SDL_free(value); }
        };
    }

    DecodedWavPcm16 DecodeWavToPcm16(
        const std::span<const std::uint8_t> wav, const std::string& origin)
    {
        SDL_IOStream* stream = SDL_IOFromConstMem(wav.data(), wav.size());
        if (stream == nullptr)
        {
            throw std::runtime_error(
                "'" + origin + "': cannot create an in-memory WAVE decode stream: " +
                SDL_GetError() + ".");
        }

        SDL_AudioSpec decodedSpec{};
        std::uint8_t* decoded = nullptr;
        std::uint32_t decodedLength = 0u;
        if (!SDL_LoadWAV_IO(stream, true, &decodedSpec, &decoded, &decodedLength))
        {
            throw std::runtime_error(
                "'" + origin + "': WAVE decode failed: " + SDL_GetError() + ".");
        }
        SdlBuffer decodedOwner{decoded};

        SDL_AudioSpec target = decodedSpec;
        target.format = SDL_AUDIO_S16LE;
        std::uint8_t* converted = nullptr;
        int convertedLength = 0;
        if (decodedLength > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            !SDL_ConvertAudioSamples(
                &decodedSpec, decoded, static_cast<int>(decodedLength),
                &target, &converted, &convertedLength))
        {
            throw std::runtime_error(
                "'" + origin + "': cannot convert decoded WAVE audio to PCM16: " +
                SDL_GetError() + ".");
        }
        SdlBuffer convertedOwner{converted};
        if (decodedSpec.freq <= 0 || decodedSpec.channels <= 0 ||
            decodedSpec.channels > std::numeric_limits<std::uint16_t>::max() ||
            convertedLength < 0)
        {
            throw std::runtime_error(
                "'" + origin + "': decoded WAVE audio has an invalid format or length.");
        }

        const std::uint32_t frameBytes =
            static_cast<std::uint32_t>(decodedSpec.channels) * 2u;
        const auto length = static_cast<std::size_t>(convertedLength);
        if (length % frameBytes != 0u ||
            length / frameBytes > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error(
                "'" + origin + "': decoded WAVE PCM length is inconsistent.");
        }

        DecodedWavPcm16 result;
        result.samples.assign(converted, converted + length);
        result.sampleRate = static_cast<std::uint32_t>(decodedSpec.freq);
        result.channels = static_cast<std::uint16_t>(decodedSpec.channels);
        result.frameCount = static_cast<std::uint32_t>(length / frameBytes);
        return result;
    }
}
