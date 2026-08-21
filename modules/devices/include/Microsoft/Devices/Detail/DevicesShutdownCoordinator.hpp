// SPDX-License-Identifier: MS-PL

#pragma once

#include <atomic>

namespace Microsoft::Devices::Detail
{
    /**
     * @brief Coordinates `Microsoft::Devices` teardown before the host platform shuts down.
     *
     * `VibrateController` is a function-local singleton and would ordinarily release its
     * platform haptic service during process-exit static teardown. That can be too late: the
     * application may already have shut down the native platform, invalidating retained device
     * handles. `Shutdown()` therefore destroys the controller's platform adapter immediately,
     * while the selected platform is still valid, and only then publishes the shutdown flag.
     * Later controller calls are safe inert operations and the singleton destructor has no
     * platform resource left to release. A process-exit fallback also marks shutdown immediately
     * before the singleton destructor runs. That fallback cannot balance native resources as
     * precisely as `Shutdown()`, but it prevents a late destructor from calling through a platform
     * whose own static lifetime has already ended.
     *
     * The dedicated shutdown-ordering harness exercises the required sequence in a standalone
     * process because the shared test executable cannot tear down process-wide native services.
     * Applications must call `Shutdown()` before their own platform shutdown whenever the default
     * vibration controller may have been used.
     */
    class DevicesShutdownCoordinator
    {
    public:
        /**
         * @brief Marks `Microsoft::Devices` as shut down. Idempotent -- safe
         * to call more than once, and safe to call even if no
         * `Microsoft::Devices` object was ever constructed.
         */
        static void Shutdown();

        /**
         * @brief Registers the process-exit fallback after the controller singleton is created.
         *
         * Idempotent. Normal hosts should still call `Shutdown()` while their platform is alive;
         * this only makes omitted shutdown safe during unordered static teardown.
         */
        static void RegisterProcessExitFallback();

        /** @brief True once `Shutdown()` has been called; false otherwise (the default). */
        [[nodiscard]] static bool IsShutdown()
        {
            return GetFlag().load(std::memory_order_acquire);
        }

        /** @brief Test-only hook: resets back to "not shut down." */
        static void ResetForTesting();

    private:
        static std::atomic<bool>& GetFlag();
    };
} // namespace Microsoft::Devices::Detail
