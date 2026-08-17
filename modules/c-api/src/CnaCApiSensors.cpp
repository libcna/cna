// SPDX-License-Identifier: MS-PL

#include "CNA/C/sensors.h"
#include "CnaCApiDetail.hpp"

#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/Compass.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp"
#include "Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/Motion.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/TimeSpan.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ValidateActiveGameHandle;

namespace {

using Microsoft::Devices::Sensors::Accelerometer;
using Microsoft::Devices::Sensors::AccelerometerReading;
using Microsoft::Devices::Sensors::Gyroscope;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::AttitudeReading;
using Microsoft::Devices::Sensors::CompassReading;
using Microsoft::Devices::Sensors::GyroscopeReading;
using Microsoft::Devices::Sensors::MotionReading;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result CopyText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The sensor text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the sensor text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

template<typename T>
[[nodiscard]] CNA_Result ValidateVersionedStructure(
    const T* const structure,
    const char* const message) noexcept
{
    if (structure == nullptr || structure->struct_size < sizeof(T) ||
        structure->struct_version != StructureVersion) {
        return InvalidInput(message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] System::DateTimeOffset ToTimestamp(const CNA_DateTimeOffset value)
{
    return System::DateTimeOffset(
        static_cast<SharpRuntime::longcs>(value.ticks),
        System::TimeSpan(static_cast<SharpRuntime::longcs>(value.offset_ticks)));
}

[[nodiscard]] CNA_DateTimeOffset MapTimestamp(const System::DateTimeOffset& value)
{
    CNA_DateTimeOffset mapped = {};
    mapped.ticks = static_cast<int64_t>(value.getTicksProperty());
    mapped.offset_ticks = static_cast<int64_t>(value.getOffsetProperty().getTicksProperty());
    return mapped;
}

[[nodiscard]] Vector3 ToVector(const CNA_Vector3 value)
{
    return Vector3(value.x, value.y, value.z);
}

[[nodiscard]] CNA_Vector3 MapVector(const Vector3& value)
{
    CNA_Vector3 mapped = {};
    mapped.x = value.X;
    mapped.y = value.Y;
    mapped.z = value.Z;
    return mapped;
}

[[nodiscard]] Quaternion ToQuaternion(const CNA_Quaternion value)
{
    return Quaternion(value.x, value.y, value.z, value.w);
}

[[nodiscard]] CNA_Quaternion MapQuaternion(const Quaternion& value)
{
    CNA_Quaternion mapped = {};
    mapped.x = value.X;
    mapped.y = value.Y;
    mapped.z = value.Z;
    mapped.w = value.W;
    return mapped;
}

[[nodiscard]] Matrix ToMatrix(const CNA_Matrix value)
{
    return Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

[[nodiscard]] CNA_Matrix MapMatrix(const Matrix& value)
{
    CNA_Matrix mapped = {};
    mapped.m11 = value.M11; mapped.m12 = value.M12; mapped.m13 = value.M13; mapped.m14 = value.M14;
    mapped.m21 = value.M21; mapped.m22 = value.M22; mapped.m23 = value.M23; mapped.m24 = value.M24;
    mapped.m31 = value.M31; mapped.m32 = value.M32; mapped.m33 = value.M33; mapped.m34 = value.M34;
    mapped.m41 = value.M41; mapped.m42 = value.M42; mapped.m43 = value.M43; mapped.m44 = value.M44;
    return mapped;
}

[[nodiscard]] CNA_Result ToAccelerometerReading(
    const CNA_AccelerometerReading* const reading,
    AccelerometerReading* const outReading)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(reading, "The accelerometer reading is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outReading = AccelerometerReading(
        ToTimestamp(reading->timestamp),
        ToVector(reading->acceleration));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToGyroscopeReading(
    const CNA_GyroscopeReading* const reading,
    GyroscopeReading* const outReading)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(reading, "The gyroscope reading is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outReading = GyroscopeReading(
        ToVector(reading->rotation_rate),
        ToTimestamp(reading->timestamp));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToAttitudeReading(
    const CNA_AttitudeReading* const reading,
    AttitudeReading* const outReading)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(reading, "The attitude reading is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outReading = AttitudeReading(
        reading->pitch,
        reading->roll,
        reading->yaw,
        ToQuaternion(reading->quaternion),
        ToMatrix(reading->rotation_matrix),
        ToTimestamp(reading->timestamp));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToCompassReading(
    const CNA_CompassReading* const reading,
    CompassReading* const outReading)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(reading, "The compass reading is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outReading = CompassReading(
        reading->heading_accuracy,
        reading->magnetic_heading,
        ToVector(reading->magnetometer_reading),
        ToTimestamp(reading->timestamp),
        reading->true_heading);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToMotionReading(
    const CNA_MotionReading* const reading,
    MotionReading* const outReading)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(reading, "The motion reading is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AttitudeReading attitude;
    if (const CNA_Result result = ToAttitudeReading(&reading->attitude, &attitude);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outReading = MotionReading(
        attitude,
        ToVector(reading->device_acceleration),
        ToVector(reading->device_rotation_rate),
        ToVector(reading->gravity),
        ToTimestamp(reading->timestamp));
    return CNA_RESULT_SUCCESS;
}

// A sensor owns its canonical object; every event registration keeps that resource alive, so a
// subscription can never outlive the sensor it detaches from.
template<typename TSensor>
struct SensorResource final {
    std::unique_ptr<TSensor> value;

    // Only the two sensors whose canonical backend is replaceable use this. It holds the state this
    // ABI's own test backend publishes through, kept beside the sensor rather than inside the
    // backend object, so an injection route can still answer "nothing is installed" after the
    // backend itself has been replaced or destroyed.
    std::shared_ptr<void> testBackend;
};

template<typename TSensor>
struct SensorKind;

template<>
struct SensorKind<Accelerometer> {
    static constexpr ObjectKind Kind = ObjectKind::Accelerometer;
};

template<>
struct SensorKind<Gyroscope> {
    static constexpr ObjectKind Kind = ObjectKind::Gyroscope;
};

template<typename TSensor>
[[nodiscard]] CNA_Result BorrowSensor(
    const CNA_Handle handle,
    std::shared_ptr<SensorResource<TSensor>>* const outSensor);

class SensorRegistrationBase {
public:
    SensorRegistrationBase() = default;
    SensorRegistrationBase(const SensorRegistrationBase&) = delete;
    SensorRegistrationBase& operator=(const SensorRegistrationBase&) = delete;
    virtual ~SensorRegistrationBase() = default;
};

template<typename TEventArgs>
class SensorRegistration final : public SensorRegistrationBase {
public:
    using Source = System::EventHandler<TEventArgs>;
    using Token = typename Source::Token;

    SensorRegistration(std::shared_ptr<void> owner, Source* const source, const Token token)
        : owner_(std::move(owner))
        , source_(source)
        , token_(token)
    {
    }

    ~SensorRegistration() override
    {
        source_->Remove(token_);
    }

private:
    std::shared_ptr<void> owner_;
    Source* source_;
    Token token_;
};

[[nodiscard]] CNA_Result PublishRegistration(
    std::shared_ptr<SensorRegistrationBase> registration,
    CNA_Handle* const outRegistration)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
        ObjectKind::SensorEventRegistration,
        std::move(registration),
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The sensor registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_AccelerometerReading MapAccelerometerReadingValue(
    const AccelerometerReading& value);

[[nodiscard]] CNA_GyroscopeReading MapGyroscopeReadingValue(const GyroscopeReading& value);

[[nodiscard]] CNA_AttitudeReading MapAttitudeReading(const AttitudeReading& value)
{
    CNA_AttitudeReading mapped = {};
    mapped.struct_size = sizeof(CNA_AttitudeReading);
    mapped.struct_version = StructureVersion;
    mapped.timestamp = MapTimestamp(value.getTimestampProperty());
    mapped.pitch = value.getPitchProperty();
    mapped.roll = value.getRollProperty();
    mapped.yaw = value.getYawProperty();
    mapped.quaternion = MapQuaternion(value.getQuaternionProperty());
    mapped.rotation_matrix = MapMatrix(value.getRotationMatrixProperty());
    return mapped;
}

template<typename TSensor>
[[nodiscard]] CNA_Result BorrowSensor(
    const CNA_Handle handle,
    std::shared_ptr<SensorResource<TSensor>>* const outSensor)
{
    const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Get(
        handle,
        SensorKind<TSensor>::Kind,
        outSensor);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The sensor handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_AccelerometerReading MapAccelerometerReadingValue(
    const AccelerometerReading& value)
{
    CNA_AccelerometerReading mapped = {};
    mapped.struct_size = sizeof(CNA_AccelerometerReading);
    mapped.struct_version = StructureVersion;
    mapped.timestamp = MapTimestamp(value.getTimestampProperty());
    mapped.acceleration = MapVector(value.getAccelerationProperty());
    return mapped;
}

[[nodiscard]] CNA_GyroscopeReading MapGyroscopeReadingValue(const GyroscopeReading& value)
{
    CNA_GyroscopeReading mapped = {};
    mapped.struct_size = sizeof(CNA_GyroscopeReading);
    mapped.struct_version = StructureVersion;
    mapped.timestamp = MapTimestamp(value.getTimestampProperty());
    mapped.rotation_rate = MapVector(value.getRotationRateProperty());
    return mapped;
}

} // namespace

CNA_Result cna_accelerometer_reading_init(CNA_AccelerometerReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The accelerometer reading output is null.");
        }
        const AccelerometerReading native;
        CNA_AccelerometerReading reading = {};
        reading.struct_size = sizeof(CNA_AccelerometerReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.acceleration = MapVector(native.getAccelerationProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_init_from_values(
    const CNA_DateTimeOffset timestamp,
    const CNA_Vector3 acceleration,
    CNA_AccelerometerReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The accelerometer reading output is null.");
        }
        const AccelerometerReading native(ToTimestamp(timestamp), ToVector(acceleration));
        CNA_AccelerometerReading reading = {};
        reading.struct_size = sizeof(CNA_AccelerometerReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.acceleration = MapVector(native.getAccelerationProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_reading_init(CNA_GyroscopeReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The gyroscope reading output is null.");
        }
        const GyroscopeReading native;
        CNA_GyroscopeReading reading = {};
        reading.struct_size = sizeof(CNA_GyroscopeReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.rotation_rate = MapVector(native.getRotationRateProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_reading_init_from_values(
    const CNA_Vector3 rotationRate,
    const CNA_DateTimeOffset timestamp,
    CNA_GyroscopeReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The gyroscope reading output is null.");
        }
        const GyroscopeReading native(ToVector(rotationRate), ToTimestamp(timestamp));
        CNA_GyroscopeReading reading = {};
        reading.struct_size = sizeof(CNA_GyroscopeReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.rotation_rate = MapVector(native.getRotationRateProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_attitude_reading_init(CNA_AttitudeReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The attitude reading output is null.");
        }
        *outReading = MapAttitudeReading(AttitudeReading());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_attitude_reading_init_from_values(
    const float pitch,
    const float roll,
    const float yaw,
    const CNA_Quaternion quaternion,
    const CNA_Matrix rotationMatrix,
    const CNA_DateTimeOffset timestamp,
    CNA_AttitudeReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The attitude reading output is null.");
        }
        *outReading = MapAttitudeReading(AttitudeReading(
            pitch,
            roll,
            yaw,
            ToQuaternion(quaternion),
            ToMatrix(rotationMatrix),
            ToTimestamp(timestamp)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_reading_init(CNA_CompassReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The compass reading output is null.");
        }
        const CompassReading native;
        CNA_CompassReading reading = {};
        reading.struct_size = sizeof(CNA_CompassReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.heading_accuracy = native.getHeadingAccuracyProperty();
        reading.magnetic_heading = native.getMagneticHeadingProperty();
        reading.true_heading = native.getTrueHeadingProperty();
        reading.magnetometer_reading = MapVector(native.getMagnetometerReadingProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_reading_init_from_values(
    const double headingAccuracy,
    const double magneticHeading,
    const CNA_Vector3 magnetometerReading,
    const CNA_DateTimeOffset timestamp,
    const double trueHeading,
    CNA_CompassReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The compass reading output is null.");
        }
        const CompassReading native(
            headingAccuracy,
            magneticHeading,
            ToVector(magnetometerReading),
            ToTimestamp(timestamp),
            trueHeading);
        CNA_CompassReading reading = {};
        reading.struct_size = sizeof(CNA_CompassReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.heading_accuracy = native.getHeadingAccuracyProperty();
        reading.magnetic_heading = native.getMagneticHeadingProperty();
        reading.true_heading = native.getTrueHeadingProperty();
        reading.magnetometer_reading = MapVector(native.getMagnetometerReadingProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_reading_init(CNA_MotionReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The motion reading output is null.");
        }
        const MotionReading native;
        CNA_MotionReading reading = {};
        reading.struct_size = sizeof(CNA_MotionReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.attitude = MapAttitudeReading(native.getAttitudeProperty());
        reading.device_acceleration = MapVector(native.getDeviceAccelerationProperty());
        reading.device_rotation_rate = MapVector(native.getDeviceRotationRateProperty());
        reading.gravity = MapVector(native.getGravityProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_reading_init_from_values(
    const CNA_AttitudeReading* const attitude,
    const CNA_Vector3 deviceAcceleration,
    const CNA_Vector3 deviceRotationRate,
    const CNA_Vector3 gravity,
    const CNA_DateTimeOffset timestamp,
    CNA_MotionReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The motion reading output is null.");
        }
        AttitudeReading nativeAttitude;
        if (const CNA_Result result = ToAttitudeReading(attitude, &nativeAttitude);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const MotionReading native(
            nativeAttitude,
            ToVector(deviceAcceleration),
            ToVector(deviceRotationRate),
            ToVector(gravity),
            ToTimestamp(timestamp));
        CNA_MotionReading reading = {};
        reading.struct_size = sizeof(CNA_MotionReading);
        reading.struct_version = StructureVersion;
        reading.timestamp = MapTimestamp(native.getTimestampProperty());
        reading.attitude = MapAttitudeReading(native.getAttitudeProperty());
        reading.device_acceleration = MapVector(native.getDeviceAccelerationProperty());
        reading.device_rotation_rate = MapVector(native.getDeviceRotationRateProperty());
        reading.gravity = MapVector(native.getGravityProperty());
        *outReading = reading;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_equals(
    const CNA_AccelerometerReading* const left,
    const CNA_AccelerometerReading* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The reading comparison output is null.");
        }
        AccelerometerReading nativeLeft;
        AccelerometerReading nativeRight;
        if (const CNA_Result result = ToAccelerometerReading(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToAccelerometerReading(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_get_hash_code(const CNA_AccelerometerReading* const reading, uint64_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The reading hash output is null.");
        }
        AccelerometerReading native;
        if (const CNA_Result result = ToAccelerometerReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<uint64_t>(native.GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_get_string_size(const CNA_AccelerometerReading* const reading, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading text byte-count output is null.");
        }
        AccelerometerReading native;
        if (const CNA_Result result = ToAccelerometerReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = native.ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_copy_string(
    const CNA_AccelerometerReading* const reading,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AccelerometerReading native;
        if (const CNA_Result result = ToAccelerometerReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(native.ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_accelerometer_reading_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading type-name byte-count output is null.");
        }
        const AccelerometerReading native;
        *outBytes = native.GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const AccelerometerReading native;
        return CopyText(native.GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gyroscope_reading_equals(
    const CNA_GyroscopeReading* const left,
    const CNA_GyroscopeReading* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The reading comparison output is null.");
        }
        GyroscopeReading nativeLeft;
        GyroscopeReading nativeRight;
        if (const CNA_Result result = ToGyroscopeReading(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToGyroscopeReading(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_reading_get_hash_code(const CNA_GyroscopeReading* const reading, uint64_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The reading hash output is null.");
        }
        GyroscopeReading native;
        if (const CNA_Result result = ToGyroscopeReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<uint64_t>(native.GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_reading_get_string_size(const CNA_GyroscopeReading* const reading, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading text byte-count output is null.");
        }
        GyroscopeReading native;
        if (const CNA_Result result = ToGyroscopeReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = native.ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_reading_copy_string(
    const CNA_GyroscopeReading* const reading,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GyroscopeReading native;
        if (const CNA_Result result = ToGyroscopeReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(native.ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gyroscope_reading_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading type-name byte-count output is null.");
        }
        const GyroscopeReading native;
        *outBytes = native.GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_reading_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const GyroscopeReading native;
        return CopyText(native.GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_attitude_reading_equals(
    const CNA_AttitudeReading* const left,
    const CNA_AttitudeReading* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The reading comparison output is null.");
        }
        AttitudeReading nativeLeft;
        AttitudeReading nativeRight;
        if (const CNA_Result result = ToAttitudeReading(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToAttitudeReading(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_attitude_reading_get_hash_code(const CNA_AttitudeReading* const reading, uint64_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The reading hash output is null.");
        }
        AttitudeReading native;
        if (const CNA_Result result = ToAttitudeReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<uint64_t>(native.GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_attitude_reading_get_string_size(const CNA_AttitudeReading* const reading, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading text byte-count output is null.");
        }
        AttitudeReading native;
        if (const CNA_Result result = ToAttitudeReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = native.ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_attitude_reading_copy_string(
    const CNA_AttitudeReading* const reading,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AttitudeReading native;
        if (const CNA_Result result = ToAttitudeReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(native.ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_attitude_reading_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading type-name byte-count output is null.");
        }
        const AttitudeReading native;
        *outBytes = native.GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_attitude_reading_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const AttitudeReading native;
        return CopyText(native.GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_compass_reading_equals(
    const CNA_CompassReading* const left,
    const CNA_CompassReading* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The reading comparison output is null.");
        }
        CompassReading nativeLeft;
        CompassReading nativeRight;
        if (const CNA_Result result = ToCompassReading(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToCompassReading(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_reading_get_hash_code(const CNA_CompassReading* const reading, uint64_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The reading hash output is null.");
        }
        CompassReading native;
        if (const CNA_Result result = ToCompassReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<uint64_t>(native.GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_reading_get_string_size(const CNA_CompassReading* const reading, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading text byte-count output is null.");
        }
        CompassReading native;
        if (const CNA_Result result = ToCompassReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = native.ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_reading_copy_string(
    const CNA_CompassReading* const reading,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CompassReading native;
        if (const CNA_Result result = ToCompassReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(native.ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_compass_reading_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading type-name byte-count output is null.");
        }
        const CompassReading native;
        *outBytes = native.GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_reading_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const CompassReading native;
        return CopyText(native.GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_motion_reading_equals(
    const CNA_MotionReading* const left,
    const CNA_MotionReading* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The reading comparison output is null.");
        }
        MotionReading nativeLeft;
        MotionReading nativeRight;
        if (const CNA_Result result = ToMotionReading(left, &nativeLeft);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToMotionReading(right, &nativeRight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_reading_get_hash_code(const CNA_MotionReading* const reading, uint64_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The reading hash output is null.");
        }
        MotionReading native;
        if (const CNA_Result result = ToMotionReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<uint64_t>(native.GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_reading_get_string_size(const CNA_MotionReading* const reading, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading text byte-count output is null.");
        }
        MotionReading native;
        if (const CNA_Result result = ToMotionReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = native.ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_reading_copy_string(
    const CNA_MotionReading* const reading,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        MotionReading native;
        if (const CNA_Result result = ToMotionReading(reading, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(native.ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_motion_reading_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The reading type-name byte-count output is null.");
        }
        const MotionReading native;
        *outBytes = native.GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_reading_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const MotionReading native;
        return CopyText(native.GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_sensors_get_last_error_id_ext(
    int32_t* const outErrorId,
    CNA_Bool* const outHasErrorId)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outErrorId == nullptr || outHasErrorId == nullptr) {
            return InvalidInput("The sensor error identifier output is null.");
        }
        const CNA::C::Detail::LastError& lastError = CNA::C::Detail::GetLastError();
        *outHasErrorId = lastError.hasSensorErrorId ? CNA_TRUE : CNA_FALSE;
        *outErrorId = lastError.hasSensorErrorId ? lastError.sensorErrorId : 0;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sensor_unsubscribe_ext(const CNA_SensorEventRegistrationHandle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorRegistrationBase> resource;
        const CNA_Result getResult = CNA::C::Detail::GetRuntimeHandles().Get(
            registration,
            ObjectKind::SensorEventRegistration,
            &resource);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The sensor registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult =
            CNA::C::Detail::GetRuntimeHandles().Release(registration);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The sensor registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_is_supported(const CNA_Handle gameHandle, CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The sensor support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = Accelerometer::getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_create(const CNA_Handle gameHandle, CNA_AccelerometerHandle* const outSensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSensor == nullptr) {
            return InvalidInput("The sensor output is null.");
        }
        *outSensor = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SensorResource<Accelerometer>>();
        resource->value = std::make_unique<Accelerometer>();
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::Accelerometer,
            std::move(resource),
            outSensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_state(const CNA_AccelerometerHandle sensor, CNA_SensorState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The sensor state output is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = static_cast<CNA_SensorState>(resource->value->getStateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_start(const CNA_AccelerometerHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Start();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_stop(const CNA_AccelerometerHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Stop();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_dispose(const CNA_AccelerometerHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_current_value(
    const CNA_AccelerometerHandle sensor,
    CNA_AccelerometerReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The sensor reading output is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outReading = MapAccelerometerReadingValue(resource->value->getCurrentValueProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_is_data_valid(const CNA_AccelerometerHandle sensor, CNA_Bool* const outValid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValid == nullptr) {
            return InvalidInput("The sensor data-validity output is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValid = resource->value->getIsDataValidProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_time_between_updates_ticks(
    const CNA_AccelerometerHandle sensor,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The sensor interval output is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(
            resource->value->getTimeBetweenUpdatesProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_set_time_between_updates_ticks(
    const CNA_AccelerometerHandle sensor,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setTimeBetweenUpdatesProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_subscribe_current_value_changed(
    const CNA_AccelerometerHandle sensor,
    const CNA_AccelerometerReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The sensor registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The sensor reading callback is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &resource->value->CurrentValueChanged;
        const auto token = source->Add(
            [callback, context](System::Object*, const SensorReadingEventArgs<AccelerometerReading>& args) {
                const CNA_AccelerometerReading reading = MapAccelerometerReadingValue(args.getSensorReadingProperty());
                callback(&reading, context);
            });
        return PublishRegistration(
            std::make_shared<SensorRegistration<SensorReadingEventArgs<AccelerometerReading>>>(
                resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_accelerometer_inject_synthetic_update_ext(
    const CNA_AccelerometerHandle sensor,
    const float x,
    const float y,
    const float z)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->InjectSyntheticSensorUpdate(x, y, z);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_set_started_for_tests_ext(
    const CNA_AccelerometerHandle sensor,
    const CNA_Bool started)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetStartedForTesting(started != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_set_supported_for_tests_ext(
    const CNA_AccelerometerHandle sensor,
    const CNA_Bool supported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetSupportedForTesting(supported != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_subsystem_held_for_tests_ext(
    const CNA_AccelerometerHandle sensor,
    CNA_Bool* const outHeld)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHeld == nullptr) {
            return InvalidInput("The subsystem-hold output is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHeld = resource->value->GetSubsystemHeldForTesting() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_register_started_instance_for_tests_ext(const CNA_AccelerometerHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Accelerometer::RegisterStartedInstanceForTesting(*resource->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_unregister_started_instance_for_tests_ext(const CNA_AccelerometerHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Accelerometer::UnregisterStartedInstanceForTesting(*resource->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_dispatch_to_instances_for_tests_ext(
    const CNA_Handle gameHandle,
    const CNA_AccelerometerHandle* const sensors,
    const uint64_t count,
    const float x,
    const float y,
    const float z)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (sensors == nullptr && count != UINT64_C(0)) {
            return InvalidInput("The sensor array is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Accelerometer*> instances;
        instances.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = UINT64_C(0); index < count; ++index) {
            std::shared_ptr<SensorResource<Accelerometer>> resource;
            if (const CNA_Result result = BorrowSensor<Accelerometer>(sensors[index], &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            instances.push_back(resource->value.get());
        }
        Accelerometer::DispatchToInstancesForTesting(instances, x, y, z);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_set_event_watch_registration_failure_for_tests_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool shouldFail)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Accelerometer::SetEventWatchRegistrationFailureForTesting(shouldFail != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_dispatch_exception_count_for_tests_ext(
    const CNA_Handle gameHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The dispatch exception count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(Accelerometer::GetDispatchExceptionCountForTesting());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_last_dispatch_exception_message_size_for_tests_ext(
    const CNA_Handle gameHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The dispatch exception message byte-count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = Accelerometer::GetLastDispatchExceptionMessageForTesting().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_copy_last_dispatch_exception_message_for_tests_ext(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            Accelerometer::GetLastDispatchExceptionMessageForTesting(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_accelerometer_is_sensor_connected_for_tests_ext(
    const CNA_Handle gameHandle,
    const int64_t sensorId,
    CNA_Bool* const outConnected)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outConnected == nullptr) {
            return InvalidInput("The sensor connection output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outConnected = Accelerometer::IsSensorConnectedForTesting(static_cast<std::int64_t>(sensorId))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_set_disposal_cleanup_hook_for_tests_ext(
    const CNA_AccelerometerHandle sensor,
    const CNA_SensorEventCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback == nullptr) {
            resource->value->SetDisposalCleanupHookForTesting(nullptr);
            return CNA_RESULT_SUCCESS;
        }
        resource->value->SetDisposalCleanupHookForTesting(
            [callback, context]() { callback(context); });
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_get_type_name_size(const CNA_AccelerometerHandle sensor, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The sensor type-name byte-count output is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_copy_type_name(
    const CNA_AccelerometerHandle sensor,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_accelerometer_destroy(const CNA_AccelerometerHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(sensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_is_supported(const CNA_Handle gameHandle, CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The sensor support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = Gyroscope::getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_create(const CNA_Handle gameHandle, CNA_GyroscopeHandle* const outSensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSensor == nullptr) {
            return InvalidInput("The sensor output is null.");
        }
        *outSensor = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SensorResource<Gyroscope>>();
        resource->value = std::make_unique<Gyroscope>();
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::Gyroscope,
            std::move(resource),
            outSensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_state(const CNA_GyroscopeHandle sensor, CNA_SensorState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The sensor state output is null.");
        }
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = static_cast<CNA_SensorState>(resource->value->getStateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_start(const CNA_GyroscopeHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Start();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_stop(const CNA_GyroscopeHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Stop();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_dispose(const CNA_GyroscopeHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_current_value(
    const CNA_GyroscopeHandle sensor,
    CNA_GyroscopeReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The sensor reading output is null.");
        }
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outReading = MapGyroscopeReadingValue(resource->value->getCurrentValueProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_is_data_valid(const CNA_GyroscopeHandle sensor, CNA_Bool* const outValid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValid == nullptr) {
            return InvalidInput("The sensor data-validity output is null.");
        }
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValid = resource->value->getIsDataValidProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_time_between_updates_ticks(
    const CNA_GyroscopeHandle sensor,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The sensor interval output is null.");
        }
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(
            resource->value->getTimeBetweenUpdatesProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_set_time_between_updates_ticks(
    const CNA_GyroscopeHandle sensor,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setTimeBetweenUpdatesProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_subscribe_current_value_changed(
    const CNA_GyroscopeHandle sensor,
    const CNA_GyroscopeReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The sensor registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The sensor reading callback is null.");
        }
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &resource->value->CurrentValueChanged;
        const auto token = source->Add(
            [callback, context](System::Object*, const SensorReadingEventArgs<GyroscopeReading>& args) {
                const CNA_GyroscopeReading reading = MapGyroscopeReadingValue(args.getSensorReadingProperty());
                callback(&reading, context);
            });
        return PublishRegistration(
            std::make_shared<SensorRegistration<SensorReadingEventArgs<GyroscopeReading>>>(
                resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_gyroscope_inject_synthetic_update_ext(
    const CNA_GyroscopeHandle sensor,
    const float x,
    const float y,
    const float z)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->InjectSyntheticSensorUpdate(x, y, z);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_set_started_for_tests_ext(
    const CNA_GyroscopeHandle sensor,
    const CNA_Bool started)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetStartedForTesting(started != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_set_supported_for_tests_ext(
    const CNA_GyroscopeHandle sensor,
    const CNA_Bool supported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->SetSupportedForTesting(supported != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_subsystem_held_for_tests_ext(
    const CNA_GyroscopeHandle sensor,
    CNA_Bool* const outHeld)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHeld == nullptr) {
            return InvalidInput("The subsystem-hold output is null.");
        }
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHeld = resource->value->GetSubsystemHeldForTesting() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_register_started_instance_for_tests_ext(const CNA_GyroscopeHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gyroscope::RegisterStartedInstanceForTesting(*resource->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_unregister_started_instance_for_tests_ext(const CNA_GyroscopeHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gyroscope::UnregisterStartedInstanceForTesting(*resource->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_dispatch_to_instances_for_tests_ext(
    const CNA_Handle gameHandle,
    const CNA_GyroscopeHandle* const sensors,
    const uint64_t count,
    const float x,
    const float y,
    const float z)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (sensors == nullptr && count != UINT64_C(0)) {
            return InvalidInput("The sensor array is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Gyroscope*> instances;
        instances.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = UINT64_C(0); index < count; ++index) {
            std::shared_ptr<SensorResource<Gyroscope>> resource;
            if (const CNA_Result result = BorrowSensor<Gyroscope>(sensors[index], &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            instances.push_back(resource->value.get());
        }
        Gyroscope::DispatchToInstancesForTesting(instances, x, y, z);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_set_event_watch_registration_failure_for_tests_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool shouldFail)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gyroscope::SetEventWatchRegistrationFailureForTesting(shouldFail != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_dispatch_exception_count_for_tests_ext(
    const CNA_Handle gameHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The dispatch exception count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(Gyroscope::GetDispatchExceptionCountForTesting());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_last_dispatch_exception_message_size_for_tests_ext(
    const CNA_Handle gameHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The dispatch exception message byte-count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = Gyroscope::GetLastDispatchExceptionMessageForTesting().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_copy_last_dispatch_exception_message_for_tests_ext(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
            Gyroscope::GetLastDispatchExceptionMessageForTesting(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_gyroscope_is_sensor_connected_for_tests_ext(
    const CNA_Handle gameHandle,
    const int64_t sensorId,
    CNA_Bool* const outConnected)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outConnected == nullptr) {
            return InvalidInput("The sensor connection output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outConnected = Gyroscope::IsSensorConnectedForTesting(static_cast<std::int64_t>(sensorId))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_set_disposal_cleanup_hook_for_tests_ext(
    const CNA_GyroscopeHandle sensor,
    const CNA_SensorEventCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback == nullptr) {
            resource->value->SetDisposalCleanupHookForTesting(nullptr);
            return CNA_RESULT_SUCCESS;
        }
        resource->value->SetDisposalCleanupHookForTesting(
            [callback, context]() { callback(context); });
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_get_type_name_size(const CNA_GyroscopeHandle sensor, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The sensor type-name byte-count output is null.");
        }
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gyroscope_copy_type_name(
    const CNA_GyroscopeHandle sensor,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gyroscope_destroy(const CNA_GyroscopeHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Gyroscope>> resource;
        if (const CNA_Result result = BorrowSensor<Gyroscope>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(sensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

using Microsoft::Devices::Sensors::AccelerometerReadingEventArgs;
using Microsoft::Devices::Sensors::CalibrationEventArgs;
using Microsoft::Devices::Sensors::Compass;
using Microsoft::Devices::Sensors::Motion;

template<>
struct SensorKind<Compass> {
    static constexpr ObjectKind Kind = ObjectKind::Compass;
};

template<>
struct SensorKind<Motion> {
    static constexpr ObjectKind Kind = ObjectKind::Motion;
};

[[nodiscard]] CNA_CompassReading MapCompassReadingValue(const CompassReading& value)
{
    CNA_CompassReading mapped = {};
    mapped.struct_size = sizeof(CNA_CompassReading);
    mapped.struct_version = StructureVersion;
    mapped.timestamp = MapTimestamp(value.getTimestampProperty());
    mapped.heading_accuracy = value.getHeadingAccuracyProperty();
    mapped.magnetic_heading = value.getMagneticHeadingProperty();
    mapped.true_heading = value.getTrueHeadingProperty();
    mapped.magnetometer_reading = MapVector(value.getMagnetometerReadingProperty());
    return mapped;
}

[[nodiscard]] CNA_MotionReading MapMotionReadingValue(const MotionReading& value)
{
    CNA_MotionReading mapped = {};
    mapped.struct_size = sizeof(CNA_MotionReading);
    mapped.struct_version = StructureVersion;
    mapped.timestamp = MapTimestamp(value.getTimestampProperty());
    mapped.attitude = MapAttitudeReading(value.getAttitudeProperty());
    mapped.device_acceleration = MapVector(value.getDeviceAccelerationProperty());
    mapped.device_rotation_rate = MapVector(value.getDeviceRotationRateProperty());
    mapped.gravity = MapVector(value.getGravityProperty());
    return mapped;
}

[[nodiscard]] CNA_Result ToAccelerometerReadingEventArgs(
    const CNA_AccelerometerReadingEventInfo* const info,
    AccelerometerReadingEventArgs* const outArgs)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(info, "The accelerometer reading description is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outArgs = AccelerometerReadingEventArgs(
        info->x,
        info->y,
        info->z,
        ToTimestamp(info->timestamp));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_AccelerometerReadingEventInfo MapAccelerometerReadingEventArgs(
    const AccelerometerReadingEventArgs& value)
{
    CNA_AccelerometerReadingEventInfo mapped = {};
    mapped.struct_size = sizeof(CNA_AccelerometerReadingEventInfo);
    mapped.struct_version = StructureVersion;
    mapped.timestamp = MapTimestamp(value.getTimestampProperty());
    mapped.x = value.getXProperty();
    mapped.y = value.getYProperty();
    mapped.z = value.getZProperty();
    return mapped;
}

// The canonical test hook takes a caller-implemented backend object; C cannot write one, so this
// ABI supplies the implementation and exposes only the switch and the two injection routes. The
// state lives in its own block so the injection routes reach it without ever touching the backend
// object the canonical sensor owns and may destroy.
template<typename TReading>
struct TestBackendState final {
    std::mutex mutex;
    bool supported = false;
    bool northReferenced = true;
    bool started = false;
    std::function<void(const TReading&)> onReading;
    std::function<void()> onCalibration;
};

using TestCompassState = TestBackendState<CompassReading>;
using TestMotionState = TestBackendState<MotionReading>;

template<typename TState, typename TInterface, typename TReading>
class TestSensorBackend : public TInterface {
public:
    explicit TestSensorBackend(std::shared_ptr<TState> state)
        : state_(std::move(state))
    {
    }

    [[nodiscard]] bool IsSupported() override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->supported;
    }

    bool Start(
        const System::TimeSpan&,
        typename TInterface::ReadingCallback onReading,
        typename TInterface::CalibrationCallback onCalibrationNeeded) override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->supported) {
            return false;
        }
        state_->onReading = std::move(onReading);
        state_->onCalibration = std::move(onCalibrationNeeded);
        state_->started = true;
        return true;
    }

    void Stop() override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        state_->started = false;
        state_->onReading = nullptr;
        state_->onCalibration = nullptr;
    }

    void SetSampleInterval(const System::TimeSpan&) override
    {
    }

protected:
    std::shared_ptr<TState> state_;
};

class TestCompassBackend final
    : public TestSensorBackend<
          TestCompassState,
          Microsoft::Devices::Sensors::Detail::ICompassBackend,
          CompassReading> {
public:
    using TestSensorBackend::TestSensorBackend;
};

class TestMotionBackend final
    : public TestSensorBackend<
          TestMotionState,
          Microsoft::Devices::Sensors::Detail::IMotionBackend,
          MotionReading> {
public:
    using TestSensorBackend::TestSensorBackend;

    [[nodiscard]] bool IsUsingNorthReferencedAttitudeSource() override
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->northReferenced;
    }
};

[[nodiscard]] CNA_Result NoTestBackend()
{
    return Fail(
        CNA_RESULT_INVALID_STATE,
        CNA_ERROR_CATEGORY_STATE,
        "No test backend is installed and started for this sensor.");
}

// Copies the handler out under the state lock and calls it after releasing it: the handler enters
// the canonical sensor, which takes its own lock, and a consumer's own handler may call straight
// back into this ABI.
template<typename TState, typename TReading>
[[nodiscard]] CNA_Result DeliverTestReading(const TState& state, const TReading& reading)
{
    std::function<void(const TReading&)> handler;
    {
        const std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->started) {
            return NoTestBackend();
        }
        handler = state->onReading;
    }
    if (!handler) {
        return NoTestBackend();
    }
    handler(reading);
    return CNA_RESULT_SUCCESS;
}

template<typename TState>
[[nodiscard]] CNA_Result DeliverTestCalibration(const TState& state)
{
    std::function<void()> handler;
    {
        const std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->started) {
            return NoTestBackend();
        }
        handler = state->onCalibration;
    }
    if (!handler) {
        return NoTestBackend();
    }
    handler();
    return CNA_RESULT_SUCCESS;
}

template<typename TSensor>
[[nodiscard]] CNA_Result SubscribeCalibrate(
    const CNA_Handle sensor,
    const CNA_SensorEventCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    if (outRegistration == nullptr) {
        return InvalidInput("The sensor registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidInput("The sensor calibration callback is null.");
    }
    std::shared_ptr<SensorResource<TSensor>> resource;
    if (const CNA_Result result = BorrowSensor<TSensor>(sensor, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const source = &resource->value->Calibrate;
    const auto token = source->Add(
        [callback, context](System::Object*, const CalibrationEventArgs&) { callback(context); });
    return PublishRegistration(
        std::make_shared<SensorRegistration<CalibrationEventArgs>>(resource, source, token),
        outRegistration);
}

} // namespace

CNA_Result cna_accelerometer_reading_event_info_init(
    CNA_AccelerometerReadingEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The accelerometer reading description output is null.");
        }
        *outInfo = MapAccelerometerReadingEventArgs(AccelerometerReadingEventArgs());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_event_info_init_from_values(
    const double x,
    const double y,
    const double z,
    const CNA_DateTimeOffset timestamp,
    CNA_AccelerometerReadingEventInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr) {
            return InvalidInput("The accelerometer reading description output is null.");
        }
        *outInfo = MapAccelerometerReadingEventArgs(
            AccelerometerReadingEventArgs(x, y, z, ToTimestamp(timestamp)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_event_info_equals(
    const CNA_AccelerometerReadingEventInfo* const left,
    const CNA_AccelerometerReadingEventInfo* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidInput("The equality output is null.");
        }
        AccelerometerReadingEventArgs first;
        if (const CNA_Result result = ToAccelerometerReadingEventArgs(left, &first);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        AccelerometerReadingEventArgs second;
        if (const CNA_Result result = ToAccelerometerReadingEventArgs(right, &second);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEqual = (first == second) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_event_info_get_hash_code(
    const CNA_AccelerometerReadingEventInfo* const info,
    uint64_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The hash output is null.");
        }
        AccelerometerReadingEventArgs args;
        if (const CNA_Result result = ToAccelerometerReadingEventArgs(info, &args);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<uint64_t>(args.GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_event_info_get_string_size(
    const CNA_AccelerometerReadingEventInfo* const info,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The text size output is null.");
        }
        AccelerometerReadingEventArgs args;
        if (const CNA_Result result = ToAccelerometerReadingEventArgs(info, &args);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = args.ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_event_info_copy_string(
    const CNA_AccelerometerReadingEventInfo* const info,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AccelerometerReadingEventArgs args;
        if (const CNA_Result result = ToAccelerometerReadingEventArgs(info, &args);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(args.ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_accelerometer_reading_event_info_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        *outBytes = AccelerometerReadingEventArgs().GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_accelerometer_reading_event_info_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyText(
            AccelerometerReadingEventArgs().GetTypeName(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_calibration_event_info_get_type_name_size(uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        *outBytes = CalibrationEventArgs().GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_calibration_event_info_copy_type_name(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyText(CalibrationEventArgs().GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_accelerometer_subscribe_reading_changed(
    const CNA_AccelerometerHandle sensor,
    const CNA_AccelerometerReadingEventCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The sensor registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The sensor reading callback is null.");
        }
        std::shared_ptr<SensorResource<Accelerometer>> resource;
        if (const CNA_Result result = BorrowSensor<Accelerometer>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &resource->value->ReadingChanged;
        const auto token = source->Add(
            [callback, context](System::Object*, const AccelerometerReadingEventArgs& args) {
                const CNA_AccelerometerReadingEventInfo info = MapAccelerometerReadingEventArgs(args);
                callback(&info, context);
            });
        return PublishRegistration(
            std::make_shared<SensorRegistration<AccelerometerReadingEventArgs>>(
                resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_compass_get_is_supported(const CNA_Handle gameHandle, CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The sensor support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = Compass::getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_create(const CNA_Handle gameHandle, CNA_CompassHandle* const outSensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSensor == nullptr) {
            return InvalidInput("The sensor output is null.");
        }
        *outSensor = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SensorResource<Compass>>();
        resource->value = std::make_unique<Compass>();
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::Compass,
            std::move(resource),
            outSensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_get_state(const CNA_CompassHandle sensor, CNA_SensorState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The sensor state output is null.");
        }
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = static_cast<CNA_SensorState>(resource->value->getStateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_start(const CNA_CompassHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Start();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_stop(const CNA_CompassHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Stop();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_dispose(const CNA_CompassHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_get_current_value(
    const CNA_CompassHandle sensor,
    CNA_CompassReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The sensor reading output is null.");
        }
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outReading = MapCompassReadingValue(resource->value->getCurrentValueProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_get_is_data_valid(const CNA_CompassHandle sensor, CNA_Bool* const outValid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValid == nullptr) {
            return InvalidInput("The sensor data-validity output is null.");
        }
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValid = resource->value->getIsDataValidProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_get_time_between_updates_ticks(
    const CNA_CompassHandle sensor,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The sensor interval output is null.");
        }
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(
            resource->value->getTimeBetweenUpdatesProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_set_time_between_updates_ticks(
    const CNA_CompassHandle sensor,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setTimeBetweenUpdatesProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_subscribe_current_value_changed(
    const CNA_CompassHandle sensor,
    const CNA_CompassReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The sensor registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The sensor reading callback is null.");
        }
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &resource->value->CurrentValueChanged;
        const auto token = source->Add(
            [callback, context](System::Object*, const SensorReadingEventArgs<CompassReading>& args) {
                const CNA_CompassReading reading = MapCompassReadingValue(args.getSensorReadingProperty());
                callback(&reading, context);
            });
        return PublishRegistration(
            std::make_shared<SensorRegistration<SensorReadingEventArgs<CompassReading>>>(
                resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_compass_subscribe_calibrate(
    const CNA_CompassHandle sensor,
    const CNA_SensorEventCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeCalibrate<Compass>(sensor, callback, context, outRegistration);
    });
}

CNA_Result cna_compass_set_test_backend_ext(
    const CNA_CompassHandle sensor,
    const CNA_Bool installed,
    const CNA_Bool supported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (installed == CNA_FALSE) {
            resource->value->SetBackendForTesting(nullptr);
            resource->testBackend.reset();
            return CNA_RESULT_SUCCESS;
        }
        auto state = std::make_shared<TestCompassState>();
        state->supported = (supported != CNA_FALSE);
        resource->value->SetBackendForTesting(std::make_unique<TestCompassBackend>(state));
        resource->testBackend = std::move(state);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_inject_synthetic_update_ext(
    const CNA_CompassHandle sensor,
    const CNA_CompassReading* const reading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CompassReading value;
        if (const CNA_Result result = ToCompassReading(reading, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto state = std::static_pointer_cast<TestCompassState>(resource->testBackend);
        if (!state) {
            return NoTestBackend();
        }
        return DeliverTestReading(state, value);
    });
}

CNA_Result cna_compass_inject_calibration_request_ext(const CNA_CompassHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto state = std::static_pointer_cast<TestCompassState>(resource->testBackend);
        if (!state) {
            return NoTestBackend();
        }
        return DeliverTestCalibration(state);
    });
}

CNA_Result cna_compass_get_type_name_size(const CNA_CompassHandle sensor, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compass_copy_type_name(
    const CNA_CompassHandle sensor,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_compass_destroy(const CNA_CompassHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Compass>> resource;
        if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(sensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_get_is_supported(const CNA_Handle gameHandle, CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return InvalidInput("The sensor support output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = Motion::getIsSupportedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_create(const CNA_Handle gameHandle, CNA_MotionHandle* const outSensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSensor == nullptr) {
            return InvalidInput("The sensor output is null.");
        }
        *outSensor = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto resource = std::make_shared<SensorResource<Motion>>();
        resource->value = std::make_unique<Motion>();
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            ObjectKind::Motion,
            std::move(resource),
            outSensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_get_state(const CNA_MotionHandle sensor, CNA_SensorState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The sensor state output is null.");
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = static_cast<CNA_SensorState>(resource->value->getStateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_get_is_attitude_north_referenced_ext(
    const CNA_MotionHandle sensor,
    CNA_Bool* const outNorthReferenced)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outNorthReferenced == nullptr) {
            return InvalidInput("The attitude-source output is null.");
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outNorthReferenced =
            resource->value->getIsAttitudeNorthReferencedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_start(const CNA_MotionHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Start();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_stop(const CNA_MotionHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Stop();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_dispose(const CNA_MotionHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_get_current_value(
    const CNA_MotionHandle sensor,
    CNA_MotionReading* const outReading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReading == nullptr) {
            return InvalidInput("The sensor reading output is null.");
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outReading = MapMotionReadingValue(resource->value->getCurrentValueProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_get_is_data_valid(const CNA_MotionHandle sensor, CNA_Bool* const outValid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValid == nullptr) {
            return InvalidInput("The sensor data-validity output is null.");
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValid = resource->value->getIsDataValidProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_get_time_between_updates_ticks(
    const CNA_MotionHandle sensor,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidInput("The sensor interval output is null.");
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(
            resource->value->getTimeBetweenUpdatesProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_set_time_between_updates_ticks(
    const CNA_MotionHandle sensor,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource->value->setTimeBetweenUpdatesProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_subscribe_current_value_changed(
    const CNA_MotionHandle sensor,
    const CNA_MotionReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The sensor registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The sensor reading callback is null.");
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &resource->value->CurrentValueChanged;
        const auto token = source->Add(
            [callback, context](System::Object*, const SensorReadingEventArgs<MotionReading>& args) {
                const CNA_MotionReading reading = MapMotionReadingValue(args.getSensorReadingProperty());
                callback(&reading, context);
            });
        return PublishRegistration(
            std::make_shared<SensorRegistration<SensorReadingEventArgs<MotionReading>>>(
                resource, source, token),
            outRegistration);
    });
}

CNA_Result cna_motion_subscribe_calibrate(
    const CNA_MotionHandle sensor,
    const CNA_SensorEventCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return SubscribeCalibrate<Motion>(sensor, callback, context, outRegistration);
    });
}

CNA_Result cna_motion_set_test_backend_ext(
    const CNA_MotionHandle sensor,
    const CNA_Bool installed,
    const CNA_Bool supported,
    const CNA_Bool northReferenced)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (installed == CNA_FALSE) {
            resource->value->SetBackendForTesting(nullptr);
            resource->testBackend.reset();
            return CNA_RESULT_SUCCESS;
        }
        auto state = std::make_shared<TestMotionState>();
        state->supported = (supported != CNA_FALSE);
        state->northReferenced = (northReferenced != CNA_FALSE);
        resource->value->SetBackendForTesting(std::make_unique<TestMotionBackend>(state));
        resource->testBackend = std::move(state);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_inject_synthetic_update_ext(
    const CNA_MotionHandle sensor,
    const CNA_MotionReading* const reading)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        MotionReading value;
        if (const CNA_Result result = ToMotionReading(reading, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto state = std::static_pointer_cast<TestMotionState>(resource->testBackend);
        if (!state) {
            return NoTestBackend();
        }
        return DeliverTestReading(state, value);
    });
}

CNA_Result cna_motion_inject_calibration_request_ext(const CNA_MotionHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto state = std::static_pointer_cast<TestMotionState>(resource->testBackend);
        if (!state) {
            return NoTestBackend();
        }
        return DeliverTestCalibration(state);
    });
}

CNA_Result cna_motion_get_type_name_size(const CNA_MotionHandle sensor, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = resource->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_motion_copy_type_name(
    const CNA_MotionHandle sensor,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(resource->value->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_motion_destroy(const CNA_MotionHandle sensor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SensorResource<Motion>> resource;
        if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Release(sensor);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The sensor handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}
