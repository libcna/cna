// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Logger.hpp"

#include <sstream>

namespace CNA::Internal::Audio
{
    /// Streams the parts into one line and hands it to CNA::Logger, so an audio diagnostic
    /// reaches the sink a consumer installed (cna_logger_set_sink_ext) rather than raw stderr.
    template <typename... Parts>
    void LogAudio(const LogLevel level, const Parts&... parts)
    {
        std::ostringstream line;
        (line << ... << parts);
        Logger::Log(level, line.str(), LogCategory::AUDIO);
    }
}
