// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

// plan_xnb.md XNB-33/XNB-33A: SoundEffectReader -- see PrimitiveContentTypeReaders.hpp's own note
// on why this lives in CNA::Internal::Xnb (FNA's SoundEffectReader is `internal class`, never
// subclassed by game code).

namespace CNA::Internal::Xnb
{
    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.SoundEffectReader`
     *        (`src/Content/ContentReaders/SoundEffectReader.cs`).
     *
     * **Current coverage** (plan_xnb.md XNB-33's support matrix, scoped to reach the M3
     * milestone with at least one working variant): only `WAVE_FORMAT_PCM` (`wFormatTag == 1`)
     * at 16 bits per sample, mono or stereo, is supported -- CNA's own `SoundEffect` PCM
     * constructors always assume 16-bit signed PCM (`SDL_AUDIO_S16LE`, see `SoundEffect.cpp`),
     * with no format-conversion path for anything else yet. 8-bit PCM, IEEE float, MS-ADPCM,
     * IMA-ADPCM, and XMA2 all parse correctly (the WAVEFORMATEX header and wave data are read
     * byte-for-byte the same as FNA) but throw a clear
     * `Microsoft::Xna::Framework::Content::ContentLoadException` naming the rejected format,
     * rather than silently constructing a `SoundEffect` that would play back as noise.
     */
    class SoundEffectReader
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Audio::SoundEffect>
    {
    public:
        SoundEffectReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Audio::SoundEffect>(
                  "Microsoft.Xna.Framework.Audio.SoundEffect") {}

    protected:
        Microsoft::Xna::Framework::Audio::SoundEffect Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<Microsoft::Xna::Framework::Audio::SoundEffect> existingInstance) override;
    };

    /** @brief Registers SoundEffectReader under its real FNA canonical name. Idempotent. */
    void RegisterSoundEffectXnbReader();
}
