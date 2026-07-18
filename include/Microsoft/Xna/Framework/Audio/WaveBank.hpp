// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEngine;
    class Cue;
    class SoundEffect;

    /** @brief Manages a bank of wave data loaded from a .XWB WaveBank file. */
    class WaveBank : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when the bank is disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /**
         * @brief Constructs a WaveBank by loading a non-streaming .XWB file.
         *
         * @param audioEngine                  The audio engine that owns this bank.
         * @param nonStreamingWaveBankFilename Path to the .XWB WaveBank file.
         * @throws System::ArgumentNullException if @p audioEngine is null or
         *         @p nonStreamingWaveBankFilename is empty.
         */
        WaveBank(AudioEngine* audioEngine, const std::string& nonStreamingWaveBankFilename);

        /**
         * @brief Constructs a WaveBank for streaming from a .XWB file.
         *
         * @param audioEngine                The audio engine that owns this bank.
         * @param streamingWaveBankFilename  Path to the streaming .XWB WaveBank file.
         * @param offset                     Byte offset into the file where the wave data begins.
         * @param packetSize                 Size of streaming packets.
         * @throws System::ArgumentNullException if @p audioEngine is null or
         *         @p streamingWaveBankFilename is empty.
         */
        WaveBank(AudioEngine* audioEngine,
                 const std::string& streamingWaveBankFilename,
                 SharpRuntime::intcs offset,
                 SharpRuntime::shortcs packetSize);

        /** @brief Destroys the wave bank and releases all wave resources. */
        ~WaveBank() override;

        WaveBank(const WaveBank&) = delete;
        WaveBank& operator=(const WaveBank&) = delete;

        /**
         * @brief Gets whether this wave bank has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets whether this wave bank has finished loading and is ready for playback.
         *
         * @return true if prepared; otherwise false.
         */
        [[nodiscard]] bool getIsPreparedProperty() const;

        /**
         * @brief Gets whether any cue is currently playing a SoundEffectInstance sourced
         * from this wave bank.
         *
         * @return true if a cue produced from this bank is still playing; otherwise false.
         */
        [[nodiscard]] bool getIsInUseProperty() const;

        /** @brief Releases this wave bank and all its wave data. */
        void Dispose() override;

        GetTypeNameHPP()

    private:
        friend class AudioEngine;
        friend class Cue;

        AudioEngine* engine_;
        bool isDisposed_ = false;

        struct XactWaveBankImpl;
        std::unique_ptr<XactWaveBankImpl> xactImpl_;

        // AUD-11-025: guards every access to xactImpl_ itself (not just its cache contents --
        // see XactWaveBankImpl's own cacheMutex, which only serializes concurrent
        // GetSoundEffect() calls against each other). Dispose() calling xactImpl_.reset() with no
        // synchronization at all, while GetSoundEffect() is concurrently mid-decode on another
        // thread (holding a live reference into the same XactWaveBankImpl), would otherwise
        // destroy the object -- including its own cacheMutex -- while still in use: a genuine
        // use-after-free, and worse than a plain UAF if the other thread is actively blocked
        // holding that mutex when it gets destroyed out from under it. Both GetSoundEffect() and
        // Dispose() take this same mutex before touching xactImpl_, making XactWaveBankImpl's own
        // cacheMutex now redundant (removed) since this outer lock already serializes the entire
        // GetSoundEffect() body.
        //
        // AUD-15-003 (lock ordering): GetSoundEffect() holds this lock across constructing a
        // SoundEffect, which transitively acquires-and-releases AudioMixer.cpp's g_mixerMutex
        // (via GetMixer()/GetMixerOrThrowXna()) -- the one real nested-lock pattern anywhere in
        // this codebase's audio locks (DynamicSoundEffectInstance::queueMutex_ and SoundEffect.cpp's
        // PendingPanStateCleanup::mutex never nest with anything). The established, and only
        // permitted, order is xactImplMutex_ (outer) -> g_mixerMutex (inner) -- never the reverse.
        // This is safe today because AudioMixer.cpp has no dependency on WaveBank at all, so
        // nothing can ever try to acquire xactImplMutex_ while already holding g_mixerMutex; any
        // future code that would do so must be restructured to preserve this order instead.
        std::mutex xactImplMutex_;

        void Init(const std::string& filename);
        void InitStreaming(const std::string& filename);

        // Test-only internal queries (T-3F): whether this bank uses streaming/lazy per-entry
        // wave-data reads, and how many file bytes are actually resident in memory (the whole
        // file when not streaming; only the header/metadata segments when streaming). Exposed
        // as private methods rather than directly to WaveBankTestAccess because XactWaveBankImpl
        // is a pimpl type fully defined only in WaveBank.cpp.
        bool StreamingInternal() const;
        std::size_t ResidentFileBytesInternal() const;

        /** @brief Returns the bank name parsed from the .XWB file header. */
        const std::string& getBankName() const;

        /**
         * @brief Returns a SoundEffect for the given wave index (lazily created).
         *
         * Returns nullptr if the index is out of range or audio is unavailable.
         *
         * @param waveIndex Index into the wave bank.
         * @return Pointer to the SoundEffect, or nullptr.
         */
        const SoundEffect* GetSoundEffect(unsigned short waveIndex);

        // Cues currently playing a SoundEffectInstance sourced from this bank
        // (registered/unregistered by Cue::Play / Cue::StopInternal).
        std::vector<Cue*> activeCues_;
        void RegisterCue(Cue* cue);
        void UnregisterCue(Cue* cue);

        // Tests need to observe streaming-vs-in-memory internal state (T-3F) without a public
        // XNA-surface way to distinguish them.
        NOXNA friend struct WaveBankTestAccess;
    };
}
