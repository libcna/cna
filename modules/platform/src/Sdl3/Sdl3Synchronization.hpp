// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>
#include <mutex>
#include <thread>

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

    /**
     * @brief The thread currently inside @ref SdlGlobalStateMutex, or a default id when none is.
     *
     * plans/plan_vulkan.md `VULKAN-159` (finding F-28). Kept beside the mutex rather than inside a
     * lock type so a destructor can ask the question without owning anything: is this very thread
     * already in the critical section?
     */
    inline std::atomic<std::thread::id>& SdlGlobalStateOwner()
    {
        static auto* owner = new std::atomic<std::thread::id>();
        return *owner;
    }

    /**
     * @brief `std::lock_guard` for @ref SdlGlobalStateMutex that also records the owning thread.
     *
     * plans/plan_vulkan.md `VULKAN-159`. Every acquisition of the SDL global-state mutex goes
     * through this type, which is what makes @ref SdlGlobalStateHeldByThisThread trustworthy: a
     * raw `std::lock_guard` on the same mutex would leave the owner unset and turn the
     * destructor's check into a silent lie.
     */
    class SdlGlobalStateLock
    {
    public:
        /** @brief Locks the SDL global-state mutex and records this thread as its owner. */
        SdlGlobalStateLock()
        {
            SdlGlobalStateMutex().lock();
            SdlGlobalStateOwner().store(std::this_thread::get_id(), std::memory_order_release);
        }
        /** @brief Clears the recorded owner and unlocks. */
        ~SdlGlobalStateLock()
        {
            SdlGlobalStateOwner().store(std::thread::id{}, std::memory_order_release);
            SdlGlobalStateMutex().unlock();
        }
        SdlGlobalStateLock(const SdlGlobalStateLock&)            = delete;
        SdlGlobalStateLock& operator=(const SdlGlobalStateLock&) = delete;
    };

    /**
     * @brief Whether the calling thread is already inside the SDL global-state critical section.
     *
     * plans/plan_vulkan.md `VULKAN-159` (F-28). `exit()` can be reached from inside a platform
     * call that holds the mutex -- Xlib's default error handler does exactly that -- and the
     * static destructors it then runs include `~Sdl3Platform`, which would re-lock a
     * non-recursive mutex on its own owning thread and never return. Asking this first is the
     * narrow alternative to making the mutex recursive, which would sanction re-entrancy in every
     * other method that must not have it.
     *
     * @return true when this thread holds the mutex.
     */
    [[nodiscard]] inline bool SdlGlobalStateHeldByThisThread() noexcept
    {
        return SdlGlobalStateOwner().load(std::memory_order_acquire) == std::this_thread::get_id();
    }

} // namespace CNA::Platform::Sdl3
