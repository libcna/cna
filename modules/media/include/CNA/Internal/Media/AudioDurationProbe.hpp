// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal::Media
{
    /**
     * @brief Probes audio-file duration for MediaLibrary metadata.
     *
     * The optional FFmpeg implementation reads only container and stream metadata, without fully
     * decoding audio, so a library scan remains inexpensive. Decoder-free builds retain this type
     * and report an unknown duration.
     */
    class AudioDurationProbe
    {
    public:
        /**
         * @brief Returns an audio file's duration in whole milliseconds.
         *
         * @param path Path to the audio file.
         * @return Duration in milliseconds, or zero when it cannot be determined or the optional
         *         FFmpeg backend is disabled.
         */
        static SharpRuntime::intcs ProbeDurationMS(const std::string& path);
    };
}
