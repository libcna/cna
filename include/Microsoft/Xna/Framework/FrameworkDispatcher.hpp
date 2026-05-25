#pragma once

#include <mutex>
#include <vector>

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
        FrameworkDispatcher() = delete;

        // Implementation state corresponding to assembly-internal framework dispatcher fields.
        static bool ActiveSongChanged;
        static bool MediaStateChanged;
        static std::vector<Audio::DynamicSoundEffectInstance*> Streams;
        static std::mutex StreamsMutex;

        /// Processes pending framework updates and raises deferred framework events.
        static void Update();
    };
}
