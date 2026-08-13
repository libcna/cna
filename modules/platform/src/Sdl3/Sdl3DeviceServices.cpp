// SPDX-License-Identifier: MS-PL

#include "Sdl3DeviceServices.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace CNA::Platform::Sdl3 {

    namespace {

        SDL_SensorType ToSdlSensorType(const SensorKind kind)
        {
            switch (kind)
            {
                case SensorKind::Accelerometer:      return SDL_SENSOR_ACCEL;
                case SensorKind::Gyroscope:          return SDL_SENSOR_GYRO;
                case SensorKind::AccelerometerLeft:  return SDL_SENSOR_ACCEL_L;
                case SensorKind::GyroscopeLeft:      return SDL_SENSOR_GYRO_L;
                case SensorKind::AccelerometerRight: return SDL_SENSOR_ACCEL_R;
                case SensorKind::GyroscopeRight:     return SDL_SENSOR_GYRO_R;
                case SensorKind::Unknown:            return SDL_SENSOR_UNKNOWN;
            }
            return SDL_SENSOR_UNKNOWN;
        }

        SensorKind ToSensorKind(const SDL_SensorType type)
        {
            switch (type)
            {
                case SDL_SENSOR_ACCEL:   return SensorKind::Accelerometer;
                case SDL_SENSOR_GYRO:    return SensorKind::Gyroscope;
                case SDL_SENSOR_ACCEL_L: return SensorKind::AccelerometerLeft;
                case SDL_SENSOR_GYRO_L:  return SensorKind::GyroscopeLeft;
                case SDL_SENSOR_ACCEL_R: return SensorKind::AccelerometerRight;
                case SDL_SENSOR_GYRO_R:  return SensorKind::GyroscopeRight;
                default:                 return SensorKind::Unknown;
            }
        }

        SensorDisplayRotation ToSensorDisplayRotation(const SDL_DisplayOrientation orientation)
        {
            switch (orientation)
            {
                case SDL_ORIENTATION_PORTRAIT:           return SensorDisplayRotation::Degrees0;
                case SDL_ORIENTATION_LANDSCAPE:          return SensorDisplayRotation::Degrees90;
                case SDL_ORIENTATION_PORTRAIT_FLIPPED:   return SensorDisplayRotation::Degrees180;
                case SDL_ORIENTATION_LANDSCAPE_FLIPPED:  return SensorDisplayRotation::Degrees270;
                default:                                 return SensorDisplayRotation::Unknown;
            }
        }

        /// Finds the first connected sensor of a type. Returns 0 when none is present -- SDL uses
        /// 0 as its invalid instance id, so it doubles as the not-found answer.
        SDL_SensorID FindSensor(const SensorKind kind)
        {
            int count = 0;
            SDL_SensorID* ids = SDL_GetSensors(&count);
            if (ids == nullptr)
            {
                return 0;
            }

            SDL_SensorID found = 0;
            const SDL_SensorType wanted = ToSdlSensorType(kind);
            for (int i = 0; i < count; ++i)
            {
                if (SDL_GetSensorTypeForID(ids[i]) == wanted)
                {
                    found = ids[i];
                    break;
                }
            }
            SDL_free(ids);
            return found;
        }

        bool IsGamepadHapticDevice(const SDL_HapticID hapticId)
        {
            int count = 0;
            SDL_JoystickID* ids = SDL_GetJoysticks(&count);
            if (ids == nullptr)
            {
                return false;
            }

            bool matched = false;
            for (int index = 0; index < count && !matched; ++index)
            {
                SDL_Joystick* joystick = SDL_OpenJoystick(ids[index]);
                if (joystick == nullptr)
                {
                    continue;
                }
                SDL_Haptic* haptic = SDL_OpenHapticFromJoystick(joystick);
                if (haptic != nullptr)
                {
                    matched = SDL_GetHapticID(haptic) == hapticId;
                    SDL_CloseHaptic(haptic);
                }
                SDL_CloseJoystick(joystick);
            }
            SDL_free(ids);
            return matched;
        }

        float SanitizeHapticStrength(const float strength)
        {
            return std::isnan(strength) ? 0.0f : std::clamp(strength, 0.0f, 1.0f);
        }

        Uint16 ToHapticMagnitude(const float strength)
        {
            return static_cast<Uint16>(SanitizeHapticStrength(strength) * 65535.0f);
        }

        class Sdl3SensorSession final : public IPlatformSensorSession
        {
            struct State final : std::enable_shared_from_this<State>
            {
                State(SDL_Sensor* nativeSensor, const SDL_SensorID sensorId,
                      SensorReadingCallback readingCallback,
                      std::shared_ptr<std::recursive_mutex> serviceMutex)
                    : sensor(nativeSensor), id(sensorId), callback(std::move(readingCallback)),
                      nativeMutex(std::move(serviceMutex)) {}

                ~State()
                {
                    if (sensor != nullptr)
                    {
                        std::lock_guard<std::recursive_mutex> lock(*nativeMutex);
                        SDL_CloseSensor(sensor);
                    }
                }

                SDL_Sensor* sensor = nullptr;
                SDL_SensorID id = 0;
                SensorReadingCallback callback;
                std::shared_ptr<std::recursive_mutex> nativeMutex;
                std::mutex mutex;
                std::condition_variable finished;
                std::vector<std::thread::id> activeCallbackThreads;
                bool closing = false;
            };

        public:
            Sdl3SensorSession(SDL_Sensor* sensor, const SDL_SensorID id,
                              SensorReadingCallback callback,
                              std::shared_ptr<std::recursive_mutex> nativeMutex)
                : state_(std::make_shared<State>(sensor, id, std::move(callback),
                                                 std::move(nativeMutex)))
            {
                if (state_->callback && !SDL_AddEventWatch(&Sdl3SensorSession::EventWatch,
                                                           state_.get()))
                {
                    state_.reset();
                    throw PlatformException("Sensors::OpenSensor", SDL_GetError());
                }
                watchRegistered_ = static_cast<bool>(state_->callback);
            }

            ~Sdl3SensorSession() override
            {
                std::shared_ptr<State> state = std::move(state_);
                if (state == nullptr)
                {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->closing = true;
                }
                if (watchRegistered_)
                {
                    SDL_RemoveEventWatch(&Sdl3SensorSession::EventWatch, state.get());
                }
                {
                    const std::thread::id current = std::this_thread::get_id();
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->finished.wait(lock, [&state, current]
                    {
                        return std::none_of(state->activeCallbackThreads.begin(),
                                            state->activeCallbackThreads.end(),
                                            [current](const std::thread::id id)
                                            {
                                                return id != current;
                                            });
                    });
                }
                // A sensor handler is allowed to dispose its own sender. In that reentrant case
                // the current callback's shared State reference closes the native sensor after
                // the callback returns; all callbacks on other threads have already drained.
            }

            [[nodiscard]] bool TryGetReading(SensorReading& reading) const override
            {
                const std::shared_ptr<State> state = state_;
                float values[3] = {};
                if (state == nullptr)
                {
                    return false;
                }
                std::lock_guard<std::recursive_mutex> lock(*state->nativeMutex);
                if (state->sensor == nullptr || !SDL_GetSensorData(state->sensor, values, 3))
                {
                    return false;
                }
                reading = {values[0], values[1], values[2], SDL_GetTicksNS()};
                return true;
            }

        private:
            static bool SDLCALL EventWatch(void* userdata, SDL_Event* event)
            {
                auto* state = static_cast<State*>(userdata);
                if (state == nullptr || event == nullptr || event->type != SDL_EVENT_SENSOR_UPDATE)
                {
                    return true;
                }

                std::shared_ptr<State> keepAlive;
                const std::thread::id current = std::this_thread::get_id();
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->closing || event->sensor.which != state->id)
                    {
                        return true;
                    }
                    keepAlive = state->shared_from_this();
                    state->activeCallbackThreads.push_back(current);
                }

                const SensorReading reading{event->sensor.data[0], event->sensor.data[1],
                                            event->sensor.data[2],
                                            event->sensor.sensor_timestamp};
                try
                {
                    keepAlive->callback(reading);
                }
                catch (...)
                {
                    // Never unwind C++ exceptions through SDL's C callback frame. Public sensor
                    // dispatch records user-handler exceptions at the consumer boundary.
                }

                {
                    std::lock_guard<std::mutex> lock(keepAlive->mutex);
                    const auto active = std::find(keepAlive->activeCallbackThreads.begin(),
                                                  keepAlive->activeCallbackThreads.end(), current);
                    if (active != keepAlive->activeCallbackThreads.end())
                    {
                        keepAlive->activeCallbackThreads.erase(active);
                    }
                }
                keepAlive->finished.notify_all();
                return true;
            }

            std::shared_ptr<State> state_;
            bool watchRegistered_ = false;
        };

    } // namespace

    // --- sensors (PLAT-85) ----------------------------------------------------------------------

    Sdl3Sensors::~Sdl3Sensors()
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        CloseAll();
    }

    void Sdl3Sensors::Deactivate()
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        CloseAll();
    }

    void Sdl3Sensors::CloseAll()
    {
        for (const auto& [kind, handle] : open_)
        {
            (void)kind;
            if (handle != nullptr)
            {
                SDL_CloseSensor(static_cast<SDL_Sensor*>(handle));
            }
        }
        open_.clear();
    }

    std::vector<SensorInfo> Sdl3Sensors::GetSensors() const
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        int count = 0;
        SDL_SensorID* ids = SDL_GetSensors(&count);
        if (ids == nullptr)
        {
            return {};
        }

        std::vector<SensorInfo> sensors;
        sensors.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            SensorInfo info;
            info.id = static_cast<std::uint64_t>(ids[i]);
            info.kind = ToSensorKind(SDL_GetSensorTypeForID(ids[i]));
            const char* name = SDL_GetSensorNameForID(ids[i]);
            info.name = name != nullptr ? std::string(name) : std::string();
            sensors.push_back(std::move(info));
        }
        SDL_free(ids);
        return sensors;
    }

    bool Sdl3Sensors::IsAvailable(const SensorKind kind) const
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        // Unknown is a classification, not a request: a caller asking whether an unclassifiable
        // sensor is present is asking a question with no answer, and reporting true would let it
        // then Start() something the type lookup cannot find.
        return kind != SensorKind::Unknown && FindSensor(kind) != 0;
    }

    SensorDisplayRotation Sdl3Sensors::GetDisplayRotation() const
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        return primary != 0
            ? ToSensorDisplayRotation(SDL_GetCurrentDisplayOrientation(primary))
            : SensorDisplayRotation::Unknown;
    }

    std::unique_ptr<IPlatformSensorSession> Sdl3Sensors::OpenSensor(
        const SensorKind kind, SensorReadingCallback callback)
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        const SDL_SensorID id = FindSensor(kind);
        if (id == 0)
        {
            return nullptr;
        }
        SDL_Sensor* sensor = SDL_OpenSensor(id);
        return sensor != nullptr
            ? std::make_unique<Sdl3SensorSession>(sensor, id, std::move(callback), nativeMutex_)
            : nullptr;
    }

    void Sdl3Sensors::Start(const SensorKind kind)
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        if (open_.find(kind) != open_.end())
        {
            return;  // already started; starting twice is not an error
        }

        const SDL_SensorID id = FindSensor(kind);
        if (id == 0)
        {
            // The device genuinely has no such sensor. That is a capability answer, not a
            // failure of the call, so it names the capability rather than an SDL error string.
            throw PlatformNotSupportedException(PlatformCapability::Sensors, "SDL3");
        }

        SDL_Sensor* sensor = SDL_OpenSensor(id);
        if (sensor == nullptr)
        {
            throw PlatformException("Sensors::Start", SDL_GetError());
        }
        open_[kind] = sensor;
    }

    void Sdl3Sensors::Stop(const SensorKind kind)
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        const auto it = open_.find(kind);
        if (it == open_.end())
        {
            return;  // stopping something never started is a no-op, matching subsystem release
        }
        if (it->second != nullptr)
        {
            SDL_CloseSensor(static_cast<SDL_Sensor*>(it->second));
        }
        open_.erase(it);
    }

    bool Sdl3Sensors::TryGetReading(const SensorKind kind, SensorReading& reading) const
    {
        std::lock_guard<std::recursive_mutex> lock(*nativeMutex_);
        const auto it = open_.find(kind);
        if (it == open_.end() || it->second == nullptr)
        {
            return false;
        }

        float values[3] = {0.0f, 0.0f, 0.0f};
        if (!SDL_GetSensorData(static_cast<SDL_Sensor*>(it->second), values, 3))
        {
            return false;
        }

        reading.x = values[0];
        reading.y = values[1];
        reading.z = values[2];
        reading.timestampNanoseconds = SDL_GetTicksNS();
        return true;
    }

    // --- haptics (PLAT-84) ----------------------------------------------------------------------

    Sdl3Haptics::~Sdl3Haptics()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        CloseAll();
    }

    void Sdl3Haptics::Deactivate()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        CloseAll();
    }

    void Sdl3Haptics::CloseAll()
    {
        for (const auto& [index, handle] : open_)
        {
            if (handle != nullptr)
            {
                SDL_CloseHaptic(static_cast<SDL_Haptic*>(handle));
            }
        }
        open_.clear();
        rumbleInitialized_.clear();
        leftRightEffects_.clear();
    }

    std::vector<DeviceId> Sdl3Haptics::EnumerateIds() const
    {
        int count = 0;
        SDL_HapticID* ids = SDL_GetHaptics(&count);
        std::vector<DeviceId> result;
        if (ids != nullptr)
        {
            result.reserve(static_cast<std::size_t>(std::max(count, 0)));
            for (int index = 0; index < count; ++index)
            {
                if (ids[index] != 0)
                {
                    result.push_back(static_cast<DeviceId>(ids[index]));
                }
            }
            SDL_free(ids);
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        RetireMissing(result);
        return result;
    }

    void Sdl3Haptics::RetireMissing(const std::vector<DeviceId>& ids) const
    {
        for (auto item = open_.begin(); item != open_.end();)
        {
            if (std::binary_search(ids.begin(), ids.end(), item->first))
            {
                ++item;
                continue;
            }
            if (item->second != nullptr)
            {
                SDL_CloseHaptic(static_cast<SDL_Haptic*>(item->second));
            }
            rumbleInitialized_.erase(item->first);
            leftRightEffects_.erase(item->first);
            item = open_.erase(item);
        }
    }

    bool Sdl3Haptics::ProbeRumble(const DeviceId id) const
    {
        if (id == 0 || id > std::numeric_limits<SDL_HapticID>::max())
        {
            return false;
        }

        const auto existing = open_.find(id);
        if (existing != open_.end() && existing->second != nullptr)
        {
            return SDL_HapticRumbleSupported(static_cast<SDL_Haptic*>(existing->second));
        }

        SDL_Haptic* haptic = SDL_OpenHaptic(static_cast<SDL_HapticID>(id));
        if (haptic == nullptr)
        {
            return false;
        }
        const bool supported = SDL_HapticRumbleSupported(haptic);
        SDL_CloseHaptic(haptic);
        return supported;
    }

    std::vector<HapticInfo> Sdl3Haptics::GetHaptics() const
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const std::vector<DeviceId> ids = EnumerateIds();
        std::vector<HapticInfo> result;
        result.reserve(ids.size());
        for (const DeviceId id : ids)
        {
            const char* name = SDL_GetHapticNameForID(static_cast<SDL_HapticID>(id));
            result.push_back({id, name != nullptr ? std::string(name) : std::string(),
                              ProbeRumble(id)});
        }
        return result;
    }

    std::optional<HapticInfo> Sdl3Haptics::GetDefaultVibrationDevice() const
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const std::vector<DeviceId> ids = EnumerateIds();
        for (const DeviceId id : ids)
        {
            if (id > std::numeric_limits<SDL_HapticID>::max()
                || IsGamepadHapticDevice(static_cast<SDL_HapticID>(id)))
            {
                continue;
            }

            const auto existing = open_.find(id);
            SDL_Haptic* haptic = existing != open_.end()
                ? static_cast<SDL_Haptic*>(existing->second)
                : SDL_OpenHaptic(static_cast<SDL_HapticID>(id));
            if (haptic == nullptr)
            {
                continue;
            }

            const char* nativeName = SDL_GetHapticName(haptic);
            HapticInfo info{id, nativeName != nullptr ? std::string(nativeName) : std::string(),
                            SDL_HapticRumbleSupported(haptic)};
            if (existing == open_.end())
            {
                SDL_CloseHaptic(haptic);
            }
            return info;
        }
        return std::nullopt;
    }

    bool Sdl3Haptics::IsConnected(const DeviceId id) const
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const std::vector<DeviceId> ids = EnumerateIds();
        return std::binary_search(ids.begin(), ids.end(), id);
    }

    bool Sdl3Haptics::SupportsRumble(const DeviceId id) const
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const std::vector<DeviceId> ids = EnumerateIds();
        return std::binary_search(ids.begin(), ids.end(), id) && ProbeRumble(id);
    }

    bool Sdl3Haptics::InitializeRumble(const DeviceId id)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        SDL_Haptic* haptic = static_cast<SDL_Haptic*>(Acquire(id));
        if (haptic == nullptr || !SDL_HapticRumbleSupported(haptic))
        {
            return false;
        }
        const auto existing = rumbleInitialized_.find(id);
        if (existing != rumbleInitialized_.end() && existing->second)
        {
            return true;
        }
        const bool initialized = SDL_InitHapticRumble(haptic);
        rumbleInitialized_[id] = initialized;
        return initialized;
    }

    void* Sdl3Haptics::Acquire(const DeviceId id)
    {
        const std::vector<DeviceId> ids = EnumerateIds();
        if (!std::binary_search(ids.begin(), ids.end(), id)
            || id > std::numeric_limits<SDL_HapticID>::max())
        {
            return nullptr;
        }

        const auto existing = open_.find(id);
        if (existing != open_.end())
        {
            return existing->second;
        }

        SDL_Haptic* haptic = SDL_OpenHaptic(static_cast<SDL_HapticID>(id));
        if (haptic == nullptr)
        {
            return nullptr;
        }
        open_[id] = haptic;
        return haptic;
    }

    bool Sdl3Haptics::PlayRumble(const DeviceId id, const float strength,
                                 const std::uint32_t durationMilliseconds)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        SDL_Haptic* haptic = static_cast<SDL_Haptic*>(Acquire(id));
        if (haptic == nullptr)
        {
            // A device that vanished on hotplug reports failure rather than throwing: losing a
            // controller mid-effect is ordinary.
            return false;
        }

        if (!InitializeRumble(id))
        {
            return false;
        }

        DestroyLeftRight(id);

        // Clamped before the call for the same reason gamepad rumble is: an out-of-range value
        // would be reinterpreted rather than saturated.
        return SDL_PlayHapticRumble(haptic, SanitizeHapticStrength(strength),
                                    durationMilliseconds);
    }

    void Sdl3Haptics::DestroyLeftRight(const DeviceId id)
    {
        const auto effect = leftRightEffects_.find(id);
        if (effect == leftRightEffects_.end())
        {
            return;
        }
        const auto device = open_.find(id);
        if (device != open_.end() && device->second != nullptr && effect->second >= 0)
        {
            SDL_DestroyHapticEffect(static_cast<SDL_Haptic*>(device->second), effect->second);
        }
        leftRightEffects_.erase(effect);
    }

    bool Sdl3Haptics::PlayLeftRight(const DeviceId id, const float largeMotor,
                                    const float smallMotor,
                                    const std::uint32_t durationMilliseconds)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        SDL_Haptic* haptic = static_cast<SDL_Haptic*>(Acquire(id));
        if (haptic == nullptr || (SDL_GetHapticFeatures(haptic) & SDL_HAPTIC_LEFTRIGHT) == 0)
        {
            return false;
        }

        (void)SDL_StopHapticRumble(haptic);
        DestroyLeftRight(id);

        SDL_HapticEffect effect{};
        effect.leftright.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = durationMilliseconds;
        effect.leftright.large_magnitude = ToHapticMagnitude(largeMotor);
        effect.leftright.small_magnitude = ToHapticMagnitude(smallMotor);

        const int effectId = SDL_CreateHapticEffect(haptic, &effect);
        if (effectId < 0)
        {
            return false;
        }
        leftRightEffects_[id] = effectId;
        if (!SDL_RunHapticEffect(haptic, effectId, 1))
        {
            DestroyLeftRight(id);
            return false;
        }
        return true;
    }

    bool Sdl3Haptics::StopRumble(const DeviceId id)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const std::vector<DeviceId> ids = EnumerateIds();
        if (!std::binary_search(ids.begin(), ids.end(), id))
        {
            return false;
        }
        const auto it = open_.find(id);
        if (it == open_.end() || it->second == nullptr)
        {
            return false;
        }
        return SDL_StopHapticRumble(static_cast<SDL_Haptic*>(it->second));
    }

    bool Sdl3Haptics::StopAll(const DeviceId id)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const std::vector<DeviceId> ids = EnumerateIds();
        if (!std::binary_search(ids.begin(), ids.end(), id))
        {
            return false;
        }
        const auto it = open_.find(id);
        if (it == open_.end() || it->second == nullptr)
        {
            return false;
        }
        const bool stopped = SDL_StopHapticEffects(static_cast<SDL_Haptic*>(it->second));
        DestroyLeftRight(id);
        return stopped;
    }

} // namespace CNA::Platform::Sdl3
