// SPDX-License-Identifier: MS-PL

#include "Sdl3DeviceServices.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

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

    Sdl3Sensors::~Sdl3Sensors()
    {
        for (const auto& [kind, handle] : open_)
        {
            if (handle != nullptr)
            {
                SDL_CloseSensor(static_cast<SDL_Sensor*>(handle));
            }
        }
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
    }

    int Sdl3Haptics::GetCount() const
    {
        int count = 0;
        SDL_HapticID* ids = SDL_GetHaptics(&count);
        ids_.clear();
        if (ids != nullptr)
        {
            ids_.assign(ids, ids + count);
            SDL_free(ids);
        }
        return static_cast<int>(ids_.size());
    }

    void* Sdl3Haptics::Acquire(const int index)
    {
        const auto it = open_.find(index);
        if (it != open_.end())
        {
            return it->second;
        }

        // Refresh the id list so an index means the same thing as the count it came from.
        (void)GetCount();
        if (index < 0 || index >= static_cast<int>(ids_.size()))
        {
            return nullptr;
        }

        SDL_Haptic* haptic = SDL_OpenHaptic(ids_[static_cast<std::size_t>(index)]);
        if (haptic == nullptr)
        {
            return nullptr;
        }
        open_[index] = haptic;
        return haptic;
    }

    std::string Sdl3Haptics::GetName(const int index) const
    {
        (void)GetCount();
        if (index < 0 || index >= static_cast<int>(ids_.size()))
        {
            return {};
        }
        const char* name = SDL_GetHapticNameForID(ids_[static_cast<std::size_t>(index)]);
        return name != nullptr ? std::string(name) : std::string();
    }

    bool Sdl3Haptics::SupportsRumble(const int index) const
    {
        // Acquire() is non-const by design (it caches), so this opens transiently rather than
        // mutating the cache from a const query.
        (void)GetCount();
        if (index < 0 || index >= static_cast<int>(ids_.size()))
        {
            return false;
        }

        const auto it = open_.find(index);
        if (it != open_.end() && it->second != nullptr)
        {
            return SDL_HapticRumbleSupported(static_cast<SDL_Haptic*>(it->second));
        }

        SDL_Haptic* haptic = SDL_OpenHaptic(ids_[static_cast<std::size_t>(index)]);
        if (haptic == nullptr)
        {
            return false;
        }
        const bool supported = SDL_HapticRumbleSupported(haptic);
        SDL_CloseHaptic(haptic);
        return supported;
    }

    bool Sdl3Haptics::PlayRumble(const int index, const float strength,
                                 const std::uint32_t durationMilliseconds)
    {
        SDL_Haptic* haptic = static_cast<SDL_Haptic*>(Acquire(index));
        if (haptic == nullptr)
        {
            // A device that vanished on hotplug reports failure rather than throwing: losing a
            // controller mid-effect is ordinary.
            return false;
        }

        if (!SDL_HapticRumbleSupported(haptic) || !SDL_InitHapticRumble(haptic))
        {
            return false;
        }

        // Clamped before the call for the same reason gamepad rumble is: an out-of-range value
        // would be reinterpreted rather than saturated.
        const float clamped = std::clamp(strength, 0.0f, 1.0f);
        return SDL_PlayHapticRumble(haptic, clamped, durationMilliseconds);
    }

    bool Sdl3Haptics::StopRumble(const int index)
    {
        const auto it = open_.find(index);
        if (it == open_.end() || it->second == nullptr)
        {
            return false;
        }
        return SDL_StopHapticRumble(static_cast<SDL_Haptic*>(it->second));
    }

} // namespace CNA::Platform::Sdl3
