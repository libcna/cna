// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace CNA::Internal::Audio { struct XsbData; }

namespace Microsoft::Xna::Framework::Audio
{
    class AudioEmitter;
    class AudioEngine;
    class AudioListener;
    class Cue;

    /** @brief Manages a collection of named cues loaded from a .XSB SoundBank file. */
    class SoundBank : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when the bank is disposed. */
        System::EventHandler<System::EventArgs> Disposing;

        /**
         * @brief Constructs a SoundBank by loading a .XSB file with the given audio engine.
         *
         * @param audioEngine The audio engine that owns this bank.
         * @param filename    Path to the .XSB SoundBank file.
         * @throws System::ArgumentNullException if @p audioEngine is null or @p filename is empty.
         */
        SoundBank(AudioEngine* audioEngine, const std::string& filename);

        /** @brief Destroys the sound bank and releases all held cue resources. */
        ~SoundBank() override;

        SoundBank(const SoundBank&) = delete;
        SoundBank& operator=(const SoundBank&) = delete;

        /**
         * @brief Gets whether this sound bank has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets whether this sound bank is still in use by active cues.
         *
         * Reflects every cue currently associated with this bank, whether it is a
         * fire-and-forget cue created internally by PlayCue or one the caller obtained via
         * GetCue and is holding independently.
         *
         * @return true if an associated cue is still playing; otherwise false.
         */
        [[nodiscard]] bool getIsInUseProperty() const;

        /**
         * @brief Returns a new Cue instance for the cue with the given name.
         *
         * The caller is responsible for disposing the returned Cue; however, if this bank is
         * itself disposed while the cue is still playing, the bank will force-stop and dispose
         * it first (matching FACTSoundBank_Destroy) so it never outlives the bank.
         *
         * @param name Cue name as defined in the .XSB file.
         * @return Pointer to the new Cue (caller is responsible for disposal).
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::ObjectDisposedException if the bank has been disposed.
         * @throws System::InvalidOperationException if @p name is not a valid cue name.
         */
        [[nodiscard]] Cue* GetCue(const std::string& name);

        /**
         * @brief Plays the named cue as a fire-and-forget sound.
         *
         * @param name Cue name as defined in the .XSB file.
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::ObjectDisposedException if the bank has been disposed.
         * @throws System::InvalidOperationException if @p name is not a valid cue name.
         */
        void PlayCue(const std::string& name);

        /**
         * @brief Plays the named cue as a fire-and-forget sound with 3D positioning.
         *
         * @param name     Cue name as defined in the .XSB file.
         * @param listener Position and orientation of the audio listener.
         * @param emitter  Position and orientation of the sound emitter.
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::ObjectDisposedException if the bank has been disposed.
         * @throws System::InvalidOperationException if @p name is not a valid cue name.
         */
        void PlayCue(const std::string& name,
                     const AudioListener& listener,
                     const AudioEmitter& emitter);

        /** @brief Releases the sound bank and all its cue resources. */
        void Dispose() override;

        GetTypeNameHPP()

    private:
        friend class Cue;
        // P9-LIFECYCLE-008/009: AudioEngine::Update() calls SweepFireAndForget() on every
        // registered bank, mirroring FNA's AudioEngine.Update() -> FACTAudioEngine_DoWork, which
        // is what actually destroys managed/fire-and-forget cues once FACT_STATE_STOPPED
        // (FACT_internal.c) instead of waiting for this bank's next PlayCue() call.
        friend class AudioEngine;

        AudioEngine* engine_;
        bool isDisposed_ = false;

        struct XactSoundBankImpl;
        std::unique_ptr<XactSoundBankImpl> xactImpl_;

        const CNA::Internal::Audio::XsbData* GetXsbData() const;

        // Shared by both PlayCue() overloads; listener/emitter are null for the non-3D one, in
        // which case Apply3D isn't called (T-4B).
        void PlayCueInternal(const std::string& name,
                              const AudioListener* listener,
                              const AudioEmitter* emitter);

        // Removes finished fire-and-forget cues; factored out of PlayCueInternal so
        // AudioEngine::Update() can also trigger it promptly (see the AudioEngine friend
        // declaration above) instead of only sweeping on this bank's next PlayCue() call.
        void SweepFireAndForget();

        // P12-BANK-001: mirrors WaveBank's activeCues_/RegisterCue/UnregisterCue -- tracks every
        // cue currently associated with this bank, both caller-owned cues obtained via GetCue()
        // and this bank's own fireAndForget_ cues (registered from the same Cue::Play() call
        // site). Needed so Dispose() can force-stop a caller-owned GetCue() cue that outlives
        // the bank instead of leaving it with a dangling bank_ pointer (matches
        // FACTSoundBank_Destroy, FACT.c:1311-1327). fireAndForget_ entries self-unregister via
        // their own Cue destructor before Dispose() ever walks activeCues_ (see Dispose()), so by
        // the time the cascade below runs, only genuinely caller-owned cues remain in the list.
        void RegisterCue(Cue* cue);
        void UnregisterCue(Cue* cue);
        std::vector<Cue*> activeCues_;

        // Fire-and-forget cues from PlayCue() — kept alive while playing (FNA has no C#-visible
        // handle for these; FACT tracks cue lifetime natively), swept once IsPlaying goes false
        // on the next PlayCue() call. A long safety-net timeout also force-sweeps a still-playing
        // entry so a caller that only ever PlayCue()s a looping/very long cue can't grow this
        // list unbounded; ordinary finite-duration cues are always swept via IsPlaying first.
        struct FireAndForget
        {
            std::unique_ptr<Cue> cue;
            std::chrono::steady_clock::time_point created;
        };
        std::vector<FireAndForget> fireAndForget_;

        // Tests need to inspect fireAndForget_'s size and backdate entries to exercise the sweep
        // logic's time-based branches without a real-time wait.
        NOXNA friend struct SoundBankTestAccess;
    };
}
