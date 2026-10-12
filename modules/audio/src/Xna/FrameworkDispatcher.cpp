// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"

#include "CNA/Internal/Audio/FrameworkUpdates.hpp"

#include <array>
#include <atomic>
#include <cstddef>

namespace
{
    // Constant-initialized: no static initialization order to depend on.
    std::array<std::atomic<void (*)()>,
               static_cast<std::size_t>(CNA::Internal::FrameworkUpdates::Slot::Count)>
        frameworkUpdates{};
}

namespace Microsoft::Xna::Framework
{
    bool FrameworkDispatcher::ActiveSongChanged = false;
    bool FrameworkDispatcher::MediaStateChanged = false;
    std::vector<Audio::DynamicSoundEffectInstance*> FrameworkDispatcher::Streams;
    std::mutex FrameworkDispatcher::StreamsMutex;

    void FrameworkDispatcher::Update()
    {
        // In XNA's order: dynamic sound effect streams, microphones, the media player and its
        // events. Each subsystem installed its slot when it first had work (FrameworkUpdates.hpp).
        for (const auto& update : frameworkUpdates)
        {
            if (auto* const run = update.load(std::memory_order_acquire))
            {
                run();
            }
        }

        if (Input::Touch::TouchPanel::getTouchDeviceExistsProperty())
        {
            Input::Touch::TouchPanel::Update();
        }
    }
}

namespace CNA::Internal::FrameworkUpdates
{
    void Install(const Slot slot, void (*const update)()) noexcept
    {
        frameworkUpdates[static_cast<std::size_t>(slot)].store(update, std::memory_order_release);
    }
}
