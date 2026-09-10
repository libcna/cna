// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
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
     * `processors/SoundEffectProcessor_pcm_matrix`; plans/plan_xna_sample_xnb_sweep.md
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
     * XNA-compatible `.xnb` output could say (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
     *
     * Everything a writer needs is here and nothing either container invented is: no
     * WAVEFORMATEX field that is not an audio fact, no chunk identifier, no schema version.
     */
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
