// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL_audio.h>

#include "CNA/Internal/Media/VideoDecoder.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

namespace Microsoft::Xna::Framework::Media
{
    // Test-only accessor for VideoPlayer's private decoder_/audioStream_, needed to verify a
    // track switch (SetAudioTrackEXT/SetVideoTrackEXT) actually reconfigured the real
    // audio/video output -- not just "didn't throw" -- and that a freshly-opened audio stream is
    // genuinely resumed, not left paused (SDL_OpenAudioDeviceStream() opens every stream paused
    // by default; VideoPlayer itself exposes no public getters for either question) --
    // plan_media.md MEDIA-90/MEDIA-131, found by external code review.
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

        // Returns true if there is a real audio stream and its device is paused, false if there
        // is a real audio stream and it's genuinely playing, or a sentinel (via out-param) if
        // there is no audio stream at all (a video with no audio track).
        static bool HasAudioStream(const VideoPlayer& player)
        {
            return player.audioStream_ != nullptr;
        }

        static bool IsAudioStreamDevicePaused(const VideoPlayer& player)
        {
            return player.audioStream_ != nullptr && SDL_AudioStreamDevicePaused(player.audioStream_);
        }
    };
}
