// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <vector>

namespace CNA::Content::Import
{
    /** @brief Stability marker for source-oriented build data introduced with the pipeline. */
    inline constexpr bool ImportedContentApiIsExperimental = true;

    /** @brief Exact PCM encoding carried by an imported WAV before processing policy is applied. */
    enum class ImportedPcmEncoding
    {
        /** @brief 8-bit unsigned samples biased by 128, as WAV stores them. */
        Unsigned8,

        /** @brief 16-bit signed little-endian samples: the encoding CNB itself stores. */
        Signed16LittleEndian,

        /** @brief 24-bit signed little-endian samples packed three bytes to a sample. */
        Signed24LittleEndian,

        /** @brief 32-bit signed little-endian samples. */
        Signed32LittleEndian,

        /** @brief 32-bit IEEE-754 little-endian samples nominally in `[-1, 1]`. */
        Float32LittleEndian,
    };

    /**
     * @brief Bytes one sample of an encoding occupies.
     *
     * @param encoding The encoding to measure.
     * @return 1, 2, 3 or 4; zero for an unrecognized value.
     */
    [[nodiscard]] constexpr std::uint32_t ImportedPcmSampleBytes(
        const ImportedPcmEncoding encoding)
    {
        switch (encoding)
        {
        case ImportedPcmEncoding::Unsigned8: return 1u;
        case ImportedPcmEncoding::Signed16LittleEndian: return 2u;
        case ImportedPcmEncoding::Signed24LittleEndian: return 3u;
        case ImportedPcmEncoding::Signed32LittleEndian: return 4u;
        case ImportedPcmEncoding::Float32LittleEndian: return 4u;
        }
        return 0u;
    }

    /**
     * @brief Whether converting an encoding to CNB's Pcm16 discards information.
     *
     * @param encoding The source encoding.
     * @return True for the encodings whose samples do not fit exactly in 16 bits.
     */
    [[nodiscard]] constexpr bool ImportedPcmNarrowsToPcm16(const ImportedPcmEncoding encoding)
    {
        return encoding == ImportedPcmEncoding::Signed24LittleEndian ||
               encoding == ImportedPcmEncoding::Signed32LittleEndian ||
               encoding == ImportedPcmEncoding::Float32LittleEndian;
    }

    /**
     * @brief Returns an encoding's name for diagnostics.
     *
     * @param encoding The encoding to name.
     * @return A short human-readable name such as `24-bit PCM`.
     */
    [[nodiscard]] constexpr const char* ImportedPcmEncodingName(
        const ImportedPcmEncoding encoding)
    {
        switch (encoding)
        {
        case ImportedPcmEncoding::Unsigned8: return "8-bit unsigned PCM";
        case ImportedPcmEncoding::Signed16LittleEndian: return "16-bit PCM";
        case ImportedPcmEncoding::Signed24LittleEndian: return "24-bit PCM";
        case ImportedPcmEncoding::Signed32LittleEndian: return "32-bit PCM";
        case ImportedPcmEncoding::Float32LittleEndian: return "32-bit IEEE float";
        }
        return "an unknown encoding";
    }

    /** @brief Source-oriented WAV semantics independent of CNB and any audio device. */
    struct ImportedSound
    {
        /** @brief Exact source PCM encoding. */
        ImportedPcmEncoding encoding = ImportedPcmEncoding::Signed16LittleEndian;

        /** @brief Source sample rate in Hz. */
        std::uint32_t sampleRate = 0u;

        /** @brief Source channel count. */
        std::uint32_t channels = 0u;

        /** @brief Number of source sample frames. */
        std::uint32_t frameCount = 0u;

        /** @brief First source frame of the optional loop region. */
        std::uint32_t loopStart = 0u;

        /** @brief Number of source frames in the loop region; zero means no loop. */
        std::uint32_t loopLength = 0u;

        /** @brief Headerless source PCM bytes in encoding. */
        std::vector<std::uint8_t> samples;
    };
}
