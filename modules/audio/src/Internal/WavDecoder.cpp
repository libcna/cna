// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/WavDecoder.hpp"

#include "CNA/Internal/Audio/ImaAdpcmDecoder.hpp"
#include "CNA/Internal/Audio/MsAdpcmDecoder.hpp"
#include "CNA/Internal/Audio/WavFormatReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace CNA::Internal::Audio
{
    namespace
    {
        constexpr std::uint16_t kFormatPcm = 1u;
        constexpr std::uint16_t kFormatMsAdpcm = 2u;
        constexpr std::uint16_t kFormatIeeeFloat = 3u;
        constexpr std::uint16_t kFormatImaAdpcm = 0x0011u;
        constexpr std::uint16_t kFormatExtensible = 0xFFFEu;

        [[nodiscard]] std::int16_t ClampToInt16(const double value) noexcept
        {
            return static_cast<std::int16_t>(
                std::clamp(value, -32768.0, 32767.0));
        }

        void AppendInt16Le(std::vector<std::uint8_t>& out, const std::int16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(value) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((static_cast<std::uint16_t>(value) >> 8) & 0xFFu));
        }

        // Resolves the WAVE_FORMAT_EXTENSIBLE sub-format to the same PCM/float distinction the
        // base formatTag field carries, from the low 16 bits of the sub-format GUID (the first
        // field of the SubFormat member, which for both KSDATAFORMAT_SUBTYPE_PCM and
        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT equals the corresponding classic wFormatTag value).
        [[nodiscard]] std::uint16_t ResolveEffectiveFormatTag(
            const WavFormatAndData& source, const std::string& origin)
        {
            if (source.formatTag != kFormatExtensible)
            {
                return source.formatTag;
            }
            // extension = validBitsPerSample(2) + channelMask(4) + SubFormat GUID(16).
            if (source.extension.size() < 8u)
            {
                throw std::runtime_error(
                    "'" + origin + "': WAVE_FORMAT_EXTENSIBLE format chunk has no sub-format GUID.");
            }
            std::uint16_t subFormat = 0u;
            std::memcpy(&subFormat, source.extension.data() + 6u, sizeof(subFormat));
            return subFormat;
        }

        // Reads one sample as a signed integer or float, already-clamped to [-1, 1] where the
        // source is floating point, so every encoding converts to PCM16 through the same scale.
        [[nodiscard]] double ReadNormalizedSample(
            const std::uint8_t* frame, const std::uint16_t bitsPerSample, const bool isFloat)
        {
            if (isFloat)
            {
                if (bitsPerSample == 32u)
                {
                    float value = 0.0f;
                    std::memcpy(&value, frame, sizeof(value));
                    return static_cast<double>(value);
                }
                if (bitsPerSample == 64u)
                {
                    double value = 0.0;
                    std::memcpy(&value, frame, sizeof(value));
                    return value;
                }
                throw std::runtime_error("unsupported IEEE float bit depth.");
            }
            switch (bitsPerSample)
            {
                case 8u:
                {
                    // 8-bit PCM is the one unsigned case (WAVE stores it offset-binary, 128 = 0).
                    const int unsignedValue = frame[0];
                    return (static_cast<double>(unsignedValue) - 128.0) / 128.0;
                }
                case 16u:
                {
                    std::int16_t value = 0;
                    std::memcpy(&value, frame, sizeof(value));
                    return static_cast<double>(value) / 32768.0;
                }
                case 24u:
                {
                    std::uint32_t raw = static_cast<std::uint32_t>(frame[0]) |
                                        (static_cast<std::uint32_t>(frame[1]) << 8) |
                                        (static_cast<std::uint32_t>(frame[2]) << 16);
                    // Sign-extend the 24-bit value into a 32-bit one.
                    if ((raw & 0x00800000u) != 0u)
                    {
                        raw |= 0xFF000000u;
                    }
                    const auto value = static_cast<std::int32_t>(raw);
                    return static_cast<double>(value) / 8388608.0;
                }
                case 32u:
                {
                    std::int32_t value = 0;
                    std::memcpy(&value, frame, sizeof(value));
                    return static_cast<double>(value) / 2147483648.0;
                }
                default:
                    throw std::runtime_error("unsupported PCM bit depth.");
            }
        }
    }

    DecodedWavPcm16 DecodeWavToPcm16(
        const std::span<const std::uint8_t> wav, const std::string& origin)
    {
        const WavFormatAndData source = ReadWavFormatAndData(wav, origin);

        const std::uint16_t effectiveFormat = ResolveEffectiveFormatTag(source, origin);
        if (effectiveFormat == kFormatMsAdpcm || effectiveFormat == kFormatImaAdpcm)
        {
            std::uint16_t samplesPerBlock = 0u;
            if (source.extension.size() >= 2u)
            {
                std::memcpy(&samplesPerBlock, source.extension.data(), sizeof(samplesPerBlock));
            }
            else if (effectiveFormat == kFormatImaAdpcm)
            {
                // The reference IMA ADPCM auto-derive formula, used when the format chunk
                // supplies no cbSize/wSamplesPerBlock extension at all.
                const std::uint32_t headerBytes = 4u * source.channels;
                if (source.blockAlign <= headerBytes || source.bitsPerSample == 0u || source.channels == 0u)
                {
                    throw std::runtime_error(
                        "'" + origin + "': cannot derive samplesPerBlock for this IMA-ADPCM format.");
                }
                samplesPerBlock = static_cast<std::uint16_t>(
                    (source.blockAlign - headerBytes) * 8u / (source.bitsPerSample * source.channels) + 1u);
            }
            else
            {
                throw std::runtime_error(
                    "'" + origin + "': MS-ADPCM format chunk has no wSamplesPerBlock.");
            }

            DecodedWavPcm16 result;
            result.sampleRate = source.sampleRate;
            result.channels = source.channels;
            try
            {
                const std::vector<std::int16_t> decoded =
                    effectiveFormat == kFormatMsAdpcm
                        ? DecodeMsAdpcm(source.data, source.channels, source.blockAlign, samplesPerBlock)
                        : DecodeImaAdpcm(source.data, source.channels, source.blockAlign, samplesPerBlock);
                result.frameCount = static_cast<std::uint32_t>(decoded.size() / source.channels);
                result.samples.reserve(decoded.size() * 2u);
                for (const std::int16_t sample : decoded)
                {
                    AppendInt16Le(result.samples, sample);
                }
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error("'" + origin + "': " + e.what());
            }
            return result;
        }

        const bool isFloat = effectiveFormat == kFormatIeeeFloat;
        if (!isFloat && effectiveFormat != kFormatPcm)
        {
            throw std::runtime_error(
                "'" + origin + "': unsupported WAVE encoding (format tag " +
                std::to_string(effectiveFormat) +
                "); only PCM, IEEE float, MS-ADPCM and IMA-ADPCM (optionally via "
                "WAVE_FORMAT_EXTENSIBLE for PCM/float) are decoded.");
        }
        if (source.bitsPerSample != 8u && source.bitsPerSample != 16u &&
            source.bitsPerSample != 24u && source.bitsPerSample != 32u &&
            !(isFloat && source.bitsPerSample == 64u))
        {
            throw std::runtime_error(
                "'" + origin + "': unsupported bits-per-sample (" +
                std::to_string(source.bitsPerSample) + ").");
        }

        const std::uint32_t bytesPerSample = static_cast<std::uint32_t>(source.bitsPerSample) / 8u;
        const std::uint32_t frameBytes = bytesPerSample * static_cast<std::uint32_t>(source.channels);
        if (frameBytes == 0u || source.data.size() % frameBytes != 0u)
        {
            throw std::runtime_error(
                "'" + origin + "': WAVE data size is not a whole number of sample frames.");
        }

        const std::size_t frameCount = source.data.size() / frameBytes;
        if (frameCount > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("'" + origin + "': WAVE file has too many sample frames.");
        }

        DecodedWavPcm16 result;
        result.sampleRate = source.sampleRate;
        result.channels = source.channels;
        result.frameCount = static_cast<std::uint32_t>(frameCount);
        result.samples.reserve(frameCount * source.channels * 2u);

        const std::uint8_t* data = source.data.data();
        for (std::size_t frame = 0u; frame < frameCount; ++frame)
        {
            const std::uint8_t* frameStart = data + frame * frameBytes;
            for (std::uint16_t channel = 0u; channel < source.channels; ++channel)
            {
                const std::uint8_t* sampleStart = frameStart + static_cast<std::size_t>(channel) * bytesPerSample;
                const double normalized =
                    ReadNormalizedSample(sampleStart, source.bitsPerSample, isFloat);
                AppendInt16Le(result.samples, ClampToInt16(normalized * 32768.0));
            }
        }
        return result;
    }
}
