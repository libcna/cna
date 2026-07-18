// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Media/VideoDecoder.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

namespace Microsoft::Xna::Framework::Media
{
    // Test-only accessor for VideoPlayer's private decoder_, needed to verify a track switch
    // (SetAudioTrackEXT/SetVideoTrackEXT) actually reconfigured the real audio/video output --
    // not just "didn't throw" -- since VideoPlayer itself exposes no public sample-rate/dimension
    // getters of its own (plan_media.md MEDIA-90, found by external code review).
    struct VideoPlayerTestAccess
    {
        static int GetDecoderSampleRate(const VideoPlayer& player)
        {
            return player.decoder_ ? player.decoder_->GetSampleRate() : -1;
        }

        static int GetDecoderChannels(const VideoPlayer& player)
        {
            return player.decoder_ ? player.decoder_->GetChannels() : -1;
        }

        static int GetDecoderWidth(const VideoPlayer& player)
        {
            return player.decoder_ ? player.decoder_->GetWidth() : -1;
        }
    };
}
