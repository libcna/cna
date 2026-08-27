// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbSourceImport.hpp"

#include <cstring>
#include <fstream>
#include <limits>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Internal/Graphics/DdsCubeDecoder.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        std::vector<std::uint8_t> ReadWholeFile(const std::string& path, const char* what)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw ContentLoadException(std::string("CNB ") + what + ": cannot open '" + path +
                                           "'.");
            }
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                              std::istreambuf_iterator<char>());
        }

        std::uint16_t ReadU16(std::span<const std::uint8_t> bytes, std::size_t offset)
        {
            std::uint16_t value = 0;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        [[noreturn]] void FailWav(const std::string& origin, const std::string& what)
        {
            throw ContentLoadException("CNB WAV import: '" + origin + "' " + what);
        }

        std::string FormatTagName(std::uint16_t tag)
        {
            switch (tag)
            {
                case 0x0001u: return "PCM";
                case 0x0002u: return "MS-ADPCM";
                case 0x0003u: return "IEEE float";
                case 0x0011u: return "IMA-ADPCM";
                case 0x0055u: return "MP3";
                case 0xFFFEu: return "WAVE_FORMAT_EXTENSIBLE";
                default: return "format tag " + std::to_string(tag);
            }
        }
    }

    CnbTextureData ImportImageAsCnbTexture2D(const std::string& imagePath,
                                              const CnbImageImportOptions& options)
    {
        // Through CNA's own decoder, so the compiled pixels are the pixels the runtime would have
        // loaded. A second image decoder here would be a second answer to "what does this PNG
        // contain", which is the failure mode this whole layer exists to avoid.
        CNA::Internal::Graphics::ImageData image =
            CNA::Internal::Graphics::ImageLoader::Load(imagePath);
        if (image.width <= 0 || image.height <= 0)
        {
            throw ContentLoadException("CNB image import: '" + imagePath + "' decoded to " +
                                       std::to_string(image.width) + "x" +
                                       std::to_string(image.height) + ".");
        }
        const std::uint64_t expected =
            static_cast<std::uint64_t>(image.width) * image.height * 4u;
        if (image.pixels.size() != expected)
        {
            throw ContentLoadException(
                "CNB image import: '" + imagePath + "' decoded to " +
                std::to_string(image.pixels.size()) + " bytes, but " + std::to_string(image.width) +
                "x" + std::to_string(image.height) + " Rgba8 needs " + std::to_string(expected) +
                ".");
        }

        if (options.colorKey.has_value())
        {
            // Exactly the runtime rule: a matching pixel keeps its RGB and loses its alpha. Not
            // "becomes transparent black" -- that would change the colour a bilinear filter blends
            // toward at the edge of a keyed region.
            const std::uint8_t keyR = (*options.colorKey)[0];
            const std::uint8_t keyG = (*options.colorKey)[1];
            const std::uint8_t keyB = (*options.colorKey)[2];
            for (std::size_t i = 0; i + 3u < image.pixels.size(); i += 4u)
            {
                if (image.pixels[i] == keyR && image.pixels[i + 1u] == keyG &&
                    image.pixels[i + 2u] == keyB)
                {
                    image.pixels[i + 3u] = 0u;
                }
            }
        }

        return MakeRgba8Texture2DData(static_cast<std::uint32_t>(image.width),
                                       static_cast<std::uint32_t>(image.height),
                                       std::move(image.pixels));
    }

    CnbTextureData DecodeDdsAsCnbTextureCube(std::span<const std::uint8_t> ddsBytes,
                                              const std::string& origin)
    {
        // The shared decoder, not a copy of it. Its diagnostics name the caller, so a compiler
        // error says which file the user pointed at.
        const CNA::Internal::Graphics::DecodedDdsCube decoded =
            CNA::Internal::Graphics::DecodeDdsCube(ddsBytes.data(), ddsBytes.size(),
                                                    "CNB TextureCube import of '" + origin + "'");

        CnbTextureData cube;
        cube.width = static_cast<std::uint32_t>(decoded.width);
        cube.height = cube.width;              // a cube face is square; the decoder enforces it
        cube.depth = 1u;
        cube.faceCount = CnbTextureCubeFaceCount;
        cube.mipCount = static_cast<std::uint32_t>(decoded.mipCount);

        CnbTextureRepresentation representation;
        representation.format = CnbTextureFormat::Rgba8;
        representation.levels.reserve(static_cast<std::size_t>(6) * decoded.mipCount);
        // Face-major then mip, which is both the DDS on-disk order and the order
        // CnbTextureRepresentation::levels documents -- so this is a move, not a reshuffle.
        for (int face = 0; face < 6; ++face)
        {
            for (int level = 0; level < decoded.mipCount; ++level)
            {
                representation.levels.push_back(
                    decoded.faces[static_cast<std::size_t>(face)][static_cast<std::size_t>(level)]);
            }
        }
        cube.representations.push_back(std::move(representation));
        return cube;
    }

    CnbTextureData ImportDdsAsCnbTextureCube(const std::string& ddsPath)
    {
        const std::vector<std::uint8_t> bytes = ReadWholeFile(ddsPath, "TextureCube import");
        return DecodeDdsAsCnbTextureCube(bytes, ddsPath);
    }

    CnbSoundEffectData DecodeWavAsCnbSoundEffect(std::span<const std::uint8_t> wavBytes,
                                                  const std::string& origin)
    {
        if (wavBytes.size() < 12u)
        {
            FailWav(origin, "is " + std::to_string(wavBytes.size()) +
                                " bytes, too short to be a RIFF/WAVE file.");
        }
        if (std::memcmp(wavBytes.data(), "RIFF", 4) != 0 ||
            std::memcmp(wavBytes.data() + 8, "WAVE", 4) != 0)
        {
            FailWav(origin, "is not a RIFF/WAVE file.");
        }

        bool haveFmt = false;
        std::uint16_t formatTag = 0u;
        std::uint16_t channels = 0u;
        std::uint32_t sampleRate = 0u;
        std::uint16_t bitsPerSample = 0u;
        std::span<const std::uint8_t> data;
        bool haveData = false;
        std::uint32_t loopStart = 0u;
        std::uint32_t loopLength = 0u;

        // One pass over the chunk list. Chunks are word-aligned and a chunk that claims to extend
        // past the file is a truncated file, not something to read anyway.
        std::size_t pos = 12u;
        while (pos + 8u <= wavBytes.size())
        {
            const std::uint8_t* id = wavBytes.data() + pos;
            const std::uint32_t chunkSize = ReadU32(wavBytes, pos + 4u);
            const std::size_t start = pos + 8u;
            if (static_cast<std::uint64_t>(start) + chunkSize > wavBytes.size())
            {
                FailWav(origin, "has a '" + std::string(reinterpret_cast<const char*>(id), 4) +
                                    "' chunk claiming " + std::to_string(chunkSize) +
                                    " bytes, which runs past the end of the file.");
            }

            if (std::memcmp(id, "fmt ", 4) == 0)
            {
                if (chunkSize < 16u) { FailWav(origin, "has a 'fmt ' chunk shorter than 16 bytes."); }
                formatTag = ReadU16(wavBytes, start);
                channels = ReadU16(wavBytes, start + 2u);
                sampleRate = ReadU32(wavBytes, start + 4u);
                bitsPerSample = ReadU16(wavBytes, start + 14u);
                // WAVE_FORMAT_EXTENSIBLE carries the real tag in its GUID's first two bytes.
                if (formatTag == 0xFFFEu && chunkSize >= 40u)
                {
                    formatTag = ReadU16(wavBytes, start + 24u);
                }
                haveFmt = true;
            }
            else if (std::memcmp(id, "data", 4) == 0)
            {
                data = wavBytes.subspan(start, chunkSize);
                haveData = true;
            }
            else if (std::memcmp(id, "smpl", 4) == 0)
            {
                // Same rules the runtime applies: 36-byte header, then 24-byte loop entries, and
                // only the first entry's Start/End matter.
                if (chunkSize >= 36u)
                {
                    const std::uint32_t loops = ReadU32(wavBytes, start + 28u);
                    if (loops != 0u && start + 36u + 24u <= wavBytes.size())
                    {
                        const std::uint32_t first = ReadU32(wavBytes, start + 36u + 8u);
                        const std::uint32_t last = ReadU32(wavBytes, start + 36u + 12u);
                        if (last > first)
                        {
                            loopStart = first;
                            loopLength = last - first;
                        }
                    }
                }
            }
            pos = start + chunkSize + (chunkSize & 1u);
        }

        if (!haveFmt) { FailWav(origin, "has no 'fmt ' chunk."); }
        if (!haveData) { FailWav(origin, "has no 'data' chunk."); }
        if (channels != 1u && channels != 2u)
        {
            FailWav(origin, "declares " + std::to_string(channels) +
                                " channels; CNB stores mono or stereo.");
        }
        if (sampleRate == 0u || sampleRate > CnbMaxAudioSampleRate)
        {
            FailWav(origin, "declares a sample rate of " + std::to_string(sampleRate) + " Hz.");
        }
        if (formatTag != 0x0001u)
        {
            FailWav(origin, "is " + FormatTagName(formatTag) +
                                ". This compiler stores Pcm16 and converts only from 8-bit "
                                "unsigned PCM, because every other conversion loses information "
                                "and that is an authoring decision rather than a compiler's.");
        }
        if (bitsPerSample != 8u && bitsPerSample != 16u)
        {
            FailWav(origin, "is " + std::to_string(bitsPerSample) +
                                "-bit PCM. Only 8-bit unsigned and 16-bit PCM convert to CNB's "
                                "Pcm16 exactly.");
        }

        CnbSoundEffectData sound;
        sound.format = CnbAudioFormat::Pcm16;
        sound.sampleRate = sampleRate;
        sound.channels = channels;

        const std::uint32_t sourceFrameBytes =
            static_cast<std::uint32_t>(bitsPerSample / 8u) * channels;
        if (data.size() % sourceFrameBytes != 0u)
        {
            FailWav(origin, "has a 'data' chunk of " + std::to_string(data.size()) +
                                " bytes, which is not a whole number of " +
                                std::to_string(sourceFrameBytes) + "-byte frames.");
        }
        const std::uint64_t frames = data.size() / sourceFrameBytes;
        if (frames > std::numeric_limits<std::uint32_t>::max())
        {
            FailWav(origin, "holds more sample frames than the format can count.");
        }
        sound.frameCount = static_cast<std::uint32_t>(frames);

        if (bitsPerSample == 16u)
        {
            sound.samples.assign(data.begin(), data.end());
        }
        else
        {
            // 8-bit WAV samples are UNSIGNED with a bias of 128; 16-bit are signed. (s - 128) << 8
            // is the exact widening, and it is exact -- nothing is rounded or clipped.
            sound.samples.resize(data.size() * 2u);
            for (std::size_t i = 0; i < data.size(); ++i)
            {
                const auto widened =
                    static_cast<std::int16_t>((static_cast<int>(data[i]) - 128) * 256);
                const auto value = static_cast<std::uint16_t>(widened);
                sound.samples[i * 2u] = static_cast<std::uint8_t>(value & 0xFFu);
                sound.samples[i * 2u + 1u] = static_cast<std::uint8_t>(value >> 8);
            }
        }

        // A loop region past the end is a broken file, and the schema would refuse it anyway --
        // but saying so here names the WAV rather than the .cnb that was never written.
        if (static_cast<std::uint64_t>(loopStart) + loopLength > sound.frameCount)
        {
            FailWav(origin, "has a 'smpl' loop covering frames " + std::to_string(loopStart) +
                                "-" + std::to_string(loopStart + loopLength) +
                                ", but the sound is " + std::to_string(sound.frameCount) +
                                " frames long.");
        }
        sound.loopStart = loopStart;
        sound.loopLength = loopLength;
        return sound;
    }

    CnbSoundEffectData ImportWavAsCnbSoundEffect(const std::string& wavPath)
    {
        const std::vector<std::uint8_t> bytes = ReadWholeFile(wavPath, "WAV import");
        return DecodeWavAsCnbSoundEffect(bytes, wavPath);
    }
}
