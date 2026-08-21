// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp"

#include "Microsoft/Devices/VibrateController.hpp"

#include <cstdlib>
#include <mutex>

namespace Microsoft::Devices::Detail
{
    namespace
    {
        std::mutex& GetShutdownMutex()
        {
            // The coordinator is itself used from process-exit teardown. Intentionally keep this
            // tiny synchronization primitive alive until the OS reclaims the process so static
            // destruction order cannot invalidate it before VibrateController's destructor.
            static auto* mutex = new std::mutex();
            return *mutex;
        }
    }

    std::atomic<bool>& DevicesShutdownCoordinator::GetFlag()
    {
        // Same static-teardown rule as GetShutdownMutex().
        static auto* flag = new std::atomic<bool>(false);
        return *flag;
    }

    void DevicesShutdownCoordinator::Shutdown()
    {
        std::lock_guard<std::mutex> lock(GetShutdownMutex());
        if (GetFlag().load(std::memory_order_acquire))
        {
            return;
        }

        // Destroy the adapter while the selected platform and its native
        // services are still valid. The controller remains safely inert if
        // an application accidentally calls it after shutdown.
        VibrateController::getDefaultProperty()->ShutdownBackendForPlatform();
        GetFlag().store(true, std::memory_order_release);
    }

    void DevicesShutdownCoordinator::RegisterProcessExitFallback()
    {
        static const bool registered = [] {
            std::atexit([] {
                // This callback is registered after VibrateController's function-local static is
                // initialized, so it runs before that singleton's destructor. Do not touch the
                // platform here: a later-created lazy platform may already have been destroyed.
                GetFlag().store(true, std::memory_order_release);
            });
            return true;
        }();
        (void)registered;
    }

    void DevicesShutdownCoordinator::ResetForTesting()
    {
        std::lock_guard<std::mutex> lock(GetShutdownMutex());
        GetFlag().store(false, std::memory_order_release);
        VibrateController::getDefaultProperty()->SetBackendForTesting(nullptr);
    }
} // namespace Microsoft::Devices::Detail
