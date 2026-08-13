// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Devices/Sensors/Detail/NativeDiagnostic.hpp"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Microsoft::Devices::Sensors::Detail
{
    template <typename F>
    class ScopeExit
    {
    public:
        explicit ScopeExit(F onExit) : onExit_(std::move(onExit)) {}
        ~ScopeExit() noexcept
        {
            try { onExit_(); } catch (...) {}
        }
        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;

    private:
        F onExit_;
    };

    template <typename F>
    ScopeExit<F> MakeScopeExit(F onExit) { return ScopeExit<F>(std::move(onExit)); }

    /**
     * @brief Shared public-sensor state layered on independent platform sensor sessions.
     *
     * One instance exists per public sensor class. It retains the original ABA-safe registration
     * identity and in-flight callback barrier: stopping invalidates a registration before an
     * object can be destroyed, and dispatch revalidates each registration immediately before use.
     */
    template <typename TSensor>
    class PlatformSensorSubsystem
    {
    public:
        struct DispatchRegistration
        {
            /** @brief Registered object, or null after invalidation; guarded by mutex_. */
            TSensor* owner = nullptr;
        };

        /** @brief Probes the ambient platform with one balanced subsystem lifetime. */
        static bool ProbeIsSupported(const CNA::Platform::SensorKind kind)
        {
            CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
            CNA::Platform::IPlatformSensors* sensors = platform.GetSensors();
            if (sensors == nullptr)
            {
                return false;
            }
            try
            {
                platform.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
                auto release = MakeScopeExit([&platform]
                {
                    platform.ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
                });
                const bool available = sensors->IsAvailable(kind);
                return available;
            }
            catch (const CNA::Platform::PlatformException&)
            {
                return false;
            }
        }

        /** @brief Opens the shared stream after the caller acquired its subsystem hold. */
        [[nodiscard]] bool EnsureSessionLocked(const CNA::Platform::SensorKind kind)
        {
            if (forceEventWatchRegistrationFailureForTesting_)
            {
                lastEventWatchError_ = "forced failure (SetEventWatchRegistrationFailureForTesting)";
                return false;
            }

            CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
            if (platform_ != nullptr && platform_ != &platform)
            {
                lastEventWatchError_ = "sensor instances cannot span two selected platforms";
                return false;
            }
            if (session_ != nullptr)
            {
                return true;
            }
            CNA::Platform::IPlatformSensors* sensors = platform.GetSensors();
            if (sensors == nullptr)
            {
                lastEventWatchError_ = "selected platform has no sensor service";
                return false;
            }
            try
            {
                session_ = sensors->OpenSensor(kind, [this](const CNA::Platform::SensorReading& reading)
                {
                    std::vector<std::shared_ptr<DispatchRegistration>> snapshot;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        snapshot = startedInstances_;
                    }
                    DispatchToInstances(snapshot, [&reading](TSensor* instance)
                    {
                        instance->ProcessSensorUpdateEvent(0, reading.x, reading.y, reading.z);
                    });
                });
            }
            catch (const std::exception& ex)
            {
                lastEventWatchError_ = ex.what();
                return false;
            }
            catch (...)
            {
                lastEventWatchError_ = "platform sensor session creation threw an unknown exception";
                return false;
            }
            if (session_ == nullptr)
            {
                lastEventWatchError_ = "no matching sensor could be opened";
                return false;
            }
            platform_ = &platform;
            return true;
        }

        /** @brief Closes the stream after its last started registration disappears. */
        [[nodiscard]] std::unique_ptr<CNA::Platform::IPlatformSensorSession>
        TakeInactiveSessionLocked()
        {
            if (startedInstances_.empty())
            {
                platform_ = nullptr;
                return std::move(session_);
            }
            return nullptr;
        }

        void RegisterStartedInstanceLocked(TSensor* instance)
        {
            const bool present = std::any_of(startedInstances_.begin(), startedInstances_.end(),
                [instance](const auto& registration) { return registration->owner == instance; });
            if (!present)
            {
                startedInstances_.push_back(std::make_shared<DispatchRegistration>(
                    DispatchRegistration{instance}));
            }
        }

        void UnregisterStartedInstanceLocked(TSensor* instance)
        {
            const auto found = std::find_if(startedInstances_.begin(), startedInstances_.end(),
                [instance](const auto& registration) { return registration->owner == instance; });
            if (found != startedInstances_.end())
            {
                (*found)->owner = nullptr;
                startedInstances_.erase(found);
            }
        }

        template <typename DispatchFn>
        void DispatchToInstances(
            const std::vector<std::shared_ptr<DispatchRegistration>>& snapshot,
            DispatchFn&& dispatchOne)
        {
            const std::thread::id threadId = std::this_thread::get_id();
            for (const auto& registration : snapshot)
            {
                TSensor* instance = nullptr;
                std::shared_ptr<std::vector<std::thread::id>> token;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    instance = registration->owner;
                    if (instance != nullptr)
                    {
                        token = instance->dispatchToken_;
                        token->push_back(threadId);
                    }
                }
                if (!token)
                {
                    continue;
                }

                auto cleanup = MakeScopeExit([this, token, threadId]
                {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto& ids = *token;
                        const auto found = std::find(ids.begin(), ids.end(), threadId);
                        if (found != ids.end())
                        {
                            ids.erase(found);
                        }
                    }
                    callbackFinished_.notify_all();
                });

                try { dispatchOne(instance); }
                catch (const std::exception& ex) { LogAndRecordDispatchException(ex.what()); }
                catch (...) { LogAndRecordDispatchException("non-std::exception value"); }
            }
        }

        /** @brief Checks whether the ambient platform still enumerates a sensor id. */
        static bool IsSensorConnected(const std::int64_t sensorId)
        {
            CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
            CNA::Platform::IPlatformSensors* sensors = platform.GetSensors();
            if (sensors == nullptr)
            {
                return false;
            }
            try
            {
                platform.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
                auto release = MakeScopeExit([&platform]
                {
                    platform.ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
                });
                const std::vector<CNA::Platform::SensorInfo> available = sensors->GetSensors();
                return std::any_of(available.begin(), available.end(), [sensorId](const auto& info)
                {
                    return info.id == static_cast<std::uint64_t>(sensorId);
                });
            }
            catch (const CNA::Platform::PlatformException&)
            {
                return false;
            }
        }

        std::string lastEventWatchError_;
        bool forceEventWatchRegistrationFailureForTesting_ = false;
        std::string lastDispatchExceptionMessageForTesting_;
        int dispatchExceptionCountForTesting_ = 0;
        int instanceCount_ = 0;
        std::vector<std::shared_ptr<DispatchRegistration>> startedInstances_;
        std::mutex mutex_;
        std::condition_variable callbackFinished_;
        std::unique_ptr<CNA::Platform::IPlatformSensorSession> session_;

    private:
        void LogAndRecordDispatchException(const std::string& message)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lastDispatchExceptionMessageForTesting_ = message;
                ++dispatchExceptionCountForTesting_;
            }

            NativeDiagnosticRecord record;
            record.Backend = platform_ != nullptr ? platform_->GetName() : "Platform";
            record.Operation = "PlatformSensorSubsystem dispatch callback";
            record.NativeMessage = message;
            record.Timestamp = System::DateTimeOffset::getUtcNowProperty();
            record.Severity = NativeDiagnosticSeverity::Warning;
            NativeDiagnosticSink::Record(record);
        }

        CNA::Platform::IPlatform* platform_ = nullptr;
    };
}
