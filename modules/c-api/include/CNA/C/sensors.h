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

#ifdef __cplusplus
}
#endif

#endif
