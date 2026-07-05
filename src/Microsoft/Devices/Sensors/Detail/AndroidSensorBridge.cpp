// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp"

#ifdef __ANDROID__
#include <android/looper.h>
#include <android/sensor.h>

#include <atomic>
#include <thread>
#endif

namespace Microsoft::Devices::Sensors::Detail
{
#ifdef __ANDROID__
    struct AndroidSensorBridge::Impl
    {
        explicit Impl(int sensorType)
            : sensorType_(sensorType)
        {
        }

        int sensorType_;
        ASensorManager* manager_ = nullptr;
        ASensor const* sensor_ = nullptr;
        ASensorEventQueue* queue_ = nullptr;
        std::atomic<ALooper*> looper_{nullptr};
        std::thread worker_;
        std::atomic<bool> stopRequested_{false};
        SampleCallback callback_;
        System::TimeSpan timeBetweenUpdates_;

        // ASensorManager_getInstanceForPackage() (the non-deprecated
        // replacement) requires API 26+; this project's minimum target is
        // API 24 (docs/devices-build.md). The deprecated,
        // package-name-agnostic ASensorManager_getInstance() is the correct
        // choice for that minimum, not an oversight — silencing the
        // deprecation warning locally rather than raising the project's
        // minimum API level for this one call.
        static ASensorManager* GetManager()
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
            return ASensorManager_getInstance();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        [[nodiscard]] bool Probe()
        {
            if (manager_ == nullptr)
            {
                manager_ = GetManager();
            }
            if (manager_ == nullptr)
            {
                return false;
            }
            if (sensor_ == nullptr)
            {
                sensor_ = ASensorManager_getDefaultSensor(manager_, sensorType_);
            }
            return sensor_ != nullptr;
        }

        void Run()
        {
            // ALooper is thread-affine: must be prepared on the same
            // thread that later polls it (Task DEVICES-0078) — hence this
            // dedicated worker thread, rather than requiring the game loop
            // to pump anything itself.
            ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
            looper_.store(looper, std::memory_order_release);

            queue_ = ASensorManager_createEventQueue(manager_, looper, 0, nullptr, nullptr);
            if (queue_ == nullptr)
            {
                return;
            }

            ASensorEventQueue_enableSensor(queue_, sensor_);

            ASensorEventQueue_setEventRate(
                queue_, sensor_, ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(timeBetweenUpdates_));

            // 100ms bounded wait: re-checks stopRequested_ promptly even if
            // ALooper_wake() is missed due to the benign startup race on
            // looper_ (Stop() may run before this store above completes) —
            // that race costs at most one extra ~100ms wait, never a hang.
            while (!stopRequested_.load(std::memory_order_acquire))
            {
                ALooper_pollOnce(100, nullptr, nullptr, nullptr);

                ASensorEvent event;
                while (ASensorEventQueue_getEvents(queue_, &event, 1) > 0)
                {
                    AndroidSensorSample sample;
                    sample.ValueCount = 16;
                    for (int i = 0; i < 16; ++i)
                    {
                        sample.Values[i] = event.data[i];
                    }
                    // Wall-clock time of delivery, deliberately NOT
                    // event.timestamp (a monotonic boot-time nanosecond
                    // value) — see AndroidSensorSample::Timestamp's own
                    // doc comment.
                    sample.Timestamp = System::DateTimeOffset::getUtcNowProperty();

                    if (callback_)
                    {
                        callback_(sample);
                    }
                }
            }

            ASensorEventQueue_disableSensor(queue_, sensor_);
            ASensorManager_destroyEventQueue(manager_, queue_);
            queue_ = nullptr;
        }
    };
#else
    struct AndroidSensorBridge::Impl
    {
        explicit Impl(int)
        {
        }
    };
#endif

    AndroidSensorBridge::AndroidSensorBridge(int androidSensorType)
        : impl_(std::make_unique<Impl>(androidSensorType))
    {
    }

    AndroidSensorBridge::~AndroidSensorBridge()
    {
        Stop();
    }

    bool AndroidSensorBridge::IsAvailable() const
    {
#ifdef __ANDROID__
        return impl_->Probe();
#else
        return false;
#endif
    }

    bool AndroidSensorBridge::Start(const System::TimeSpan& timeBetweenUpdates, SampleCallback callback)
    {
#ifdef __ANDROID__
        if (!impl_->Probe())
        {
            return false;
        }

        impl_->timeBetweenUpdates_ = timeBetweenUpdates;
        impl_->callback_ = std::move(callback);
        impl_->stopRequested_.store(false, std::memory_order_release);
        impl_->worker_ = std::thread([this]() { impl_->Run(); });
        return true;
#else
        (void)timeBetweenUpdates;
        (void)callback;
        return false;
#endif
    }

    void AndroidSensorBridge::Stop()
    {
#ifdef __ANDROID__
        if (impl_->worker_.joinable())
        {
            impl_->stopRequested_.store(true, std::memory_order_release);
            ALooper* looper = impl_->looper_.load(std::memory_order_acquire);
            if (looper != nullptr)
            {
                ALooper_wake(looper);
            }

            if (std::this_thread::get_id() == impl_->worker_.get_id())
            {
                // Reentrant call from within this bridge's own callback, on
                // its own worker thread (e.g. a Compass/Motion instance
                // disposing itself from inside its own CurrentValueChanged
                // handler). Joining our own thread would throw
                // std::system_error (resource_deadlock_would_occur) —
                // detach instead and let Run() finish exiting on its own.
                // Mirrors Accelerometer's own documented, accepted
                // "destroying from within your own callback" boundary
                // rather than fully solving it here.
                impl_->worker_.detach();
            }
            else
            {
                impl_->worker_.join();
            }
        }
#endif
    }
} // namespace Microsoft::Devices::Sensors::Detail
