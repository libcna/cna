// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace CNA::Content::Pipeline
{
    /**
     * @brief The sample encodings a processed sound effect can carry.
     *
     * Exactly the two the genuine XNA `WavImporter` accepts. Measured over the whole PCM matrix --
     * 8, 16, 24 and 32-bit integer and IEEE float, mono and stereo, at 8 kHz, 22.05 kHz and
     * 44.1 kHz: the two below pass through the importer and `AudioContent.ConvertFormat(Pcm, Best)`
     * with their width, block alignment, byte rate, frame count and loop points unchanged, and
     * every wider one is refused at import with *"Only 8-bit and 16-bit audio data is
     * supported."* (`tests/reference/xna40/audio/audio-content-oracle.json`,
     * `processors/SoundEffectProcessor_pcm_matrix`;
     * `XNASWEEP-197`).
     */
    enum class ProcessedPcmEncoding
    {
        /** @brief 8-bit unsigned samples biased by 128, as WAV and WAVEFORMATEX store them. */
        Unsigned8,

        /** @brief 16-bit signed little-endian samples. */
        Signed16LittleEndian,
    };

    /**
     * @brief Bytes one sample of an encoding occupies.
     *
     * @param encoding The encoding to measure.
     * @return 1 or 2; zero for an unrecognized value.
     */
    [[nodiscard]] constexpr std::uint32_t ProcessedPcmSampleBytes(
        const ProcessedPcmEncoding encoding)
    {
        switch (encoding)
        {
        case ProcessedPcmEncoding::Unsigned8: return 1u;
        case ProcessedPcmEncoding::Signed16LittleEndian: return 2u;
        }
        return 0u;
    }

    /**
     * @brief Bits one sample of an encoding occupies, as WAVEFORMATEX counts them.
     *
     * @param encoding The encoding to measure.
     * @return 8 or 16; zero for an unrecognized value.
     */
    [[nodiscard]] constexpr std::uint32_t ProcessedPcmBitsPerSample(
        const ProcessedPcmEncoding encoding)
    {
        return ProcessedPcmSampleBytes(encoding) * 8u;
    }

    /**
     * @brief Returns an encoding's name for diagnostics.
     *
     * @param encoding The encoding to name.
     * @return A short human-readable name.
     */
    [[nodiscard]] constexpr const char* ProcessedPcmEncodingName(
        const ProcessedPcmEncoding encoding)
    {
        switch (encoding)
        {
        case ProcessedPcmEncoding::Unsigned8: return "8-bit unsigned PCM";
        case ProcessedPcmEncoding::Signed16LittleEndian: return "16-bit PCM";
        }
        return "an unknown encoding";
    }

    /**
     * @brief A sound effect as the processor settles it, before any container has a say.
     *
     * `SoundEffectProcessor` answers this, and both writers adapt it: the `.xnb` writer to a
     * WAVEFORMATEX and its payload, the `.cnb` writer to the `SoundEffect` schema. Neither
     * container owns the representation, which is the point -- the processor used to answer
     * `CNA.Content.Cnb.SoundEffectData` and the CNB schema's 16-bit-only rule then decided what
     * XNA-compatible `.xnb` output could say (`XNASWEEP-197`).
     *
     * Everything a writer needs is here and nothing either container invented is: no
     * WAVEFORMATEX field that is not an audio fact, no chunk identifier, no schema version.
     */
    /**
     * @brief A sound effect a build-time service encoded to a console-native format (L16).
     *
     * The XNA Xbox 360 build writes XMA rather than PCM. With an external XMA encoder attached,
     * the SoundEffect processor fills this for that target; the XNB writer then emits these fields
     * instead of the PCM description. The extension is little-endian, as the encoder wrote it.
     */
    struct EncodedSoundEffectFormat
    {
        /** @brief WAVEFORMATEX format tag (0x0166 for XMA2). */
        std::uint16_t formatTag = 0u;
        /** @brief Channel count. */
        std::uint16_t channels = 0u;
        /** @brief Sample rate in Hz, as the encoder declared it. */
        std::uint32_t sampleRate = 0u;
        /** @brief Average bytes per second. */
        std::uint32_t averageBytesPerSecond = 0u;
        /** @brief Block alignment. */
        std::uint16_t blockAlign = 0u;
        /** @brief Bits per sample. */
        std::uint16_t bitsPerSample = 0u;
        /** @brief The format extension after cbSize, little-endian. */
        std::vector<std::uint8_t> extension;
        /** @brief The encoded payload. */
        std::vector<std::uint8_t> data;
        /** @brief The loop start the encoder measured, in its own frames. */
        std::int32_t loopStart = 0;
        /** @brief The loop length the encoder measured. */
        std::int32_t loopLength = 0;
    };

    struct ProcessedSoundEffect
    {
        /** @brief The sample encoding, preserved from the source. */
        ProcessedPcmEncoding encoding = ProcessedPcmEncoding::Signed16LittleEndian;

        /** @brief Sample rate in Hz. */
        std::uint32_t sampleRate = 0u;

        /** @brief Channel count: 1 for mono, 2 for stereo. */
        std::uint32_t channels = 0u;

        /** @brief Number of sample frames, i.e. samples per channel. */
        std::uint32_t frameCount = 0u;

        /** @brief First frame of the loop region. */
        std::uint32_t loopStart = 0u;

        /** @brief Number of frames in the loop region. */
        std::uint32_t loopLength = 0u;

        /** @brief Headerless little-endian sample bytes in `encoding`. */
        std::vector<std::uint8_t> samples;

        /** @brief The target-native encoding, when the build produced one (L16). */
        std::optional<EncodedSoundEffectFormat> encoded;
    };

    /**
     * @brief Bytes one sample frame of a processed sound occupies.
     *
     * @param sound The sound to measure.
     * @return The frame size in bytes.
     */
    [[nodiscard]] constexpr std::uint32_t ProcessedFrameBytes(const ProcessedSoundEffect& sound)
    {
        return ProcessedPcmSampleBytes(sound.encoding) * sound.channels;
    }
}
