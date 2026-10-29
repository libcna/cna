// SPDX-License-Identifier: MS-PL
// The canonical .xnb SoundEffect decoder and its conversion to an imported sound.
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "XnbCanonicalReaderAccess.hpp"
#include "XnbModelGraphReader.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "CNA/Internal/Audio/WavDecoder.hpp"
#include "CNA/Internal/Audio/WavWrapper.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Xnb/XnbArithmetic.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        constexpr std::uint16_t WaveFormatPcm = 0x0001u;
        constexpr std::uint16_t WaveFormatMsAdpcm = 0x0002u;
        constexpr std::uint16_t WaveFormatIeeeFloat = 0x0003u;
        constexpr std::uint16_t WaveFormatImaAdpcm = 0x0011u;
        constexpr std::uint16_t WaveFormatXma2 = 0x0166u;
        constexpr std::size_t MinRealMsAdpcmExtensionSize = 32u;

        [[nodiscard]] std::uint16_t Swap16(const bool swap, const std::uint16_t value)
        {
            if (!swap) { return value; }
            return static_cast<std::uint16_t>((value >> 8u) | (value << 8u));
        }

        [[nodiscard]] std::uint32_t Swap32(const bool swap, const std::uint32_t value)
        {
            if (!swap) { return value; }
            return ((value >> 24u) & 0x000000FFu) | ((value >> 8u) & 0x0000FF00u) |
                   ((value << 8u) & 0x00FF0000u) | ((value << 24u) & 0xFF000000u);
        }


        [[nodiscard]] std::uint16_t ComputeMsAdpcmSamplesPerBlock(
            const std::uint16_t blockAlign, const std::uint16_t channels)
        {
            const std::uint32_t channelCount = channels == 0u ? 1u : channels;
            const std::uint32_t headerBits = channelCount * 7u * 8u;
            const std::uint32_t blockBits = static_cast<std::uint32_t>(blockAlign) * 8u;
            const std::uint32_t available = blockBits > headerBits ? blockBits - headerBits : 0u;
            return static_cast<std::uint16_t>(available / (4u * channelCount) + 2u);
        }

        // Available in every build: DecodeWavToPcm16 is CNA's own decoder
        // (modules/audio/src/Internal, compiled for every CNA_AUDIO_PLATFORM). This used to be
        // SOUND_ENABLED-only because its predecessor was SDL3's, and the guard outlived the
        // reason -- an SDL-free build refused float and ADPCM SoundEffects it could decode
        // (NPV-0104).
        [[nodiscard]] std::vector<std::uint8_t> DecodeWaveToPcm16(
            const XnbSoundEffectData& source, const std::string& origin,
            std::uint32_t& frameCount)
        {
            const bool synthesizeMsAdpcm =
                source.formatTag == WaveFormatMsAdpcm &&
                source.extensionData.size() < MinRealMsAdpcmExtensionSize;
            const std::vector<std::uint8_t> wav = Audio::BuildWavFromWaveFormatEx(
                source.samples.data(), static_cast<std::uint32_t>(source.samples.size()),
                source.formatTag, source.channels, source.sampleRate,
                source.averageBytesPerSecond, source.blockAlign, source.bitsPerSample,
                synthesizeMsAdpcm
                    ? Audio::BuildStandardMsAdpcmExtension(
                          ComputeMsAdpcmSamplesPerBlock(source.blockAlign, source.channels))
                    : source.extensionData,
                0u);

            try
            {
                Audio::DecodedWavPcm16 decoded = Audio::DecodeWavToPcm16(wav, origin);
                if (decoded.sampleRate != source.sampleRate ||
                    decoded.channels != source.channels)
                {
                    throw ContentLoadException(
                        "'" + origin +
                        "': decoded SoundEffect rate/channels disagree with its XNB WAVEFORMATEX.");
                }
                frameCount = decoded.frameCount;
                return std::move(decoded.samples);
            }
            catch (const ContentLoadException&)
            {
                throw;
            }
            catch (const std::exception& inner)
            {
                throw ContentLoadException(
                    "'" + origin + "': headless XNB SoundEffect decode failed.", inner);
            }
        }

        void ValidateSoundMetadata(const XnbSoundEffectData& source,
                                   const std::uint32_t frameCount,
                                   const std::string& origin)
        {
            if (source.loopStart < 0 || source.loopLength < 0 ||
                static_cast<std::uint64_t>(source.loopStart) +
                        static_cast<std::uint64_t>(source.loopLength) > frameCount)
            {
                throw ContentLoadException(
                    "'" + origin + "': SoundEffect loop region lies outside decoded frames.");
            }
            if (source.storedDurationMs == 0u || frameCount == 0u) { return; }
            const double decodedMs =
                static_cast<double>(frameCount) * 1000.0 / source.sampleRate;
            const double ratio = decodedMs / source.storedDurationMs;
            if (ratio > 2.0 || ratio < 0.5)
            {
                throw ContentLoadException(
                    "'" + origin + "': decoded audio duration (" +
                    std::to_string(decodedMs) +
                    "ms) disagrees drastically with the .xnb's own stored duration (" +
                    std::to_string(source.storedDurationMs) + "ms).");
            }
        }
    }

    XnbSoundEffectData DecodeSoundEffectXnbData(ContentReader& input)
    {
        XnbSoundEffectData result;
        result.platform = input.getPlatformProperty();
        const bool swap = input.getPlatformProperty() == 'x';
        const std::uint32_t formatLength = input.ReadUInt32();
        if (formatLength < 16u || formatLength >
                static_cast<std::uint32_t>(DefaultXnbReadLimits().maxStringBytes))
        {
            throw ContentLoadException(
                "'" + input.getAssetNameProperty() +
                "': SoundEffectReader has an invalid format block length.");
        }
        result.formatTag = Swap16(swap, input.ReadUInt16());
        result.channels = Swap16(swap, input.ReadUInt16());
        result.sampleRate = Swap32(swap, input.ReadUInt32());
        result.averageBytesPerSecond = Swap32(swap, input.ReadUInt32());
        result.blockAlign = Swap16(swap, input.ReadUInt16());
        result.bitsPerSample = Swap16(swap, input.ReadUInt16());

        if (formatLength > 16u)
        {
            const std::uint16_t extensionSize = Swap16(swap, input.ReadUInt16());
            const std::int64_t remaining = static_cast<std::int64_t>(formatLength) - 18;
            if (remaining < 0 || static_cast<std::uint64_t>(remaining) >
                    static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
            {
                throw ContentLoadException(
                    "'" + input.getAssetNameProperty() +
                    "': SoundEffectReader formatLength is too small for its extension.");
            }
            result.extensionData = input.ReadBytesExactOrThrow(
                static_cast<std::int32_t>(remaining), "SoundEffectReader");
            if (result.formatTag != WaveFormatXma2 && extensionSize > result.extensionData.size())
            {
                throw ContentLoadException(
                    "'" + input.getAssetNameProperty() +
                    "': SoundEffectReader cbSize exceeds its format block.");
            }
        }

        result.samples =
            input.ReadBytesExactOrThrow(input.ReadInt32(), "SoundEffectReader");
        result.loopStart = input.ReadInt32();
        result.loopLength = input.ReadInt32();
        result.storedDurationMs = input.ReadUInt32();
        return result;
    }

    CNA::Content::Import::ImportedSound ConvertXnbSoundToImportedSound(
        const XnbSoundEffectData& source, const std::string& origin,
        const bool allowXboxPayload)
    {
        if (source.platform == 'x' && !allowXboxPayload)
        {
            throw ContentLoadException(
                "'" + origin +
                "': Xbox 360 SoundEffect sample byte order is not supported for native CNB "
                "transcoding.");
        }
        if (source.channels != 1u && source.channels != 2u)
        {
            throw ContentLoadException(
                "'" + origin + "': unsupported SoundEffect channel count (" +
                std::to_string(source.channels) + "); only mono and stereo are supported.");
        }
        if (source.sampleRate == 0u)
        {
            throw ContentLoadException(
                "'" + origin + "': SoundEffect sample rate must not be zero.");
        }

        CNA::Content::Import::ImportedSound imported;
        imported.sampleRate = source.sampleRate;
        imported.channels = source.channels;
        if (source.formatTag == WaveFormatPcm && source.bitsPerSample == 16u)
        {
            imported.encoding = CNA::Content::Import::ImportedPcmEncoding::Signed16LittleEndian;
            imported.samples = source.samples;
            const std::uint32_t frameBytes = source.channels * 2u;
            if (source.samples.size() % frameBytes != 0u ||
                source.samples.size() / frameBytes > std::numeric_limits<std::uint32_t>::max())
            {
                throw ContentLoadException(
                    "'" + origin + "': PCM16 SoundEffect byte count is not frame-aligned.");
            }
            imported.frameCount =
                static_cast<std::uint32_t>(source.samples.size() / frameBytes);
        }
        else if (source.formatTag == WaveFormatPcm && source.bitsPerSample == 8u)
        {
            imported.encoding = CNA::Content::Import::ImportedPcmEncoding::Unsigned8;
            imported.samples = source.samples;
            if (source.samples.size() % source.channels != 0u ||
                source.samples.size() / source.channels >
                    std::numeric_limits<std::uint32_t>::max())
            {
                throw ContentLoadException(
                    "'" + origin + "': PCM8 SoundEffect byte count is not frame-aligned.");
            }
            imported.frameCount =
                static_cast<std::uint32_t>(source.samples.size() / source.channels);
        }
        else if ((source.formatTag == WaveFormatIeeeFloat && source.bitsPerSample == 32u) ||
                 (source.formatTag == WaveFormatMsAdpcm && source.bitsPerSample == 4u) ||
                 (source.formatTag == WaveFormatImaAdpcm && source.bitsPerSample == 4u))
        {
            imported.encoding = CNA::Content::Import::ImportedPcmEncoding::Signed16LittleEndian;
            imported.samples = DecodeWaveToPcm16(source, origin, imported.frameCount);
        }
        else
        {
            throw ContentLoadException(
                "'" + origin + "': unsupported SoundEffect wave format (formatTag=" +
                std::to_string(source.formatTag) + ", bitsPerSample=" +
                std::to_string(source.bitsPerSample) + ", channels=" +
                std::to_string(source.channels) + ", sampleRate=" +
                std::to_string(source.sampleRate) + "). XMA2 and unknown codecs have no "
                "native CNB decode path.");
        }
        imported.loopStart = static_cast<std::uint32_t>(std::max(0, source.loopStart));
        imported.loopLength = static_cast<std::uint32_t>(std::max(0, source.loopLength));
        ValidateSoundMetadata(source, imported.frameCount, origin);
        return imported;
    }
}
