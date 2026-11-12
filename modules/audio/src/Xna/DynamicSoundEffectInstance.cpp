// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "CNA/Internal/Audio/FrameworkUpdates.hpp"
#include "Internal/AudioFormatMath.hpp"
#include "Internal/AudioLog.hpp"

#include <utility>
#include <algorithm>

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ObjectDisposedException.hpp"


#ifdef SOUND_ENABLED
#include "CNA/Internal/Audio/MixerEngine.hpp"
#endif

namespace Microsoft::Xna::Framework::Audio
{

    namespace
    {
        // FrameworkDispatcher::Update()'s stream step, installed by the first Play().
        void UpdateFrameworkStreams()
        {
            using Microsoft::Xna::Framework::FrameworkDispatcher;
            // P11-DISPATCH-001: instance->Update() below can synchronously fire BufferNeeded (a
            // realistic XNA usage pattern is disposing the instance from inside that very handler
            // once no more data will be provided), which reaches StopInternal() -- which itself
            // locks StreamsMutex to remove itself from Streams. A std::mutex is not recursive, so
            // calling instance->Update() while still holding StreamsMutex here would self-deadlock
            // the very first time real game code disposed a stream from its own BufferNeeded
            // handler. Snapshot the list under the lock, then run every instance's Update() with
            // the lock released; a disposed instance already removes itself from Streams via
            // StopInternal(), so the trailing cleanup pass is mostly defensive.
            std::vector<DynamicSoundEffectInstance*> snapshot;
            {
                std::lock_guard<std::mutex> lock(FrameworkDispatcher::StreamsMutex);
                snapshot = FrameworkDispatcher::Streams;
            }

            for (DynamicSoundEffectInstance* instance : snapshot)
            {
                if (instance != nullptr)
                {
                    instance->Update();
                }
            }

            {
                std::lock_guard<std::mutex> lock(FrameworkDispatcher::StreamsMutex);
                auto& streams = FrameworkDispatcher::Streams;
                streams.erase(
                    std::remove_if(streams.begin(), streams.end(),
                        [](DynamicSoundEffectInstance* instance)
                        {
                            return instance == nullptr || instance->getIsDisposedProperty();
                        }),
                    streams.end());
            }
        }
    }
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
        // XNA's constructor refuses what its SoundEffect refuses (cna-killer KF-12).
        if (sampleRate < 8000 || sampleRate > 48000)
            throw System::ArgumentOutOfRangeException("sampleRate");
        if (channels != AudioChannels::Mono && channels != AudioChannels::Stereo)
            throw System::ArgumentOutOfRangeException("channels");
    }

    DynamicSoundEffectInstance::~DynamicSoundEffectInstance()
    {
        if (!getIsDisposedProperty())
        {
            // Names the hook rather than Dispose(): inside a destructor the dynamic type is this
            // one, so the call is the same, and saying so keeps it independent of which of the two
            // entry points the base happens to route through.
            DynamicSoundEffectInstance::Dispose(true);
        }
    }

    // --- properties ---

    SharpRuntime::intcs DynamicSoundEffectInstance::getPendingBufferCountProperty() const
    {
        // XNA get_PendingBufferCount checks disposal before reading the native queue.
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        std::lock_guard<std::mutex> lock(queueMutex_);
        // XNA get_PendingBufferCount IL_002f-0035 reads the live native count, which buffer
        // completion maintains on its own (OnBufferEnd IL_001f-0027), not the dispatcher.
        RetireConsumedChunksLocked();
        return static_cast<SharpRuntime::intcs>(queuedBuffers_.size() + submittedChunkSizes_.size());
    }

    bool DynamicSoundEffectInstance::getIsLoopedProperty() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        return false;
    }

    void DynamicSoundEffectInstance::setIsLoopedProperty(const bool& looped)
    {
        // A streamed voice cannot loop. XNA refuses true rather than ignoring it, and accepts
        // false; FNA ignored both (KF-12).
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        if (looped)
            throw System::InvalidOperationException("A DynamicSoundEffectInstance cannot be looped.");
    }

    void DynamicSoundEffectInstance::setIsLoopedProperty(bool&& looped)
    {
        setIsLoopedProperty(static_cast<const bool&>(looped));
    }

    SoundState DynamicSoundEffectInstance::getStateProperty() const
    {
        // XNA's State is SoundEffectInstance's, which refuses a disposed instance.
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
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
        // XNA IL checks disposal before delegating to AudioFormat; FNA omits this guard.
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        // XNA then refuses a negative size and answers zero for an empty one. Always 16-bit
        // PCM, even after SubmitFloatBufferEXT put this instance in float mode, as FNA does.
        if (sizeInBytes < 0)
            // XNA 4.0 Refresh: DynamicSoundEffectInstance.GetSampleDuration IL_0033-003d / InvalidBufferSize.
            throw System::ArgumentException("Buffer size cannot be negative.");
        if (sizeInBytes == 0)
            return System::TimeSpan::Zero;
        return CNA::Internal::Audio::Pcm16DurationFromSize(
            sizeInBytes, sampleRate_, static_cast<int>(channels_));
    }

    SharpRuntime::intcs DynamicSoundEffectInstance::GetSampleSizeInBytes(
        System::TimeSpan duration) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        const double milliseconds = duration.getTotalMillisecondsProperty();
        if (milliseconds < 0.0 || milliseconds > 2147483647.0)
            throw System::ArgumentOutOfRangeException("duration");
        if (duration == System::TimeSpan::Zero)
            return 0;
        const auto bytes = CNA::Internal::Audio::Pcm16SizeFromDuration(
            duration, sampleRate_, static_cast<int>(channels_));
        if (!bytes)
            throw System::ArgumentOutOfRangeException("duration");
        return *bytes;
    }

    // --- playback control ---

    void DynamicSoundEffectInstance::Play()
    {
        if (getIsDisposedProperty())
        {
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        }

        // XNA's Play never raises BufferNeeded itself (DynamicSoundEffectInstance.Play IL_002f-0041
        // only starts the voice); a notification it posts is delivered by FrameworkDispatcher.Update.
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

        // Stopped — set up fresh playback. XNA's kernel Play posts one BufferNeeded when it starts
        // a Stopped voice with at most two buffers pending (IL_001a-0030, IL_0055-0061).
        bool postBufferNeeded = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            postBufferNeeded =
                queuedBuffers_.size() + submittedChunkSizes_.size() <= BUFFER_NEEDED_AT_OR_BELOW;
        }

#ifdef SOUND_ENABLED
        // Establish the selected output before allocating a mixer-specific queued source. This
        // keeps the source lifetime subordinate to a started playback device under both SDL3 and
        // NULL, rather than manufacturing an unattached native object on a failed open.
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
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                "[DynamicSoundEffectInstance] queued stream creation failed: ",
                CNA::Internal::Audio::GetMixerError());
            return;
        }

        // Destroy previous track if any.
        auto* track = AsTrackD(GetLiveTrackHandle());
        if (!track)
        {
            track = CNA::Internal::Audio::CreateMixerTrack();
            if (!track)
            {
                CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                    "[DynamicSoundEffectInstance] track creation failed: ",
                    CNA::Internal::Audio::GetMixerError());
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
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                "[DynamicSoundEffectInstance] stream attachment failed: ",
                CNA::Internal::Audio::GetMixerError());
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
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                "[DynamicSoundEffectInstance] track playback failed: ",
                CNA::Internal::Audio::GetMixerError());
            return;
        }

        // Submit any already-queued buffers.
        QueueInitialBuffers();
#endif

        // TQ-032: XNA DynamicSoundEffectInstance.Play IL_003f-0041 sets the
        // inherited packet latch after successful native Play, locking Pan/Apply3D mode.
        packetSubmitted_ = true;
        State_   = SoundState::Playing;
        playing_ = true;
        if (postBufferNeeded)
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            ++pendingNotifications_;
        }

        std::lock_guard<std::mutex> lock(Microsoft::Xna::Framework::FrameworkDispatcher::StreamsMutex);
        auto& streams = Microsoft::Xna::Framework::FrameworkDispatcher::Streams;
        if (std::find(streams.begin(), streams.end(), this) == streams.end())
        {
            streams.push_back(this);
        }
        CNA::Internal::FrameworkUpdates::Install(CNA::Internal::FrameworkUpdates::Slot::DynamicSoundEffects,
                                                 &UpdateFrameworkStreams);
    }

    void DynamicSoundEffectInstance::Stop(bool immediate)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("DynamicSoundEffectInstance");
        // Before any Play() there is no track. XNA's voice exists from the constructor
        // (DynamicSoundEffectInstance..ctor IL_0043-0048) and an immediate Stop flushes its queued
        // buffers (KernelSoundEffectInstance.Stop IL_0011-0062), so the staged buffers go; a
        // non-immediate Stop has no started voice to be invalid about.
#ifdef SOUND_ENABLED
        if (!AsTrackD(GetLiveTrackHandle()))
        {
            if (immediate)
            {
                ClearBuffers();
            }
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
        {
            // A stopped voice's undelivered notifications go with its buffers; a ClearBuffersEXT
            // on a playing one keeps them, as a starving stream still needs its answer.
            std::lock_guard<std::mutex> lock(queueMutex_);
            pendingNotifications_ = 0;
        }

        std::lock_guard<std::mutex> lock(Microsoft::Xna::Framework::FrameworkDispatcher::StreamsMutex);
        auto& streams = Microsoft::Xna::Framework::FrameworkDispatcher::Streams;
        streams.erase(std::remove(streams.begin(), streams.end(), this), streams.end());
    }

    // Pause()/Resume() are intentionally not defined here (P13-DYNAMIC-001) -- see the "not
    // overridden here" comment on their declaration site (DynamicSoundEffectInstance.hpp): the
    // inherited SoundEffectInstance::Pause()/Resume() now operate on the correct, shared `track_`
    // and need no dynamic-specific override.

    void DynamicSoundEffectInstance::Dispose(bool disposing)
    {
        if (getIsDisposedProperty())
        {
            return;
        }
        // Disposal releases CNA-owned queued storage even when no native voice was ever created.
        StopInternal();
        DestroyStream();                 // frees the queued mixer stream
        // StopInternal has nulled track_, so the base hook only marks the instance disposed.
        SoundEffectInstance::Dispose(disposing);
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

        // TQ-032: XNA SubmitBuffer IL_0047-00ac uses message-only ArgumentException;
        // no buffer, offset or count ParamName is supplied.
        // XNA's SubmitBuffer: a non-empty buffer of whole 16-bit frames, and an offset and count on
        // frame boundaries inside it (KF-12). The sum is taken in 64 bits: C++ has no bounds check to
        // catch a wrapped int, where C#'s checked addition throws.
        const std::int64_t blockAlign = 2 * static_cast<std::int64_t>(channels_);
        const auto size = static_cast<std::int64_t>(buffer.size());
        if (size == 0 || size % blockAlign != 0)
            throw System::ArgumentException("The buffer is empty or not a whole number of sample frames.");
        if (offset < 0 || offset >= size || offset % blockAlign != 0)
            throw System::ArgumentException("The offset is outside the buffer or not on a sample frame.");
        if (count <= 0 || static_cast<std::int64_t>(offset) + count > size || count % blockAlign != 0)
            throw System::ArgumentException("The count is outside the buffer or not a whole number of sample frames.");
        const auto off = static_cast<std::size_t>(offset);
        const auto cnt = static_cast<std::size_t>(count);

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
            // XNA submits to the voice whatever its state (AllocateData IL_001c-0029), so data
            // given to a paused instance is already there when it resumes.
            const SoundState state = getStateProperty();
            if (state == SoundState::Playing || state == SoundState::Paused)
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
            // XNA submits to the voice whatever its state (AllocateData IL_001c-0029), so data
            // given to a paused instance is already there when it resumes.
            const SoundState state = getStateProperty();
            if (state == SoundState::Playing || state == SoundState::Paused)
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
        if (getIsDisposedProperty())
        {
            return;
        }
        SharpRuntime::intcs notifications = 0;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            // Submit any freshly queued buffers.
            if (getStateProperty() == SoundState::Playing)
            {
                SubmitQueuedToStreamLocked();
            }
            RetireConsumedChunksLocked();
            notifications = std::exchange(pendingNotifications_, 0);
        }
        // XNA delivers each posted notification once (DynamicSoundEffectInstance.OnBufferNeeded
        // IL_0000-0012), with no lock held, so a handler may submit, stop or dispose.
        for (; notifications > 0 && !getIsDisposedProperty(); --notifications)
        {
            BufferNeeded.Raise(this, System::EventArgs::Empty);
        }
    }

    void DynamicSoundEffectInstance::RetireConsumedChunksLocked() const
    {
#ifdef SOUND_ENABLED
        // A submitted chunk only counts as consumed once the stream reports it no longer holds
        // that many bytes queued as input -- not the instant it was handed to the mixer engine.
        auto* stream = AsStream(audioStream_);
        if (!stream)
        {
            return;
        }
        const int queuedBytes = CNA::Internal::Audio::GetMixerStreamQueuedBytes(stream);
        if (queuedBytes < 0)
        {
            return;
        }
        std::size_t total = 0;
        for (std::size_t size : submittedChunkSizes_) total += size;

        // AUDIO-BUFFER-001: only pop a chunk once the consumed byte count covers that chunk's
        // whole size, decrementing the budget as each one is confirmed, so a second chunk isn't
        // credited with bytes the first one hasn't finished consuming yet.
        if (static_cast<std::size_t>(queuedBytes) > total)
        {
            return;
        }
        std::size_t consumed = total - static_cast<std::size_t>(queuedBytes);
        while (!submittedChunkSizes_.empty() && consumed >= submittedChunkSizes_.front())
        {
            consumed -= submittedChunkSizes_.front();
            submittedChunkSizes_.pop_front();
            // XNA OnBufferEnd IL_004b-006a: a completion leaving at most two pending posts one.
            if (queuedBuffers_.size() + submittedChunkSizes_.size() <= BUFFER_NEEDED_AT_OR_BELOW)
            {
                ++pendingNotifications_;
            }
        }
#endif
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
                CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                    "[DynamicSoundEffectInstance] stream submit failed (", chunk.size(),
                    " bytes dropped): ", CNA::Internal::Audio::GetMixerError());
                continue;
            }

            submittedChunkSizes_.push_back(chunk.size());
        }
#endif
    }

    GetTypeNameCPP(DynamicSoundEffectInstance, "Microsoft.Xna.Framework.Audio.DynamicSoundEffectInstance")
}
