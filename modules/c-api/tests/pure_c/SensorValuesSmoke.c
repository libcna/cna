// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>

/* Every route in this header is a pure value operation, so the whole suite runs with no game and
   on whatever thread the runtime starts on. */

static CNA_DateTimeOffset stamp(const int64_t ticks, const int64_t offset)
{
    CNA_DateTimeOffset value;
    value.ticks = ticks;
    value.offset_ticks = offset;
    return value;
}

static CNA_Vector3 vec(const float x, const float y, const float z)
{
    CNA_Vector3 value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

static int validate_accelerometer_reading(void)
{
    CNA_AccelerometerReading reading;
    CNA_AccelerometerReading other;
    CNA_Bool equal = UINT8_C(9);
    uint64_t hash = UINT64_C(0);
    uint64_t other_hash = UINT64_C(0);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    memset(&reading, 9, sizeof(reading));
    if (cna_accelerometer_reading_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_accelerometer_reading_init(&reading) != CNA_RESULT_SUCCESS ||
        reading.struct_size != sizeof(CNA_AccelerometerReading) ||
        reading.struct_version != UINT32_C(1) ||
        reading.acceleration.x != 0.0F || reading.acceleration.y != 0.0F ||
        reading.acceleration.z != 0.0F) {
        return 0;
    }

    if (cna_accelerometer_reading_init_from_values(
            stamp(INT64_C(637000000000000000), INT64_C(36000000000)),
            vec(1.0F, 2.0F, 3.0F),
            &reading) != CNA_RESULT_SUCCESS ||
        reading.timestamp.ticks != INT64_C(637000000000000000) ||
        reading.timestamp.offset_ticks != INT64_C(36000000000) ||
        reading.acceleration.y != 2.0F ||
        cna_accelerometer_reading_init_from_values(
            stamp(INT64_C(0), INT64_C(0)), vec(0.0F, 0.0F, 0.0F), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Equality is the acceleration and the timestamp together. */
    other = reading;
    if (cna_accelerometer_reading_equals(&reading, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    other.acceleration.z = 4.0F;
    if (cna_accelerometer_reading_equals(&reading, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = reading;
    other.timestamp.ticks += INT64_C(1);
    if (cna_accelerometer_reading_equals(&reading, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    /* Equal readings hash equal. */
    other = reading;
    if (cna_accelerometer_reading_get_hash_code(&reading, &hash) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_reading_get_hash_code(&other, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash) {
        return 0;
    }

    /* The canonical text carries only the acceleration. */
    memset(text, 0, sizeof(text));
    if (cna_accelerometer_reading_get_string_size(&reading, &bytes) != CNA_RESULT_SUCCESS ||
        bytes == UINT64_C(0) || bytes >= (uint64_t)sizeof(text) ||
        cna_accelerometer_reading_copy_string(&reading, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strncmp(text, "Acceleration:", 13U) != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_accelerometer_reading_get_type_name_size(&bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_accelerometer_reading_copy_type_name(text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Devices.Sensors.AccelerometerReading") != 0) {
        return 0;
    }

    /* An invalid structure is refused by every route that takes one. */
    other = reading;
    other.struct_version = UINT32_C(2);
    return cna_accelerometer_reading_equals(&other, &reading, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_get_hash_code(&other, &hash) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_get_string_size(&other, &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_equals(0, &reading, &equal) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_equals(&reading, &reading, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_get_type_name_size(0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_gyroscope_reading(void)
{
    CNA_GyroscopeReading reading;
    CNA_GyroscopeReading other;
    CNA_Bool equal = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    memset(&reading, 9, sizeof(reading));
    if (cna_gyroscope_reading_init(&reading) != CNA_RESULT_SUCCESS ||
        reading.rotation_rate.x != 0.0F ||
        cna_gyroscope_reading_init_from_values(
            vec(0.5F, 1.5F, 2.5F), stamp(INT64_C(42), INT64_C(0)), &reading) !=
            CNA_RESULT_SUCCESS ||
        reading.rotation_rate.z != 2.5F || reading.timestamp.ticks != INT64_C(42)) {
        return 0;
    }
    other = reading;
    other.rotation_rate.x = -0.5F;
    if (cna_gyroscope_reading_equals(&reading, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_gyroscope_reading_get_string_size(&reading, &bytes) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_reading_copy_string(&reading, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strncmp(text, "RotationRate:", 13U) != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    return cna_gyroscope_reading_get_type_name_size(&bytes) == CNA_RESULT_SUCCESS &&
        cna_gyroscope_reading_copy_type_name(text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, "Microsoft.Devices.Sensors.GyroscopeReading") == 0;
}

static int validate_attitude_reading(void)
{
    CNA_AttitudeReading reading;
    CNA_AttitudeReading other;
    CNA_Quaternion quaternion;
    CNA_Matrix matrix;
    CNA_Bool equal = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[256];
    char guard[8];

    memset(&quaternion, 0, sizeof(quaternion));
    quaternion.w = 1.0F;
    memset(&matrix, 0, sizeof(matrix));
    matrix.m11 = 1.0F;
    matrix.m22 = 1.0F;
    matrix.m33 = 1.0F;
    matrix.m44 = 1.0F;

    memset(&reading, 9, sizeof(reading));
    if (cna_attitude_reading_init(&reading) != CNA_RESULT_SUCCESS ||
        reading.pitch != 0.0F || reading.roll != 0.0F || reading.yaw != 0.0F ||
        cna_attitude_reading_init_from_values(
            0.25F, 0.5F, 0.75F, quaternion, matrix, stamp(INT64_C(7), INT64_C(0)), &reading) !=
            CNA_RESULT_SUCCESS ||
        reading.pitch != 0.25F || reading.roll != 0.5F || reading.yaw != 0.75F ||
        reading.quaternion.w != 1.0F || reading.rotation_matrix.m33 != 1.0F) {
        return 0;
    }
    /* Every canonical field participates in equality, including the rotation matrix. */
    other = reading;
    other.rotation_matrix.m12 = 0.5F;
    if (cna_attitude_reading_equals(&reading, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_attitude_reading_get_string_size(&reading, &bytes) != CNA_RESULT_SUCCESS ||
        cna_attitude_reading_copy_string(&reading, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strncmp(text, "Pitch:", 6U) != 0) {
        return 0;
    }
    /* A short capacity reports the requirement and writes nothing. */
    memset(guard, 0x7F, sizeof(guard));
    if (cna_attitude_reading_copy_string(&reading, guard, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        guard[0] != 0x7F) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    return cna_attitude_reading_get_type_name_size(&bytes) == CNA_RESULT_SUCCESS &&
        cna_attitude_reading_copy_type_name(text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, "Microsoft.Devices.Sensors.AttitudeReading") == 0;
}

static int validate_compass_reading(void)
{
    CNA_CompassReading reading;
    CNA_CompassReading other;
    CNA_Bool equal = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    memset(&reading, 9, sizeof(reading));
    if (cna_compass_reading_init(&reading) != CNA_RESULT_SUCCESS ||
        reading.heading_accuracy != 0.0 || reading.magnetic_heading != 0.0 ||
        reading.true_heading != 0.0 ||
        cna_compass_reading_init_from_values(
            1.5, 90.0, vec(4.0F, 5.0F, 6.0F), stamp(INT64_C(11), INT64_C(0)), 92.5, &reading) !=
            CNA_RESULT_SUCCESS ||
        reading.heading_accuracy != 1.5 || reading.magnetic_heading != 90.0 ||
        reading.true_heading != 92.5 || reading.magnetometer_reading.z != 6.0F) {
        return 0;
    }
    other = reading;
    other.true_heading = 93.0;
    if (cna_compass_reading_equals(&reading, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_compass_reading_get_string_size(&reading, &bytes) != CNA_RESULT_SUCCESS ||
        cna_compass_reading_copy_string(&reading, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strncmp(text, "MagneticHeading:", 16U) != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    return cna_compass_reading_get_type_name_size(&bytes) == CNA_RESULT_SUCCESS &&
        cna_compass_reading_copy_type_name(text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, "Microsoft.Devices.Sensors.CompassReading") == 0;
}

static int validate_motion_reading(void)
{
    CNA_MotionReading reading;
    CNA_MotionReading other;
    CNA_AttitudeReading attitude;
    CNA_Bool equal = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    if (cna_attitude_reading_init(&attitude) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    attitude.yaw = 1.25F;

    memset(&reading, 9, sizeof(reading));
    if (cna_motion_reading_init(&reading) != CNA_RESULT_SUCCESS ||
        reading.gravity.y != 0.0F ||
        reading.attitude.struct_size != sizeof(CNA_AttitudeReading) ||
        cna_motion_reading_init_from_values(
            &attitude,
            vec(1.0F, 0.0F, 0.0F),
            vec(0.0F, 1.0F, 0.0F),
            vec(0.0F, 0.0F, -1.0F),
            stamp(INT64_C(21), INT64_C(0)),
            &reading) != CNA_RESULT_SUCCESS ||
        reading.attitude.yaw != 1.25F || reading.device_acceleration.x != 1.0F ||
        reading.device_rotation_rate.y != 1.0F || reading.gravity.z != -1.0F) {
        return 0;
    }
    /* The nested attitude is validated too. */
    {
        CNA_AttitudeReading invalid = attitude;
        invalid.struct_version = UINT32_C(2);
        if (cna_motion_reading_init_from_values(
                &invalid,
                vec(0.0F, 0.0F, 0.0F),
                vec(0.0F, 0.0F, 0.0F),
                vec(0.0F, 0.0F, 0.0F),
                stamp(INT64_C(0), INT64_C(0)),
                &reading) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_motion_reading_init_from_values(
                0,
                vec(0.0F, 0.0F, 0.0F),
                vec(0.0F, 0.0F, 0.0F),
                vec(0.0F, 0.0F, 0.0F),
                stamp(INT64_C(0), INT64_C(0)),
                &reading) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    if (cna_motion_reading_init_from_values(
            &attitude,
            vec(1.0F, 0.0F, 0.0F),
            vec(0.0F, 1.0F, 0.0F),
            vec(0.0F, 0.0F, -1.0F),
            stamp(INT64_C(21), INT64_C(0)),
            &reading) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    other = reading;
    other.attitude.pitch = 2.0F;
    if (cna_motion_reading_equals(&reading, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    /* The canonical text carries only the device acceleration and the gravity vector, not the
       attitude or the rotation rate -- preserved rather than tidied. */
    memset(text, 0, sizeof(text));
    if (cna_motion_reading_get_string_size(&reading, &bytes) != CNA_RESULT_SUCCESS ||
        cna_motion_reading_copy_string(&reading, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strncmp(text, "DeviceAcceleration:", 19U) != 0 || strstr(text, " Gravity:") == 0 ||
        strstr(text, "Pitch:") != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    return cna_motion_reading_get_type_name_size(&bytes) == CNA_RESULT_SUCCESS &&
        cna_motion_reading_copy_type_name(text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, "Microsoft.Devices.Sensors.MotionReading") == 0;
}

int main(void)
{
    /* One code per validator, so a failure names the reading it came from. */
    if (!validate_accelerometer_reading()) {
        return 1;
    }
    if (!validate_gyroscope_reading()) {
        return 2;
    }
    if (!validate_attitude_reading()) {
        return 3;
    }
    if (!validate_compass_reading()) {
        return 4;
    }
    if (!validate_motion_reading()) {
        return 5;
    }
    return 0;
}
