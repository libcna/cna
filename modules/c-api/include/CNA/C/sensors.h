// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_SENSORS_H
#define CNA_C_SENSORS_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A point in time with a UTC offset.
 *
 * The canonical sensor readings timestamp themselves with the runtime's date-and-offset type, so
 * this is that type's ABI form: both members are 100-nanosecond ticks — the unit every duration in
 * this ABI already uses — with @ref ticks counted from the runtime's own epoch of 0001-01-01 rather
 * than from the Unix epoch. Subtract @ref offset_ticks from @ref ticks to obtain UTC, which is what
 * the canonical equality and hashing compare.
 */
typedef struct CNA_DateTimeOffset {
    /** @brief Local time in 100-nanosecond ticks since 0001-01-01. */
    int64_t ticks;

    /** @brief Offset from UTC in 100-nanosecond ticks. */
    int64_t offset_ticks;
} CNA_DateTimeOffset;

/** @brief Fixed-width identity of a sensor's current state. */
typedef uint32_t CNA_SensorState;

/** @brief The sensor is not supported on this device. */
#define CNA_SENSOR_STATE_NOT_SUPPORTED UINT32_C(0)
/** @brief The sensor is ready and providing data. */
#define CNA_SENSOR_STATE_READY UINT32_C(1)
/** @brief The sensor is initializing. */
#define CNA_SENSOR_STATE_INITIALIZING UINT32_C(2)
/** @brief The sensor has no data available. */
#define CNA_SENSOR_STATE_NO_DATA UINT32_C(3)
/** @brief The sensor cannot be accessed because permission is missing. */
#define CNA_SENSOR_STATE_NO_PERMISSIONS UINT32_C(4)
/** @brief The sensor is disabled. */
#define CNA_SENSOR_STATE_DISABLED UINT32_C(5)
/** @brief Highest defined sensor-state identity. */
#define CNA_SENSOR_STATE_MAXIMUM CNA_SENSOR_STATE_DISABLED

/**
 * @brief One accelerometer reading.
 *
 * Every reading value in this header carries a timestamp, which is how the canonical reading
 * interface's single member is expressed in C: there is no interface type, only the field every
 * implementer must have.
 */
typedef struct CNA_AccelerometerReading {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief When the reading was taken. */
    CNA_DateTimeOffset timestamp;

    /** @brief Acceleration in g, per axis. */
    CNA_Vector3 acceleration;
} CNA_AccelerometerReading;

/** @brief One gyroscope reading. */
typedef struct CNA_GyroscopeReading {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief When the reading was taken. */
    CNA_DateTimeOffset timestamp;

    /** @brief Angular velocity in radians per second, per axis. */
    CNA_Vector3 rotation_rate;
} CNA_GyroscopeReading;

/** @brief One device-orientation reading. */
typedef struct CNA_AttitudeReading {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief When the reading was taken. */
    CNA_DateTimeOffset timestamp;

    /** @brief Rotation around the X axis, in radians. */
    float pitch;

    /** @brief Rotation around the Y axis, in radians. */
    float roll;

    /** @brief Rotation around the Z axis, in radians. */
    float yaw;

    /** @brief The same orientation as a quaternion. */
    CNA_Quaternion quaternion;

    /** @brief The same orientation as a rotation matrix. */
    CNA_Matrix rotation_matrix;
} CNA_AttitudeReading;

/** @brief One compass reading. */
typedef struct CNA_CompassReading {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief When the reading was taken. */
    CNA_DateTimeOffset timestamp;

    /** @brief Accuracy of the heading, in degrees. */
    double heading_accuracy;

    /** @brief Heading relative to magnetic north, in degrees. */
    double magnetic_heading;

    /** @brief Heading relative to true north, in degrees. */
    double true_heading;

    /** @brief Raw magnetometer reading in micro-teslas, per axis. */
    CNA_Vector3 magnetometer_reading;
} CNA_CompassReading;

/** @brief One fused motion reading. */
typedef struct CNA_MotionReading {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief When the reading was taken. */
    CNA_DateTimeOffset timestamp;

    /** @brief The fused device orientation. */
    CNA_AttitudeReading attitude;

    /** @brief Acceleration excluding gravity, in g, per axis. */
    CNA_Vector3 device_acceleration;

    /** @brief Angular velocity in radians per second, per axis. */
    CNA_Vector3 device_rotation_rate;

    /** @brief The gravity vector, in g, per axis. */
    CNA_Vector3 gravity;
} CNA_MotionReading;

/**
 * @brief Initializes an accelerometer reading to the canonical default.
 *
 * @param out_reading Receives a zeroed reading with a default timestamp.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_init(CNA_AccelerometerReading* out_reading);

/**
 * @brief Initializes an accelerometer reading from its canonical constructor arguments.
 *
 * @param timestamp When the reading was taken.
 * @param acceleration Acceleration in g, per axis.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_init_from_values(
    CNA_DateTimeOffset timestamp,
    CNA_Vector3 acceleration,
    CNA_AccelerometerReading* out_reading);

/**
 * @brief Initializes a gyroscope reading to the canonical default.
 *
 * @param out_reading Receives a zeroed reading with a default timestamp.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_init(CNA_GyroscopeReading* out_reading);

/**
 * @brief Initializes a gyroscope reading from its canonical constructor arguments.
 *
 * @param rotation_rate Angular velocity in radians per second, per axis.
 * @param timestamp When the reading was taken.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The argument order follows the canonical constructor, which takes the rate first.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_init_from_values(
    CNA_Vector3 rotation_rate,
    CNA_DateTimeOffset timestamp,
    CNA_GyroscopeReading* out_reading);

/**
 * @brief Initializes an attitude reading to the canonical default.
 *
 * @param out_reading Receives a zeroed reading with a default timestamp.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_attitude_reading_init(CNA_AttitudeReading* out_reading);

/**
 * @brief Initializes an attitude reading from its canonical constructor arguments.
 *
 * @param pitch Rotation around the X axis, in radians.
 * @param roll Rotation around the Y axis, in radians.
 * @param yaw Rotation around the Z axis, in radians.
 * @param quaternion The same orientation as a quaternion.
 * @param rotation_matrix The same orientation as a rotation matrix.
 * @param timestamp When the reading was taken.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_attitude_reading_init_from_values(
    float pitch,
    float roll,
    float yaw,
    CNA_Quaternion quaternion,
    CNA_Matrix rotation_matrix,
    CNA_DateTimeOffset timestamp,
    CNA_AttitudeReading* out_reading);

/**
 * @brief Initializes a compass reading to the canonical default.
 *
 * @param out_reading Receives a zeroed reading with a default timestamp.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_compass_reading_init(CNA_CompassReading* out_reading);

/**
 * @brief Initializes a compass reading from its canonical constructor arguments.
 *
 * @param heading_accuracy Accuracy of the heading, in degrees.
 * @param magnetic_heading Heading relative to magnetic north, in degrees.
 * @param magnetometer_reading Raw magnetometer reading in micro-teslas, per axis.
 * @param timestamp When the reading was taken.
 * @param true_heading Heading relative to true north, in degrees.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_compass_reading_init_from_values(
    double heading_accuracy,
    double magnetic_heading,
    CNA_Vector3 magnetometer_reading,
    CNA_DateTimeOffset timestamp,
    double true_heading,
    CNA_CompassReading* out_reading);

/**
 * @brief Initializes a motion reading to the canonical default.
 *
 * @param out_reading Receives a zeroed reading with a default timestamp.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_motion_reading_init(CNA_MotionReading* out_reading);

/**
 * @brief Initializes a motion reading from its canonical constructor arguments.
 *
 * @param attitude The fused device orientation.
 * @param device_acceleration Acceleration excluding gravity, in g, per axis.
 * @param device_rotation_rate Angular velocity in radians per second, per axis.
 * @param gravity The gravity vector, in g, per axis.
 * @param timestamp When the reading was taken.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or an invalid
 *         attitude structure.
 */
CNA_C_API CNA_Result cna_motion_reading_init_from_values(
    const CNA_AttitudeReading* attitude,
    CNA_Vector3 device_acceleration,
    CNA_Vector3 device_rotation_rate,
    CNA_Vector3 gravity,
    CNA_DateTimeOffset timestamp,
    CNA_MotionReading* out_reading);

/**
 * @brief Compares two accelerometer readings.
 *
 * @param left First reading.
 * @param right Second reading.
 * @param out_equal Receives `CNA_TRUE` when every canonical field matches.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_equals(
    const CNA_AccelerometerReading* left,
    const CNA_AccelerometerReading* right,
    CNA_Bool* out_equal);

/**
 * @brief Returns a accelerometer reading's hash code.
 *
 * @param reading The reading.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical hash is `std::size_t`; C reports it as `uint64_t`, which is the same value on every
 * platform this ABI supports. This pure POD operation may run on any thread.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_get_hash_code(const CNA_AccelerometerReading* reading, uint64_t* out_hash);

/**
 * @brief Returns the byte count of a accelerometer reading's text.
 *
 * @param reading The reading.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_get_string_size(const CNA_AccelerometerReading* reading, uint64_t* out_bytes);

/**
 * @brief Copies a accelerometer reading's text.
 *
 * @param reading The reading.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_copy_string(
    const CNA_AccelerometerReading* reading,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the accelerometer-reading type's fully-qualified .NET type name.
 *
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The name belongs to the type rather than to a value, so this route takes none.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the accelerometer-reading type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Compares two gyroscope readings.
 *
 * @param left First reading.
 * @param right Second reading.
 * @param out_equal Receives `CNA_TRUE` when every canonical field matches.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_equals(
    const CNA_GyroscopeReading* left,
    const CNA_GyroscopeReading* right,
    CNA_Bool* out_equal);

/**
 * @brief Returns a gyroscope reading's hash code.
 *
 * @param reading The reading.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical hash is `std::size_t`; C reports it as `uint64_t`, which is the same value on every
 * platform this ABI supports. This pure POD operation may run on any thread.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_get_hash_code(const CNA_GyroscopeReading* reading, uint64_t* out_hash);

/**
 * @brief Returns the byte count of a gyroscope reading's text.
 *
 * @param reading The reading.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_get_string_size(const CNA_GyroscopeReading* reading, uint64_t* out_bytes);

/**
 * @brief Copies a gyroscope reading's text.
 *
 * @param reading The reading.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_copy_string(
    const CNA_GyroscopeReading* reading,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the gyroscope-reading type's fully-qualified .NET type name.
 *
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The name belongs to the type rather than to a value, so this route takes none.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the gyroscope-reading type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_gyroscope_reading_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Compares two attitude readings.
 *
 * @param left First reading.
 * @param right Second reading.
 * @param out_equal Receives `CNA_TRUE` when every canonical field matches.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_attitude_reading_equals(
    const CNA_AttitudeReading* left,
    const CNA_AttitudeReading* right,
    CNA_Bool* out_equal);

/**
 * @brief Returns a attitude reading's hash code.
 *
 * @param reading The reading.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical hash is `std::size_t`; C reports it as `uint64_t`, which is the same value on every
 * platform this ABI supports. This pure POD operation may run on any thread.
 */
CNA_C_API CNA_Result cna_attitude_reading_get_hash_code(const CNA_AttitudeReading* reading, uint64_t* out_hash);

/**
 * @brief Returns the byte count of a attitude reading's text.
 *
 * @param reading The reading.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 */
CNA_C_API CNA_Result cna_attitude_reading_get_string_size(const CNA_AttitudeReading* reading, uint64_t* out_bytes);

/**
 * @brief Copies a attitude reading's text.
 *
 * @param reading The reading.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_attitude_reading_copy_string(
    const CNA_AttitudeReading* reading,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the attitude-reading type's fully-qualified .NET type name.
 *
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The name belongs to the type rather than to a value, so this route takes none.
 */
CNA_C_API CNA_Result cna_attitude_reading_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the attitude-reading type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_attitude_reading_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Compares two compass readings.
 *
 * @param left First reading.
 * @param right Second reading.
 * @param out_equal Receives `CNA_TRUE` when every canonical field matches.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_compass_reading_equals(
    const CNA_CompassReading* left,
    const CNA_CompassReading* right,
    CNA_Bool* out_equal);

/**
 * @brief Returns a compass reading's hash code.
 *
 * @param reading The reading.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical hash is `std::size_t`; C reports it as `uint64_t`, which is the same value on every
 * platform this ABI supports. This pure POD operation may run on any thread.
 */
CNA_C_API CNA_Result cna_compass_reading_get_hash_code(const CNA_CompassReading* reading, uint64_t* out_hash);

/**
 * @brief Returns the byte count of a compass reading's text.
 *
 * @param reading The reading.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 */
CNA_C_API CNA_Result cna_compass_reading_get_string_size(const CNA_CompassReading* reading, uint64_t* out_bytes);

/**
 * @brief Copies a compass reading's text.
 *
 * @param reading The reading.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_compass_reading_copy_string(
    const CNA_CompassReading* reading,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the compass-reading type's fully-qualified .NET type name.
 *
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The name belongs to the type rather than to a value, so this route takes none.
 */
CNA_C_API CNA_Result cna_compass_reading_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the compass-reading type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_compass_reading_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Compares two motion readings.
 *
 * @param left First reading.
 * @param right Second reading.
 * @param out_equal Receives `CNA_TRUE` when every canonical field matches.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_motion_reading_equals(
    const CNA_MotionReading* left,
    const CNA_MotionReading* right,
    CNA_Bool* out_equal);

/**
 * @brief Returns a motion reading's hash code.
 *
 * @param reading The reading.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical hash is `std::size_t`; C reports it as `uint64_t`, which is the same value on every
 * platform this ABI supports. This pure POD operation may run on any thread.
 */
CNA_C_API CNA_Result cna_motion_reading_get_hash_code(const CNA_MotionReading* reading, uint64_t* out_hash);

/**
 * @brief Returns the byte count of a motion reading's text.
 *
 * @param reading The reading.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 */
CNA_C_API CNA_Result cna_motion_reading_get_string_size(const CNA_MotionReading* reading, uint64_t* out_bytes);

/**
 * @brief Copies a motion reading's text.
 *
 * @param reading The reading.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_motion_reading_copy_string(
    const CNA_MotionReading* reading,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the motion-reading type's fully-qualified .NET type name.
 *
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The name belongs to the type rather than to a value, so this route takes none.
 */
CNA_C_API CNA_Result cna_motion_reading_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the motion-reading type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_motion_reading_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Sensor devices ---- */

/** @brief Owned handle to one sensor event subscription. */
typedef CNA_Handle CNA_SensorEventRegistrationHandle;

/**
 * @brief Handler invoked for a sensor event that carries no data.
 *
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_SensorEventCallback)(void* context);

/**
 * @brief Handler invoked with a new accelerometer reading.
 *
 * @param reading The reading, borrowed for the duration of the call.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_AccelerometerReadingCallback)(
    const CNA_AccelerometerReading* reading,
    void* context);

/**
 * @brief Handler invoked with a new gyroscope reading.
 *
 * @param reading The reading, borrowed for the duration of the call.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_GyroscopeReadingCallback)(
    const CNA_GyroscopeReading* reading,
    void* context);

/**
 * @brief Returns the error identifier of the last sensor failure on this thread.
 *
 * @param out_error_id Receives the identifier the canonical exception carried.
 * @param out_has_error_id Receives `CNA_TRUE` when the last failure on this thread was a sensor
 *        failure carrying an identifier.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * A sensor failure's identifier is not expressible in its message, so the exception firewall records
 * it per thread — the same treatment a network join failure already gets. Read it immediately after
 * the failing call: the next failed call replaces it.
 */
CNA_C_API CNA_Result cna_sensors_get_last_error_id_ext(
    int32_t* out_error_id,
    CNA_Bool* out_has_error_id);

/**
 * @brief Releases a sensor event registration.
 *
 * @param registration Owned registration handle from any sensor subscribe route.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 *
 * One route releases every sensor event, because a registration already knows which event and which
 * sensor it came from.
 */
CNA_C_API CNA_Result cna_sensor_unsubscribe_ext(
    CNA_SensorEventRegistrationHandle registration);

/** @brief Owned handle to one accelerometer sensor. */
typedef CNA_Handle CNA_AccelerometerHandle;

/**
 * @brief Reports whether this device supports an accelerometer.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when the sensor is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical query is static and probes the platform. A device without the sensor is an ordinary
 * answer of `CNA_FALSE`, which is what every verification tree reports.
 */
CNA_C_API CNA_Result cna_accelerometer_get_is_supported(CNA_Handle game, CNA_Bool* out_supported);

/**
 * @brief Creates an accelerometer sensor.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_sensor Receives an owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Creation succeeds even where the sensor is unsupported; the state reports which case it is.
 */
CNA_C_API CNA_Result cna_accelerometer_create(CNA_Handle game, CNA_AccelerometerHandle* out_sensor);

/**
 * @brief Returns the sensor's current state.
 *
 * @param sensor Owned sensor handle.
 * @param out_state Receives one `CNA_SENSOR_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_get_state(CNA_AccelerometerHandle sensor, CNA_SensorState* out_state);

/**
 * @brief Starts data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed or
 *         acquisition is already started — the canonical failure carries an error id, readable with
 *         `cna_sensors_get_last_error_id_ext`.
 */
CNA_C_API CNA_Result cna_accelerometer_start(CNA_AccelerometerHandle sensor);

/**
 * @brief Stops data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed or
 *         acquisition was never started.
 */
CNA_C_API CNA_Result cna_accelerometer_stop(CNA_AccelerometerHandle sensor);

/**
 * @brief Disposes the sensor without releasing its handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Disposal stops acquisition and releases the platform subsystem hold. **Disposing twice is
 * refused** with `CNA_RESULT_INVALID_STATE`, unlike most disposables in this ABI — the canonical
 * sensor treats a second disposal as use-after-disposal rather than a no-op, and that is reported
 * rather than smoothed over. Every other route reports the same failure afterwards, which is how a
 * caller observes the disposed state: the canonical disposal flag is protected, so there is no
 * query route for it.
 */
CNA_C_API CNA_Result cna_accelerometer_dispose(CNA_AccelerometerHandle sensor);


/**
 * @brief Returns the sensor's most recent reading.
 *
 * @param sensor Owned sensor handle.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **An unsupported sensor refuses this call** with `CNA_RESULT_INVALID_STATE` rather than
 * answering a default, because the canonical property throws there: check
 * `cna_accelerometer_get_is_supported` first. A supported sensor that has produced nothing yet
 * answers the canonical default reading, which `cna_accelerometer_get_is_data_valid` tells apart
 * from a real measurement.
 */
CNA_C_API CNA_Result cna_accelerometer_get_current_value(
    CNA_AccelerometerHandle sensor,
    CNA_AccelerometerReading* out_reading);

/**
 * @brief Reports whether the sensor's current reading is real data.
 *
 * @param sensor Owned sensor handle.
 * @param out_valid Receives `CNA_TRUE` once a reading has been produced.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_get_is_data_valid(CNA_AccelerometerHandle sensor, CNA_Bool* out_valid);

/**
 * @brief Returns the requested interval between updates.
 *
 * @param sensor Owned sensor handle.
 * @param out_ticks Receives the interval in 100-nanosecond ticks; zero means "as fast as possible".
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_get_time_between_updates_ticks(
    CNA_AccelerometerHandle sensor,
    int64_t* out_ticks);

/**
 * @brief Sets the requested interval between updates.
 *
 * @param sensor Owned sensor handle.
 * @param ticks New interval in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Setting a different value raises the interval-changed event.
 */
CNA_C_API CNA_Result cna_accelerometer_set_time_between_updates_ticks(
    CNA_AccelerometerHandle sensor,
    int64_t ticks);

/**
 * @brief Subscribes to the sensor's reading-changed event.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked with each new reading.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 *
 * The canonical event carries an event-argument object wrapping the reading; C delivers the reading
 * itself, because that is the only thing the wrapper holds. Release the registration with
 * `cna_sensor_unsubscribe_ext`; it detaches from the sensor it was created for, which must still
 * exist.
 */
CNA_C_API CNA_Result cna_accelerometer_subscribe_current_value_changed(
    CNA_AccelerometerHandle sensor,
    CNA_AccelerometerReadingCallback callback,
    void* context,
    CNA_SensorEventRegistrationHandle* out_registration);


/**
 * @brief Feeds the sensor a synthetic reading.
 *
 * @param sensor Owned sensor handle.
 * @param x First axis value.
 * @param y Second axis value.
 * @param z Third axis value.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This maps the canonical test-support injector, which exists so a caller can exercise its own
 * event wiring on a machine with no sensor hardware — which is every machine this ABI is currently
 * verified on. The values are **platform units**, and the reading that comes back out is in the
 * canonical unit: an accelerometer injection in metres per second squared is reported in g, so
 * injecting 9.80665 yields a reading of 1.
 */
CNA_C_API CNA_Result cna_accelerometer_inject_synthetic_update_ext(
    CNA_AccelerometerHandle sensor,
    float x,
    float y,
    float z);

/**
 * @brief Forces the sensor's started flag.
 *
 * @param sensor Owned sensor handle.
 * @param started New started state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_accelerometer_set_started_for_tests_ext(
    CNA_AccelerometerHandle sensor,
    CNA_Bool started);

/**
 * @brief Forces the sensor's supported flag.
 *
 * @param sensor Owned sensor handle.
 * @param supported New supported state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This overrides the platform probe for this instance only, which is what lets a test drive the
 * supported path on a machine without the hardware.
 */
CNA_C_API CNA_Result cna_accelerometer_set_supported_for_tests_ext(
    CNA_AccelerometerHandle sensor,
    CNA_Bool supported);

/**
 * @brief Reports whether the sensor currently holds the platform subsystem.
 *
 * @param sensor Owned sensor handle.
 * @param out_held Receives `CNA_TRUE` while the hold is taken.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_get_subsystem_held_for_tests_ext(
    CNA_AccelerometerHandle sensor,
    CNA_Bool* out_held);

/**
 * @brief Registers the sensor in the started-instance list the dispatcher walks.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_accelerometer_register_started_instance_for_tests_ext(CNA_AccelerometerHandle sensor);

/**
 * @brief Removes the sensor from the started-instance list.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_accelerometer_unregister_started_instance_for_tests_ext(CNA_AccelerometerHandle sensor);

/**
 * @brief Dispatches one synthetic reading to an explicit set of sensors.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param sensors Array of sensor handles; may be null only when @p count is zero.
 * @param count Number of handles in @p sensors.
 * @param x First axis value.
 * @param y Second axis value.
 * @param z Third axis value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null array with a nonzero
 *         count, `CNA_RESULT_INVALID_HANDLE` when an element is not a live sensor, or a documented
 *         handle/thread/native failure.
 *
 * The canonical helper takes a vector of instances; C takes an array of handles, which is the same
 * set expressed the only way this ABI can express it.
 */
CNA_C_API CNA_Result cna_accelerometer_dispatch_to_instances_for_tests_ext(
    CNA_Handle game,
    const CNA_AccelerometerHandle* sensors,
    uint64_t count,
    float x,
    float y,
    float z);

/**
 * @brief Forces the platform event-watch registration to fail.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param should_fail `CNA_TRUE` to make the next registration fail.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Process-wide state: set it back when the test is done.
 */
CNA_C_API CNA_Result cna_accelerometer_set_event_watch_registration_failure_for_tests_ext(
    CNA_Handle game,
    CNA_Bool should_fail);

/**
 * @brief Returns how many exceptions the dispatcher has swallowed.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical dispatcher must not let a subscriber's exception escape into the platform event
 * loop, so it counts them instead; this is how a caller sees that happened.
 */
CNA_C_API CNA_Result cna_accelerometer_get_dispatch_exception_count_for_tests_ext(
    CNA_Handle game,
    int32_t* out_count);

/**
 * @brief Returns the byte count of the dispatcher's last swallowed exception message.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_get_last_dispatch_exception_message_size_for_tests_ext(
    CNA_Handle game,
    uint64_t* out_bytes);

/**
 * @brief Copies the dispatcher's last swallowed exception message.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_copy_last_dispatch_exception_message_for_tests_ext(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a platform sensor identifier is currently connected.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param sensor_id Platform sensor identifier.
 * @param out_connected Receives `CNA_TRUE` when that identifier is connected.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_accelerometer_is_sensor_connected_for_tests_ext(
    CNA_Handle game,
    int64_t sensor_id,
    CNA_Bool* out_connected);

/**
 * @brief Installs a hook invoked while the sensor is being disposed.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked during disposal; null clears the hook.
 * @param context Caller context passed back to the handler; may be null.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The hook is a single slot rather than a subscription list, exactly as the canonical setter is, so
 * installing a second one replaces the first. The context must outlive the sensor.
 */
CNA_C_API CNA_Result cna_accelerometer_set_disposal_cleanup_hook_for_tests_ext(
    CNA_AccelerometerHandle sensor,
    CNA_SensorEventCallback callback,
    void* context);

/**
 * @brief Returns the byte count of the sensor type's fully-qualified .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_get_type_name_size(CNA_AccelerometerHandle sensor, uint64_t* out_bytes);

/**
 * @brief Copies the sensor type's fully-qualified .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_accelerometer_copy_type_name(
    CNA_AccelerometerHandle sensor,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases a sensor handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Releasing disposes the sensor if it has not been disposed already, which is what the canonical
 * destructor does.
 */
CNA_C_API CNA_Result cna_accelerometer_destroy(CNA_AccelerometerHandle sensor);

/** @brief Owned handle to one gyroscope sensor. */
typedef CNA_Handle CNA_GyroscopeHandle;

/**
 * @brief Reports whether this device supports a gyroscope.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when the sensor is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical query is static and probes the platform. A device without the sensor is an ordinary
 * answer of `CNA_FALSE`, which is what every verification tree reports.
 */
CNA_C_API CNA_Result cna_gyroscope_get_is_supported(CNA_Handle game, CNA_Bool* out_supported);

/**
 * @brief Creates a gyroscope sensor.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_sensor Receives an owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Creation succeeds even where the sensor is unsupported; the state reports which case it is.
 */
CNA_C_API CNA_Result cna_gyroscope_create(CNA_Handle game, CNA_GyroscopeHandle* out_sensor);

/**
 * @brief Returns the sensor's current state.
 *
 * @param sensor Owned sensor handle.
 * @param out_state Receives one `CNA_SENSOR_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_get_state(CNA_GyroscopeHandle sensor, CNA_SensorState* out_state);

/**
 * @brief Starts data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed or
 *         acquisition is already started — the canonical failure carries an error id, readable with
 *         `cna_sensors_get_last_error_id_ext`.
 */
CNA_C_API CNA_Result cna_gyroscope_start(CNA_GyroscopeHandle sensor);

/**
 * @brief Stops data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed or
 *         acquisition was never started.
 */
CNA_C_API CNA_Result cna_gyroscope_stop(CNA_GyroscopeHandle sensor);

/**
 * @brief Disposes the sensor without releasing its handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Disposal stops acquisition and releases the platform subsystem hold. **Disposing twice is
 * refused** with `CNA_RESULT_INVALID_STATE`, unlike most disposables in this ABI — the canonical
 * sensor treats a second disposal as use-after-disposal rather than a no-op, and that is reported
 * rather than smoothed over. Every other route reports the same failure afterwards, which is how a
 * caller observes the disposed state: the canonical disposal flag is protected, so there is no
 * query route for it.
 */
CNA_C_API CNA_Result cna_gyroscope_dispose(CNA_GyroscopeHandle sensor);


/**
 * @brief Returns the sensor's most recent reading.
 *
 * @param sensor Owned sensor handle.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **An unsupported sensor refuses this call** with `CNA_RESULT_INVALID_STATE` rather than
 * answering a default, because the canonical property throws there: check
 * `cna_gyroscope_get_is_supported` first. A supported sensor that has produced nothing yet answers
 * the canonical default reading, which `cna_gyroscope_get_is_data_valid` tells apart from a real
 * measurement.
 */
CNA_C_API CNA_Result cna_gyroscope_get_current_value(
    CNA_GyroscopeHandle sensor,
    CNA_GyroscopeReading* out_reading);

/**
 * @brief Reports whether the sensor's current reading is real data.
 *
 * @param sensor Owned sensor handle.
 * @param out_valid Receives `CNA_TRUE` once a reading has been produced.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_get_is_data_valid(CNA_GyroscopeHandle sensor, CNA_Bool* out_valid);

/**
 * @brief Returns the requested interval between updates.
 *
 * @param sensor Owned sensor handle.
 * @param out_ticks Receives the interval in 100-nanosecond ticks; zero means "as fast as possible".
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_get_time_between_updates_ticks(
    CNA_GyroscopeHandle sensor,
    int64_t* out_ticks);

/**
 * @brief Sets the requested interval between updates.
 *
 * @param sensor Owned sensor handle.
 * @param ticks New interval in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Setting a different value raises the interval-changed event.
 */
CNA_C_API CNA_Result cna_gyroscope_set_time_between_updates_ticks(
    CNA_GyroscopeHandle sensor,
    int64_t ticks);

/**
 * @brief Subscribes to the sensor's reading-changed event.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked with each new reading.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 *
 * The canonical event carries an event-argument object wrapping the reading; C delivers the reading
 * itself, because that is the only thing the wrapper holds. Release the registration with
 * `cna_sensor_unsubscribe_ext`; it detaches from the sensor it was created for, which must still
 * exist.
 */
CNA_C_API CNA_Result cna_gyroscope_subscribe_current_value_changed(
    CNA_GyroscopeHandle sensor,
    CNA_GyroscopeReadingCallback callback,
    void* context,
    CNA_SensorEventRegistrationHandle* out_registration);


/**
 * @brief Feeds the sensor a synthetic reading.
 *
 * @param sensor Owned sensor handle.
 * @param x First axis value.
 * @param y Second axis value.
 * @param z Third axis value.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This maps the canonical test-support injector, which exists so a caller can exercise its own
 * event wiring on a machine with no sensor hardware — which is every machine this ABI is currently
 * verified on. The reading is delivered exactly as a real one would be.
 */
CNA_C_API CNA_Result cna_gyroscope_inject_synthetic_update_ext(
    CNA_GyroscopeHandle sensor,
    float x,
    float y,
    float z);

/**
 * @brief Forces the sensor's started flag.
 *
 * @param sensor Owned sensor handle.
 * @param started New started state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gyroscope_set_started_for_tests_ext(
    CNA_GyroscopeHandle sensor,
    CNA_Bool started);

/**
 * @brief Forces the sensor's supported flag.
 *
 * @param sensor Owned sensor handle.
 * @param supported New supported state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This overrides the platform probe for this instance only, which is what lets a test drive the
 * supported path on a machine without the hardware.
 */
CNA_C_API CNA_Result cna_gyroscope_set_supported_for_tests_ext(
    CNA_GyroscopeHandle sensor,
    CNA_Bool supported);

/**
 * @brief Reports whether the sensor currently holds the platform subsystem.
 *
 * @param sensor Owned sensor handle.
 * @param out_held Receives `CNA_TRUE` while the hold is taken.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_get_subsystem_held_for_tests_ext(
    CNA_GyroscopeHandle sensor,
    CNA_Bool* out_held);

/**
 * @brief Registers the sensor in the started-instance list the dispatcher walks.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gyroscope_register_started_instance_for_tests_ext(CNA_GyroscopeHandle sensor);

/**
 * @brief Removes the sensor from the started-instance list.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gyroscope_unregister_started_instance_for_tests_ext(CNA_GyroscopeHandle sensor);

/**
 * @brief Dispatches one synthetic reading to an explicit set of sensors.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param sensors Array of sensor handles; may be null only when @p count is zero.
 * @param count Number of handles in @p sensors.
 * @param x First axis value.
 * @param y Second axis value.
 * @param z Third axis value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null array with a nonzero
 *         count, `CNA_RESULT_INVALID_HANDLE` when an element is not a live sensor, or a documented
 *         handle/thread/native failure.
 *
 * The canonical helper takes a vector of instances; C takes an array of handles, which is the same
 * set expressed the only way this ABI can express it.
 */
CNA_C_API CNA_Result cna_gyroscope_dispatch_to_instances_for_tests_ext(
    CNA_Handle game,
    const CNA_GyroscopeHandle* sensors,
    uint64_t count,
    float x,
    float y,
    float z);

/**
 * @brief Forces the platform event-watch registration to fail.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param should_fail `CNA_TRUE` to make the next registration fail.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Process-wide state: set it back when the test is done.
 */
CNA_C_API CNA_Result cna_gyroscope_set_event_watch_registration_failure_for_tests_ext(
    CNA_Handle game,
    CNA_Bool should_fail);

/**
 * @brief Returns how many exceptions the dispatcher has swallowed.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical dispatcher must not let a subscriber's exception escape into the platform event
 * loop, so it counts them instead; this is how a caller sees that happened.
 */
CNA_C_API CNA_Result cna_gyroscope_get_dispatch_exception_count_for_tests_ext(
    CNA_Handle game,
    int32_t* out_count);

/**
 * @brief Returns the byte count of the dispatcher's last swallowed exception message.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_get_last_dispatch_exception_message_size_for_tests_ext(
    CNA_Handle game,
    uint64_t* out_bytes);

/**
 * @brief Copies the dispatcher's last swallowed exception message.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_copy_last_dispatch_exception_message_for_tests_ext(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a platform sensor identifier is currently connected.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param sensor_id Platform sensor identifier.
 * @param out_connected Receives `CNA_TRUE` when that identifier is connected.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gyroscope_is_sensor_connected_for_tests_ext(
    CNA_Handle game,
    int64_t sensor_id,
    CNA_Bool* out_connected);

/**
 * @brief Installs a hook invoked while the sensor is being disposed.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked during disposal; null clears the hook.
 * @param context Caller context passed back to the handler; may be null.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The hook is a single slot rather than a subscription list, exactly as the canonical setter is, so
 * installing a second one replaces the first. The context must outlive the sensor.
 */
CNA_C_API CNA_Result cna_gyroscope_set_disposal_cleanup_hook_for_tests_ext(
    CNA_GyroscopeHandle sensor,
    CNA_SensorEventCallback callback,
    void* context);

/**
 * @brief Returns the byte count of the sensor type's fully-qualified .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_get_type_name_size(CNA_GyroscopeHandle sensor, uint64_t* out_bytes);

/**
 * @brief Copies the sensor type's fully-qualified .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gyroscope_copy_type_name(
    CNA_GyroscopeHandle sensor,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases a sensor handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Releasing disposes the sensor if it has not been disposed already, which is what the canonical
 * destructor does.
 */
CNA_C_API CNA_Result cna_gyroscope_destroy(CNA_GyroscopeHandle sensor);

/* ---- Legacy accelerometer reading event ---- */

/**
 * @brief The payload of the canonical legacy accelerometer reading event.
 *
 * The canonical event-argument type predates the reading values above and carries the same
 * acceleration as three separate `double` components rather than a vector, which is why this is a
 * value of its own instead of a `CNA_AccelerometerReading`. Only this event delivers it.
 */
typedef struct CNA_AccelerometerReadingEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief When the reading was taken. */
    CNA_DateTimeOffset timestamp;

    /** @brief Acceleration along the X axis, in g. */
    double x;

    /** @brief Acceleration along the Y axis, in g. */
    double y;

    /** @brief Acceleration along the Z axis, in g. */
    double z;
} CNA_AccelerometerReadingEventInfo;

/**
 * @brief Initializes a legacy reading description to the canonical default.
 *
 * @param out_info Receives a zeroed description with a default timestamp.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_init(
    CNA_AccelerometerReadingEventInfo* out_info);

/**
 * @brief Initializes a legacy reading description from its canonical constructor arguments.
 *
 * @param x Acceleration along the X axis, in g.
 * @param y Acceleration along the Y axis, in g.
 * @param z Acceleration along the Z axis, in g.
 * @param timestamp When the reading was taken.
 * @param out_info Receives the description.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The argument order is the canonical one — the components first, the timestamp last — which is the
 * opposite of `cna_accelerometer_reading_init_from_values`. Neither order was normalized.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_init_from_values(
    double x,
    double y,
    double z,
    CNA_DateTimeOffset timestamp,
    CNA_AccelerometerReadingEventInfo* out_info);

/**
 * @brief Compares two legacy reading descriptions for canonical equality.
 *
 * @param left First description.
 * @param right Second description.
 * @param out_equal Receives `CNA_TRUE` when all three components and the timestamp match.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid input.
 *
 * The canonical inequality operator is this comparison negated and has no route of its own.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_equals(
    const CNA_AccelerometerReadingEventInfo* left,
    const CNA_AccelerometerReadingEventInfo* right,
    CNA_Bool* out_equal);

/**
 * @brief Returns the canonical hash of a legacy reading description.
 *
 * @param info The description.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid input.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_get_hash_code(
    const CNA_AccelerometerReadingEventInfo* info,
    uint64_t* out_hash);

/**
 * @brief Returns the byte count of a legacy reading description's canonical text.
 *
 * @param info The description.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid input.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_get_string_size(
    const CNA_AccelerometerReadingEventInfo* info,
    uint64_t* out_bytes);

/**
 * @brief Copies a legacy reading description's canonical text.
 *
 * @param info The description.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 *
 * The canonical text names the three components and omits the timestamp equality compares.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_copy_string(
    const CNA_AccelerometerReadingEventInfo* info,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the legacy reading event type's .NET type name.
 *
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the legacy reading event type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_accelerometer_reading_event_info_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the calibration event type's .NET type name.
 *
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The canonical calibration event argument carries no data at all, so it has no value in this ABI —
 * only its name, and a callback that receives nothing but the caller context.
 */
CNA_C_API CNA_Result cna_calibration_event_info_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the calibration event type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_calibration_event_info_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Handler invoked with a legacy accelerometer reading description.
 *
 * @param info The description, borrowed for the duration of the call.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_AccelerometerReadingEventCallback)(
    const CNA_AccelerometerReadingEventInfo* info,
    void* context);

/**
 * @brief Subscribes to the canonical legacy reading event of an accelerometer.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked for each accepted reading.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical event is obsolete and superseded by
 * `cna_accelerometer_subscribe_current_value_changed`, which is why it delivers a description of its
 * own rather than a reading. Both are raised for the same reading, and the canonical order is fixed:
 * the current-value handlers run first, this one second. Unlike the current-value event, this one is
 * raised **only** when the reading is valid.
 */
CNA_C_API CNA_Result cna_accelerometer_subscribe_reading_changed(
    CNA_AccelerometerHandle sensor,
    CNA_AccelerometerReadingEventCallback callback,
    void* context,
    CNA_SensorEventRegistrationHandle* out_registration);

/* ---- Compass ---- */

/** @brief Owned handle to one compass sensor. */
typedef CNA_Handle CNA_CompassHandle;

/**
 * @brief Handler invoked with a new compass reading.
 *
 * @param reading The reading, borrowed for the duration of the call.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_CompassReadingCallback)(
    const CNA_CompassReading* reading,
    void* context);

/**
 * @brief Reports whether this device supports a compass.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when the sensor is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical implementation answers `CNA_TRUE` on one platform only, so every verification tree —
 * and every desktop consumer — sees `CNA_FALSE` here. That is the sensor's real availability, not a
 * gap in this ABI.
 */
CNA_C_API CNA_Result cna_compass_get_is_supported(CNA_Handle game, CNA_Bool* out_supported);

/**
 * @brief Creates a compass sensor.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_sensor Receives an owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the canonical limit of ten
 *         simultaneous instances is already reached, or a documented argument/handle/thread failure.
 *
 * Creation succeeds even where the sensor is unsupported; the state reports which case it is.
 */
CNA_C_API CNA_Result cna_compass_create(CNA_Handle game, CNA_CompassHandle* out_sensor);

/**
 * @brief Returns the sensor's current state.
 *
 * @param sensor Owned sensor handle.
 * @param out_state Receives one `CNA_SENSOR_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_get_state(CNA_CompassHandle sensor, CNA_SensorState* out_state);

/**
 * @brief Starts data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed,
 *         acquisition is already started, or the platform has no compass — the canonical failure
 *         carries an error id, readable with `cna_sensors_get_last_error_id_ext`.
 */
CNA_C_API CNA_Result cna_compass_start(CNA_CompassHandle sensor);

/**
 * @brief Stops data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed.
 *
 * Stopping a sensor that never started is an ordinary success that still moves the state to
 * disabled, which is the canonical contract rather than an oversight.
 */
CNA_C_API CNA_Result cna_compass_stop(CNA_CompassHandle sensor);

/**
 * @brief Disposes the sensor without releasing its handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor was already disposed.
 *
 * As with every sensor in this ABI, a second disposal is refused rather than ignored.
 */
CNA_C_API CNA_Result cna_compass_dispose(CNA_CompassHandle sensor);

/**
 * @brief Returns the most recent reading.
 *
 * @param sensor Owned sensor handle.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the sensor is unsupported or
 *         disposed, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_get_current_value(
    CNA_CompassHandle sensor,
    CNA_CompassReading* out_reading);

/**
 * @brief Reports whether the sensor has produced a reading yet.
 *
 * @param sensor Owned sensor handle.
 * @param out_valid Receives `CNA_TRUE` once a reading has arrived.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_get_is_data_valid(CNA_CompassHandle sensor, CNA_Bool* out_valid);

/**
 * @brief Returns the requested interval between updates, in 100-nanosecond ticks.
 *
 * @param sensor Owned sensor handle.
 * @param out_ticks Receives the interval.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_get_time_between_updates_ticks(
    CNA_CompassHandle sensor,
    int64_t* out_ticks);

/**
 * @brief Requests a new interval between updates, in 100-nanosecond ticks.
 *
 * @param sensor Owned sensor handle.
 * @param ticks Requested interval.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_set_time_between_updates_ticks(
    CNA_CompassHandle sensor,
    int64_t ticks);

/**
 * @brief Subscribes to the sensor's reading event.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked for each accepted reading.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_subscribe_current_value_changed(
    CNA_CompassHandle sensor,
    CNA_CompassReadingCallback callback,
    void* context,
    CNA_SensorEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the sensor's calibration-needed event.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked when the sensor reports that it needs calibrating.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical event argument carries no data, so the handler receives only its context.
 */
CNA_C_API CNA_Result cna_compass_subscribe_calibrate(
    CNA_CompassHandle sensor,
    CNA_SensorEventCallback callback,
    void* context,
    CNA_SensorEventRegistrationHandle* out_registration);

/**
 * @brief Installs or removes this ABI's own compass backend for testing.
 *
 * @param sensor Owned sensor handle.
 * @param installed `CNA_TRUE` to install the test backend, `CNA_FALSE` to restore the platform one.
 * @param supported Whether the installed backend reports the sensor as supported; ignored when
 *        @p installed is `CNA_FALSE`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when acquisition is currently started, or
 *         a documented argument/handle/thread failure.
 *
 * The canonical hook takes a caller-implemented backend object, which C cannot write, so this ABI
 * supplies the backend and exposes only the switch. Without it there is no compass on any
 * verification machine and no way to reach a single line past the unsupported refusal.
 */
CNA_C_API CNA_Result cna_compass_set_test_backend_ext(
    CNA_CompassHandle sensor,
    CNA_Bool installed,
    CNA_Bool supported);

/**
 * @brief Delivers a reading through the installed test backend.
 *
 * @param sensor Owned sensor handle.
 * @param reading The reading to deliver.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no test backend is installed or
 *         acquisition is not started, or a documented argument/handle/thread failure.
 *
 * The reading travels the canonical delivery path, so it publishes the current value, marks the data
 * valid and raises the reading event exactly as a real sensor would.
 */
CNA_C_API CNA_Result cna_compass_inject_synthetic_update_ext(
    CNA_CompassHandle sensor,
    const CNA_CompassReading* reading);

/**
 * @brief Reports a calibration need through the installed test backend.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no test backend is installed or
 *         acquisition is not started, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_inject_calibration_request_ext(CNA_CompassHandle sensor);

/**
 * @brief Returns the byte count of the sensor's .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_get_type_name_size(CNA_CompassHandle sensor, uint64_t* out_bytes);

/**
 * @brief Copies the sensor's fully-qualified .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_compass_copy_type_name(
    CNA_CompassHandle sensor,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases a sensor handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Releasing disposes the sensor if it has not been disposed already, which is what the canonical
 * destructor does.
 */
CNA_C_API CNA_Result cna_compass_destroy(CNA_CompassHandle sensor);

/* ---- Motion ---- */

/** @brief Owned handle to one fused motion sensor. */
typedef CNA_Handle CNA_MotionHandle;

/**
 * @brief Handler invoked with a new fused motion reading.
 *
 * @param reading The reading, borrowed for the duration of the call.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_MotionReadingCallback)(
    const CNA_MotionReading* reading,
    void* context);

/**
 * @brief Reports whether this device supports fused motion sensing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when the sensor is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_motion_get_is_supported(CNA_Handle game, CNA_Bool* out_supported);

/**
 * @brief Creates a fused motion sensor.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_sensor Receives an owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the canonical limit of ten
 *         simultaneous instances is already reached, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_create(CNA_Handle game, CNA_MotionHandle* out_sensor);

/**
 * @brief Returns the sensor's current state.
 *
 * @param sensor Owned sensor handle.
 * @param out_state Receives one `CNA_SENSOR_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_get_state(CNA_MotionHandle sensor, CNA_SensorState* out_state);

/**
 * @brief Reports whether the active attitude source is referenced to north.
 *
 * @param sensor Owned sensor handle.
 * @param out_north_referenced Receives `CNA_TRUE` when the yaw has an absolute reference.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A canonical extension, and one whose default is deliberately vacuous: with no backend or before
 * acquisition starts the answer is `CNA_TRUE`, which means "nothing is drifting yet", not "north is
 * known". It is only informative once a started backend answers for itself.
 */
CNA_C_API CNA_Result cna_motion_get_is_attitude_north_referenced_ext(
    CNA_MotionHandle sensor,
    CNA_Bool* out_north_referenced);

/**
 * @brief Starts data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed,
 *         acquisition is already started, or the platform has no fused motion sensing — the
 *         canonical failure carries an error id, readable with `cna_sensors_get_last_error_id_ext`.
 */
CNA_C_API CNA_Result cna_motion_start(CNA_MotionHandle sensor);

/**
 * @brief Stops data acquisition.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor has been disposed.
 */
CNA_C_API CNA_Result cna_motion_stop(CNA_MotionHandle sensor);

/**
 * @brief Disposes the sensor without releasing its handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when the sensor was already disposed.
 */
CNA_C_API CNA_Result cna_motion_dispose(CNA_MotionHandle sensor);

/**
 * @brief Returns the most recent reading.
 *
 * @param sensor Owned sensor handle.
 * @param out_reading Receives the reading.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the sensor is unsupported or
 *         disposed, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_get_current_value(
    CNA_MotionHandle sensor,
    CNA_MotionReading* out_reading);

/**
 * @brief Reports whether the sensor has produced a reading yet.
 *
 * @param sensor Owned sensor handle.
 * @param out_valid Receives `CNA_TRUE` once a reading has arrived.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_get_is_data_valid(CNA_MotionHandle sensor, CNA_Bool* out_valid);

/**
 * @brief Returns the requested interval between updates, in 100-nanosecond ticks.
 *
 * @param sensor Owned sensor handle.
 * @param out_ticks Receives the interval.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_get_time_between_updates_ticks(
    CNA_MotionHandle sensor,
    int64_t* out_ticks);

/**
 * @brief Requests a new interval between updates, in 100-nanosecond ticks.
 *
 * @param sensor Owned sensor handle.
 * @param ticks Requested interval.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_set_time_between_updates_ticks(
    CNA_MotionHandle sensor,
    int64_t ticks);

/**
 * @brief Subscribes to the sensor's reading event.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked for each accepted reading.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_subscribe_current_value_changed(
    CNA_MotionHandle sensor,
    CNA_MotionReadingCallback callback,
    void* context,
    CNA_SensorEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the sensor's calibration-needed event.
 *
 * @param sensor Owned sensor handle.
 * @param callback Handler invoked when the fused sensor's magnetometer needs calibrating.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_subscribe_calibrate(
    CNA_MotionHandle sensor,
    CNA_SensorEventCallback callback,
    void* context,
    CNA_SensorEventRegistrationHandle* out_registration);

/**
 * @brief Installs or removes this ABI's own motion backend for testing.
 *
 * @param sensor Owned sensor handle.
 * @param installed `CNA_TRUE` to install the test backend, `CNA_FALSE` to restore the platform one.
 * @param supported Whether the installed backend reports the sensor as supported; ignored when
 *        @p installed is `CNA_FALSE`.
 * @param north_referenced Whether the installed backend claims a north-referenced attitude source;
 *        ignored when @p installed is `CNA_FALSE`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when acquisition is currently started, or
 *         a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_set_test_backend_ext(
    CNA_MotionHandle sensor,
    CNA_Bool installed,
    CNA_Bool supported,
    CNA_Bool north_referenced);

/**
 * @brief Delivers a reading through the installed test backend.
 *
 * @param sensor Owned sensor handle.
 * @param reading The reading to deliver.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no test backend is installed or
 *         acquisition is not started, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_inject_synthetic_update_ext(
    CNA_MotionHandle sensor,
    const CNA_MotionReading* reading);

/**
 * @brief Reports a calibration need through the installed test backend.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no test backend is installed or
 *         acquisition is not started, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_inject_calibration_request_ext(CNA_MotionHandle sensor);

/**
 * @brief Returns the byte count of the sensor's .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_get_type_name_size(CNA_MotionHandle sensor, uint64_t* out_bytes);

/**
 * @brief Copies the sensor's fully-qualified .NET type name.
 *
 * @param sensor Owned sensor handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_copy_type_name(
    CNA_MotionHandle sensor,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases a sensor handle.
 *
 * @param sensor Owned sensor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_motion_destroy(CNA_MotionHandle sensor);

#ifdef __cplusplus
}
#endif

#endif
