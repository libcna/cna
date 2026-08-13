// SPDX-License-Identifier: MS-PL

#include "Sdl3DeviceServices.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

    } // namespace

    // --- sensors (PLAT-85) ----------------------------------------------------------------------

    Sdl3Sensors::~Sdl3Sensors() { CloseAll(); }

    void Sdl3Sensors::Deactivate() { CloseAll(); }

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
        // Unknown is a classification, not a request: a caller asking whether an unclassifiable
        // sensor is present is asking a question with no answer, and reporting true would let it
        // then Start() something the type lookup cannot find.
        return kind != SensorKind::Unknown && FindSensor(kind) != 0;
    }

    void Sdl3Sensors::Start(const SensorKind kind)
    {
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

    Sdl3Haptics::~Sdl3Haptics() { CloseAll(); }

    void Sdl3Haptics::Deactivate() { CloseAll(); }

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

    bool Sdl3Haptics::IsConnected(const DeviceId id) const
    {
        const std::vector<DeviceId> ids = EnumerateIds();
        return std::binary_search(ids.begin(), ids.end(), id);
    }

    bool Sdl3Haptics::SupportsRumble(const DeviceId id) const
    {
        const std::vector<DeviceId> ids = EnumerateIds();
        return std::binary_search(ids.begin(), ids.end(), id) && ProbeRumble(id);
    }

    bool Sdl3Haptics::InitializeRumble(const DeviceId id)
    {
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

        // Clamped before the call for the same reason gamepad rumble is: an out-of-range value
        // would be reinterpreted rather than saturated.
        const float clamped = std::isnan(strength) ? 0.0f
                                                   : std::clamp(strength, 0.0f, 1.0f);
        return SDL_PlayHapticRumble(haptic, clamped, durationMilliseconds);
    }

    bool Sdl3Haptics::StopRumble(const DeviceId id)
    {
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

} // namespace CNA::Platform::Sdl3
