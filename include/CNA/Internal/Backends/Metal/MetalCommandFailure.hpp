// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Metal
{
    /** @brief Required ordering before a render-target readback blit is submitted. */
    enum class MetalReadbackSourcePolicy
    {
        /** @brief Commit and synchronously verify the exact active source-render command. */
        SynchronizeActiveSource,
        /** @brief Source work is already committed; rely on queue ordering and the failure latch. */
        QueueOrderedCommittedSource
    };

    /**
     * @brief Chooses whether readback must synchronously verify an active source command.
     *
     * @param sourceIsActive True when the requested target still owns the open render pass.
     * @return Exact synchronization for an active source, otherwise queue-ordered submission.
     */
    [[nodiscard]] constexpr MetalReadbackSourcePolicy DescribeMetalReadbackSourcePolicy(
        bool sourceIsActive) noexcept
    {
        return sourceIsActive ? MetalReadbackSourcePolicy::SynchronizeActiveSource
                              : MetalReadbackSourcePolicy::QueueOrderedCommittedSource;
    }

    /** @brief Classification after waiting for one exact synchronous command submission. */
    enum class MetalSynchronousCommandResult
    {
        /** @brief The exact submission completed and no older asynchronous failure is pending. */
        Complete,
        /** @brief The exact submission being awaited failed. */
        ExactSubmissionFailed,
        /** @brief The exact submission completed but an older asynchronous failure was latched. */
        OlderSubmissionFailed
    };

    /**
     * @brief Separates exact synchronous failure from an older asynchronous failure.
     *
     * @param exactSubmissionCompleted Whether the command explicitly awaited completed normally.
     * @param olderFailureLatched Whether the shared asynchronous latch became set while waiting.
     * @return The diagnostic ownership classification; exact failure takes precedence when both
     *         conditions are present because that submitted command is independently known bad.
     */
    [[nodiscard]] constexpr MetalSynchronousCommandResult DescribeMetalSynchronousCommandResult(
        bool exactSubmissionCompleted,
        bool olderFailureLatched) noexcept
    {
        if(!exactSubmissionCompleted)
            return MetalSynchronousCommandResult::ExactSubmissionFailed;
        if(olderFailureLatched)
            return MetalSynchronousCommandResult::OlderSubmissionFailed;
        return MetalSynchronousCommandResult::Complete;
    }

    /** @brief Thread-safe handoff of an asynchronous Metal command-buffer failure. */
    class MetalCommandFailureLatch
    {
    public:
        /** @brief Records that a completed native command buffer failed. */
        void RecordFailure() noexcept
        {
            failed_.store(true, std::memory_order_release);
        }

        /**
         * @brief Reports a pending failure without consuming its required backend teardown.
         *
         * @return True after a failed completion and before the owning backend consumes it.
         */
        [[nodiscard]] bool HasFailure() const noexcept
        {
            return failed_.load(std::memory_order_acquire);
        }

        /**
         * @brief Consumes the pending failure exactly once at a later synchronous API entry.
         *
         * @return True for the first consume after one or more failures, otherwise false.
         */
        [[nodiscard]] bool ConsumeFailure() noexcept
        {
            return failed_.exchange(false, std::memory_order_acq_rel);
        }

    private:
        std::atomic<bool> failed_{false};
    };

    /** @brief Observable lifetime and command-health state for retained Metal resources. */
    enum class MetalResourceHealthState
    {
        /** @brief The owning backend is active and no asynchronous failure is pending. */
        Active,
        /** @brief The owner is active, but its command-failure latch is pending cleanup. */
        PendingCommandFailure,
        /** @brief The owning backend has begun teardown and must no longer be dereferenced. */
        OwnerInactive
    };

    /**
     * @brief Shared lifetime-safe health token for resources that can outlive their graphics device.
     *
     * The token owns only a const view of the command-failure latch. Resource operations may
     * observe a pending failure, but only the live backend can consume it and perform the required
     * encoder/command teardown. Owner inactivity takes precedence over later command completions.
     */
    class MetalResourceHealth final
    {
    public:
        /**
         * @brief Constructs an active resource-health token.
         *
         * @param commandFailureLatch Shared read-only view of the owner's failure latch.
         */
        explicit MetalResourceHealth(
            std::shared_ptr<const MetalCommandFailureLatch> commandFailureLatch)
            : commandFailureLatch_(std::move(commandFailureLatch))
        {
            if(!commandFailureLatch_)
                throw std::invalid_argument("Metal resource health requires a failure latch");
        }

        /** @brief Marks the owning backend inactive before its native teardown begins. */
        void MarkOwnerInactive() noexcept
        {
            ownerActive_.store(false, std::memory_order_release);
        }

        /**
         * @brief Observes the current owner/resource state without consuming command failure.
         *
         * @return Active, pending-failure, or inactive state; inactive wins a concurrent teardown.
         */
        [[nodiscard]] MetalResourceHealthState ObserveState() const noexcept
        {
            if(!ownerActive_.load(std::memory_order_acquire))
                return MetalResourceHealthState::OwnerInactive;
            const bool failed=commandFailureLatch_->HasFailure();
            if(!ownerActive_.load(std::memory_order_acquire))
                return MetalResourceHealthState::OwnerInactive;
            return failed ? MetalResourceHealthState::PendingCommandFailure
                          : MetalResourceHealthState::Active;
        }

        /**
         * @brief Rejects only owner teardown while allowing a live owner to clean pending failure.
         *
         * A resource uses this before weak-locking its owner. If command failure is pending, the
         * retained live owner remains responsible for invoking its common consume/abandon path.
         */
        void RequireOwnerActive() const
        {
            if(!ownerActive_.load(std::memory_order_acquire))
                throw std::runtime_error(
                    "Metal: resource operation rejected after owning backend teardown");
        }

        /**
         * @brief Rejects native resource work when its owner is failed or inactive.
         *
         * A pending failure remains latched for the owning backend's common cleanup path.
         */
        void RequireUsable() const
        {
            const MetalResourceHealthState state=ObserveState();
            if(state==MetalResourceHealthState::Active)
                return;
            if(state==MetalResourceHealthState::PendingCommandFailure)
                throw std::runtime_error(
                    "Metal: a command failure is pending owning-backend cleanup");
            throw std::runtime_error(
                "Metal: resource operation rejected after owning backend teardown");
        }

    private:
        std::shared_ptr<const MetalCommandFailureLatch> commandFailureLatch_;
        std::atomic<bool> ownerActive_{true};
    };

    /**
     * @brief Locks a weak native-resource owner after validating shared resource health.
     *
     * @tparam TOwner Backend implementation type retained for the duration of the operation.
     * @param resourceHealth Shared health token published by the owner.
     * @param owner Weak owner reference that must not prolong GraphicsDevice lifetime by itself.
     * @return Strong owner reference that keeps teardown from starting during the operation.
     */
    template<class TOwner>
    [[nodiscard]] std::shared_ptr<TOwner> RequireMetalResourceOwner(
        const std::shared_ptr<MetalResourceHealth>& resourceHealth,
        const std::weak_ptr<TOwner>& owner)
    {
        if(!resourceHealth)
            throw std::invalid_argument("Metal resource requires owner health");
        resourceHealth->RequireOwnerActive();
        auto retainedOwner=owner.lock();
        if(!retainedOwner)
            throw std::runtime_error("Metal: resource owner is no longer active");
        return retainedOwner;
    }

    /**
     * @brief Optionally locks an owner for nonthrowing resource-destructor cleanup.
     *
     * @tparam TOwner Backend implementation type that owns active binding state.
     * @param owner Weak owner reference held by the native resource.
     * @return Strong owner while it is still alive, otherwise an empty pointer.
     */
    template<class TOwner>
    [[nodiscard]] std::shared_ptr<TOwner> LockMetalResourceOwnerForCleanup(
        const std::weak_ptr<TOwner>& owner) noexcept
    {
        return owner.lock();
    }
}
