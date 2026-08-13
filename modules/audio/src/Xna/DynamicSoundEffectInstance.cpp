// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"

#include <algorithm>

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <iostream>

#ifdef SOUND_ENABLED
#include "Internal/MixerEngine.hpp"
#endif

namespace Microsoft::Xna::Framework::Audio
{
#ifdef SOUND_ENABLED
    namespace
    {
        CNA::Internal::Audio::MixerTrack* AsTrackD(void* p)
        {
            return static_cast<CNA::Internal::Audio::MixerTrack*>(p);
        }
        CNA::Internal::Audio::MixerStream* AsStream(void* p)
        {
            return static_cast<CNA::Internal::Audio::MixerStream*>(p);
        }

        // P9-HARDWARE-002: see SoundEffect.cpp's identically-named/documented helper --
        // DynamicSoundEffectInstance's constructor never touches the mixer (unlike FNA's, which
        // eagerly calls SoundEffect.Device()), so Play() here is this class's own first-possible
        // GetMixer() failure point, needing the same std::runtime_error -> NoAudioHardwareException
        // conversion.
        void EnsureMixerOrThrowXna()
        {
            try
            {
                CNA::Internal::Audio::EnsureMixer();
            }
            catch (const std::exception& ex)
            {
                throw NoAudioHardwareException(ex.what());
            }
        }
    }
#endif

    DynamicSoundEffectInstance::DynamicSoundEffectInstance(
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
        : SoundEffectInstance(),
          sampleRate_(sampleRate),
          channels_(channels)
    {
    }

    DynamicSoundEffectInstance::~DynamicSoundEffectInstance()
    {
        if (!getIsDisposedProperty())
        {
            Dispose();
        }
    }

    // --- properties ---

    SharpRuntime::intcs DynamicSoundEffectInstance::getPendingBufferCountProperty() const
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return static_cast<SharpRuntime::intcs>(queuedBuffers_.size() + submittedChunkSizes_.size());
    }

    bool DynamicSoundEffectInstance::getIsLoopedProperty() const
    {
        return false;
    }

    void DynamicSoundEffectInstance::setIsLoopedProperty(const bool& /*looped*/)
    {
        // No-op: DynamicSoundEffectInstance cannot be looped.
    }

    void DynamicSoundEffectInstance::setIsLoopedProperty(bool&& /*looped*/)
    {
        // No-op: DynamicSoundEffectInstance cannot be looped.
    }

    SoundState DynamicSoundEffectInstance::getStateProperty() const
    {
#ifdef SOUND_ENABLED
        auto* track = AsTrackD(GetLiveTrackHandle());
        if (!track) return SoundState::Stopped;
        if (CNA::Internal::Audio::IsMixerTrackPaused(track)) return SoundState::Paused;
        if (CNA::Internal::Audio::IsMixerTrackPlaying(track)) return SoundState::Playing;
#endif
        return State_;
    }

    System::TimeSpan DynamicSoundEffectInstance::GetSampleDuration(
        SharpRuntime::intcs sizeInBytes) const
    {
        // FNA delegates straight to SoundEffect.GetSampleDuration, which always assumes 16-bit
        // PCM regardless of whether SubmitFloatBufferEXT put this instance in float (32-bit)
        // mode -- match that exactly rather than computing from the real bytes-per-sample.
        return SoundEffect::GetSampleDuration(sizeInBytes, sampleRate_, channels_);
    }

    SharpRuntime::intcs DynamicSoundEffectInstance::GetSampleSizeInBytes(
        System::TimeSpan duration) const
    {
        return SoundEffect::GetSampleSizeInBytes(duration, sampleRate_, channels_);
    }

    // --- playback control ---

    void DynamicSoundEffectInstance::Play()
    {
        if (getIsDisposedProperty())
        {
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        }

        // P9-DYNAMIC-001: matches FNA's DynamicSoundEffectInstance.Play() ("Wait! What if we
        // need moar buffers?"), which unconditionally calls Update() before dispatching on
        // State -- a no-op here on a fresh/Stopped/Paused instance (Update() itself no-ops
        // unless already Playing), but a real, observable pump (submits freshly queued data,
        // fires BufferNeeded if starved) when Play() is called redundantly while already
        // Playing. Previously only called from the Stopped branch below, silently skipping this
        // pump for the "already playing" case.
        Update();

        SoundState current = getStateProperty();

        if (current == SoundState::Paused)
        {
#ifdef SOUND_ENABLED
            auto* track = AsTrackD(GetLiveTrackHandle());
            if (track)
            {
                CNA::Internal::Audio::ResumeMixerTrack(track);
                State_   = SoundState::Playing;
                playing_ = true;
            }
#endif
            return;
        }

        if (current == SoundState::Playing)
        {
            return;
        }

        // Stopped — set up fresh playback.

#ifdef SOUND_ENABLED
        // Establish the selected output before allocating a mixer-specific queued source. This is
        // load-bearing for CNA_AUDIO_PLATFORM=NULL: its refusal must not create a native object as
        // a hidden side effect on the way to reporting NoAudioHardwareException.
        EnsureMixerOrThrowXna();
        EnsureStream();

        // AUD-02-007/AUD-07-007: queued-source creation can fail (e.g. invalid format or
        // allocation failure); attaching a null source is
        // documented as *legal* -- it detaches the track's input rather than failing -- so
        // without this check a failed EnsureStream() would silently attach no input at all and
        // fall through to a false "Playing" state below with total silence, not caught by any
        // downstream return-value check.
        if (!audioStream_)
        {
            std::cerr << "[DynamicSoundEffectInstance] queued stream creation failed: "
                      << CNA::Internal::Audio::GetMixerError() << "\n";
            return;
        }

        // Destroy previous track if any.
        auto* track = AsTrackD(GetLiveTrackHandle());
        if (!track)
        {
            track = CNA::Internal::Audio::CreateMixerTrack();
            if (!track)
            {
                std::cerr << "[DynamicSoundEffectInstance] track creation failed: "
                          << CNA::Internal::Audio::GetMixerError() << "\n";
                return;
            }
            // AUD-15-006: track_/trackMixerGeneration_ are also read (via getStateProperty())
            // by SubmitBuffer()/SubmitFloatBufferEXT() from a producer thread while holding
            // queueMutex_ -- write them under the same lock for symmetry with StopInternal()'s/
            // DestroyStream()'s teardown, even though this specific creation path was not the
            // site of the originally-reproduced crash (that was Stop()'s destroy racing a read).
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                track_ = track;
                // AUD-04-008/009: see SoundEffectInstance::Play()'s identical capture -- lets
                // GetLiveTrackHandle() detect an AudioMixer::DestroyMixer() call that frees this
                // track out from under this instance.
                trackMixerGeneration_ = CNA::Internal::Audio::GetMixerEngineGeneration();
            }
        }

        if (!CNA::Internal::Audio::SetMixerTrackStream(track, AsStream(audioStream_)))
        {
            std::cerr << "[DynamicSoundEffectInstance] stream attachment failed: "
                      << CNA::Internal::Audio::GetMixerError() << "\n";
            return;
        }

        // P13-DYNAMIC-001: this used to set the track gain directly (master volume itself is
        // still applied globally at the mixer, not baked in here -- CP-16,
        // see SoundEffect::setMasterVolumeProperty) -- now shares the same composition routine
        // Play()/Apply3D()/the Volume/Pitch/Pan setters use on a static SoundEffectInstance, so any
        // Volume/Pitch/Pan/Apply3D state set on this instance before this first real Play() (e.g.
        // before track_ existed at all) is applied now instead of silently staying unapplied until
        // one of those setters happens to be called again afterward.
        INTERNAL_applyComposedTrackProperties();

        // Don't halt the track when the stream runs dry; we'll keep feeding it.
        CNA::Internal::Audio::MixerPlayOptions playOptions;
        playOptions.haltWhenExhausted = false;
        playOptions.loopCount = -1;
        const bool played = CNA::Internal::Audio::PlayMixerTrack(track, playOptions);

        // AUD-02-009/AUD-07-010: failed mixer playback must never be followed by a public
        // Playing state -- without this check, a track that failed to start (e.g. the mixer
        // rejected the track for some backend reason) would still report SoundState::Playing
        // and be registered with FrameworkDispatcher::Streams as if audio were flowing.
        if (!played)
        {
            std::cerr << "[DynamicSoundEffectInstance] track playback failed: "
                      << CNA::Internal::Audio::GetMixerError() << "\n";
            return;
        }

        // Submit any already-queued buffers.
        QueueInitialBuffers();
#endif

        State_   = SoundState::Playing;
        playing_ = true;

        std::lock_guard<std::mutex> lock(Microsoft::Xna::Framework::FrameworkDispatcher::StreamsMutex);
        auto& streams = Microsoft::Xna::Framework::FrameworkDispatcher::Streams;
        if (std::find(streams.begin(), streams.end(), this) == streams.end())
        {
            streams.push_back(this);
        }
    }

    void DynamicSoundEffectInstance::Stop(bool immediate)
    {
        // Matches FNA's early return when there is no active voice/track yet (e.g. Stop() called
        // before any Play()) -- only once playback has actually started does a non-immediate
        // Stop become a meaningful (and, for dynamic instances, invalid) request.
#ifdef SOUND_ENABLED
        if (!AsTrackD(GetLiveTrackHandle()))
        {
            return;
        }
#endif
        // FNA throws for a non-immediate Stop on a dynamic instance: there is no authored loop
        // for FAudioSourceVoice_ExitLoop to release into, unlike a static SoundEffectInstance.
        if (!immediate)
        {
            throw System::InvalidOperationException();
        }
        StopInternal();
    }

    void DynamicSoundEffectInstance::Stop()
    {
        // P9-DYNAMIC-001: matches the base SoundEffectInstance::Stop() (and FNA's, which is
        // exactly `Stop(true)`) -- delegates through Stop(bool)'s own guard rather than
        // duplicating StopInternal()'s work unconditionally. Previously this override skipped
        // that guard entirely, so calling the no-arg Stop() directly (not via Stop(bool)) on a
        // never-played (or already-stopped) instance would still clear any staged buffers and
        // reset state, unlike FNA, which no-ops in that case (handle == IntPtr.Zero).
        Stop(true);
    }

    void DynamicSoundEffectInstance::StopInternal()
    {
#ifdef SOUND_ENABLED
        // AUD-04-008/009: GetLiveTrackHandle() returns nullptr (and already clears track_) if
        // the mixer that owned this track was destroyed since it was created -- stop/destroy
        // must never run against that freed pointer. track_ is explicitly
        // cleared again below regardless, matching this function's pre-existing "always end up
        // with no track" contract for both the live and already-stale cases.
        //
        // AUD-15-006: this whole block (destroying the track and clearing track_) must run
        // under queueMutex_ -- a producer thread's SubmitBuffer() reads track_ (via
        // getStateProperty(), under the same lock as of this task's fix) to decide whether to
        // submit immediately; without this lock here, destruction could free the track
        // while that read was using it, a real sanitizer-reproduced use-after-free (stream lock
        // segfaulting on a track already freed by this function running concurrently).
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            auto* track = AsTrackD(GetLiveTrackHandle());
            if (track)
            {
                CNA::Internal::Audio::StopMixerTrack(track);
                CNA::Internal::Audio::DestroyMixerTrack(track);
            }
            track_ = nullptr;
        }
#endif
        State_   = SoundState::Stopped;
        playing_ = false;

        ClearBuffers();

        std::lock_guard<std::mutex> lock(Microsoft::Xna::Framework::FrameworkDispatcher::StreamsMutex);
        auto& streams = Microsoft::Xna::Framework::FrameworkDispatcher::Streams;
        streams.erase(std::remove(streams.begin(), streams.end(), this), streams.end());
    }

    // Pause()/Resume() are intentionally not defined here (P13-DYNAMIC-001) -- see the "not
    // overridden here" comment on their declaration site (DynamicSoundEffectInstance.hpp): the
    // inherited SoundEffectInstance::Pause()/Resume() now operate on the correct, shared `track_`
    // and need no dynamic-specific override.

    void DynamicSoundEffectInstance::Dispose()
    {
        if (getIsDisposedProperty())
        {
            return;
        }
        Stop();                          // stops/destroys track_ and deregisters from the dispatcher
        DestroyStream();                 // frees the queued mixer stream
        // track_ is already null by the time this runs (Stop() above nulled it via
        // StopInternal()), so DestroyTrackSafe(track_) is a no-op here; this call's only real
        // effect is marking the instance disposed.
        SoundEffectInstance::Dispose();
    }

    // --- buffer submission ---

    void DynamicSoundEffectInstance::SubmitBuffer(
        const std::vector<SharpRuntime::bytecs>& buffer)
    {
        SubmitBuffer(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    void DynamicSoundEffectInstance::SubmitBuffer(
        const std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count)
    {
        // P9-VALIDATION-011: cannot queue buffers after disposal -- without this, a caller that
        // keeps submitting after Dispose() would grow queuedBuffers_ unboundedly (the buffers
        // would never be consumed, since Dispose() nulls track_ and getStateProperty()
        // reports Stopped, so SubmitQueuedToStream() is never reached below).
        if (getIsDisposedProperty())
        {
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        }

        // P9-VALIDATION-010: offset+count must never be computed as a plain intcs addition -- see
        // SoundEffect's buffer/range constructor for why (int32 overflow can silently wrap past
        // this check, then buffer.begin()+offset+count below is out-of-bounds iterator arithmetic).
        if (offset < 0 || count < 0)
        {
            throw System::ArgumentOutOfRangeException("count");
        }
        const auto off = static_cast<std::size_t>(offset);
        const auto cnt = static_cast<std::size_t>(count);
        if (off > buffer.size() || cnt > buffer.size() - off)
        {
            throw System::ArgumentOutOfRangeException("count");
        }

        // AUD-07-001/002/A-03: real FNA's own SubmitBuffer has no equivalent guard at all --
        // once SubmitFloatBufferEXT (an FNA/CNAEXT extension, not real XNA) has flipped
        // format.wFormatTag to float, nothing in FNA ever resets it, and a later plain
        // SubmitBuffer() submits raw int16 bytes into what may still be a float-format voice.
        // Since SubmitFloatBufferEXT is a CNAEXT-only surface to begin with, CNA is free to make
        // this safer without diverging from any real XNA behavior: symmetric to
        // SubmitFloatBufferEXT's own "float buffer while live int stream" guard below, reject an
        // int submission into a live float stream, and -- only while Stopped, when EnsureStream()
        // is about to rebuild the stream from scratch on the next Play() anyway -- let it commit
        // the mode back to int. A caller that never touches SubmitFloatBufferEXT (i.e. every real
        // XNA game) never has isFloat_ true and never observes any behavior change here.
        // AUD-15-006: same track_/audioStream_ UAF risk as the state check further below --
        // getStateProperty() must not run outside queueMutex_'s protection against a concurrent
        // Stop() destroying the track mid-read. Lock only taken when isFloat_ is actually true
        // (the rare CNAEXT float-submission path), preserving the short-circuit for the common
        // (always-int16) case.
        if (isFloat_)
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (getStateProperty() != SoundState::Stopped)
            {
                throw System::InvalidOperationException("Submit an integer buffer before Playing!");
            }
        }
        isFloat_ = false;

        std::vector<SharpRuntime::bytecs> chunk(
            buffer.begin() + offset,
            buffer.begin() + offset + count
        );

        // AUD-15-006: the state check and (if Playing) immediate submission to the stream must
        // happen under the SAME lock as the push, matching FNA's real SubmitBuffer (which checks
        // State and submits to the native voice atomically inside its own `lock (queuedBuffers)`
        // block). Doing this check *after* releasing the lock let a producer thread's
        // getStateProperty()/SubmitQueuedToStream() call race a concurrent Stop() destroying
        // track_/audioStream_ out from under it -- a real, ASan-reproduced use-after-free crash
        // (locking a stream on a track that destruction had already freed).
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queuedBuffers_.push_back(std::move(chunk));
            if (getStateProperty() == SoundState::Playing)
            {
                SubmitQueuedToStreamLocked();
            }
        }
    }

    void DynamicSoundEffectInstance::SubmitFloatBufferEXT(
        const std::vector<float>& buffer)
    {
        SubmitFloatBufferEXT(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    void DynamicSoundEffectInstance::SubmitFloatBufferEXT(
        const std::vector<float>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count)
    {
        // P9-VALIDATION-011: see SubmitBuffer's identical guard above.
        if (getIsDisposedProperty())
        {
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        }

        // P9-VALIDATION-010: overflow-safe bounds check, see SubmitBuffer above.
        if (offset < 0 || count < 0)
        {
            throw System::ArgumentOutOfRangeException("count");
        }
        const auto off = static_cast<std::size_t>(offset);
        const auto cnt = static_cast<std::size_t>(count);
        if (off > buffer.size() || cnt > buffer.size() - off)
        {
            throw System::ArgumentOutOfRangeException("count");
        }

        // The format may only switch from int to float while stopped; otherwise the live
        // S16 stream would be fed F32 bytes. EnsureStream rebuilds the stream on the next Play.
        // AUD-15-006: see SubmitBuffer's identical lock rationale above.
        if (!isFloat_)
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (getStateProperty() != SoundState::Stopped)
            {
                throw System::InvalidOperationException("Submit a float buffer before Playing!");
            }
        }

        isFloat_ = true;

        const auto* bytes = reinterpret_cast<const SharpRuntime::bytecs*>(buffer.data() + offset);
        const std::size_t byteCount = static_cast<std::size_t>(count) * sizeof(float);

        std::vector<SharpRuntime::bytecs> chunk(bytes, bytes + byteCount);

        // AUD-15-006: see SubmitBuffer's identical rationale above -- state check and immediate
        // submit must happen under the same lock as the push.
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queuedBuffers_.push_back(std::move(chunk));
            if (getStateProperty() == SoundState::Playing)
            {
                SubmitQueuedToStreamLocked();
            }
        }
    }

    void DynamicSoundEffectInstance::QueueInitialBuffers()
    {
        SubmitQueuedToStream();
    }

    void DynamicSoundEffectInstance::ClearBuffers()
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queuedBuffers_.clear();
            submittedChunkSizes_.clear();
        }

#ifdef SOUND_ENABLED
        auto* stream = AsStream(audioStream_);
        if (stream)
        {
            CNA::Internal::Audio::ClearMixerStream(stream);
        }
#endif
    }

    void DynamicSoundEffectInstance::Update()
    {
        if (getStateProperty() != SoundState::Playing)
        {
            return;
        }

        // Submit any freshly queued buffers.
        SubmitQueuedToStream();

#ifdef SOUND_ENABLED
        // A submitted chunk only counts as consumed once the stream reports it no longer holds
        // that many bytes queued as input (matches FNA polling the native voice's real
        // BuffersQueued state) -- not the instant it was handed to the mixer engine.
        auto* stream = AsStream(audioStream_);
        if (stream)
        {
            const int queuedBytes = CNA::Internal::Audio::GetMixerStreamQueuedBytes(stream);
            if (queuedBytes >= 0)
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                std::size_t total = 0;
                for (std::size_t size : submittedChunkSizes_) total += size;

                // AUDIO-BUFFER-001 (external audit, 2026-07-16): was `while (total >
                // queuedBytes) pop_front()`, which dropped an ENTIRE chunk the instant the
                // stream had consumed even a single byte of it (any total > queuedBytes at all),
                // not once that chunk's own full byte count had actually been played. `consumed`
                // is how many bytes have genuinely been played since these chunks were tracked --
                // only pop a chunk once `consumed` covers that chunk's whole size, decrementing
                // the budget as each one is confirmed, so a second chunk isn't credited with
                // bytes the first one hasn't finished consuming yet.
                if (static_cast<std::size_t>(queuedBytes) <= total)
                {
                    std::size_t consumed = total - static_cast<std::size_t>(queuedBytes);
                    while (!submittedChunkSizes_.empty() &&
                           consumed >= submittedChunkSizes_.front())
                    {
                        consumed -= submittedChunkSizes_.front();
                        submittedChunkSizes_.pop_front();
                    }
                }
            }
        }
#endif

        // Raise BufferNeeded while we are short of MINIMUM_BUFFER_CHECK worth of data.
        for (
            SharpRuntime::intcs i = MINIMUM_BUFFER_CHECK - getPendingBufferCountProperty();
            (i > 0) && !BufferNeeded.Empty();
            --i
        )
        {
            BufferNeeded.Raise(this, System::EventArgs::Empty);
        }
    }

    // --- private helpers ---

    void DynamicSoundEffectInstance::EnsureStream()
    {
#ifdef SOUND_ENABLED
        if (audioStream_ && streamIsFloat_ == isFloat_) return;
        if (audioStream_)
        {
            // The sample format changed (int -> float) since the stream was created; rebuild it.
            DestroyStream();
        }

        audioStream_ = CNA::Internal::Audio::CreateMixerStream({
            sampleRate_, static_cast<int>(channels_),
            isFloat_ ? CNA::Internal::Audio::MixerSampleFormat::Float32
                     : CNA::Internal::Audio::MixerSampleFormat::Signed16});
        streamIsFloat_ = isFloat_;
#endif
    }

    void DynamicSoundEffectInstance::DestroyStream()
    {
#ifdef SOUND_ENABLED
        // AUD-15-006: same rationale as StopInternal()'s lock around destroying track_ --
        // SubmitQueuedToStreamLocked() (reachable from a producer thread via SubmitBuffer())
        // reads audioStream_ under queueMutex_, so freeing it must happen under the same lock.
        std::lock_guard<std::mutex> lock(queueMutex_);
        auto* stream = AsStream(audioStream_);
        if (stream)
        {
            CNA::Internal::Audio::DestroyMixerStream(stream);
            audioStream_ = nullptr;
        }
#endif
    }

    void DynamicSoundEffectInstance::SubmitQueuedToStream()
    {
#ifdef SOUND_ENABLED
        std::lock_guard<std::mutex> lock(queueMutex_);
        SubmitQueuedToStreamLocked();
#endif
    }

    void DynamicSoundEffectInstance::SubmitQueuedToStreamLocked()
    {
#ifdef SOUND_ENABLED
        // AUD-15-006: audioStream_ must be read under queueMutex_, same as track_ elsewhere in
        // this file -- Dispose()'s DestroyStream() now takes this same lock around freeing it,
        // so a caller (e.g. SubmitBuffer() from a producer thread) that reads it here while
        // holding the lock can never observe a freed/mid-teardown stream.
        auto* stream = AsStream(audioStream_);
        if (!stream) return;

        std::vector<std::vector<SharpRuntime::bytecs>> toSubmit;
        toSubmit.swap(queuedBuffers_);

        for (const auto& chunk : toSubmit)
        {
            // AUD-02-008/AUD-07-009: a failed queued-source submit must not be counted as
            // submitted -- the engine guarantees no partial data is queued on failure, so crediting the
            // chunk to submittedChunkSizes_ anyway would corrupt PendingBufferCount forever (the
            // stream never actually receives those bytes, so Update()'s consumed-byte accounting
            // would never reach far enough to pop it).
            const auto* first = reinterpret_cast<const std::byte*>(chunk.data());
            if (!CNA::Internal::Audio::PutMixerStreamData(
                stream, std::span<const std::byte>(first, chunk.size())))
            {
                std::cerr << "[DynamicSoundEffectInstance] stream submit failed ("
                          << chunk.size() << " bytes dropped): "
                          << CNA::Internal::Audio::GetMixerError() << "\n";
                continue;
            }

            submittedChunkSizes_.push_back(chunk.size());
        }
#endif
    }

    GetTypeNameCPP(DynamicSoundEffectInstance, "Microsoft.Xna.Framework.Audio.DynamicSoundEffectInstance")
}
