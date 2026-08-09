// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>

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
}
