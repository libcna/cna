// SPDX-License-Identifier: MS-PL

#include "CNA/C/sensors.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

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
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Detail::ValidateCanonicalBool;

namespace {

using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateVersionedStructure;

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
        ToNative(reading->acceleration));
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
        ToNative(reading->rotation_rate),
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
        ToNative(reading->magnetometer_reading),
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
        ToNative(reading->device_acceleration),
        ToNative(reading->device_rotation_rate),
        ToNative(reading->gravity),
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

    // Set by this ABI's dispose route. The canonical class keeps its own disposal flag protected,
    // and the accelerometer and gyroscope document that every later route refuses the handle.
    bool disposed = false;
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
    return CreateHandle(ObjectKind::SensorEventRegistration,
        std::move(registration),
        outRegistration);
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
    return LookupHandle(handle, SensorKind<TSensor>::Kind, outSensor);
}

// For the accelerometer and gyroscope, whose dispose routes promise that every other route then
// fails with INVALID_STATE; the canonical getters themselves keep answering (CNA-DEMOS-ISSUE-062).
// Destroy and the test hooks still reach a disposed sensor.
template<typename TSensor>
[[nodiscard]] CNA_Result RequireLiveSensor(const SensorResource<TSensor>& sensor)
{
    return sensor.disposed ? InvalidState("The sensor has been disposed.") : CNA_RESULT_SUCCESS;
}

template<typename TSensor>
[[nodiscard]] CNA_Result BorrowLiveSensor(
    const CNA_Handle handle,
    std::shared_ptr<SensorResource<TSensor>>* const outSensor)
{
    if (const CNA_Result result = BorrowSensor<TSensor>(handle, outSensor);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = RequireLiveSensor(**outSensor); result != CNA_RESULT_SUCCESS) {
        outSensor->reset();
        return result;
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TSensor>
void DisposeSensor(SensorResource<TSensor>& sensor)
{
    sensor.value->Dispose();
    sensor.disposed = true;
}

template<typename TSensor>
[[nodiscard]] CNA_Result CreateSensor(CNA_Handle* const outSensor)
{
    auto resource = std::make_shared<SensorResource<TSensor>>();
    resource->value = std::make_unique<TSensor>();
    return CreateHandle(SensorKind<TSensor>::Kind, std::move(resource), outSensor);
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

[[nodiscard]] CNA_Result AccelerometerReadingInit(CNA_AccelerometerReading* const outReading)
{
    const AccelerometerReading native;
    CNA_AccelerometerReading reading = {};
    reading.struct_size = sizeof(CNA_AccelerometerReading);
    reading.struct_version = StructureVersion;
    reading.timestamp = MapTimestamp(native.getTimestampProperty());
    reading.acceleration = MapVector(native.getAccelerationProperty());
    *outReading = reading;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AccelerometerReadingInitFromValues(
    const CNA_DateTimeOffset timestamp,
    const CNA_Vector3 acceleration,
    CNA_AccelerometerReading* const outReading)
{
    const AccelerometerReading native(ToTimestamp(timestamp), ToNative(acceleration));
    CNA_AccelerometerReading reading = {};
    reading.struct_size = sizeof(CNA_AccelerometerReading);
    reading.struct_version = StructureVersion;
    reading.timestamp = MapTimestamp(native.getTimestampProperty());
    reading.acceleration = MapVector(native.getAccelerationProperty());
    *outReading = reading;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GyroscopeReadingInit(CNA_GyroscopeReading* const outReading)
{
    const GyroscopeReading native;
    CNA_GyroscopeReading reading = {};
    reading.struct_size = sizeof(CNA_GyroscopeReading);
    reading.struct_version = StructureVersion;
    reading.timestamp = MapTimestamp(native.getTimestampProperty());
    reading.rotation_rate = MapVector(native.getRotationRateProperty());
    *outReading = reading;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GyroscopeReadingInitFromValues(
    const CNA_Vector3 rotationRate,
    const CNA_DateTimeOffset timestamp,
    CNA_GyroscopeReading* const outReading)
{
    const GyroscopeReading native(ToNative(rotationRate), ToTimestamp(timestamp));
    CNA_GyroscopeReading reading = {};
    reading.struct_size = sizeof(CNA_GyroscopeReading);
    reading.struct_version = StructureVersion;
    reading.timestamp = MapTimestamp(native.getTimestampProperty());
    reading.rotation_rate = MapVector(native.getRotationRateProperty());
    *outReading = reading;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CompassReadingInit(CNA_CompassReading* const outReading)
{
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
}

[[nodiscard]] CNA_Result CompassReadingInitFromValues(
    const double headingAccuracy,
    const double magneticHeading,
    const CNA_Vector3 magnetometerReading,
    const CNA_DateTimeOffset timestamp,
    const double trueHeading,
    CNA_CompassReading* const outReading)
{
    const CompassReading native(
        headingAccuracy,
        magneticHeading,
        ToNative(magnetometerReading),
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
}

[[nodiscard]] CNA_Result MotionReadingInit(CNA_MotionReading* const outReading)
{
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
}

[[nodiscard]] CNA_Result MotionReadingInitFromValues(
    const CNA_AttitudeReading* const attitude,
    const CNA_Vector3 deviceAcceleration,
    const CNA_Vector3 deviceRotationRate,
    const CNA_Vector3 gravity,
    const CNA_DateTimeOffset timestamp,
    CNA_MotionReading* const outReading)
{
    AttitudeReading nativeAttitude;
    if (const CNA_Result result = ToAttitudeReading(attitude, &nativeAttitude);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const MotionReading native(
        nativeAttitude,
        ToNative(deviceAcceleration),
        ToNative(deviceRotationRate),
        ToNative(gravity),
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
}

[[nodiscard]] CNA_Result AccelerometerReadingEquals(
    const CNA_AccelerometerReading* const left,
    const CNA_AccelerometerReading* const right,
    CNA_Bool* const outEqual)
{
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
    *outEqual = ToCBool(nativeLeft == nativeRight);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GyroscopeReadingEquals(
    const CNA_GyroscopeReading* const left,
    const CNA_GyroscopeReading* const right,
    CNA_Bool* const outEqual)
{
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
    *outEqual = ToCBool(nativeLeft == nativeRight);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AttitudeReadingEquals(
    const CNA_AttitudeReading* const left,
    const CNA_AttitudeReading* const right,
    CNA_Bool* const outEqual)
{
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
    *outEqual = ToCBool(nativeLeft == nativeRight);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CompassReadingEquals(
    const CNA_CompassReading* const left,
    const CNA_CompassReading* const right,
    CNA_Bool* const outEqual)
{
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
    *outEqual = ToCBool(nativeLeft == nativeRight);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MotionReadingEquals(
    const CNA_MotionReading* const left,
    const CNA_MotionReading* const right,
    CNA_Bool* const outEqual)
{
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
    *outEqual = ToCBool(nativeLeft == nativeRight);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SensorsGetLastErrorIdExt(
    int32_t* const outErrorId,
    CNA_Bool* const outHasErrorId)
{
    const CNA::C::Detail::LastError& lastError = CNA::C::Detail::GetLastError();
    *outHasErrorId = ToCBool(lastError.hasSensorErrorId);
    *outErrorId = lastError.hasSensorErrorId ? lastError.sensorErrorId : 0;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SensorUnsubscribeExt(const CNA_SensorEventRegistrationHandle registration)
{
    std::shared_ptr<SensorRegistrationBase> resource;
    const CNA_Result getResult =
            LookupHandle(registration, ObjectKind::SensorEventRegistration, &resource);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    return ReleaseHandle(registration);
}

[[nodiscard]] CNA_Result AccelerometerSubscribeCurrentValueChanged(
    const CNA_AccelerometerHandle sensor,
    const CNA_AccelerometerReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The sensor reading callback is null.");
    }
    std::shared_ptr<SensorResource<Accelerometer>> resource;
    if (const CNA_Result result = BorrowLiveSensor<Accelerometer>(sensor, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const source = &resource->value->CurrentValueChanged;
    const auto token = source->Add(
        [callback, context](
            System::Object*, const SensorReadingEventArgs<AccelerometerReading>& args) {
            const CNA_AccelerometerReading reading =
                MapAccelerometerReadingValue(args.getSensorReadingProperty());
            callback(&reading, context);
        });
    return PublishRegistration(
        std::make_shared<SensorRegistration<SensorReadingEventArgs<AccelerometerReading>>>(
            resource, source, token),
        outRegistration);
}

[[nodiscard]] CNA_Result AccelerometerDispatchToInstancesForTestsExt(
    const CNA_Handle gameHandle,
    const CNA_AccelerometerHandle* const sensors,
    const uint64_t count,
    const float x,
    const float y,
    const float z)
{
    if (sensors == nullptr && count != UINT64_C(0)) {
        return InvalidArgument("The sensor array is null.");
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
}

[[nodiscard]] CNA_Result AccelerometerSetDisposalCleanupHookForTestsExt(
    const CNA_AccelerometerHandle sensor,
    const CNA_SensorEventCallback callback,
    void* const context)
{
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
}

[[nodiscard]] CNA_Result GyroscopeSubscribeCurrentValueChanged(
    const CNA_GyroscopeHandle sensor,
    const CNA_GyroscopeReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The sensor reading callback is null.");
    }
    std::shared_ptr<SensorResource<Gyroscope>> resource;
    if (const CNA_Result result = BorrowLiveSensor<Gyroscope>(sensor, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const source = &resource->value->CurrentValueChanged;
    const auto token = source->Add(
        [callback, context](System::Object*, const SensorReadingEventArgs<GyroscopeReading>& args) {
            const CNA_GyroscopeReading reading =
                MapGyroscopeReadingValue(args.getSensorReadingProperty());
            callback(&reading, context);
        });
    return PublishRegistration(
        std::make_shared<SensorRegistration<SensorReadingEventArgs<GyroscopeReading>>>(
            resource, source, token),
        outRegistration);
}

[[nodiscard]] CNA_Result GyroscopeDispatchToInstancesForTestsExt(
    const CNA_Handle gameHandle,
    const CNA_GyroscopeHandle* const sensors,
    const uint64_t count,
    const float x,
    const float y,
    const float z)
{
    if (sensors == nullptr && count != UINT64_C(0)) {
        return InvalidArgument("The sensor array is null.");
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
}

[[nodiscard]] CNA_Result GyroscopeSetDisposalCleanupHookForTestsExt(
    const CNA_GyroscopeHandle sensor,
    const CNA_SensorEventCallback callback,
    void* const context)
{
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
}

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
    return InvalidState("No test backend is installed and started for this sensor.");
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
        return InvalidArgument("The sensor registration output is null.");
    }
    *outRegistration = CNA_INVALID_HANDLE;
    if (callback == nullptr) {
        return InvalidArgument("The sensor calibration callback is null.");
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

[[nodiscard]] CNA_Result AccelerometerReadingEventInfoEquals(
    const CNA_AccelerometerReadingEventInfo* const left,
    const CNA_AccelerometerReadingEventInfo* const right,
    CNA_Bool* const outEqual)
{
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
    *outEqual = ToCBool(first == second);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AccelerometerSubscribeReadingChanged(
    const CNA_AccelerometerHandle sensor,
    const CNA_AccelerometerReadingEventCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The sensor reading callback is null.");
    }
    std::shared_ptr<SensorResource<Accelerometer>> resource;
    if (const CNA_Result result = BorrowLiveSensor<Accelerometer>(sensor, &resource);
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
}

[[nodiscard]] CNA_Result CompassSubscribeCurrentValueChanged(
    const CNA_CompassHandle sensor,
    const CNA_CompassReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The sensor reading callback is null.");
    }
    std::shared_ptr<SensorResource<Compass>> resource;
    if (const CNA_Result result = BorrowSensor<Compass>(sensor, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const source = &resource->value->CurrentValueChanged;
    const auto token = source->Add(
        [callback, context](System::Object*, const SensorReadingEventArgs<CompassReading>& args) {
            const CNA_CompassReading reading =
                MapCompassReadingValue(args.getSensorReadingProperty());
            callback(&reading, context);
        });
    return PublishRegistration(
        std::make_shared<SensorRegistration<SensorReadingEventArgs<CompassReading>>>(
            resource, source, token),
        outRegistration);
}

[[nodiscard]] CNA_Result CompassSetTestBackendExt(
    const CNA_CompassHandle sensor,
    const CNA_Bool installed,
    const CNA_Bool supported)
{
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
}

[[nodiscard]] CNA_Result CompassInjectSyntheticUpdateExt(
    const CNA_CompassHandle sensor,
    const CNA_CompassReading* const reading)
{
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
}

[[nodiscard]] CNA_Result CompassInjectCalibrationRequestExt(const CNA_CompassHandle sensor)
{
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
}

[[nodiscard]] CNA_Result MotionSubscribeCurrentValueChanged(
    const CNA_MotionHandle sensor,
    const CNA_MotionReadingCallback callback,
    void* const context,
    CNA_SensorEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The sensor reading callback is null.");
    }
    std::shared_ptr<SensorResource<Motion>> resource;
    if (const CNA_Result result = BorrowSensor<Motion>(sensor, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const source = &resource->value->CurrentValueChanged;
    const auto token = source->Add(
        [callback, context](System::Object*, const SensorReadingEventArgs<MotionReading>& args) {
            const CNA_MotionReading reading =
                MapMotionReadingValue(args.getSensorReadingProperty());
            callback(&reading, context);
        });
    return PublishRegistration(
        std::make_shared<SensorRegistration<SensorReadingEventArgs<MotionReading>>>(
            resource, source, token),
        outRegistration);
}

[[nodiscard]] CNA_Result MotionSetTestBackendExt(
    const CNA_MotionHandle sensor,
    const CNA_Bool installed,
    const CNA_Bool supported,
    const CNA_Bool northReferenced)
{
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
}

[[nodiscard]] CNA_Result MotionInjectSyntheticUpdateExt(
    const CNA_MotionHandle sensor,
    const CNA_MotionReading* const reading)
{
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
}

[[nodiscard]] CNA_Result MotionInjectCalibrationRequestExt(const CNA_MotionHandle sensor)
{
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
}

} // namespace

// The routes generated from modules/c-api/spec/Sensors.toml.
#include "generated/Sensors.inc"
