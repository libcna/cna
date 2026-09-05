// SPDX-License-Identifier: MS-PL
#pragma once

#include <string_view>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Audio
{
    /** @brief The file format an audio source was read from. */
    enum class AudioFileType
    {
        /** @brief A RIFF/WAVE file. */
        Wav = 0,
        /** @brief An MP3 file. */
        Mp3 = 1,
        /** @brief A Windows Media Audio file. */
        Wma = 2,
    };

    /** @brief The encoding `AudioContent::ConvertFormat` produces. */
    enum class ConversionFormat
    {
        /** @brief Uncompressed PCM. */
        Pcm = 0,
        /** @brief Microsoft ADPCM. */
        Adpcm = 1,
        /** @brief Windows Media Audio. */
        WindowsMedia = 2,
        /** @brief Xbox 360 XMA. */
        Xma = 3,
    };

    /** @brief How much of the source a conversion keeps. */
    enum class ConversionQuality
    {
        /** @brief The smallest result. */
        Low = 0,
        /** @brief Between the two. */
        Medium = 1,
        /** @brief The closest to the source. */
        Best = 2,
    };

    /** @brief .NET full name of `AudioFileType`. */
    CNAEXT inline constexpr std::string_view AudioFileTypeXnaTypeName =
        "Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioFileType";

    /** @brief .NET full name of `ConversionFormat`. */
    CNAEXT inline constexpr std::string_view ConversionFormatXnaTypeName =
        "Microsoft.Xna.Framework.Content.Pipeline.Audio.ConversionFormat";

    /** @brief .NET full name of `ConversionQuality`. */
    CNAEXT inline constexpr std::string_view ConversionQualityXnaTypeName =
        "Microsoft.Xna.Framework.Content.Pipeline.Audio.ConversionQuality";
}
