// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/VideoDecoder.hpp"

#include "System/NotSupportedException.hpp"

namespace CNA::Internal::Media
{
    namespace
    {
        constexpr const char* UnavailableMessage =
            "Video playback is unavailable because CNA was built without the optional FFmpeg "
            "video backend. Configure with -DCNA_ENABLE_VIDEO=ON, or use AUTO with all required "
            "FFmpeg development packages installed.";
    }

    bool IsVideoDecoderAvailable() noexcept
    {
        return false;
    }

    void RequireVideoDecoderAvailable()
    {
        throw System::NotSupportedException(UnavailableMessage);
    }

    VideoDecoder::VideoDecoder() = default;
    VideoDecoder::~VideoDecoder() = default;

    bool VideoDecoder::Open(const std::string&)
    {
        RequireVideoDecoderAvailable();
        return false;
    }

    void VideoDecoder::Close()
    {
    }

    void VideoDecoder::SeekToStart()
    {
        RequireVideoDecoderAvailable();
    }

    bool VideoDecoder::SetAudioStream(int)
    {
        RequireVideoDecoderAvailable();
        return false;
    }

    bool VideoDecoder::SetVideoStream(int)
    {
        RequireVideoDecoderAvailable();
        return false;
    }

    bool VideoDecoder::NextFrame(std::vector<uint8_t>&, double&)
    {
        RequireVideoDecoderAvailable();
        return false;
    }

    void VideoDecoder::DrainAudio(std::vector<float>&)
    {
        RequireVideoDecoderAvailable();
    }
}
