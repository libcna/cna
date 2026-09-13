// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Platform::Win32 {

    /**
     * @brief A balanced, host-respecting COM initialization for the calling thread.
     *
     * COM's apartment model is per-thread process policy, and CNA is a framework inside somebody
     * else's process: the host may already have chosen an apartment, and taking that choice away
     * would break every COM object it already owns.
     *
     * So this is deliberately conservative. It calls `CoInitializeEx` and treats three outcomes
     * differently:
     *
     * - `S_OK` — this token initialized COM and will uninitialize it;
     * - `S_FALSE` — COM was already initialized on this thread with the same model, so the
     *   reference is ours to release but the apartment is not ours to end;
     * - `RPC_E_CHANGED_MODE` — the host chose a different apartment. **Nothing is released**, and
     *   the caller is told COM is usable, because it is: the thread has an apartment, just not the
     *   one that was asked for.
     *
     * RAII rather than a pair of calls, because an unbalanced `CoUninitialize` in a host's thread
     * is not a leak, it is a crash in the host's own objects.
     */
    class Win32ComRuntime
    {
    public:
        /** @brief Initializes COM for the calling thread, if the host has not already fixed it. */
        Win32ComRuntime();

        /** @brief Releases only what this token acquired. */
        ~Win32ComRuntime();

        Win32ComRuntime(const Win32ComRuntime&) = delete;
        Win32ComRuntime& operator=(const Win32ComRuntime&) = delete;

        /**
         * @brief Gets whether the calling thread has a usable COM apartment.
         *
         * @return True when COM can be used, whoever initialized it.
         */
        [[nodiscard]] bool IsUsable() const { return usable_; }

    private:
        bool usable_ = false;
        bool ownsReference_ = false;
    };

} // namespace CNA::Platform::Win32
