// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"

#include <string>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        constexpr const char* kCanonicalName = "Microsoft.Xna.Framework.Audio.SoundEffect";

        // 16-byte payload alignment, matching the texture payloads: a future memory-mapped or
        // streaming reader (CNBF-108) wants a sample buffer it can hand to a backend without
        // copying to realign it.
        constexpr std::uint32_t kPayloadAlignment = 16u;

        [[noreturn]] void Fail(const std::string& what)
        {
            throw ContentLoadException(std::string("CNB SoundEffect: ") + what);
        }

        /// The rules both directions share. A `reader` is supplied on the decode side so the
        /// diagnostic names the file rather than reading like a programming error.
        void ValidateSound(const CnbSoundEffectData& data, CnbByteReader* reader)
        {
            const auto fail = [&](const std::string& what)
            {
                if (reader != nullptr) { reader->Fail(what); }
                Fail(what);
            };

            if (data.format != CnbAudioFormat::Pcm16)
            {
                fail("schema 1 stores Pcm16 only; " + CnbAudioFormatToString(data.format) +
                     " is a reserved identifier with no codec in this build.");
            }
            if (data.sampleRate == 0u || data.sampleRate > CnbMaxAudioSampleRate)
            {
                fail("declares a sample rate of " + std::to_string(data.sampleRate) +
                     " Hz; the range is 1-" + std::to_string(CnbMaxAudioSampleRate) + ".");
            }
            if (data.channels != 1u && data.channels != 2u)
            {
                fail("declares " + std::to_string(data.channels) +
                     " channels; XNA's AudioChannels is Mono(1) or Stereo(2).");
            }

            const std::uint32_t frameBytes = CnbAudioFrameBytes(data.format, data.channels);
            const std::uint64_t expected =
                CheckedMultiply(data.frameCount, frameBytes, "CNB SoundEffect");
            if (data.samples.size() != expected)
            {
                fail("declares " + std::to_string(data.frameCount) + " frames of " +
                     std::to_string(frameBytes) + " bytes, which is " + std::to_string(expected) +
                     ", but the sample data is " + std::to_string(data.samples.size()) +
                     " bytes.");
            }

            // A loop region reaching past the end is the failure that would otherwise be an
            // out-of-range read inside the mixer, at playback time, on someone else's machine.
            const std::uint64_t loopEnd =
                CheckedAdd(data.loopStart, data.loopLength, "CNB SoundEffect");
            if (loopEnd > data.frameCount)
            {
                fail("its loop region covers frames " + std::to_string(data.loopStart) + "-" +
                     std::to_string(loopEnd) + ", but the sound is only " +
                     std::to_string(data.frameCount) + " frames long.");
            }
        }
    }

    std::uint32_t CnbAudioFrameBytes(CnbAudioFormat format, std::uint32_t channels)
    {
        switch (format)
        {
            case CnbAudioFormat::Pcm16: return 2u * channels;
            case CnbAudioFormat::Pcm8: return 1u * channels;
            case CnbAudioFormat::PcmFloat32: return 4u * channels;
            // ADPCM and Vorbis are block/packet formats with no fixed frame size, so a file using
            // one cannot have its length checked this way. Neither has a v1 codec; when one gains
            // it, it needs its own length rule rather than this one.
            case CnbAudioFormat::Adpcm:
            case CnbAudioFormat::Vorbis:
            case CnbAudioFormat::Unknown:
            default: return 0u;
        }
    }

    std::string CnbAudioFormatToString(CnbAudioFormat format)
    {
        switch (format)
        {
            case CnbAudioFormat::Pcm16: return "Pcm16";
            case CnbAudioFormat::Pcm8: return "Pcm8";
            case CnbAudioFormat::PcmFloat32: return "PcmFloat32";
            case CnbAudioFormat::Adpcm: return "Adpcm";
            case CnbAudioFormat::Vorbis: return "Vorbis";
            case CnbAudioFormat::Unknown: return "Unknown";
            default:
                return "unknown audio format " +
                       std::to_string(static_cast<std::uint32_t>(format));
        }
    }

    std::vector<std::uint8_t> EncodeSoundEffectToCnb(const CnbSoundEffectData& data,
                                                     const std::string& contentName)
    {
        ValidateSound(data, nullptr);

        CnbByteWriter header;
        header.WriteU32(static_cast<std::uint32_t>(data.format));
        header.WriteU32(data.sampleRate);
        header.WriteU32(data.channels);
        header.WriteU32(data.frameCount);
        header.WriteU32(data.loopStart);
        header.WriteU32(data.loopLength);
        header.WriteU32(0u); // flags: reserved, must be zero

        CnbWriter writer(CnbAssetTypeId::SoundEffect, CnbSoundEffectSchemaVersion);
        writer.SetMetadata(kCanonicalName, contentName);
        writer.AddChunk(CnbSoundEffectChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbSoundEffectChunk::Data, data.samples, CnbChunkFlags::Mandatory,
                        kPayloadAlignment);
        return writer.Build();
    }

    CnbSoundEffectData DecodeSoundEffectFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::SoundEffect, CnbSoundEffectSchemaVersion);
        const CnbChunkId known[] = {CnbSoundEffectChunk::Header, CnbSoundEffectChunk::Data};
        document.RequireMandatoryChunksUnderstood(known);

        CnbByteReader header =
            document.OpenChunk(document.RequireSingle(CnbSoundEffectChunk::Header));
        CnbSoundEffectData data;
        const std::uint32_t rawFormat = header.ReadU32();
        data.sampleRate = header.ReadU32();
        data.channels = header.ReadU32();
        data.frameCount = header.ReadU32();
        data.loopStart = header.ReadU32();
        data.loopLength = header.ReadU32();
        const std::uint32_t flags = header.ReadU32();
        header.RequireExhausted();

        if (rawFormat == 0u || rawFormat > CnbAudioFormatMax)
        {
            header.Fail("declares audio format " + std::to_string(rawFormat) +
                        ", which this build does not understand.");
        }
        if (flags != 0u)
        {
            header.Fail("sets reserved flag bits; this schema version defines none.");
        }
        data.format = static_cast<CnbAudioFormat>(rawFormat);

        const std::span<const std::uint8_t> samples =
            document.ChunkData(document.RequireSingle(CnbSoundEffectChunk::Data));
        data.samples.assign(samples.begin(), samples.end());

        ValidateSound(data, &header);
        return data;
    }
}
