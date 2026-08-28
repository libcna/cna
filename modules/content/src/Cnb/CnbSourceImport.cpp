// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbSourceImport.hpp"

#include <cstring>
#include <fstream>
#include <limits>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Graphics/DdsCubeDecoder.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& path, const char* what)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw ContentLoadException(std::string("CNB ") + what + ": cannot open '" +
                                           CNA::Internal::ContentPathToUtf8(path) + "'.");
            }
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                              std::istreambuf_iterator<char>());
        }

        // RIFF integers are little-endian by definition, so they are assembled from individual
        // bytes rather than memcpy'd into a host integer (plans/plan_cnb.md CNBF-117). A memcpy
        // decodes correctly only on a little-endian host: on a big-endian one every field --
        // channel count, sample rate, every chunk length -- came out byte-swapped, so a perfectly
        // ordinary WAV would have been refused or, worse, accepted with a nonsense sample rate.
        // CnbByteReader assembles its integers this way for exactly the same reason.
        std::uint16_t ReadU16(std::span<const std::uint8_t> bytes, std::size_t offset)
        {
            return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                              static_cast<std::uint16_t>(bytes[offset + 1u] << 8));
        }

        std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset)
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                   (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8) |
                   (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16) |
                   (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24);
        }

        /// The twelve bytes every KSDATAFORMAT_SUBTYPE_* GUID shares, after its leading
        /// format-tag word: `{0000xxxx-0000-0010-8000-00AA00389B71}` in the little-endian layout
        /// the file stores. Checking them is what stops an unrelated GUID whose first two bytes
        /// happen to be 0x0001 from being read as PCM.
        constexpr std::uint8_t kKsDataFormatSubtypeSuffix[12] = {
            0x00u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u,
            0x80u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x38u};
        constexpr std::uint8_t kKsDataFormatSubtypeTail[2] = {0x9Bu, 0x71u};

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
        return ImportImageAsCnbTexture2D(std::filesystem::path(imagePath), options);
    }

    CnbTextureData ImportImageAsCnbTexture2D(const std::filesystem::path& imagePath,
                                              const CnbImageImportOptions& options)
    {
        // Through CNA's own decoder, so the compiled pixels are the pixels the runtime would have
        // loaded. A second image decoder here would be a second answer to "what does this PNG
        // contain", which is the failure mode this whole layer exists to avoid.
        const std::vector<std::uint8_t> bytes = ReadWholeFile(imagePath, "image import");
        CNA::Internal::Graphics::ImageData image = CNA::Internal::Graphics::ImageLoader::LoadFromMemory(
            bytes.data(), bytes.size());
        const std::string origin = CNA::Internal::ContentPathToUtf8(imagePath);
        if (image.width <= 0 || image.height <= 0)
        {
            throw ContentLoadException("CNB image import: '" + origin + "' decoded to " +
                                       std::to_string(image.width) + "x" +
                                       std::to_string(image.height) + ".");
        }
        const std::uint64_t expected =
            static_cast<std::uint64_t>(image.width) * image.height * 4u;
        if (image.pixels.size() != expected)
        {
            throw ContentLoadException(
                "CNB image import: '" + origin + "' decoded to " +
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
        return ImportDdsAsCnbTextureCube(std::filesystem::path(ddsPath));
    }

    CnbTextureData ImportDdsAsCnbTextureCube(const std::filesystem::path& ddsPath)
    {
        const std::vector<std::uint8_t> bytes = ReadWholeFile(ddsPath, "TextureCube import");
        return DecodeDdsAsCnbTextureCube(bytes, CNA::Internal::ContentPathToUtf8(ddsPath));
    }

    CNA::Content::Import::ImportedSound DecodeWavAsImportedSound(
        std::span<const std::uint8_t> wavBytes, const std::string& origin)
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

        // The RIFF header's own length field bounds the chunk list, and it is checked rather than
        // ignored (plans/plan_cnb.md CNBF-117). A declared length longer than the file is a
        // truncated download; a shorter one means the chunks after it are not part of this RIFF
        // form and must not be walked into.
        const std::uint32_t riffSize = ReadU32(wavBytes, 4u);
        if (riffSize < 4u)
        {
            FailWav(origin, "declares a RIFF length of " + std::to_string(riffSize) +
                                " bytes, too short even for its own 'WAVE' form identifier.");
        }
        if (static_cast<std::uint64_t>(riffSize) + 8u > wavBytes.size())
        {
            FailWav(origin, "declares a RIFF length of " + std::to_string(riffSize) +
                                " bytes, which runs past the end of the " +
                                std::to_string(wavBytes.size()) + "-byte file by " +
                                std::to_string(static_cast<std::uint64_t>(riffSize) + 8u -
                                               wavBytes.size()) +
                                " byte(s).");
        }
        const std::size_t riffEnd = 8u + static_cast<std::size_t>(riffSize);

        bool haveFmt = false;
        std::uint16_t formatTag = 0u;
        std::uint16_t channels = 0u;
        std::uint32_t sampleRate = 0u;
        std::uint32_t byteRate = 0u;
        std::uint16_t blockAlign = 0u;
        std::uint16_t bitsPerSample = 0u;
        std::span<const std::uint8_t> data;
        bool haveData = false;
        std::uint32_t loopStart = 0u;
        std::uint32_t loopLength = 0u;

        // One pass over the chunk list, bounded by the RIFF form rather than by the file. Chunks
        // are word-aligned and a chunk that claims to extend past the form is malformed, not
        // something to read anyway.
        std::size_t pos = 12u;
        while (pos + 8u <= riffEnd)
        {
            const std::uint8_t* id = wavBytes.data() + pos;
            const std::string idText(reinterpret_cast<const char*>(id), 4);
            const std::uint32_t chunkSize = ReadU32(wavBytes, pos + 4u);
            const std::size_t start = pos + 8u;
            if (static_cast<std::uint64_t>(start) + chunkSize > riffEnd)
            {
                FailWav(origin, "has a '" + idText + "' chunk claiming " +
                                    std::to_string(chunkSize) +
                                    " bytes, which runs past the end of the RIFF form.");
            }
            const std::size_t chunkEnd = start + chunkSize;

            if (idText == "fmt ")
            {
                if (haveFmt) { FailWav(origin, "has more than one 'fmt ' chunk."); }
                if (chunkSize < 16u) { FailWav(origin, "has a 'fmt ' chunk shorter than 16 bytes."); }
                formatTag = ReadU16(wavBytes, start);
                channels = ReadU16(wavBytes, start + 2u);
                sampleRate = ReadU32(wavBytes, start + 4u);
                byteRate = ReadU32(wavBytes, start + 8u);
                blockAlign = ReadU16(wavBytes, start + 12u);
                bitsPerSample = ReadU16(wavBytes, start + 14u);
                if (formatTag == 0xFFFEu)
                {
                    // WAVE_FORMAT_EXTENSIBLE carries the real tag in the first two bytes of its
                    // SubFormat GUID -- but ONLY for the KSDATAFORMAT_SUBTYPE_* family. Reading
                    // those two bytes without checking the rest of the GUID means any unrelated
                    // format whose GUID happens to start 01 00 is decoded as PCM, which is a wrong
                    // answer rather than a refusal. The remaining fourteen bytes are fixed, so
                    // checking them costs nothing and closes that entirely.
                    if (chunkSize < 40u)
                    {
                        FailWav(origin, "is WAVE_FORMAT_EXTENSIBLE but its 'fmt ' chunk is " +
                                            std::to_string(chunkSize) +
                                            " bytes; the extension needs 40.");
                    }
                    const std::uint16_t cbSize = ReadU16(wavBytes, start + 16u);
                    if (cbSize < 22u)
                    {
                        FailWav(origin, "is WAVE_FORMAT_EXTENSIBLE but declares an extension size "
                                        "of " + std::to_string(cbSize) + " bytes; 22 is required.");
                    }
                    // plans/plan_cnb.md CNBF-122: cbSize is the extension's DECLARED length, and
                    // the 18 bytes of WAVEFORMATEX in front of it plus that length must fit inside
                    // the chunk that carries them. `chunkSize >= 40` above only proves the 22 bytes
                    // this decoder READS are present; a cbSize of 4000 in a 40-byte 'fmt ' is a
                    // header describing a chunk that is not there, and accepting it means trusting
                    // the rest of the same header.
                    if (18u + static_cast<std::uint64_t>(cbSize) > chunkSize)
                    {
                        FailWav(origin, "is WAVE_FORMAT_EXTENSIBLE declaring an extension of " +
                                            std::to_string(cbSize) + " bytes, which with the "
                                            "18-byte WAVEFORMATEX in front of it needs " +
                                            std::to_string(18u + static_cast<std::uint64_t>(cbSize)) +
                                            " bytes; its 'fmt ' chunk holds " +
                                            std::to_string(chunkSize) + ".");
                    }
                    const std::uint16_t validBits = ReadU16(wavBytes, start + 18u);
                    if (validBits != 0u && validBits != bitsPerSample)
                    {
                        FailWav(origin, "declares " + std::to_string(validBits) +
                                            " valid bits inside a " +
                                            std::to_string(bitsPerSample) +
                                            "-bit container; CNB stores whole samples only.");
                    }
                    if (std::memcmp(wavBytes.data() + start + 26u, kKsDataFormatSubtypeSuffix,
                                    sizeof(kKsDataFormatSubtypeSuffix)) != 0 ||
                        std::memcmp(wavBytes.data() + start + 38u, kKsDataFormatSubtypeTail,
                                    sizeof(kKsDataFormatSubtypeTail)) != 0)
                    {
                        FailWav(origin, "is WAVE_FORMAT_EXTENSIBLE with a SubFormat GUID outside "
                                        "the KSDATAFORMAT_SUBTYPE_* family, so its leading bytes "
                                        "are not a wave format tag and this compiler will not "
                                        "guess what the samples are.");
                    }
                    formatTag = ReadU16(wavBytes, start + 24u);
                }
                haveFmt = true;
            }
            else if (idText == "data")
            {
                if (haveData) { FailWav(origin, "has more than one 'data' chunk."); }
                data = wavBytes.subspan(start, chunkSize);
                haveData = true;
            }
            else if (idText == "smpl")
            {
                // Same rules the runtime applies: 36-byte header, then 24-byte loop entries, and
                // only the first entry's Start/End matter.
                //
                // Every read is bounded by the smpl chunk's OWN payload, not by the file
                // (CNBF-117). A 36-byte smpl declaring a loop, followed by any other chunk, used
                // to satisfy `start + 36 + 24 <= fileSize` and take that "loop entry" out of the
                // next chunk's header and contents -- a loop region invented from unrelated bytes,
                // which is exactly the kind of plausible wrong answer a refusal exists to prevent.
                // CNBF-122 widened that from "the first entry fits" to "the declared table fits".
                if (chunkSize >= 36u)
                {
                    const std::uint32_t loops = ReadU32(wavBytes, start + 28u);
                    if (loops != 0u)
                    {
                        // plans/plan_cnb.md CNBF-122: the WHOLE declared loop table must fit, not
                        // just the first entry this importer consumes. A count of 1000 in a
                        // 60-byte 'smpl' is a header that disagrees with its own chunk, and the
                        // first entry's Start/End are read out of that same header -- so believing
                        // one number while refusing to check the other is how a plausible wrong
                        // loop region gets accepted. Computed in std::uint64_t because
                        // `loops * 24` wraps for a count above 178 956 970.
                        //
                        // This subsumes the old "is there room for the FIRST entry?" bound, which
                        // is now unreachable: `loops >= 1` makes `36 + tableBytes >= 60`, so a
                        // chunk that satisfies this one always holds at least one whole entry.
                        // Left as dead code it would read like a case someone still thought about.
                        const std::uint64_t tableBytes = static_cast<std::uint64_t>(loops) * 24u;
                        if (36u + tableBytes > chunkSize)
                        {
                            FailWav(origin, "has a 'smpl' chunk declaring " +
                                                std::to_string(loops) +
                                                " loop(s), which need " +
                                                std::to_string(tableBytes) +
                                                " bytes of loop table, but only " +
                                                std::to_string(chunkSize - 36u) +
                                                " byte(s) follow its 36-byte header.");
                        }
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

            // RIFF pads an odd-length chunk to an even boundary. The pad byte has to be there when
            // anything follows; its value is not constrained here, because real encoders differ
            // and refusing on it would reject files every other tool plays.
            //
            // The pad byte is required only when something FOLLOWS the chunk: an odd-length chunk
            // ending exactly at the form's end needs none, and every real encoder omits it there.
            // `chunkEnd` cannot exceed `riffEnd` -- the bound above already refused that -- so the
            // two cases here are the whole space.
            std::size_t next = chunkEnd;
            if ((chunkSize & 1u) != 0u && chunkEnd < riffEnd) { next = chunkEnd + 1u; }
            pos = next;
        }
        if (pos != riffEnd)
        {
            FailWav(origin, "has " + std::to_string(riffEnd - pos) +
                                " byte(s) after its last chunk, too few to be another chunk "
                                "header; the RIFF form and its chunk list disagree.");
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

        // blockAlign and byteRate are redundant with the three fields above -- which is exactly
        // why they are worth checking (CNBF-117). A file whose blockAlign disagrees with
        // channels x bitsPerSample/8 is describing two different frame layouts, and the one this
        // importer would use to split the data chunk is not necessarily the one the encoder used.
        const std::uint32_t expectedBlockAlign =
            static_cast<std::uint32_t>(bitsPerSample / 8u) * channels;
        if (blockAlign != expectedBlockAlign)
        {
            FailWav(origin, "declares a block alignment of " + std::to_string(blockAlign) +
                                " bytes, but " + std::to_string(channels) + " channel(s) of " +
                                std::to_string(bitsPerSample) + "-bit samples is " +
                                std::to_string(expectedBlockAlign) + ".");
        }
        const std::uint64_t expectedByteRate =
            static_cast<std::uint64_t>(sampleRate) * expectedBlockAlign;
        if (static_cast<std::uint64_t>(byteRate) != expectedByteRate)
        {
            FailWav(origin, "declares a byte rate of " + std::to_string(byteRate) +
                                ", but " + std::to_string(sampleRate) + " Hz x " +
                                std::to_string(expectedBlockAlign) + " bytes per frame is " +
                                std::to_string(expectedByteRate) + ".");
        }

        CNA::Content::Import::ImportedSound sound;
        sound.encoding = bitsPerSample == 16u
                             ? CNA::Content::Import::ImportedPcmEncoding::Signed16LittleEndian
                             : CNA::Content::Import::ImportedPcmEncoding::Unsigned8;
        sound.sampleRate = sampleRate;
        sound.channels = channels;

        const std::uint32_t sourceFrameBytes = expectedBlockAlign;
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

        sound.samples.assign(data.begin(), data.end());

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

    CnbSoundEffectData ProcessImportedSoundEffect(
        const CNA::Content::Import::ImportedSound& imported)
    {
        if (imported.channels != 1u && imported.channels != 2u)
        {
            throw ContentLoadException(
                "CNB SoundEffect processing: imported PCM must be mono or stereo.");
        }
        if (imported.sampleRate == 0u || imported.sampleRate > CnbMaxAudioSampleRate)
        {
            throw ContentLoadException(
                "CNB SoundEffect processing: imported PCM sample rate is outside 1.." +
                std::to_string(CnbMaxAudioSampleRate) + " Hz.");
        }
        if (static_cast<std::uint64_t>(imported.loopStart) + imported.loopLength >
            imported.frameCount)
        {
            throw ContentLoadException(
                "CNB SoundEffect processing: imported loop region exceeds the frame count.");
        }
        const std::uint64_t sourceBytesPerSample =
            imported.encoding == CNA::Content::Import::ImportedPcmEncoding::Unsigned8
                ? 1u
                : imported.encoding ==
                          CNA::Content::Import::ImportedPcmEncoding::Signed16LittleEndian
                      ? 2u
                      : 0u;
        if (sourceBytesPerSample == 0u)
        {
            throw ContentLoadException("CNB SoundEffect processing: unknown imported PCM encoding.");
        }
        const std::uint64_t expectedSourceBytes =
            static_cast<std::uint64_t>(imported.frameCount) * imported.channels *
            sourceBytesPerSample;
        if (imported.samples.size() != expectedSourceBytes)
        {
            throw ContentLoadException(
                "CNB SoundEffect processing: imported PCM holds " +
                std::to_string(imported.samples.size()) + " bytes, but " +
                std::to_string(imported.frameCount) + " frame(s) x " +
                std::to_string(imported.channels) + " channel(s) need " +
                std::to_string(expectedSourceBytes) + ".");
        }

        CnbSoundEffectData sound;
        sound.format = CnbAudioFormat::Pcm16;
        sound.sampleRate = imported.sampleRate;
        sound.channels = imported.channels;
        sound.frameCount = imported.frameCount;
        sound.loopStart = imported.loopStart;
        sound.loopLength = imported.loopLength;

        if (imported.encoding ==
            CNA::Content::Import::ImportedPcmEncoding::Signed16LittleEndian)
        {
            sound.samples = imported.samples;
        }
        else
        {
            // 8-bit WAV samples are UNSIGNED with a bias of 128; 16-bit are signed. (s - 128) << 8
            // is the exact widening, and it is exact -- nothing is rounded or clipped.
            if (imported.samples.size() > std::numeric_limits<std::size_t>::max() / 2u)
            {
                throw ContentLoadException(
                    "CNB SoundEffect processing: widened PCM exceeds addressable memory.");
            }
            sound.samples.resize(imported.samples.size() * 2u);
            for (std::size_t i = 0; i < imported.samples.size(); ++i)
            {
                const auto widened = static_cast<std::int16_t>(
                    (static_cast<int>(imported.samples[i]) - 128) * 256);
                const auto value = static_cast<std::uint16_t>(widened);
                sound.samples[i * 2u] = static_cast<std::uint8_t>(value & 0xFFu);
                sound.samples[i * 2u + 1u] = static_cast<std::uint8_t>(value >> 8);
            }
        }
        return sound;
    }

    CnbSoundEffectData DecodeWavAsCnbSoundEffect(std::span<const std::uint8_t> wavBytes,
                                                  const std::string& origin)
    {
        return ProcessImportedSoundEffect(DecodeWavAsImportedSound(wavBytes, origin));
    }

    CNA::Content::Import::ImportedSound ImportWavAsImportedSound(const std::string& wavPath)
    {
        return ImportWavAsImportedSound(std::filesystem::path(wavPath));
    }

    CNA::Content::Import::ImportedSound ImportWavAsImportedSound(
        const std::filesystem::path& wavPath)
    {
        const std::vector<std::uint8_t> bytes = ReadWholeFile(wavPath, "WAV import");
        return DecodeWavAsImportedSound(bytes, CNA::Internal::ContentPathToUtf8(wavPath));
    }

    CnbSoundEffectData ImportWavAsCnbSoundEffect(const std::string& wavPath)
    {
        return ImportWavAsCnbSoundEffect(std::filesystem::path(wavPath));
    }

    CnbSoundEffectData ImportWavAsCnbSoundEffect(const std::filesystem::path& wavPath)
    {
        return ProcessImportedSoundEffect(ImportWavAsImportedSound(wavPath));
    }
}
