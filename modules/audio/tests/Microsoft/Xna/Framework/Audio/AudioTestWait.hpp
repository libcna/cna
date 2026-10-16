// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <thread>

namespace CnaAudioTest
{
    /**
     * @brief Polls @p condition until it holds or @p timeout has passed.
     *
     * For a state the audio device thread reaches in its own time, such as a cue finishing
     * naturally: a fixed sleep measures the machine's scheduling as much as CNA, because a loaded
     * machine runs the device late. The default deadline is long enough that missing it means the
     * state is not reached at all.
     *
     * @param condition Returns true once the awaited state is observable.
     * @param timeout How long to keep polling.
     * @return Whether the condition held before the deadline.
     */
    template <class Condition>
    bool WaitUntil(Condition condition, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!condition())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return condition();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }
}
