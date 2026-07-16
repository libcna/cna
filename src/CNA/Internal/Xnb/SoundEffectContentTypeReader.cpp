// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Audio::AudioChannels;
    using Microsoft::Xna::Framework::Audio::SoundEffect;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    namespace
    {
        // FNA's SoundEffectReader.Swap(bool, ushort/uint) -- only ever exercised on the Xbox
        // ('x') platform byte-order, which real fixtures used by this port never target, but
        // ported verbatim rather than dropped (matches this plan's precedent for FNA's own
        // unreachable-in-practice code, e.g. the LZX decoder's Intel E8 path).
        uint16_t Swap16(bool swap, uint16_t x)
        {
            if (!swap) return x;
            return static_cast<uint16_t>(((x >> 8) & 0x00FF) | ((x << 8) & 0xFF00));
        }

        uint32_t Swap32(bool swap, uint32_t x)
        {
            if (!swap) return x;
            return ((x >> 24) & 0x000000FFu) | ((x >> 8) & 0x0000FF00u) |
                   ((x << 8) & 0x00FF0000u) | ((x << 24) & 0xFF000000u);
        }

        constexpr uint16_t kWaveFormatPcm = 0x0001;
        constexpr uint16_t kWaveFormatXma2 = 0x0166;
    }

    SoundEffect SoundEffectReader::Read(ContentReader& input, std::optional<SoundEffect> existingInstance)
    {
        (void)existingInstance; // never provided: getCanDeserializeIntoExistingObjectProperty() defaults false, matching FNA

        const bool se = input.getPlatformProperty() == 'x';

        const uint32_t formatLength = input.ReadUInt32();
        const uint16_t wFormatTag = Swap16(se, input.ReadUInt16());
        const uint16_t nChannels = Swap16(se, input.ReadUInt16());
        const uint32_t nSamplesPerSec = Swap32(se, input.ReadUInt32());
        Swap32(se, input.ReadUInt32());  // nAvgBytesPerSec -- unused, matches FNA
        Swap16(se, input.ReadUInt16());  // nBlockAlign -- unused, matches FNA
        const uint16_t wBitsPerSample = Swap16(se, input.ReadUInt16());

        if (formatLength > 16)
        {
            const uint16_t cbSize = Swap16(se, input.ReadUInt16());
            if (wFormatTag == kWaveFormatXma2 && cbSize == 34)
            {
                // XMA2's extra FAudioXMA2WaveFormatEx fields -- consumed only for stream-position
                // correctness; CNA's SoundEffect has no XMA2 decode path, so XMA2 is rejected
                // below regardless of these values.
                input.ReadUInt16();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadByte();
                input.ReadByte();
                input.ReadUInt16();
                const int64_t skip = static_cast<int64_t>(formatLength) - 18 - 34;
                if (skip < 0)
                {
                    throw ContentLoadException(
                        "'" + input.getAssetNameProperty() + "': SoundEffectReader formatLength too "
                        "small for its XMA2 extension.");
                }
                input.ReadBytesExactOrThrow(static_cast<int32_t>(skip), "SoundEffectReader");
            }
            else
            {
                const int64_t skip = static_cast<int64_t>(formatLength) - 18;
                if (skip < 0)
                {
                    throw ContentLoadException(
                        "'" + input.getAssetNameProperty() + "': SoundEffectReader formatLength too "
                        "small for its format extension.");
                }
                input.ReadBytesExactOrThrow(static_cast<int32_t>(skip), "SoundEffectReader");
            }
        }

        std::vector<uint8_t> data = input.ReadBytesExactOrThrow(input.ReadInt32(), "SoundEffectReader");

        const int32_t loopStart = input.ReadInt32();
        const int32_t loopLength = input.ReadInt32();
        input.ReadUInt32(); // duration in milliseconds -- unused, matches FNA

        if (wFormatTag != kWaveFormatPcm || wBitsPerSample != 16)
        {
            throw ContentLoadException(
                "'" + input.getAssetNameProperty() + "': unsupported SoundEffect wave format "
                "(formatTag=" + std::to_string(wFormatTag) + ", bitsPerSample=" +
                std::to_string(wBitsPerSample) + "). CNA's .xnb SoundEffectReader currently only "
                "supports 16-bit PCM (plan_xnb.md XNB-33 support matrix).");
        }
        if (nChannels != 1 && nChannels != 2)
        {
            throw ContentLoadException(
                "'" + input.getAssetNameProperty() + "': unsupported SoundEffect channel count (" +
                std::to_string(nChannels) + "); only mono and stereo are supported.");
        }

        SoundEffect effect(
            data, 0, static_cast<int32_t>(data.size()),
            static_cast<int32_t>(nSamplesPerSec),
            static_cast<AudioChannels>(nChannels),
            loopStart, loopLength);
        effect.setNameProperty(input.getAssetNameProperty());
        return effect;
    }

    void RegisterSoundEffectXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SoundEffectReader",
            [] { return std::make_unique<SoundEffectReader>(); });
    }
}
