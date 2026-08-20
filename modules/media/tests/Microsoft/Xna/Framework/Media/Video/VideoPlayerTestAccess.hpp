// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>

// plans/plan_platform.md PLAT-SDL2-8: the mixer engine exists only under SOUND_ENABLED (the SDL3_mixer
// selection). The two accessors that need it are guarded with it; the rest reach VideoPlayer's own
// state and stay available in every audio profile, so a suite that only asks about the decoder or
// the scratch buffer still builds under SDL2/NULL audio.
#ifdef SOUND_ENABLED
#include "CNA/Internal/Audio/MixerEngine.hpp"
#endif
#include "CNA/Internal/Media/VideoDecoder.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

namespace Microsoft::Xna::Framework::Media
{
    // Test-only accessor for VideoPlayer's private decoder_/audioStream_, needed to verify a
    // track switch (SetAudioTrackEXT/SetVideoTrackEXT) actually reconfigured the real
    // audio/video output -- not just "didn't throw" -- and that a freshly-opened audio stream is
    // genuinely resumed, not left paused (the playback stream opens paused by contract;
    // VideoPlayer itself exposes no public getters for either question) --
    // plans/plan_media.md MEDIA-90/MEDIA-131, found by external code review.
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

#ifdef SOUND_ENABLED
        static bool IsAudioStreamDevicePaused(const VideoPlayer& player)
        {
            return CNA::Internal::Audio::IsMixerStreamPaused(player.audioStream_);
        }
#endif

        // Raw pointer identity, not just presence/pause-state -- proves a track switch that
        // shouldn't touch the audio stream at all (e.g. a video-only track switch) genuinely left
        // it alone rather than tearing it down and reopening a new one, which would discard
        // whatever audio was already queued for playback (plans/plan_media.md MEDIA-148, found by
        // external code review).
        static const void* GetAudioStreamPtr(const VideoPlayer& player)
        {
            return player.audioStream_;
        }

        // Size of the transient decoded-audio scratch buffer -- needed to prove it doesn't grow
        // unboundedly when there's no audio stream to drain it into (plans/plan_media.md MEDIA-153,
        // found by external code review).
        static std::size_t GetAudioBufferSize(const VideoPlayer& player)
        {
            return player.audioBuffer_.size();
        }

        // Simulates the audio device becoming unavailable mid-playback (e.g. what
        // ReconfigureAudioOutputForCurrentTrack() already does gracefully when
        // playback-stream creation itself fails) by tearing down a real, already-open stream the
        // same way that function's own failure path does, without touching any other state. Lets a
        // test exercise the "no audio device" code path deterministically against a fixture that
        // genuinely has audio, rather than depending on this sandbox's real audio driver failing.
#ifdef SOUND_ENABLED
        static void SimulateAudioDeviceBecomingUnavailable(VideoPlayer& player)
        {
            if (player.audioStream_)
            {
                CNA::Internal::Audio::DestroyMixerStream(player.audioStream_);
                player.audioStream_ = nullptr;
            }
        }
#endif
    };
}
