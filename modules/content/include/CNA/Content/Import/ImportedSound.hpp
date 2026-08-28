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
        Unsigned8,
        Signed16LittleEndian,
    };

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
