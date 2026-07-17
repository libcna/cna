// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/WavWrapper.hpp"

namespace CNA::Internal::Audio
{
    namespace
    {
        void w16(std::vector<uint8_t>& v, uint16_t x)
        {
            v.push_back(static_cast<uint8_t>(x));
            v.push_back(static_cast<uint8_t>(x >> 8));
        }
        void w32(std::vector<uint8_t>& v, uint32_t x)
        {
            v.push_back(static_cast<uint8_t>(x));
            v.push_back(static_cast<uint8_t>(x >> 8));
            v.push_back(static_cast<uint8_t>(x >> 16));
            v.push_back(static_cast<uint8_t>(x >> 24));
        }
        void tag(std::vector<uint8_t>& v, const char* t)
        {
            v.push_back(static_cast<uint8_t>(t[0]));
            v.push_back(static_cast<uint8_t>(t[1]));
            v.push_back(static_cast<uint8_t>(t[2]));
            v.push_back(static_cast<uint8_t>(t[3]));
        }
    }

    std::vector<uint8_t> BuildWavFromWaveFormatEx(
        const uint8_t* audioData, uint32_t audioLen,
        uint16_t wFormatTag, uint16_t channels, uint32_t sampleRate, uint32_t avgBytesPerSec,
        uint16_t blockAlign, uint16_t bitsPerSample,
        const std::vector<uint8_t>& extensionData,
        uint32_t factSampleFrames)
    {
        // A plain (non-extensible) fmt chunk is 16 bytes; adding a cbSize-prefixed extension
        // makes it 18 + extensionData.size(). Only PCM (formatTag 1) is ever written without an
        // extension in practice, but any caller passing an empty extensionData gets the plain
        // 16-byte form, matching what SDL's WAV parser itself expects for a bare PCM fmt chunk.
        const bool hasExtension = !extensionData.empty();
        const uint32_t fmtSize = hasExtension
            ? static_cast<uint32_t>(18 + extensionData.size())
            : 16;
        const bool hasFact = factSampleFrames != 0;
        const uint32_t factChunkBytes = hasFact ? (8 + 4) : 0;
        const uint32_t riffPayload =
            4 +                       // "WAVE"
            (8 + fmtSize) +           // "fmt " chunk
            factChunkBytes +          // optional "fact" chunk
            (8 + audioLen);           // "data" chunk

        std::vector<uint8_t> wav;
        wav.reserve(8 + riffPayload);

        tag(wav, "RIFF"); w32(wav, riffPayload);
        tag(wav, "WAVE");

        tag(wav, "fmt "); w32(wav, fmtSize);
        w16(wav, wFormatTag);
        w16(wav, channels);
        w32(wav, sampleRate);
        w32(wav, avgBytesPerSec);
        w16(wav, blockAlign);
        w16(wav, bitsPerSample);
        if (hasExtension)
        {
            w16(wav, static_cast<uint16_t>(extensionData.size()));
            wav.insert(wav.end(), extensionData.begin(), extensionData.end());
        }

        if (hasFact)
        {
            tag(wav, "fact"); w32(wav, 4);
            w32(wav, factSampleFrames);
        }

        tag(wav, "data"); w32(wav, audioLen);
        wav.insert(wav.end(), audioData, audioData + audioLen);
        return wav;
    }

    std::vector<uint8_t> BuildStandardMsAdpcmExtension(uint16_t samplesPerBlock)
    {
        // The 7 industry-standard MS-ADPCM coefficient pairs (Microsoft's own ADPCM spec,
        // reproduced verbatim in SDL3's WAV decoder and every real MS-ADPCM encoder, including
        // XACT's) -- SDL3's decoder validates the first 14 int16 values against this exact table
        // and rejects anything else, so these are the only values that can ever work.
        static const int16_t kPresetCoeffs[14] = {
            256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232
        };
        constexpr uint16_t kNumCoef = 7;

        std::vector<uint8_t> ext;
        w16(ext, samplesPerBlock);
        w16(ext, kNumCoef);
        for (int16_t coeff : kPresetCoeffs)
        {
            w16(ext, static_cast<uint16_t>(coeff));
        }
        return ext;
    }
}
