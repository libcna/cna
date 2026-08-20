// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

// plans/plan_xnb.md XNB-33/XNB-33A: SoundEffectReader -- see PrimitiveContentTypeReaders.hpp's own note
// on why this lives in CNA::Internal::Xnb (FNA's SoundEffectReader is `internal class`, never
// subclassed by game code).

namespace CNA::Internal::Xnb
{
    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.SoundEffectReader`
     *        (`src/Content/ContentReaders/SoundEffectReader.cs`).
     *
     * **Current coverage** (`plans/plan_audio.md` AUD-06 support matrix, widened 2026-07-17 from the
     * original PCM16-only M3 baseline): 16-bit PCM uses CNA's own direct `SoundEffect` raw-buffer
     * constructor (signed 16-bit little-endian, unchanged fast path). 8-bit PCM, 32-bit IEEE
     * float, and MS/IMA ADPCM are wrapped in a minimal synthetic in-memory WAV file
     * (`CNA::Internal::Audio::BuildWavFromWaveFormatEx`, forwarding the real captured
     * WAVEFORMATEX extension bytes verbatim) and decoded via `SoundEffect::FromStream`
     * through the audio module's mixer facade instead of requiring the content module to own a
     * decoder or a native stream. XMA2 remains rejected -- no decode path exists anywhere in this
     * stack -- with a clear
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
