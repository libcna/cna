// SPDX-License-Identifier: MS-PL
#include "Backend/CnaMixer/CnaMixer.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace CNA::Internal::Audio
{
    namespace
    {
        // The largest block one Render() pass mixes at a time; a larger request is done in blocks.
        constexpr int kBlockFrames = 1024;

        float ClampRatio(const float ratio) noexcept
        {
            return ratio < 0.01f ? 0.01f : (ratio > 100.0f ? 100.0f : ratio);
        }

        void ReadStreamFrame(const MixerStream& stream, const std::size_t offset,
                             std::array<float, 2>& frame) noexcept
        {
            const std::byte* data = stream.queue.data() + offset;
            const int channels = stream.format.channels;
            if (stream.format.sampleFormat == MixerSampleFormat::Float32)
            {
                float first = 0.0f;
                std::memcpy(&first, data, sizeof(float));
                float second = first;
                if (channels > 1)
                {
                    std::memcpy(&second, data + sizeof(float), sizeof(float));
                }
                frame = {first, second};
                return;
            }
            std::int16_t first = 0;
            std::memcpy(&first, data, sizeof(std::int16_t));
            std::int16_t second = first;
            if (channels > 1)
            {
                std::memcpy(&second, data + sizeof(std::int16_t), sizeof(std::int16_t));
            }
            frame = {static_cast<float>(first) / 32768.0f, static_cast<float>(second) / 32768.0f};
        }

        std::size_t StreamFrameBytes(const MixerStream& stream) noexcept
        {
            const std::size_t sample =
                stream.format.sampleFormat == MixerSampleFormat::Float32 ? sizeof(float)
                                                                         : sizeof(std::int16_t);
            return sample * static_cast<std::size_t>(stream.format.channels);
        }
    }

    CnaMixer::CnaMixer(const int sampleRate) : sampleRate_(sampleRate)
    {
        scratch_.resize(static_cast<std::size_t>(kBlockFrames) * 2);
    }

    CnaMixer::~CnaMixer()
    {
        for (MixerTrack* track : tracks_)
        {
            delete track;
        }
        for (MixerTrack* track : deferred_)
        {
            delete track;
        }
    }

    MixerTrack* CnaMixer::CreateTrack()
    {
        auto* track = std::make_unique<MixerTrack>().release();
        try
        {
            tracks_.push_back(track);
            // Room for every track to be deferred at once, reserved here on the caller's thread so
            // moving one to the deferred list during a mix never allocates on the audio thread.
            deferred_.reserve(tracks_.size() + deferred_.size());
        }
        catch (...)
        {
            tracks_.erase(std::remove(tracks_.begin(), tracks_.end(), track), tracks_.end());
            delete track;
            throw;
        }
        return track;
    }

    void CnaMixer::DestroyTrack(MixerTrack* track) noexcept
    {
        if (track == nullptr || track->destroyDeferred)
        {
            // Already on its way out: a second request must not free it twice.
            return;
        }
        if (track->inCallback || rendering_)
        {
            // From inside one of its callbacks, or while the mix walks the track list: leave the
            // mix now, free later.
            track->destroyDeferred = true;
            track->state = MixerTrack::State::Stopped;
            return;
        }
        tracks_.erase(std::remove(tracks_.begin(), tracks_.end(), track), tracks_.end());
        delete track;
    }

    void CnaMixer::RemoveDeferredFromMix() noexcept
    {
        for (auto it = tracks_.begin(); it != tracks_.end();)
        {
            if ((*it)->destroyDeferred)
            {
                // Never allocates: CreateTrack() reserved room for every track.
                deferred_.push_back(*it);
                it = tracks_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void CnaMixer::DrainDeferred() noexcept
    {
        RemoveDeferredFromMix();
        for (MixerTrack* track : deferred_)
        {
            delete track;
        }
        deferred_.clear();
    }

    bool CnaMixer::BindAudio(MixerTrack* track, const MixerAudio* audio) noexcept
    {
        if (track == nullptr)
        {
            return false;
        }
        track->stream = nullptr;
        track->vorbis.reset();
        track->audio.reset();
        track->position = 0;
        track->ResetResampler();
        if (audio == nullptr || !audio->data)
        {
            return audio == nullptr;
        }
        if (audio->data->encoding == MixerAudioData::Encoding::Vorbis)
        {
            try
            {
                track->vorbis = OpenVorbisCursor(audio->data);
            }
            catch (...)
            {
                track->vorbis.reset();
            }
            if (!track->vorbis)
            {
                return false;
            }
        }
        track->audio = audio->data;
        return true;
    }

    void CnaMixer::BindStream(MixerTrack* track, MixerStream* stream) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        track->audio.reset();
        track->vorbis.reset();
        track->stream = stream;
        track->position = 0;
        track->ResetResampler();
    }

    void CnaMixer::DetachStream(const MixerStream* stream) noexcept
    {
        for (MixerTrack* track : tracks_)
        {
            if (track->stream == stream)
            {
                track->stream = nullptr;
                track->ResetResampler();
            }
        }
        for (MixerTrack* track : deferred_)
        {
            if (track->stream == stream)
            {
                track->stream = nullptr;
            }
        }
    }

    bool CnaMixer::Play(MixerTrack* track, const MixerPlayOptions& options) noexcept
    {
        if (track == nullptr || track->destroyDeferred || (!track->audio && track->stream == nullptr))
        {
            return false;
        }
        if (track->vorbis && !SeekVorbis(*track->vorbis, 0))
        {
            return false;
        }
        track->loopsRemaining = options.loopCount < -1 ? -1 : options.loopCount;
        track->haltWhenExhausted = options.haltWhenExhausted;
        track->loopStart = options.hasLoopStartFrame ? options.loopStartFrame : 0;
        track->maxFrame = options.hasMaxFrame ? static_cast<std::int64_t>(options.maxFrame) : -1;
        track->position = 0;
        track->ResetResampler();
        track->state = MixerTrack::State::Playing;
        return true;
    }

    void CnaMixer::Stop(MixerTrack* track) noexcept
    {
        if (track == nullptr || track->state == MixerTrack::State::Stopped)
        {
            return;
        }
        FinishTrack(*track);
    }

    void CnaMixer::Pause(MixerTrack* track) noexcept
    {
        if (track != nullptr && track->state == MixerTrack::State::Playing)
        {
            track->state = MixerTrack::State::Paused;
        }
    }

    void CnaMixer::Resume(MixerTrack* track) noexcept
    {
        if (track != nullptr && track->state == MixerTrack::State::Paused)
        {
            track->state = MixerTrack::State::Playing;
        }
    }

    void CnaMixer::FinishTrack(MixerTrack& track) noexcept
    {
        track.state = MixerTrack::State::Stopped;
        track.ResetResampler();
        if (track.stoppedCallback != nullptr)
        {
            // The callback may restart the track (Play) or destroy it (deferred while it runs).
            track.inCallback = true;
            track.stoppedCallback(track.stoppedUserdata, &track);
            track.inCallback = false;
        }
    }

    MixerFrameResult CnaMixer::ReadFrame(MixerTrack& track, std::array<float, 2>& frame) noexcept
    {
        if (track.stream != nullptr)
        {
            MixerStream& stream = *track.stream;
            const std::size_t frameBytes = StreamFrameBytes(stream);
            if (frameBytes == 0 || stream.Queued() < frameBytes)
            {
                return track.haltWhenExhausted ? MixerFrameResult::Ended : MixerFrameResult::Starved;
            }
            ReadStreamFrame(stream, stream.readOffset, frame);
            stream.readOffset += frameBytes;
            ++track.position;
            return MixerFrameResult::Frame;
        }
        if (!track.audio)
        {
            return MixerFrameResult::Ended;
        }

        const MixerAudioData& data = *track.audio;
        // A pass ends at the play's max frame, or at the end of the data.
        std::uint64_t end = data.frames >= 0 ? static_cast<std::uint64_t>(data.frames)
                                             : static_cast<std::uint64_t>(-1);
        if (track.maxFrame >= 0 && static_cast<std::uint64_t>(track.maxFrame) < end)
        {
            end = static_cast<std::uint64_t>(track.maxFrame);
        }

        // At most two attempts: this pass's frame, or the first frame of the next pass.
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (track.position < end)
            {
                if (data.encoding == MixerAudioData::Encoding::Vorbis)
                {
                    if (track.vorbis && ReadVorbisFrame(*track.vorbis, frame))
                    {
                        ++track.position;
                        return MixerFrameResult::Frame;
                    }
                    // The stream ended before its declared length: that is its end.
                    end = track.position;
                }
                else
                {
                    const std::size_t channels = static_cast<std::size_t>(data.channels);
                    const std::size_t index = static_cast<std::size_t>(track.position) * channels;
                    if (data.encoding == MixerAudioData::Encoding::Pcm16)
                    {
                        const float first = static_cast<float>(data.pcm16[index]) / 32768.0f;
                        const float second =
                            channels > 1 ? static_cast<float>(data.pcm16[index + 1]) / 32768.0f
                                         : first;
                        frame = {first, second};
                    }
                    else
                    {
                        const float first = data.pcmFloat[index];
                        frame = {first, channels > 1 ? data.pcmFloat[index + 1] : first};
                    }
                    ++track.position;
                    return MixerFrameResult::Frame;
                }
            }
            // The end of a pass: loop if a pass remains and the loop region holds anything.
            if (track.loopsRemaining == 0 || track.loopStart >= end)
            {
                return MixerFrameResult::Ended;
            }
            if (track.vorbis && !SeekVorbis(*track.vorbis, track.loopStart))
            {
                return MixerFrameResult::Ended;
            }
            if (track.loopsRemaining > 0)
            {
                --track.loopsRemaining;
            }
            track.position = track.loopStart;
        }
        return MixerFrameResult::Ended;
    }

    MixerFrameResult CnaMixer::Settle(MixerTrack& track) noexcept
    {
        // Leaves the track with the two frames its position lies between, `fraction` in [0, 1).
        if (!track.primed)
        {
            const MixerFrameResult result = ReadFrame(track, track.a);
            if (result != MixerFrameResult::Frame)
            {
                return result;
            }
            track.primed = true;
            track.haveB = false;
            track.lastFrame = false;
            track.fraction = 0.0;
        }
        for (;;)
        {
            if (!track.haveB)
            {
                const MixerFrameResult result =
                    track.lastFrame ? MixerFrameResult::Ended : ReadFrame(track, track.b);
                if (result == MixerFrameResult::Starved)
                {
                    return result;
                }
                if (result == MixerFrameResult::Ended)
                {
                    // Nothing follows `a`: hold it for its own interval, then stop.
                    track.b = track.a;
                    track.lastFrame = true;
                }
                track.haveB = true;
            }
            if (track.fraction < 1.0)
            {
                return MixerFrameResult::Frame;
            }
            if (track.lastFrame)
            {
                return MixerFrameResult::Ended;
            }
            track.fraction -= 1.0;
            track.a = track.b;
            track.haveB = false;
        }
    }

    void CnaMixer::Render(float* stereo, const int frames) noexcept
    {
        std::fill(stereo, stereo + static_cast<std::ptrdiff_t>(frames) * 2, 0.0f);
        rendering_ = true;
        for (int offset = 0; offset < frames; offset += kBlockFrames)
        {
            const int block = std::min(kBlockFrames, frames - offset);
            float* out = stereo + static_cast<std::ptrdiff_t>(offset) * 2;

            // By index: a callback may create a track (it is appended, and mixed from the next
            // block on); a destroyed one only leaves the list after this loop.
            for (std::size_t index = 0; index < tracks_.size(); ++index)
            {
                MixerTrack& track = *tracks_[index];
                if (track.state != MixerTrack::State::Playing || track.destroyDeferred)
                {
                    continue;
                }
                const int sourceRate = track.audio ? track.audio->sampleRate
                                                   : (track.stream != nullptr
                                                          ? track.stream->format.sampleRate
                                                          : 0);
                if (sourceRate <= 0)
                {
                    continue;
                }
                const double step = static_cast<double>(sourceRate) / sampleRate_ *
                                    static_cast<double>(ClampRatio(track.ratio));

                int produced = 0;
                bool ended = false;
                float* pcm = scratch_.data();
                while (produced < block)
                {
                    const MixerFrameResult result = Settle(track);
                    if (result == MixerFrameResult::Ended)
                    {
                        ended = true;
                        break;
                    }
                    if (result == MixerFrameResult::Starved)
                    {
                        break;
                    }
                    const float t = static_cast<float>(track.fraction);
                    pcm[produced * 2] = (track.a[0] + (track.b[0] - track.a[0]) * t) * track.gain;
                    pcm[produced * 2 + 1] =
                        (track.a[1] + (track.b[1] - track.a[1]) * t) * track.gain;
                    ++produced;
                    track.fraction += step;
                }

                if (produced > 0)
                {
                    if (track.mixCallback != nullptr)
                    {
                        track.inCallback = true;
                        track.mixCallback(track.mixUserdata, &track, 2, pcm, produced * 2);
                        track.inCallback = false;
                    }
                    const float master = masterGain_;
                    for (int sample = 0; sample < produced * 2; ++sample)
                    {
                        out[sample] += pcm[sample] * master;
                    }
                }

                if (ended && !track.destroyDeferred && track.state == MixerTrack::State::Playing)
                {
                    if (track.haltWhenExhausted)
                    {
                        FinishTrack(track);
                    }
                    else
                    {
                        // Exhausted but told not to halt: stays "playing", silent, until stopped.
                        track.ResetResampler();
                    }
                }
            }
        }
        rendering_ = false;
        RemoveDeferredFromMix();

        if (postMix_ != nullptr)
        {
            postMix_(postMixUserdata_, 2, stereo, frames * 2);
        }
    }
}
