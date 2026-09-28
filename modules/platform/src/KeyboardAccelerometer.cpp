// SPDX-License-Identifier: MS-PL
#include "CNA/Platform/Input/KeyboardAccelerometer.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace CNA::Platform
{
    namespace
    {
        constexpr float gravity = 9.80665f;
        struct Stream
        {
            std::mutex mutex;
            std::condition_variable finished;
            bool closed = false;
            SensorReadingCallback callback;
            std::vector<std::thread::id> executing;
            SensorReading reading{0, 0, -gravity, 0};
        };
        class Session final : public IPlatformSensorSession
        {
        public:
            explicit Session(std::shared_ptr<Stream> stream) : stream_(std::move(stream)) {}
            ~Session() override
            {
                auto stream = stream_;
                std::unique_lock<std::mutex> lock(stream->mutex);
                stream->closed = true;
                stream->callback = {};
                stream->finished.wait(lock, [&]
                {
                    return std::all_of(stream->executing.begin(), stream->executing.end(),
                        [](auto id) { return id == std::this_thread::get_id(); });
                });
            }
            bool TryGetReading(SensorReading& reading) const override
            {
                std::lock_guard<std::mutex> lock(stream_->mutex);
                if (stream_->closed) return false;
                reading = stream_->reading;
                return true;
            }
        private:
            std::shared_ptr<Stream> stream_;
        };
        class SoftwareSensors final : public IPlatformSensors
        {
        public:
            std::atomic<bool> enabled{false};
            mutable std::mutex mutex;
            bool polling = false;
            SensorReading latest{0, 0, -gravity, 0};
            std::vector<std::weak_ptr<Stream>> streams;

            std::vector<SensorInfo> GetSensors() const override
            {
                if (!enabled.load()) return {};
                return {{std::numeric_limits<std::uint32_t>::max(), SensorKind::Accelerometer,
                         "Keyboard-emulated accelerometer"}};
            }
            bool IsAvailable(SensorKind kind) const override
            { return enabled.load() && kind == SensorKind::Accelerometer; }
            SensorDisplayRotation GetDisplayRotation() const override
            { return SensorDisplayRotation::Degrees0; }
            std::unique_ptr<IPlatformSensorSession> OpenSensor(
                SensorKind kind, SensorReadingCallback callback) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!IsAvailable(kind)) return nullptr;
                auto stream = std::make_shared<Stream>();
                stream->callback = std::move(callback);
                stream->reading = latest;
                streams.emplace_back(stream);
                return std::make_unique<Session>(std::move(stream));
            }
            void Start(SensorKind kind) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (IsAvailable(kind)) polling = true;
            }
            void Stop(SensorKind kind) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (kind == SensorKind::Accelerometer) polling = false;
            }
            bool TryGetReading(SensorKind kind, SensorReading& reading) const override
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!IsAvailable(kind) || !polling) return false;
                reading = latest;
                return true;
            }
        };
        SoftwareSensors& Service() { static SoftwareSensors service; return service; }

        void Publish(const std::shared_ptr<Stream>& stream, const SensorReading& reading)
        {
            SensorReadingCallback callback;
            const auto thread = std::this_thread::get_id();
            {
                std::lock_guard<std::mutex> lock(stream->mutex);
                if (stream->closed) return;
                stream->reading = reading;
                callback = stream->callback;
                if (!callback) return;
                stream->executing.push_back(thread);
            }
            struct Finish
            {
                std::shared_ptr<Stream> stream;
                std::thread::id thread;
                ~Finish()
                {
                    {
                        std::lock_guard<std::mutex> lock(stream->mutex);
                        const auto found = std::find(stream->executing.begin(), stream->executing.end(), thread);
                        if (found != stream->executing.end()) stream->executing.erase(found);
                    }
                    stream->finished.notify_all();
                }
            } finish{stream, thread};
            callback(reading);
        }
    }

    bool KeyboardAccelerometer::IsEnabled() { return Service().enabled.load(); }
    void KeyboardAccelerometer::SetEnabled(bool enabled)
    {
        auto& service = Service();
        std::lock_guard<std::mutex> lock(service.mutex);
        if (service.enabled.load() == enabled) return;
        if (service.polling) throw System::InvalidOperationException("Stop the accelerometer before changing keyboard emulation.");
        for (const auto& weak : service.streams)
        {
            if (const auto stream = weak.lock())
            {
                std::lock_guard<std::mutex> streamLock(stream->mutex);
                if (!stream->closed) throw System::InvalidOperationException("Stop the accelerometer before changing keyboard emulation.");
            }
        }
        service.latest = {0, 0, -gravity, 0};
        service.enabled.store(enabled);
    }
    IPlatformSensors& KeyboardAccelerometer::GetSensors() { return Service(); }
    IPlatformSensors* KeyboardAccelerometer::Resolve(IPlatform& platform, SensorKind kind)
    {
        return kind == SensorKind::Accelerometer && IsEnabled() ? &Service() : platform.GetSensors();
    }
    void KeyboardAccelerometer::Update(const KeyboardSnapshot& keyboard, bool focused)
    {
        auto& service = Service();
        if (!service.enabled.load()) return;
        const auto held = [&](KeyCode key)
        {
            return focused && std::find(keyboard.pressedKeys.begin(), keyboard.pressedKeys.end(), key)
                != keyboard.pressedKeys.end();
        };
        const float x = static_cast<float>(held(KeyCode::Right)) - static_cast<float>(held(KeyCode::Left));
        const float y = static_cast<float>(held(KeyCode::Up)) - static_cast<float>(held(KeyCode::Down));
        const float scale = gravity / std::sqrt(x * x + y * y + 1.0f);
        const SensorReading reading{x * scale, y * scale, -scale,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count())};
        std::vector<std::shared_ptr<Stream>> snapshot;
        {
            std::lock_guard<std::mutex> lock(service.mutex);
            if (!service.enabled.load()) return;
            service.latest = reading;
            service.streams.erase(std::remove_if(service.streams.begin(), service.streams.end(),
                [&](const auto& weak)
                {
                    const auto stream = weak.lock();
                    if (!stream) return true;
                    snapshot.push_back(stream);
                    return false;
                }), service.streams.end());
        }
        for (const auto& stream : snapshot) Publish(stream, reading);
    }
}
