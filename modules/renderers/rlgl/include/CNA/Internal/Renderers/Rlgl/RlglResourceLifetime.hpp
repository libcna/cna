// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    class RlglThreadContextLeaseControl;

    /** @brief Native-resource release hook tracked by one RLGL renderer lifetime. */
    class IRlglNativeResource
    {
    public:
        /** @brief Destroys the native-resource release-hook interface. */
        virtual ~IRlglNativeResource() = default;

        /** @brief Releases and clears every native handle owned by this resource. */
        virtual void ReleaseNativeResource() noexcept = 0;
    };

    /** @brief Observable resource-lifetime facts used by focused renderer validation. */
    struct RlglResourceLifetimeSnapshot
    {
        /** @brief Number of resources still registered with the live device. */
        std::size_t registeredResources = 0;
        /** @brief Number of release hooks completed while the device context was alive. */
        std::size_t releasedResources = 0;
        /** @brief Number of destructors observed after the device registry closed. */
        std::size_t lateDisposals = 0;
        /** @brief Whether the device registry still accepts resources and native disposal. */
        bool active = false;
    };

    /**
     * @brief Serializes native resource disposal with one RLGL device and its GL context.
     */
    class RlglResourceLifetime final
    {
    public:
        /**
         * @brief Creates an active resource registry for one renderer context.
         * @param contextControl Weak access to the renderer's serialized context owner.
         */
        explicit RlglResourceLifetime(
            const std::shared_ptr<RlglThreadContextLeaseControl>& contextControl);

        /**
         * @brief Registers a newly created native child with the renderer lifetime.
         * @param resource Resource whose handles must be released before context shutdown.
         */
        void Register(IRlglNativeResource& resource);

        /**
         * @brief Releases and unregisters a resource while the renderer context is current.
         * @param resource Resource entering its C++ destructor.
         */
        void Dispose(IRlglNativeResource& resource) noexcept;

        /**
         * @brief Makes the renderer context current, releases all native children, and closes the registry.
         */
        void Shutdown() noexcept;

        /**
         * @brief Returns the current registry counters without retaining native children.
         * @return Active state plus registered, released, and late-disposal counts.
         */
        [[nodiscard]] RlglResourceLifetimeSnapshot GetSnapshotForTesting() const noexcept;

    private:
        std::weak_ptr<RlglThreadContextLeaseControl> contextControl_;
        std::vector<IRlglNativeResource*> resources_;
        std::atomic<std::size_t> registeredResources_{0};
        std::atomic<std::size_t> releasedResources_{0};
        std::atomic<std::size_t> lateDisposals_{0};
        std::atomic<bool> active_{true};
    };
}
