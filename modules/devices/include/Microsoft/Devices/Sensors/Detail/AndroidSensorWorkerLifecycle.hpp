// SPDX-License-Identifier: MS-PL

#pragma once

#include <thread>

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Platform-independent state machine for one Android sensor worker generation.
     *
     * The owning AndroidSensorBridge serializes every call with its state mutex. Keeping the
     * transition policy here makes the self-stop/external-stop arbitration executable on hosts
     * that cannot compile or run the Android NDK sensor queue.
     */
    class AndroidSensorWorkerLifecycle final
    {
    public:
        /** @brief Observable execution state of the current worker generation. */
        enum class RunState
        {
            /** @brief No worker call stack is still executing. */
            NotRunning,
            /** @brief The worker is running and has not received a stop request. */
            Running,
            /** @brief Stop was requested but the worker has not confirmed exit yet. */
            Stopping,
        };

        /** @brief Decision returned for one caller entering Stop(). */
        struct StopDecision
        {
            /** @brief Whether the caller is the current worker itself. */
            bool IsWorkerThread = false;
            /** @brief Whether this call changed Running to Stopping and must signal the worker. */
            bool ShouldRequestStop = false;
            /** @brief Whether this call alone owns join/detach for the current generation. */
            bool ShouldReclaimWorker = false;
        };

        /**
         * @brief Begins a generation if no worker or unreclaimed thread object remains.
         *
         * @return true if the state changed to Running; false if a start must be refused.
         */
        [[nodiscard]] bool TryBeginStart();

        /**
         * @brief Records the stable worker thread identity after the thread is spawned.
         *
         * @param id Identity returned by the newly-created std::thread.
         */
        void SetWorkerThreadId(std::thread::id id);

        /**
         * @brief Applies stop arbitration for one caller.
         *
         * @param caller Identity of the thread calling Stop().
         * @return Whether the caller is the worker, must signal stop, and/or owns reclamation.
         */
        [[nodiscard]] StopDecision RequestStop(std::thread::id caller);

        /** @brief Confirms that the worker's Run() call stack has completely exited. */
        void MarkExited();

        /**
         * @brief Gets the current execution state.
         *
         * @return Current worker execution state.
         */
        [[nodiscard]] RunState GetRunState() const;

    private:
        RunState runState_ = RunState::NotRunning;
        std::thread::id workerThreadId_;
        bool reclaimClaimed_ = true;
    };
} // namespace Microsoft::Devices::Sensors::Detail
