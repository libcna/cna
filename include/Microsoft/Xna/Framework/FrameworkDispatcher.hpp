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
    /** @brief Updates framework-level systems such as dynamic audio streams, media and touch input. */
    class FrameworkDispatcher final
    {
    public:
        /** @brief Static-only class; not instantiable. */
        FrameworkDispatcher() = delete;

        /** @brief Processes pending framework updates and raises deferred framework events. */
        static void Update();

        /** @brief Pending active-song-changed notification flag (internal use). */
        NOXNA static bool ActiveSongChanged;
        /** @brief Pending media-state-changed notification flag (internal use). */
        NOXNA static bool MediaStateChanged;
        /** @brief Dynamic sound effect instances registered for update (internal use). */
        NOXNA static std::vector<Audio::DynamicSoundEffectInstance*> Streams;
        /** @brief Mutex protecting the Streams list (internal use). */
        NOXNA static std::mutex StreamsMutex;
    };
}
