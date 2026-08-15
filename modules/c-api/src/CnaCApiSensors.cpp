// SPDX-License-Identifier: MS-PL

#include "CNA/C/sensors.h"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/TimeSpan.hpp"

#include <cstring>
#include <string>

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;

namespace {

using Microsoft::Devices::Sensors::AccelerometerReading;
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
