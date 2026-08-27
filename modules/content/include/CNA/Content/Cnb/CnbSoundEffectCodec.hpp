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

        /** @brief Unsigned 8-bit PCM. Identifier reserved; no v1 codec. */
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

    /** @brief Highest `SoundEffect` schema version this build understands. */
    inline constexpr std::uint32_t CnbSoundEffectSchemaVersion = 1u;

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
        /** @brief The sample encoding. Schema 1 writes CnbAudioFormat::Pcm16. */
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
