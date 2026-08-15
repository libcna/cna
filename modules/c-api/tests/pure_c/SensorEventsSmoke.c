// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>

typedef struct SensorEventsSmokeState {
    int validated;
} SensorEventsSmokeState;

typedef struct OrderState {
    int next;
    int current_value_order;
    int reading_changed_order;
    CNA_AccelerometerReadingEventInfo last;
} OrderState;

typedef struct CompassState {
    int readings;
    int calibrations;
    CNA_CompassReading last;
} CompassState;

typedef struct MotionState {
    int readings;
    int calibrations;
    CNA_MotionReading last;
} MotionState;

static void on_current_value(const CNA_AccelerometerReading* const reading, void* const context)
{
    OrderState* const state = (OrderState*)context;
    (void)reading;
    state->current_value_order = ++state->next;
}

static void on_reading_changed(
    const CNA_AccelerometerReadingEventInfo* const info,
    void* const context)
{
    OrderState* const state = (OrderState*)context;
    state->last = *info;
    state->reading_changed_order = ++state->next;
}

static void on_compass_reading(const CNA_CompassReading* const reading, void* const context)
{
    CompassState* const state = (CompassState*)context;
    state->last = *reading;
    ++state->readings;
}

static void on_calibration(void* const context)
{
    ++((CompassState*)context)->calibrations;
}

static void on_motion_reading(const CNA_MotionReading* const reading, void* const context)
{
    MotionState* const state = (MotionState*)context;
    state->last = *reading;
    ++state->readings;
}

static void on_motion_calibration(void* const context)
{
    ++((MotionState*)context)->calibrations;
}

/* The legacy event's payload is a value of its own, not a reading: the canonical type carries the
   acceleration as three separate components. */
static int validate_reading_event_info(void)
{
    const CNA_DateTimeOffset timestamp = {INT64_C(637000000000000000), INT64_C(36000000000)};
    CNA_AccelerometerReadingEventInfo info;
    CNA_AccelerometerReadingEventInfo other;
    CNA_Bool equal = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    uint64_t hash_first = UINT64_C(0);
    uint64_t hash_second = UINT64_C(1);
    char text[256];

    memset(&info, 9, sizeof(info));
    if (cna_accelerometer_reading_event_info_init(&info) != CNA_RESULT_SUCCESS ||
        info.struct_size != (uint32_t)sizeof(info) || info.struct_version != UINT32_C(1) ||
        info.x != 0.0 || info.y != 0.0 || info.z != 0.0 || info.timestamp.ticks != INT64_C(0) ||
        cna_accelerometer_reading_event_info_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical constructor takes the components first and the timestamp last, the opposite of
       the reading value's own order. Neither was normalized. */
    if (cna_accelerometer_reading_event_info_init_from_values(1.0, 2.0, 3.0, timestamp, &info) !=
            CNA_RESULT_SUCCESS ||
        info.x != 1.0 || info.y != 2.0 || info.z != 3.0 ||
        info.timestamp.ticks != timestamp.ticks ||
        info.timestamp.offset_ticks != timestamp.offset_ticks ||
        cna_accelerometer_reading_event_info_init_from_values(1.0, 2.0, 3.0, timestamp, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other = info;
    if (cna_accelerometer_reading_event_info_equals(&info, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE ||
        cna_accelerometer_reading_event_info_get_hash_code(&info, &hash_first) !=
            CNA_RESULT_SUCCESS ||
        cna_accelerometer_reading_event_info_get_hash_code(&other, &hash_second) !=
            CNA_RESULT_SUCCESS ||
        hash_first != hash_second) {
        return 0;
    }
    /* Equality separates on a component and, independently, on the timestamp the text omits. */
    other.z = 4.0;
    if (cna_accelerometer_reading_event_info_equals(&info, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = info;
    other.timestamp.ticks += INT64_C(1);
    if (cna_accelerometer_reading_event_info_equals(&info, &other, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_accelerometer_reading_event_info_get_string_size(&info, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_accelerometer_reading_event_info_copy_string(
            &info, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "{X:1 Y:2 Z:3}") != 0) {
        return 0;
    }
    /* A capacity one byte short refuses without writing anything. */
    memset(text, 0, sizeof(text));
    if (cna_accelerometer_reading_event_info_copy_string(&info, text, bytes - UINT64_C(1), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        text[0] != '\0') {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_accelerometer_reading_event_info_get_type_name_size(&bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_accelerometer_reading_event_info_copy_type_name(
            text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Devices.Sensors.AccelerometerReadingEventArgs") != 0 ||
        cna_accelerometer_reading_event_info_get_type_name_size(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An unversioned description is refused everywhere it is read. */
    other = info;
    other.struct_version = UINT32_C(0);
    return cna_accelerometer_reading_event_info_equals(&info, &other, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_event_info_get_hash_code(&other, &hash_first) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_event_info_get_string_size(0, &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_accelerometer_reading_event_info_equals(&info, &other, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

/* The calibration event argument carries nothing at all, so only its name reaches C. */
static int validate_calibration_type_name(void)
{
    uint64_t bytes = UINT64_C(9);
    char text[256];

    memset(text, 0, sizeof(text));
    return cna_calibration_event_info_get_type_name_size(&bytes) == CNA_RESULT_SUCCESS &&
        bytes < (uint64_t)sizeof(text) &&
        cna_calibration_event_info_copy_type_name(text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, "Microsoft.Devices.Sensors.CalibrationEventArgs") == 0 &&
        cna_calibration_event_info_copy_type_name(text, UINT64_C(1), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        cna_calibration_event_info_get_type_name_size(0) == CNA_RESULT_INVALID_ARGUMENT;
}

/* Both accelerometer events fire for one reading, in the canonical order. */
static int validate_reading_changed(const CNA_Handle game)
{
    CNA_AccelerometerHandle sensor = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle current_value = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle reading_changed = CNA_INVALID_HANDLE;
    OrderState order;

    memset(&order, 0, sizeof(order));
    if (cna_accelerometer_create(game, &sensor) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_set_supported_for_tests_ext(sensor, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_set_started_for_tests_ext(sensor, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_subscribe_current_value_changed(
            sensor, on_current_value, &order, &current_value) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_subscribe_reading_changed(sensor, 0, &order, &reading_changed) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_accelerometer_subscribe_reading_changed(sensor, on_reading_changed, &order, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_accelerometer_subscribe_reading_changed(
            sensor, on_reading_changed, &order, &reading_changed) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The injected value is in the platform's metres per second squared and the description
       carries the canonical g, exactly as the reading does. */
    if (cna_accelerometer_inject_synthetic_update_ext(sensor, 0.0F, 0.0F, 9.80665F) !=
            CNA_RESULT_SUCCESS ||
        order.current_value_order != 1 || order.reading_changed_order != 2 ||
        order.last.z < 0.999 || order.last.z > 1.001 || order.last.x != 0.0 ||
        order.last.timestamp.ticks == INT64_C(0) ||
        order.last.struct_size != (uint32_t)sizeof(order.last)) {
        return 0;
    }
    /* Detaching the legacy registration leaves the other event attached. */
    if (cna_sensor_unsubscribe_ext(reading_changed) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_inject_synthetic_update_ext(sensor, 0.0F, 0.0F, 9.80665F) !=
            CNA_RESULT_SUCCESS ||
        order.reading_changed_order != 2 || order.current_value_order != 3) {
        return 0;
    }
    return cna_sensor_unsubscribe_ext(current_value) == CNA_RESULT_SUCCESS &&
        cna_accelerometer_destroy(sensor) == CNA_RESULT_SUCCESS &&
        cna_accelerometer_subscribe_reading_changed(
            sensor, on_reading_changed, &order, &reading_changed) == CNA_RESULT_INVALID_HANDLE;
}

/* No machine this ABI is verified on has a compass, and the canonical implementation supports one
   platform only, so the whole supported path is driven through this ABI's own test backend. */
static int validate_compass(const CNA_Handle game)
{
    CNA_CompassHandle sensor = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle readings = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle calibrations = CNA_INVALID_HANDLE;
    CNA_CompassReading reading;
    CNA_CompassReading current;
    CompassState state;
    CNA_SensorState sensor_state = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    const CNA_Vector3 magnetometer = {1.0F, 2.0F, 3.0F};
    const CNA_DateTimeOffset timestamp = {INT64_C(637000000000000000), INT64_C(0)};
    int32_t error_id = 0;
    uint64_t bytes = UINT64_C(9);
    char text[256];

    memset(&state, 0, sizeof(state));
    if (cna_compass_get_is_supported(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_compass_get_is_supported(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_compass_create(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_compass_create(game, &sensor) != CNA_RESULT_SUCCESS ||
        cna_compass_get_state(sensor, &sensor_state) != CNA_RESULT_SUCCESS ||
        sensor_state > CNA_SENSOR_STATE_MAXIMUM) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_compass_get_type_name_size(sensor, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_compass_copy_type_name(sensor, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Devices.Sensors.Compass") != 0) {
        return 0;
    }
    /* Without a backend the sensor is unsupported: starting fails with an error identifier, and
       the current value is refused rather than defaulted. */
    if (cna_compass_start(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_sensors_get_last_error_id_ext(&error_id, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_compass_get_current_value(sensor, &current) != CNA_RESULT_INVALID_STATE ||
        cna_compass_inject_calibration_request_ext(sensor) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    /* A backend that reports the sensor as unsupported still refuses to start. */
    if (cna_compass_set_test_backend_ext(sensor, CNA_TRUE, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_compass_start(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_compass_inject_calibration_request_ext(sensor) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_compass_set_test_backend_ext(sensor, CNA_TRUE, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_compass_subscribe_current_value_changed(
            sensor, on_compass_reading, &state, &readings) != CNA_RESULT_SUCCESS ||
        cna_compass_subscribe_calibrate(sensor, 0, &state, &calibrations) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        calibrations != CNA_INVALID_HANDLE ||
        cna_compass_subscribe_calibrate(sensor, on_calibration, &state, &calibrations) !=
            CNA_RESULT_SUCCESS ||
        cna_compass_start(sensor) != CNA_RESULT_SUCCESS ||
        cna_compass_get_state(sensor, &sensor_state) != CNA_RESULT_SUCCESS ||
        sensor_state != CNA_SENSOR_STATE_READY) {
        return 0;
    }
    /* Replacing a backend while acquisition runs is refused, so a started session can never lose
       the object delivering its readings. */
    if (cna_compass_set_test_backend_ext(sensor, CNA_FALSE, CNA_FALSE) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_compass_reading_init_from_values(
            2.5, 45.0, magnetometer, timestamp, 46.5, &reading) != CNA_RESULT_SUCCESS ||
        cna_compass_inject_synthetic_update_ext(sensor, &reading) != CNA_RESULT_SUCCESS ||
        state.readings != 1 || state.last.magnetic_heading != 45.0 ||
        state.last.true_heading != 46.5 || state.last.magnetometer_reading.z != 3.0F ||
        cna_compass_get_current_value(sensor, &current) != CNA_RESULT_SUCCESS ||
        current.heading_accuracy != 2.5 ||
        cna_compass_get_is_data_valid(sensor, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_compass_inject_synthetic_update_ext(sensor, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The calibration event carries nothing but the caller context. */
    if (cna_compass_inject_calibration_request_ext(sensor) != CNA_RESULT_SUCCESS ||
        state.calibrations != 1 ||
        cna_sensor_unsubscribe_ext(calibrations) != CNA_RESULT_SUCCESS ||
        cna_compass_inject_calibration_request_ext(sensor) != CNA_RESULT_SUCCESS ||
        state.calibrations != 1) {
        return 0;
    }
    {
        int64_t ticks = INT64_C(-1);
        if (cna_compass_set_time_between_updates_ticks(sensor, INT64_C(300000)) !=
                CNA_RESULT_SUCCESS ||
            cna_compass_get_time_between_updates_ticks(sensor, &ticks) != CNA_RESULT_SUCCESS ||
            ticks != INT64_C(300000)) {
            return 0;
        }
    }
    /* Stopping ends delivery and moves the state to disabled; the backend can then be removed. */
    if (cna_compass_stop(sensor) != CNA_RESULT_SUCCESS ||
        cna_compass_get_state(sensor, &sensor_state) != CNA_RESULT_SUCCESS ||
        sensor_state != CNA_SENSOR_STATE_DISABLED ||
        cna_compass_inject_synthetic_update_ext(sensor, &reading) != CNA_RESULT_INVALID_STATE ||
        state.readings != 1 ||
        cna_compass_set_test_backend_ext(sensor, CNA_FALSE, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_compass_inject_synthetic_update_ext(sensor, &reading) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_sensor_unsubscribe_ext(readings) != CNA_RESULT_SUCCESS ||
        cna_compass_dispose(sensor) != CNA_RESULT_SUCCESS ||
        cna_compass_dispose(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_compass_start(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_compass_destroy(sensor) != CNA_RESULT_SUCCESS ||
        cna_compass_destroy(sensor) != CNA_RESULT_INVALID_HANDLE ||
        cna_compass_get_state(sensor, &sensor_state) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    /* The canonical class refuses an eleventh simultaneous instance. */
    {
        CNA_CompassHandle instances[10];
        CNA_CompassHandle overflow = CNA_INVALID_HANDLE;
        int created = 0;
        int index = 0;
        int limit_reported = 0;
        for (index = 0; index < 10; ++index) {
            instances[index] = CNA_INVALID_HANDLE;
            if (cna_compass_create(game, &instances[index]) != CNA_RESULT_SUCCESS) {
                break;
            }
            ++created;
        }
        limit_reported = (created == 10) &&
            (cna_compass_create(game, &overflow) == CNA_RESULT_INVALID_STATE) &&
            (overflow == CNA_INVALID_HANDLE);
        for (index = 0; index < created; ++index) {
            if (cna_compass_destroy(instances[index]) != CNA_RESULT_SUCCESS) {
                return 0;
            }
        }
        if (!limit_reported) {
            return 0;
        }
    }
    return 1;
}

static int validate_motion(const CNA_Handle game)
{
    CNA_MotionHandle sensor = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle readings = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle calibrations = CNA_INVALID_HANDLE;
    CNA_AttitudeReading attitude;
    CNA_MotionReading reading;
    CNA_MotionReading current;
    MotionState state;
    CNA_SensorState sensor_state = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    const CNA_Vector3 acceleration = {0.25F, 0.5F, 0.75F};
    const CNA_Vector3 rate = {1.0F, 1.25F, 1.5F};
    const CNA_Vector3 gravity = {0.0F, -1.0F, 0.0F};
    const CNA_Quaternion orientation = {0.0F, 0.0F, 0.0F, 1.0F};
    const CNA_DateTimeOffset timestamp = {INT64_C(637000000000000000), INT64_C(0)};
    CNA_Matrix rotation;
    uint64_t bytes = UINT64_C(9);
    char text[256];

    memset(&state, 0, sizeof(state));
    memset(&rotation, 0, sizeof(rotation));
    rotation.m11 = 1.0F;
    rotation.m22 = 1.0F;
    rotation.m33 = 1.0F;
    rotation.m44 = 1.0F;
    if (cna_motion_get_is_supported(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_motion_create(game, &sensor) != CNA_RESULT_SUCCESS ||
        cna_motion_get_state(sensor, &sensor_state) != CNA_RESULT_SUCCESS ||
        sensor_state > CNA_SENSOR_STATE_MAXIMUM) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_motion_get_type_name_size(sensor, &bytes) != CNA_RESULT_SUCCESS ||
        cna_motion_copy_type_name(sensor, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Devices.Sensors.Motion") != 0) {
        return 0;
    }
    /* With no backend the north-referenced answer is a vacuous true: nothing is drifting because
       nothing is running. */
    if (cna_motion_get_is_attitude_north_referenced_ext(sensor, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_motion_get_is_attitude_north_referenced_ext(sensor, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_motion_start(sensor) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    /* A backend that admits to the drift-prone attitude source says so. */
    if (cna_motion_set_test_backend_ext(sensor, CNA_TRUE, CNA_TRUE, CNA_FALSE) !=
            CNA_RESULT_SUCCESS ||
        cna_motion_get_is_attitude_north_referenced_ext(sensor, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_motion_subscribe_current_value_changed(
            sensor, on_motion_reading, &state, &readings) != CNA_RESULT_SUCCESS ||
        cna_motion_subscribe_calibrate(sensor, on_motion_calibration, &state, &calibrations) !=
            CNA_RESULT_SUCCESS ||
        cna_motion_start(sensor) != CNA_RESULT_SUCCESS ||
        cna_motion_get_state(sensor, &sensor_state) != CNA_RESULT_SUCCESS ||
        sensor_state != CNA_SENSOR_STATE_READY) {
        return 0;
    }
    if (cna_attitude_reading_init_from_values(
            0.0F, 0.25F, 0.5F, orientation, rotation, timestamp, &attitude) !=
            CNA_RESULT_SUCCESS ||
        cna_motion_reading_init_from_values(
            &attitude, acceleration, rate, gravity, timestamp, &reading) != CNA_RESULT_SUCCESS ||
        cna_motion_inject_synthetic_update_ext(sensor, &reading) != CNA_RESULT_SUCCESS ||
        state.readings != 1 || state.last.device_acceleration.z != 0.75F ||
        state.last.gravity.y != -1.0F || state.last.attitude.roll != 0.25F ||
        cna_motion_get_current_value(sensor, &current) != CNA_RESULT_SUCCESS ||
        current.device_rotation_rate.x != 1.0F ||
        cna_motion_get_is_data_valid(sensor, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }
    if (cna_motion_inject_calibration_request_ext(sensor) != CNA_RESULT_SUCCESS ||
        state.calibrations != 1) {
        return 0;
    }
    {
        int64_t ticks = INT64_C(-1);
        if (cna_motion_set_time_between_updates_ticks(sensor, INT64_C(400000)) !=
                CNA_RESULT_SUCCESS ||
            cna_motion_get_time_between_updates_ticks(sensor, &ticks) != CNA_RESULT_SUCCESS ||
            ticks != INT64_C(400000)) {
            return 0;
        }
    }
    if (cna_motion_stop(sensor) != CNA_RESULT_SUCCESS ||
        cna_motion_inject_synthetic_update_ext(sensor, &reading) != CNA_RESULT_INVALID_STATE ||
        cna_sensor_unsubscribe_ext(readings) != CNA_RESULT_SUCCESS ||
        cna_sensor_unsubscribe_ext(calibrations) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_motion_dispose(sensor) == CNA_RESULT_SUCCESS &&
        cna_motion_dispose(sensor) == CNA_RESULT_INVALID_STATE &&
        cna_motion_get_is_attitude_north_referenced_ext(sensor, &flag) == CNA_RESULT_INVALID_STATE &&
        cna_motion_destroy(sensor) == CNA_RESULT_SUCCESS &&
        cna_motion_destroy(sensor) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    SensorEventsSmokeState* const state = (SensorEventsSmokeState*)context;
    if (game_time == 0 || !validate_reading_event_info() || !validate_calibration_type_name() ||
        !validate_reading_changed(game) || !validate_compass(game) || !validate_motion(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    SensorEventsSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API sensor event smoke", UINT64_C(24)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return 1;
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 2;
}
