// SPDX-License-Identifier: MS-PL
#pragma once

#include <mutex>

namespace CNA::Platform::Sdl3 {

    /**
     * @brief Serialises SDL process-global lifecycle and graphics-loader state.
     *
     * SDL subsystem counts, OpenGL attributes and the Vulkan loader are process-global; a mutex
     * stored in one Sdl3Platform instance would not protect them from another instance or from a
     * graphics service. The intentionally process-lifetime allocation also remains valid during
     * late static teardown by an embedding host.
     */
    inline std::mutex& SdlGlobalStateMutex()
    {
        static auto* mutex = new std::mutex();
        return *mutex;
    }

} // namespace CNA::Platform::Sdl3
