// SPDX-License-Identifier: MS-PL

#pragma once

#include <mutex>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"

namespace Microsoft::Xna::Framework
{
    /// Updates framework-level systems such as dynamic audio streams, media and touch input.
    class FrameworkDispatcher final
    {
    public:
        /// Static-only class; not instantiable.
        FrameworkDispatcher() = delete;

        /// Processes pending framework updates and raises deferred framework events.
        static void Update();

        /// Pending active-song-changed notification flag (internal use).
        NOXNA static bool ActiveSongChanged;
        /// Pending media-state-changed notification flag (internal use).
        NOXNA static bool MediaStateChanged;
        /// Dynamic sound effect instances registered for update (internal use).
        NOXNA static std::vector<Audio::DynamicSoundEffectInstance*> Streams;
        /// Mutex protecting the Streams list (internal use).
        NOXNA static std::mutex StreamsMutex;
    };
}
