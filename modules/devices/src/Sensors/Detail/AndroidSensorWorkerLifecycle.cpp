// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorWorkerLifecycle.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    bool AndroidSensorWorkerLifecycle::TryBeginStart()
    {
        if (runState_ != RunState::NotRunning || !reclaimClaimed_)
        {
            return false;
        }

        runState_ = RunState::Running;
        workerThreadId_ = {};
        reclaimClaimed_ = false;
        return true;
    }

    void AndroidSensorWorkerLifecycle::SetWorkerThreadId(const std::thread::id id)
    {
        workerThreadId_ = id;
    }

    AndroidSensorWorkerLifecycle::StopDecision AndroidSensorWorkerLifecycle::RequestStop(
        const std::thread::id caller)
    {
        StopDecision decision;
        decision.IsWorkerThread = caller == workerThreadId_;

        if (runState_ == RunState::Running)
        {
            runState_ = RunState::Stopping;
            decision.ShouldRequestStop = true;
        }

        if (!reclaimClaimed_)
        {
            reclaimClaimed_ = true;
            decision.ShouldReclaimWorker = true;
        }

        return decision;
    }

    void AndroidSensorWorkerLifecycle::MarkExited()
    {
        runState_ = RunState::NotRunning;
        workerThreadId_ = {};
    }

    AndroidSensorWorkerLifecycle::RunState AndroidSensorWorkerLifecycle::GetRunState() const
    {
        return runState_;
    }
} // namespace Microsoft::Devices::Sensors::Detail
