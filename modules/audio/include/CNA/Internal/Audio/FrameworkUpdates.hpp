// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::FrameworkUpdates
{
    /**
     * @brief The per-frame work FrameworkDispatcher::Update() runs for a subsystem a program may
     *        not use, in the order it runs.
     */
    enum class Slot : unsigned char
    {
        /** @brief Streaming DynamicSoundEffectInstances (BufferNeeded). */
        DynamicSoundEffects,
        /** @brief Started microphones (BufferReady). */
        Microphones,
        /** @brief MediaPlayer's song tracking and its ActiveSongChanged/MediaStateChanged events. */
        MediaPlayer,
        /** @brief Number of slots. */
        Count
    };

    /**
     * @brief Installs a subsystem's per-frame update. The subsystem calls it when it first has
     *        work -- a stream starts, a microphone list is created, the media player changes
     *        state -- so FrameworkDispatcher::Update() never names it.
     *
     * Link boundary: the dispatcher used to call DynamicSoundEffectInstance, Microphone and
     * MediaPlayer directly, which linked the audio capture and media subsystems into every game
     * that only plays sound effects. Installation is idempotent and lock-free (any thread); the
     * dispatcher reads the slots in order, without allocation or lookup.
     *
     * @param slot Which work. @param update The work.
     */
    void Install(Slot slot, void (*update)()) noexcept;
}
