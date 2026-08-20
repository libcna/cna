// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal::Media
{
    /// Lightweight audio-file duration probing for MediaLibrary's real Song/Album/Playlist
    /// Duration (plans/plan_media.md MEDIA-65/68 -- "Duration (sum of member Song.Duration)"). Uses
    /// FFmpeg's container/stream metadata parsing only (avformat_find_stream_info) -- no full
    /// audio decode -- so it stays fast enough to run once per song during MediaLibrary's
    /// synchronous constructor-time scan.
    class AudioDurationProbe
    {
    public:
        /// Returns the audio file's duration in whole milliseconds, or 0 if it couldn't be
        /// determined (file unreadable/unsupported, or this build has no FFmpeg available --
        /// Windows/Android/Emscripten, matching Video/VideoPlayer's own platform availability,
        /// see CMakeLists' CNA_FFMPEG_AVAILABLE). A 0 return is indistinguishable from a
        /// genuinely instantaneous/empty file, matching Song's own pre-existing "0 = unknown"
        /// convention for its 2-arg constructor.
        static SharpRuntime::intcs ProbeDurationMS(const std::string& path);
    };
}
