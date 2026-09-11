// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"

#include <exception>
#include <stdexcept>

#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Audio::AudioChannels;
    using Microsoft::Xna::Framework::Audio::SoundEffect;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    SoundEffect SoundEffectReader::Read(
        ContentReader& input, std::optional<SoundEffect> existingInstance)
    {
        (void)existingInstance;
        const XnbSoundEffectData decoded = DecodeSoundEffectXnbData(input);
        if (decoded.sampleRate == 0u)
        {
            try
            {
                throw std::invalid_argument("Invalid sample rate: zero");
            }
            catch (const std::exception& inner)
            {
                const bool directPcm16 =
                    decoded.formatTag == 0x0001u && decoded.bitsPerSample == 16u;
                throw ContentLoadException(
                    "'" + input.getAssetNameProperty() +
                        (directPcm16
                             ? "': SoundEffectReader failed to construct 16-bit PCM audio "
                             : "': SoundEffectReader failed to decode WAV-wrapped audio ") +
                        "(channels=" + std::to_string(decoded.channels) +
                        ", sampleRate=" + std::to_string(decoded.sampleRate) + ").",
                    inner);
            }
        }
        const CNA::Content::Import::ImportedSound imported =
            ConvertXnbSoundToImportedSound(decoded, input.getAssetNameProperty(), true);
        const CNA::Content::Cnb::CnbSoundEffectData sound =
            CNA::Content::Cnb::ProcessImportedSoundEffect(imported);
        // `SoundEffect` takes a 16-bit PCM buffer, so an 8-bit payload -- which the processing
        // above now keeps at its own width -- is widened here and nowhere earlier
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
        const std::vector<std::uint8_t> samples =
            CNA::Content::Cnb::CnbSoundEffectSamplesAsPcm16(sound);
        try
        {
            SoundEffect result(
                samples, 0, static_cast<std::int32_t>(samples.size()),
                static_cast<std::int32_t>(sound.sampleRate),
                static_cast<AudioChannels>(sound.channels),
                static_cast<std::int32_t>(sound.loopStart),
                static_cast<std::int32_t>(sound.loopLength));
            result.setNameProperty(input.getAssetNameProperty());
            return result;
        }
        catch (const std::exception& inner)
        {
            throw ContentLoadException(
                "'" + input.getAssetNameProperty() +
                    "': SoundEffectReader failed to construct decoded PCM16 audio.",
                inner);
        }
    }

    void RegisterSoundEffectXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SoundEffectReader",
            [] { return std::make_unique<SoundEffectReader>(); });
    }
}
