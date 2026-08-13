// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <istream>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#ifdef SOUND_ENABLED
#include "Internal/MixerEngine.hpp"
#endif

namespace Microsoft::Xna::Framework::Audio
{
    class SoundEffect::Impl
    {
    public:
#ifdef SOUND_ENABLED
        CNA::Internal::Audio::MixerAudioPtr audio;
        SharpRuntime::intcs sampleRate = 44100;
        SharpRuntime::uintcs channels  = 2;
#endif
        // Live SoundEffectInstance objects created via CreateInstance(), for Dispose()'s
        // cascade (T-3G). Raw, non-owning pointers: the instances' lifetime belongs to their
        // caller; SoundEffectInstance registers/unregisters itself (see SoundEffect::Register/
        // UnregisterInstance). Not gated by SOUND_ENABLED -- this is pure object-lifecycle
        // bookkeeping, independent of the audio backend.
        std::vector<SoundEffectInstance*> instances;
    };

    void SoundEffect::RegisterInstance(const std::shared_ptr<void>& keepAlive, SoundEffectInstance* instance)
    {
        if (!keepAlive || !instance) return;
        static_cast<Impl*>(keepAlive.get())->instances.push_back(instance);
    }

    void SoundEffect::UnregisterInstance(const std::shared_ptr<void>& keepAlive, SoundEffectInstance* instance)
    {
        if (!keepAlive || !instance) return;
        auto& v = static_cast<Impl*>(keepAlive.get())->instances;
        v.erase(std::remove(v.begin(), v.end(), instance), v.end());
    }

    std::size_t SoundEffect::GetLiveInstanceCountInternal() const
    {
        return impl_ ? impl_->instances.size() : 0;
    }

    // --- static members ---

    float SoundEffect::MasterVolume_   = 1.0f;
    float SoundEffect::DistanceScale_  = 1.0f;
    float SoundEffect::DopplerScale_   = 1.0f;
    float SoundEffect::SpeedOfSound_   = 343.5f;

    // --- internal helpers ---

#ifdef SOUND_ENABLED
    namespace
    {
        // P11-PAN-002 (RFC-1 for the fire-and-forget path): minimal per-track pan-only state.
        // Unlike SoundEffectInstance's FilterState (P11-PAN-001), a fire-and-forget track never
        // has a filter to share the single mixer cooked-callback slot with -- none of
        // P11-PAN-001's original shared-slot regression risk applies here at all, so this is
        // intentionally much smaller: just the crossfeed matrix. Holds the already-computed
        // 4 coefficients (not just `pan`) because the matrix is only ever computed once, at
        // Play() time, by a SoundEffect member function (a friend of SoundEffectInstance) --
        // FireAndForgetPanCallback below is a free-function mixer trampoline, not a class member,
        // so it can't call the private, friended INTERNAL_calculatePanCrossfeedMatrix itself.
        // Heap-allocated with a lifetime tied to the track itself, freed in
        // OnFireAndForgetStopped below (which already runs exactly once, when this track is
        // destroyed on natural completion).
        struct FireAndForgetPanState
        {
            bool  active = false; // false: pan == 0 at Play() time, matrix is the identity
            float ll = 1.0f, rl = 0.0f, lr = 0.0f, rr = 1.0f;

            // AUD-15-008: intrusive link for PendingPanStateCleanup's lock-free queue below --
            // reuses this already-heap-allocated object as its own queue node instead of a
            // separate std::vector entry, so queuing one for cleanup never allocates.
            FireAndForgetPanState* next = nullptr;
        };

        // AUD-05-006: best-effort detection that the caller passed whole-file container bytes
        // (a common misuse: WAV/RIFF, Ogg, MP3/ID3, or XNB) to a raw PCM16LE constructor instead
        // of raw sample data. These constructors have no way to reject this outright -- a RIFF
        // header, read as PCM16 samples, is still "valid" 16-bit data, just garbage -- so this
        // only ever emits a diagnostic, never throws (see the constructor doc comment,
        // AUD-05-005: the correct fix for real misuse is `SoundEffect(const std::string&)`
        // instead, not a rejection here). Returns nullptr if no known signature is found.
        const char* DetectLikelyContainerSignature(const SharpRuntime::bytecs* data, std::size_t len)
        {
            if (len >= 4 && std::memcmp(data, "RIFF", 4) == 0) return "RIFF/WAVE";
            if (len >= 4 && std::memcmp(data, "OggS", 4) == 0) return "Ogg";
            if (len >= 3 && std::memcmp(data, "ID3", 3) == 0) return "MP3 (ID3 tag)";
            if (len >= 3 && data[0] == 'X' && data[1] == 'N' && data[2] == 'B') return "XNB";
            return nullptr;
        }

        // AUD-05-007: best-effort detection that raw PCM16 statistics look implausible for real
        // audio (e.g. float32 data byte-reinterpreted as PCM16, or Ogg/MP3-compressed data
        // without a recognizable header). Compressed/encoded bitstreams are specifically designed
        // to approach maximum byte-level entropy (~8 bits/byte, statistically close to random
        // noise); real quantized 16-bit audio essentially never does, even for loud/percussive
        // content -- adjacent samples/channels stay correlated, and typical byte-value
        // distributions are skewed rather than uniform. Advisory only, like
        // DetectLikelyContainerSignature above -- never throws, no release rejection, and the
        // 7.9-bit threshold (out of a theoretical max of 8.0) is deliberately conservative to
        // avoid flagging genuinely loud/noisy game audio.
        bool LooksImplausiblyHighEntropyForPcm16(const SharpRuntime::bytecs* data, std::size_t len)
        {
            if (len < 256) return false; // too short to estimate entropy meaningfully
            std::array<std::size_t, 256> histogram{};
            for (std::size_t i = 0; i < len; ++i) histogram[data[i]]++;
            double entropy = 0.0;
            for (std::size_t count : histogram)
            {
                if (count == 0) continue;
                double p = static_cast<double>(count) / static_cast<double>(len);
                entropy -= p * std::log2(p);
            }
            return entropy > 7.9;
        }

        // Applies the already-computed crossfeed matrix (see FireAndForgetPanState above) to
        // interleaved stereo PCM -- matches ApplyPanCrossfeed's identical transform in
        // SoundEffectInstance.cpp. Only meaningful for `channels == 2`; `!active` (pan == 0.0f at
        // Play() time, the common case -- most fire-and-forget sounds never pan) skips the
        // transform entirely.
        void FireAndForgetPanCallback(void* userdata,
                                      CNA::Internal::Audio::MixerTrack* /*track*/,
                                      int channels, float* pcm, int samples)
        {
            const auto* state = static_cast<const FireAndForgetPanState*>(userdata);
            if (channels != 2 || !state->active) return;

            for (int i = 0; i + 2 <= samples; i += 2)
            {
                const float l = pcm[i];
                const float r = pcm[i + 1];
                pcm[i]     = l * state->ll + r * state->rl;
                pcm[i + 1] = l * state->lr + r * state->rr;
            }
        }

        // P11-PAN-002: NOT freed directly in OnFireAndForgetStopped below -- caught by a real,
        // reproducible ASan heap-use-after-free during this task's own verification pass.
        // The mixer's callback can invoke the STOPPED callback *partway
        // through* pulling a track's FINAL buffer of audio (when the track's underlying data
        // runs dry while pulling the track's output stream), then
        // still deliver that already-pulled final buffer to the COOKED callback
        // (FireAndForgetPanCallback above) moments later, in the same synchronous mixer-thread
        // call -- so the cooked callback can genuinely still read this track's userdata AFTER
        // the stopped callback already ran. Deferred track destruction's documented behavior
        // ("destroying a track from the mixer thread itself... will cause it to be destroyed as
        // soon as this iteration of the mixer thread is not using it") solves this
        // exact problem for the *track* -- panState needs the same deferred treatment, which
        // the engine has no equivalent API for app-owned userdata, so it is deferred to the next
        // fire-and-forget Play() call instead (DrainPendingPanStateCleanup, below), which is
        // always safely later than any mixer-thread activity for this track/iteration.
        //
        // Wrapped in a struct with its own destructor (rather than bare namespace-scope statics)
        // so a static-duration instance frees any still-pending entries at program exit too --
        // otherwise the last fire-and-forget sound(s) played before exit, with no later Play()
        // call to opportunistically drain them, would show up as a real (if tiny and harmless)
        // ASan leak report, which this project's testing culture treats as a bar worth clearing.
        //
        // AUD-15-008: Queue() runs from OnFireAndForgetStopped, a real mixer track-stopped
        // callback that fires on the mixer thread (see that function's own comment) -- real-time
        // audio code must not block on a mutex or allocate. The original implementation did both
        // (std::mutex + std::vector::push_back, which can reallocate). Rewritten as a classic
        // lock-free Treiber-stack push: FireAndForgetPanState now carries its own intrusive
        // `next` link (already heap-allocated at Play() time, so pushing it costs nothing but an
        // atomic CAS), and Drain() atomically claims the whole chain in one exchange.
        struct PendingPanStateCleanup
        {
            std::atomic<FireAndForgetPanState*> head{nullptr};

            void Queue(FireAndForgetPanState* state)
            {
                if (!state) return;
                state->next = head.load(std::memory_order_relaxed);
                while (!head.compare_exchange_weak(
                    state->next, state,
                    std::memory_order_release, std::memory_order_relaxed))
                {
                    // state->next is updated to the current head by compare_exchange_weak on
                    // failure, so the retry always links onto the latest head.
                }
            }

            // Called from Play() (main/calling thread) before creating a new fire-and-forget
            // track -- by the time any NEW Play() call happens, the mixer thread has long since
            // finished processing whatever iteration queued these for cleanup, so freeing them
            // here is safe.
            void Drain()
            {
                FireAndForgetPanState* node = head.exchange(nullptr, std::memory_order_acquire);
                while (node)
                {
                    FireAndForgetPanState* next = node->next;
                    delete node;
                    node = next;
                }
            }

            ~PendingPanStateCleanup()
            {
                Drain();
            }
        };

        PendingPanStateCleanup g_pendingPanStateCleanup;

        void OnFireAndForgetStopped(void* userdata, CNA::Internal::Audio::MixerTrack* track)
        {
            g_pendingPanStateCleanup.Queue(static_cast<FireAndForgetPanState*>(userdata));
            CNA::Internal::Audio::DestroyMixerTrack(track);
        }

        // P9-HARDWARE-002: CNA::Internal::Audio::GetMixer() throws a raw std::runtime_error on
        // its first-ever call if no audio hardware/device is available -- the internal layer
        // stays exception-type-agnostic re: the XNA surface (matches the established
        // XactParser-throws-std/SoundBank-catches-and-converts pattern, CHECKLIST.md). This
        // converts that failure into NoAudioHardwareException at the XNA-facing entry points
        // that can be the very first GetMixer() call in the process, matching FNA's
        // SoundEffect.Device() throwing the identical exception from the identical failure.
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

    // --- private constructor ---

    SoundEffect::SoundEffect(std::shared_ptr<Impl> impl, std::string name)
        : impl_(std::move(impl)), name_(std::move(name))
    {
    }

    // --- public constructors ---

    SoundEffect::SoundEffect(const std::string& assetName)
        : impl_(std::make_shared<Impl>())
    {
        if (assetName.empty())
        {
            return;
        }

#ifdef SOUND_ENABLED
        EnsureMixerOrThrowXna();
        impl_->audio = CNA::Internal::Audio::LoadMixerAudioFile(assetName);
        if (!impl_->audio)
        {
            throw System::NotSupportedException(
                "Failed to load sound: " + assetName + " — "
                + CNA::Internal::Audio::GetMixerError()
            );
        }

        const auto format = CNA::Internal::Audio::GetMixerAudioFormat(impl_->audio.get());
        if (format.sampleRate > 0 && format.channels > 0)
        {
            impl_->sampleRate = format.sampleRate;
            impl_->channels   = static_cast<SharpRuntime::uintcs>(format.channels);
        }
#else
        (void)assetName;
#endif
    }

    SoundEffect::SoundEffect(
        const std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
        : SoundEffect(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()),
                      sampleRate, channels, 0, 0)
    {
    }

    SoundEffect::SoundEffect(
        const std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels,
        SharpRuntime::intcs loopStart,
        SharpRuntime::intcs loopLength)
        : impl_(std::make_shared<Impl>())
    {
        // P9-VALIDATION-002: loopStart/loopLength are intentionally NOT validated here, matching
        // FNA's own internal ctor (SoundEffect.cs: `this.loopStart = (uint) loopStart;`) -- a
        // negative value wraps to a huge unsigned value in both FNA and here, identically.
        loopStart_  = static_cast<SharpRuntime::uintcs>(loopStart);
        loopLength_ = static_cast<SharpRuntime::uintcs>(loopLength);

        // P9-VALIDATION-003: offset+count must never be computed as a plain intcs addition --
        // two individually-plausible-looking values can overflow int32 (UB), and on a typical
        // two's-complement wraparound the overflowed sum can come out negative/small, silently
        // passing this check while `buffer.data() + offset` below is a wildly out-of-bounds
        // pointer. FNA gets away without this check because C#'s array bounds checking is the
        // real safety net there; C++ has none, so this has to be exact.
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

#ifdef SOUND_ENABLED
        if (const char* sig = DetectLikelyContainerSignature(buffer.data() + off, cnt))
        {
            std::cerr << "[SoundEffect] Warning: raw PCM buffer starts with a " << sig
                      << " signature, not raw PCM16LE sample data -- passing whole-file bytes "
                      << "to this constructor decodes the container's header as audio samples, "
                      << "producing garbage output. Use SoundEffect(const std::string&) to load "
                      << "a file instead.\n";
        }
        else if (LooksImplausiblyHighEntropyForPcm16(buffer.data() + off, cnt))
        {
            std::cerr << "[SoundEffect] Warning: raw PCM buffer has implausibly high byte-level "
                      << "entropy for real 16-bit audio -- this often indicates compressed "
                      << "(Ogg/MP3, without a recognizable header) or otherwise non-PCM16 data "
                      << "(e.g. float32 samples byte-reinterpreted as PCM16) was passed to this "
                      << "constructor instead of raw PCM16LE samples.\n";
        }

        EnsureMixerOrThrowXna();
        const auto* first = reinterpret_cast<const std::byte*>(buffer.data() + offset);
        impl_->audio = CNA::Internal::Audio::LoadMixerRawAudio(
            std::span<const std::byte>(first, static_cast<std::size_t>(count)),
            {sampleRate, static_cast<int>(channels),
             CNA::Internal::Audio::MixerSampleFormat::Signed16});
        if (!impl_->audio)
        {
            throw System::NotSupportedException(
                std::string("Failed to create sound from buffer: ")
                + CNA::Internal::Audio::GetMixerError()
            );
        }

        impl_->sampleRate = sampleRate;
        impl_->channels   = static_cast<SharpRuntime::uintcs>(channels);
#else
        (void)offset;
        (void)count;
        (void)sampleRate;
        (void)channels;
#endif
    }

    SoundEffect::~SoundEffect() = default;

    // --- properties ---

    System::TimeSpan SoundEffect::getDurationProperty() const
    {
#ifdef SOUND_ENABLED
        if (impl_ && impl_->audio && impl_->sampleRate > 0)
        {
            const std::int64_t frames =
                CNA::Internal::Audio::GetMixerAudioDuration(impl_->audio.get());
            if (frames > 0)
            {
                return System::TimeSpan::FromSeconds(
                    static_cast<double>(frames) / impl_->sampleRate
                );
            }
        }
#endif
        return System::TimeSpan::Zero;
    }

    bool SoundEffect::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    const std::string& SoundEffect::getNameProperty() const
    {
        return name_;
    }

    void SoundEffect::setNameProperty(const std::string& value)
    {
        name_ = value;
    }

    void SoundEffect::setNameProperty(std::string&& value)
    {
        name_ = std::move(value);
    }

    float SoundEffect::getMasterVolumeProperty()
    {
#ifdef SOUND_ENABLED
        // CP-16: query the real mixer-engine master gain (matches FNA, which likewise always
        // queries the live FAudio master voice rather than a cached value) so this reflects
        // the mixer engine's actual current value, not a value that could drift from it.
        EnsureMixerOrThrowXna();
        return CNA::Internal::Audio::GetMixerMasterGain();
#else
        return MasterVolume_;
#endif
    }

    void SoundEffect::setMasterVolumeProperty(const float& v)
    {
#ifdef SOUND_ENABLED
        // CP-16: the mixer engine's gain is a real global, applied to every track (including
        // already-playing ones) at mix time -- unlike the old per-track-baked-in approach, this
        // needs no per-instance re-application. FNA passes the value straight through, without
        // clamping; the mixer engine does the same (only rejects negative values as an error).
        EnsureMixerOrThrowXna();
        CNA::Internal::Audio::SetMixerMasterGain(v);
#else
        MasterVolume_ = v;
#endif
    }

    void SoundEffect::setMasterVolumeProperty(float&& v)
    {
        setMasterVolumeProperty(v);
    }

    float SoundEffect::getDistanceScaleProperty()
    {
        return DistanceScale_;
    }

    void SoundEffect::setDistanceScaleProperty(float value)
    {
        if (value <= 0.0f)
        {
            throw System::ArgumentOutOfRangeException("value <= 0.0f");
        }
        DistanceScale_ = value;
    }

    float SoundEffect::getDopplerScaleProperty()
    {
        return DopplerScale_;
    }

    void SoundEffect::setDopplerScaleProperty(float value)
    {
        if (value < 0.0f)
        {
            throw System::ArgumentOutOfRangeException("value < 0.0f");
        }
        DopplerScale_ = value;
    }

    float SoundEffect::getSpeedOfSoundProperty()
    {
        return SpeedOfSound_;
    }

    void SoundEffect::setSpeedOfSoundProperty(float value)
    {
        SpeedOfSound_ = value;
    }

    // --- methods ---

    SoundEffectInstance SoundEffect::CreateInstance() const
    {
        return SoundEffectInstance(*this);
    }

    bool SoundEffect::Play()
    {
        return Play(1.0f, 0.0f, 0.0f);
    }

    bool SoundEffect::Play(float volume, float pitch, float pan)
    {
        if (isDisposed_)
        {
            return false;
        }

        // FNA constructs a real SoundEffectInstance and assigns Volume/Pitch/Pan through its
        // property setters before Play(); mirror their validation here: Pan is range-checked
        // (throws), Pitch is clamped rather than validated.
        if (pan > 1.0f || pan < -1.0f)
        {
            throw System::ArgumentOutOfRangeException("pan");
        }
        pitch = (pitch < -1.0f) ? -1.0f : ((pitch > 1.0f) ? 1.0f : pitch);

#ifdef SOUND_ENABLED
        // P11-PAN-002: opportunistically free any FireAndForgetPanState from a previously
        // finished fire-and-forget track -- see PendingPanStateCleanup's own comment for why
        // this can't happen directly in OnFireAndForgetStopped (a real ASan-caught
        // heap-use-after-free otherwise).
        g_pendingPanStateCleanup.Drain();

        auto* audio = static_cast<CNA::Internal::Audio::MixerAudio*>(getNativeAudioHandle());
        if (!audio)
        {
            return false;
        }

        CNA::Internal::Audio::MixerTrack* track = CNA::Internal::Audio::CreateMixerTrack();
        if (!track)
        {
            return false;
        }

        if (!CNA::Internal::Audio::SetMixerTrackAudio(track, audio))
        {
            CNA::Internal::Audio::DestroyMixerTrack(track);
            return false;
        }

        // CP-16: master volume is applied once, globally, via the mixer's own
        // master gain stage) -- not baked into each track's own gain, which would double-apply it.
        CNA::Internal::Audio::SetMixerTrackGain(track, volume);

        // P11-PAN-002 (RFC-1 for the fire-and-forget path): the engine's per-channel stereo
        // gain has no crossfeed term (CHECKLIST.md CP-19), so it's fixed to unity here and this
        // track's own pan state (FireAndForgetPanCallback above) owns 100% of the stereo image
        // instead, matching P11-PAN-001's design for SoundEffectInstance.
        CNA::Internal::Audio::SetMixerTrackStereoUnity(track);

        auto* panState = new FireAndForgetPanState{};
        if (pan != 0.0f)
        {
            // Computed once here, not in FireAndForgetPanCallback -- SoundEffect::Play() is a
            // member function (a friend of SoundEffectInstance), so it can call the private,
            // canonical matrix implementation directly; the callback itself cannot (see
            // FireAndForgetPanState's own comment above).
            panState->active = true;
            SoundEffectInstance::INTERNAL_calculatePanCrossfeedMatrix(
                pan, panState->ll, panState->rl, panState->lr, panState->rr);
        }
        CNA::Internal::Audio::SetMixerTrackMixCallback(
            track, FireAndForgetPanCallback, panState);

        if (pitch != 0.0f)
        {
            // P12-PITCH-001: matches FNA's real exponential octave curve (SoundEffectInstance.cs:
            // 589-591, `Math.Pow(2.0, INTERNAL_pitch)`) via SoundEffectInstance's shared, friended
            // conversion helper -- NOT a linear multiplier (this call site previously duplicated
            // the same wrong formula setPitchProperty()/ApplyTrackProperties() also had).
            const float ratio = SoundEffectInstance::INTERNAL_calculatePitchRatio(pitch);
            CNA::Internal::Audio::SetMixerTrackFrequencyRatio(
                track, ratio < 0.01f ? 0.01f : ratio);
        }

        // Auto-destroy track (and free panState) when playback finishes.
        CNA::Internal::Audio::SetMixerTrackStoppedCallback(
            track, OnFireAndForgetStopped, panState);

        if (!CNA::Internal::Audio::PlayMixerTrack(track))
        {
            // A failed play means the track never actually started, so
            // OnFireAndForgetStopped will never fire for it -- free panState here instead.
            delete panState;
            CNA::Internal::Audio::DestroyMixerTrack(track);
            return false;
        }

        return true;
#else
        (void)volume; (void)pitch; (void)pan;
        return false;
#endif
    }

    void SoundEffect::Dispose()
    {
        if (!isDisposed_)
        {
            if (impl_)
            {
                // instance->Dispose() unregisters itself from impl_->instances, mutating the
                // live vector -- iterate a snapshot so that's safe (matches FNA's
                // Instances.ToArray() before the foreach in SoundEffect.Dispose()).
                auto instancesSnapshot = impl_->instances;
                for (auto* instance : instancesSnapshot)
                {
                    if (instance) instance->Dispose();
                }
            }
            impl_.reset();
            isDisposed_ = true;
        }
    }

    void* SoundEffect::getNativeAudioHandle() const
    {
#ifdef SOUND_ENABLED
        if (impl_ && impl_->audio)
        {
            return impl_->audio.get();
        }
#endif
        return nullptr;
    }

    // --- static methods ---

    System::TimeSpan SoundEffect::GetSampleDuration(
        SharpRuntime::intcs sizeInBytes,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
    {
        const int ch = static_cast<int>(channels);
        if (ch <= 0 || sampleRate <= 0)
        {
            return System::TimeSpan::Zero;
        }
        // Matches FNA: truncate to whole milliseconds. 16-bit PCM => 2 bytes per sample.
        const int samples = sizeInBytes / 2;
        const int ms = static_cast<int>(
            (samples / ch) / (sampleRate / 1000.0f)
        );
        return System::TimeSpan::FromMilliseconds(ms);
    }

    SharpRuntime::intcs SoundEffect::GetSampleSizeInBytes(
        System::TimeSpan duration,
        SharpRuntime::intcs sampleRate,
        AudioChannels channels)
    {
        return static_cast<SharpRuntime::intcs>(
            duration.getTotalSecondsProperty() *
            sampleRate *
            static_cast<int>(channels) *
            2 // 16-bit PCM
        );
    }

    namespace
    {
        // CP-17: FNA's FromStream hand-parses the WAV file and scans for an optional "smpl"
        // chunk (RIFF metadata, not audio data) to recover an authored loop region. CNA's
        // FromStream instead delegates the actual audio decode to the internal mixer engine, but the
        // smpl chunk still needs this small, independent, platform-free scan to recover
        // loopStart/loopLength for SoundEffectInstance::Play() to apply. Returns false (leaving
        // loopStart/loopLength untouched) if the stream isn't a WAV file or has no smpl chunk.
        bool TryParseWavSmplChunk(const std::vector<char>& bytes,
                                   SharpRuntime::uintcs& loopStart,
                                   SharpRuntime::uintcs& loopLength)
        {
            auto u32At = [&](std::size_t offset) -> uint32_t
            {
                uint32_t v;
                std::memcpy(&v, bytes.data() + offset, 4);
                return v;
            };

            if (bytes.size() < 12) return false;
            if (std::memcmp(bytes.data(), "RIFF", 4) != 0) return false;
            if (std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;

            std::size_t pos = 12;
            while (pos + 8 <= bytes.size())
            {
                const char* chunkId = bytes.data() + pos;
                const uint32_t chunkSize = u32At(pos + 4);
                const std::size_t chunkDataStart = pos + 8;

                if (std::memcmp(chunkId, "smpl", 4) == 0)
                {
                    // Sampler chunk: 7 x u32 (Manufacturer..SMPTEOffset) + numSampleLoops +
                    // samplerData = 36 bytes, then numSampleLoops x 24-byte loop entries.
                    if (chunkDataStart + 36 > bytes.size()) return false;
                    const uint32_t numSampleLoops = u32At(chunkDataStart + 28);
                    if (numSampleLoops == 0) return false;

                    const std::size_t firstLoopOffset = chunkDataStart + 36;
                    if (firstLoopOffset + 24 > bytes.size()) return false;

                    // Loop entry: CuePointID(4), Type(4), Start(4), End(4), Fraction(4),
                    // PlayCount(4) -- only the first entry's Start/End matter (matches FNA).
                    const uint32_t start = u32At(firstLoopOffset + 8);
                    const uint32_t end   = u32At(firstLoopOffset + 12);
                    if (end <= start) return false;

                    loopStart  = static_cast<SharpRuntime::uintcs>(start);
                    loopLength = static_cast<SharpRuntime::uintcs>(end - start);
                    return true;
                }

                if (chunkDataStart + chunkSize > bytes.size()) return false;
                pos = chunkDataStart + chunkSize + (chunkSize & 1u); // chunks are word-aligned
            }
            return false;
        }
    }

    SoundEffect* SoundEffect::FromStream(std::istream& stream)
    {
        // Read all bytes from the stream.
        std::vector<char> bytes(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>()
        );

        if (bytes.empty())
        {
            throw System::NotSupportedException("SoundEffect::FromStream: empty stream");
        }

#ifdef SOUND_ENABLED
        EnsureMixerOrThrowXna();
        auto implPtr = std::make_shared<Impl>();
        const auto* first = reinterpret_cast<const std::byte*>(bytes.data());
        implPtr->audio = CNA::Internal::Audio::LoadMixerAudioMemory(
            std::span<const std::byte>(first, bytes.size()));
        if (!implPtr->audio)
        {
            throw System::NotSupportedException(
                std::string("SoundEffect::FromStream: audio decode failed: ")
                + CNA::Internal::Audio::GetMixerError()
            );
        }

        const auto format = CNA::Internal::Audio::GetMixerAudioFormat(implPtr->audio.get());
        if (format.sampleRate > 0 && format.channels > 0)
        {
            implPtr->sampleRate = format.sampleRate;
            implPtr->channels   = static_cast<SharpRuntime::uintcs>(format.channels);
        }

        auto* result = new SoundEffect(std::move(implPtr));
        TryParseWavSmplChunk(bytes, result->loopStart_, result->loopLength_); // best-effort (CP-17)
        return result;
#else
        (void)bytes;
        return new SoundEffect(std::make_shared<Impl>());
#endif
    }

    GetTypeNameCPP(SoundEffect, "Microsoft.Xna.Framework.Audio.SoundEffect")
}
