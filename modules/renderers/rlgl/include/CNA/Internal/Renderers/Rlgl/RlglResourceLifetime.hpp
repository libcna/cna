// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    class RlglThreadContextLeaseControl;

    /** @brief CPU state retained by one native child for later context recovery. */
    struct RlglResourceRecoveryInfo
    {
        /** @brief Bytes retained specifically to recreate the resource or restore its contents. */
        std::size_t retainedCpuBytes = 0;
        /** @brief Texture mip levels or cube face/level pairs with defined retained contents. */
        std::size_t definedTextureSubresources = 0;
        /** @brief Whether recreation must report content loss through the public resource contract. */
        bool contentLostOnReset = false;
    };

    /** @brief Native-resource release hook tracked by one RLGL renderer lifetime. */
    class IRlglNativeResource
    {
    public:
        /** @brief Destroys the native-resource release-hook interface. */
        virtual ~IRlglNativeResource() = default;

        /** @brief Releases and clears every native handle owned by this resource. */
        virtual void ReleaseNativeResource() noexcept = 0;

        /** @brief Clears old-context native identities without issuing graphics API calls. */
        virtual void InvalidateNativeResource() noexcept = 0;

        /** @brief Recreates this resource from its retained CPU description in the current context. */
        virtual void RecreateNativeResource() = 0;

        /**
         * @brief Describes CPU state retained for context recovery.
         * @return Retained byte count, defined texture subresources, and content-loss policy.
         */
        [[nodiscard]] virtual RlglResourceRecoveryInfo GetRecoveryInfo() const noexcept
        {
            return {};
        }
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
        /** @brief Number of live children registered for native-context recreation. */
        std::size_t recoveryResources = 0;
        /** @brief Recovery children whose contents or state can be restored from CPU descriptions. */
        std::size_t restorableResources = 0;
        /** @brief Recovery children that must raise the existing content-loss contract. */
        std::size_t contentLostResources = 0;
        /** @brief Total bytes retained specifically for registered resource recovery. */
        std::size_t retainedCpuBytes = 0;
        /** @brief Total currently defined texture mip or cube face/level shadows. */
        std::size_t definedTextureSubresources = 0;
        /** @brief Number of context-loss invalidation passes applied to the live children. */
        std::size_t contextLossInvalidations = 0;
        /** @brief Number of complete registered-resource restoration passes. */
        std::size_t resourceRestorations = 0;
        /** @brief Number of registered-resource restoration attempts rolled back after failure. */
        std::size_t failedResourceRestorations = 0;
        /** @brief Whether resources created from now on join the recovery registry. */
        bool recoveryEnabledForNewResources = true;
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
         * @return True when the resource also joined the context-recovery registry.
         */
        [[nodiscard]] bool Register(IRlglNativeResource& resource);

        /**
         * @brief Selects whether subsequently created children retain context-recovery state.
         * @param enabled True to register future resources for recovery.
         */
        void SetRecoveryEnabled(bool enabled);

        /**
         * @brief Invalidates every child identity before the old graphics context is replaced.
         *
         * This path deliberately makes no graphics API calls. CPU recovery descriptions and both
         * registries remain intact for a later recreation transaction.
         */
        void InvalidateNativeResourcesForContextLoss() noexcept;

        /** @brief Recreates every recovery-registered child in original creation order. */
        void RestoreNativeResourcesAfterContextRecreation();

        /** @brief Releases any replacement child identities after a later recovery-stage failure. */
        void ReleaseNativeResourcesForRecoveryRollback() noexcept;

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
        std::vector<IRlglNativeResource*> recoveryResources_;
        std::atomic<std::size_t> registeredResources_{0};
        std::atomic<std::size_t> registeredRecoveryResources_{0};
        std::atomic<std::size_t> releasedResources_{0};
        std::atomic<std::size_t> lateDisposals_{0};
        std::atomic<std::size_t> contextLossInvalidations_{0};
        std::atomic<std::size_t> resourceRestorations_{0};
        std::atomic<std::size_t> failedResourceRestorations_{0};
        std::atomic<bool> active_{true};
        std::atomic<bool> recoveryEnabled_{true};
    };
}
