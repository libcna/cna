// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief The sample formats a `.cnb` sound effect stores (plans/plan_cnb.md `CNBF-103A`).
     *
     * A serialization enum of CNB's own, for the same reason `CnbTextureFormat` is one: a file
     * format must not depend on the declaration order of a runtime enumeration, and no audio
     * backend's identifiers are a serialisation ABI.
     */
    enum class CnbAudioFormat : std::uint32_t
    {
        /** @brief Not a valid format; a file declaring it is rejected. */
        Unknown = 0u,

        /** @brief Signed 16-bit little-endian PCM: the portable baseline and CNA's native form. */
        Pcm16 = 1u,

        /** @brief Unsigned 8-bit PCM, as WAV and WAVEFORMATEX store it. Schema 2 and later. */
        Pcm8 = 2u,

        /** @brief 32-bit float PCM. Identifier reserved; no v1 codec. */
        PcmFloat32 = 3u,

        /** @brief IMA/MS ADPCM. Identifier reserved; no v1 codec. */
        Adpcm = 4u,

        /** @brief Vorbis in an Ogg container. Identifier reserved; no v1 codec. */
        Vorbis = 5u,
    };

    /** @brief Highest audio format identifier this build assigns. */
    inline constexpr std::uint32_t CnbAudioFormatMax = 5u;

    /** @brief Chunk identifiers defined by the `SoundEffect` asset schema. */
    namespace CnbSoundEffectChunk
    {
        /** @brief `AUDH` -- format, rate, channels, frame count and loop points. Mandatory, exactly one. */
        inline constexpr CnbChunkId Header = MakeChunkId('A', 'U', 'D', 'H');

        /** @brief `AUDD` -- the sample bytes. Mandatory, exactly one. */
        inline constexpr CnbChunkId Data = MakeChunkId('A', 'U', 'D', 'D');
    }

    /**
     * @brief Highest `SoundEffect` schema version this build understands, and the one it writes.
     *
     * Version 2 activates `CnbAudioFormat::Pcm8`, which version 1 already had an identifier for.
     * **The physical header is unchanged**: `AUDH` is the same 28 bytes in the same order, and the
     * only difference is which `format` identifiers a file of that version may declare. Version 1
     * still means Pcm16 and nothing else -- a version-1 file declaring Pcm8 is refused, so no
     * historical file changes meaning -- and a version-1 file already on disk still reads.
     *
     * The width has to survive the pipeline because the genuine XNA one preserves it: an 8-bit
     * source comes out of `SoundEffectProcessor` 8-bit, and CNB was the reason CNA's did not
     * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
     */
    inline constexpr std::uint32_t CnbSoundEffectSchemaVersion = 2u;

    /**
     * @brief The lowest `SoundEffect` schema version that can express a sample format.
     *
     * @param format The sample encoding.
     * @return 1 for `Pcm16`, 2 for `Pcm8`, and 0 for a format no schema version can write.
     */
    [[nodiscard]] std::uint32_t CnbSoundEffectSchemaVersionForFormat(CnbAudioFormat format);

    /** @brief Bytes the `AUDH` chunk occupies. */
    inline constexpr std::uint32_t CnbSoundEffectHeaderStride = 28u;

    /** @brief Highest sample rate a file may declare, in Hz. */
    inline constexpr std::uint32_t CnbMaxAudioSampleRate = 384000u;

    /**
     * @brief The decoded contents of a `SoundEffect` `.cnb`.
     *
     * `samples` is headerless little-endian PCM in `format` — not a WAV or other container's raw
     * bytes. That is what the runtime's raw-buffer constructor takes, and storing anything else
     * would mean decoding a container at load time for no benefit.
     */
    struct CnbSoundEffectData
    {
        /** @brief The sample encoding: `Pcm16` (schema 1 and 2) or `Pcm8` (schema 2). */
        CnbAudioFormat format = CnbAudioFormat::Pcm16;

        /** @brief Sample rate in Hz; 1…384000. */
        std::uint32_t sampleRate = 44100u;

        /** @brief Channel count: 1 for mono, 2 for stereo. */
        std::uint32_t channels = 1u;

        /** @brief Number of sample frames, i.e. samples per channel. */
        std::uint32_t frameCount = 0u;

        /** @brief First frame of the loop region. */
        std::uint32_t loopStart = 0u;

        /** @brief Number of frames in the loop region; 0 means no loop. */
        std::uint32_t loopLength = 0u;

        /** @brief Headerless little-endian sample bytes. */
        std::vector<std::uint8_t> samples;
    };

    /**
     * @brief Bytes one sample frame occupies in @p format.
     *
     * @param format   The sample encoding.
     * @param channels The channel count.
     * @return The frame size in bytes, or 0 when @p format has no fixed frame size.
     */
    [[nodiscard]] std::uint32_t CnbAudioFrameBytes(CnbAudioFormat format, std::uint32_t channels);

    /**
     * @brief Renders an audio format identifier for diagnostics.
     *
     * @param format The format to render.
     * @return The format's name, or `"unknown"` plus its value.
     */
    [[nodiscard]] std::string CnbAudioFormatToString(CnbAudioFormat format);

    /**
     * @brief The sound's samples as signed 16-bit little-endian PCM.
     *
     * The one place a width has to be given up rather than preserved: CNA's `SoundEffect` takes a
     * raw 16-bit PCM buffer, so a `Pcm8` file is widened when it is *loaded*, not when it is
     * built. `(sample - 128) * 256` is exact -- nothing is rounded and nothing clips -- so a
     * round trip through the runtime loses no information either.
     *
     * @param data The decoded sound.
     * @return The samples in `Pcm16`, copied unchanged when the sound already is.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the format is one with
     *         no codec in this build.
     */
    [[nodiscard]] std::vector<std::uint8_t> CnbSoundEffectSamplesAsPcm16(
        const CnbSoundEffectData& data);

    /**
     * @brief Encodes a `SoundEffect` as a complete `.cnb` byte image.
     *
     * @param data        The sound to encode.
     * @param contentName Logical content name recorded in the `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the description is
     *         inconsistent, the loop region falls outside the sound, or the format is a reserved
     *         identifier with no v1 codec.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeSoundEffectToCnb(
        const CnbSoundEffectData& data, const std::string& contentName = {});

    /**
     * @brief Decodes a `SoundEffect` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded sound description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document is not a
     *         `SoundEffect`, uses an unsupported schema version, is missing a mandatory chunk, or
     *         declares counts that disagree with the payload length.
     */
    [[nodiscard]] CnbSoundEffectData DecodeSoundEffectFromCnb(const CnbDocument& document);
}
