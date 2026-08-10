// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/AudioDurationProbe.hpp"

#ifdef CNA_FFMPEG_AVAILABLE
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
#endif

namespace CNA::Internal::Media
{
#ifdef CNA_FFMPEG_AVAILABLE
    SharpRuntime::intcs AudioDurationProbe::ProbeDurationMS(const std::string& path)
    {
        AVFormatContext* fmtCtx = nullptr;
        if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0)
        {
            return 0;
        }
        if (avformat_find_stream_info(fmtCtx, nullptr) < 0 || fmtCtx->duration == AV_NOPTS_VALUE)
        {
            avformat_close_input(&fmtCtx);
            return 0;
        }

        const int64_t durationMs = fmtCtx->duration / (AV_TIME_BASE / 1000);
        avformat_close_input(&fmtCtx);

        return durationMs > 0 ? static_cast<SharpRuntime::intcs>(durationMs) : 0;
    }
#else
    SharpRuntime::intcs AudioDurationProbe::ProbeDurationMS(const std::string&)
    {
        // No FFmpeg on this platform (Windows/Android/Emscripten) -- matches Video/VideoPlayer's
        // own unavailability there. Callers treat 0 as "unknown", not an error.
        return 0;
    }
#endif
}
